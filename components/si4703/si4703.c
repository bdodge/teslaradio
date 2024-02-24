
#include "si4703.h"
#include "i2c_zephyr.h"
#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(si4703, LOG_LEVEL_INF);

#define CONFIG_RADIO_FM_USA
#include "si4703regs.h"

#define REG_READ_START REG_RSSI
#define REG_WRITE_START REG_POWERCFG
#define NR_SI4703_REGS 16

static const struct i2c_dt_spec i2c_dev = I2C_DT_SPEC_GET(DT_NODELABEL(si4703));
static const struct gpio_dt_spec gpio_rst = GPIO_DT_SPEC_GET(DT_NODELABEL(gpio_si4703_rst), gpios);

#define MAX_CHANNELS    (32)

#define POST_RESET_MS       (510)
#define POST_POWERUP_MS     (110)
#define POST_CONFIG_MS      (50)

static struct si4703_device
{
    const struct i2c_dt_spec *i2c_spec;
    const struct gpio_dt_spec *rst_spec;
    tuner_state_t    state;
    uint64_t    state_time;

    uint16_t    regs[NR_SI4703_REGS];

    uint32_t    disco_station;
    uint16_t    disco_chan;
    bool        rds_changed;
    char        name[9];
    char        text[65];

    uint16_t    seek_thr;
    uint16_t    sksnr;
    uint16_t    skcnt;

    uint16_t    cur_station;
    uint8_t     curvol;
    bool        stereo;
    bool        muted;

    struct station_info stations[MAX_CHANNELS];
}
m_chip;

static int si4703_write(struct si4703_device *chip, uint8_t reg, uint16_t val)
{
    uint16_t count;
    uint8_t buf[NR_SI4703_REGS * 2];
    int i;
    int ret;
    int index;

    chip->regs[reg] = val;

    for (i = 0; i < NR_SI4703_REGS; i++)
    {
        index = (REG_WRITE_START + i) % NR_SI4703_REGS;
        buf[2*i]     = chip->regs[index] >> 8;
        buf[2*i + 1] = chip->regs[index] & 0xff;
    }

    count = ((reg + NR_SI4703_REGS - REG_WRITE_START) % NR_SI4703_REGS + 1) * 2;

    ret = i2c_write_dt(chip->i2c_spec, buf, count);
    if (ret)
    {
        LOG_ERR("i2c write failed!");
        return -EIO;
    }

    return ret;
}

static int si4703_read(struct si4703_device *chip, uint8_t reg, uint16_t *val)
{
    int ret;
    uint16_t count;
    uint8_t buf[NR_SI4703_REGS * 2];
    int i;

    count = ((reg + NR_SI4703_REGS - REG_READ_START) % NR_SI4703_REGS + 1) * 2;

    ret = i2c_read_dt(chip->i2c_spec, buf, count);
    if (ret)
    {
        LOG_ERR("i2c read failed!");
        return -EIO;
    }
    else
    {
        for (i = 0; i < count / 2; i++)
        {
            chip->regs[(REG_READ_START + i) % NR_SI4703_REGS] =
                        (buf[2 * i] << 8) + (buf[2 * i + 1] & 0xff);
        }

        *val = chip->regs[reg];
    }

    return ret;
}

static void si4703_dump_regs(void)
{
    for (int i = 0; i < NR_SI4703_REGS; i++)
    {
        LOG_INF("R %02d  %04X", i, m_chip.regs[i]);
    }
}

static uint16_t si4703_get_rssi(struct si4703_device *chip)
{
    uint16_t val;

    si4703_read(chip, REG_RSSI, &val);
    return val;
}

static int si4703_wait_stc(struct si4703_device *chip, int timeoutms, int on_off)
{
    int timeout = timeoutms;
    uint16_t val;

    do
    {
        val = si4703_get_rssi(chip);
        if (on_off && (val & STC))
        {
            break;
        }
        else if(!on_off && !(val & STC))
        {
            break;
        }

        k_msleep(2);
        timeout -= 2;
    }
    while (timeout > 0);

    if (timeout < 0)
    {
        LOG_ERR("STC wont %s", on_off ? "set" : "clear");
        return -ENODEV;
    }

    return 0;
}

static uint16_t si4703_get_chan(struct si4703_device *chip)
{
    uint16_t val;

    si4703_read(chip, REG_READCHAN, &val);
    return val;
}

static int si4703_decode_rds(struct si4703_device *chip)
{
    uint16_t rds[4];
    uint8_t txtoff;
    uint16_t group;
    uint16_t version;
    int ret;

    char name[9];
    char text[65];

    memcpy(name, chip->name, sizeof(name));
    memcpy(text, chip->text, sizeof(text));
    chip->rds_changed = false;

    ret  = si4703_read(chip, REG_RDSA, &rds[0]);
    ret += si4703_read(chip, REG_RDSB, &rds[1]);
    ret += si4703_read(chip, REG_RDSC, &rds[2]);
    ret += si4703_read(chip, REG_RDSD, &rds[3]);
    if (ret)
    {
        return ret;
    }

    group = rds[1] >> 12;
    version = (rds[1] & (1 << 11)) ? 1 : 0;

    switch (group)
    {
    case 0:     // Basic tuning info
        if (version)
        {
            // group ver B
            txtoff = (rds[1] & 0x3) << 1;
            chip->name[txtoff] = rds[3] >> 8;
            chip->name[txtoff + 1] = rds[3] & 0xFF;
        }
        else
        {
            // group ver A
            txtoff = (rds[1] & 0x3) << 1;
            chip->name[txtoff] = rds[3] >> 8;
            chip->name[txtoff + 1] = rds[3] & 0xFF;
        }
        break;
    case 2:     // Radio text
        if (version)
        {
            // group ver B
            txtoff = (rds[1] & 0xF) << 1;
            chip->text[txtoff] = rds[3] >> 8;
            chip->text[txtoff + 1] = rds[3] & 0xFF;
        }
        else
        {
            // group ver A
            txtoff = (rds[1] & 0xF) << 2;
            chip->text[txtoff] = rds[2] >> 8;
            chip->text[txtoff + 1] = rds[2] & 0xFF;
            chip->text[txtoff + 2] = rds[3] >> 8;
            chip->text[txtoff + 3] = rds[3] & 0xFF;
        }
        break;
    default:
        LOG_DBG("Skip rds type %u", rds[1] >> 12);
        break;
    }

    if (memcmp(name, chip->name, sizeof(name)))
    {
        chip->rds_changed = true;
    }
    else if (memcmp(text, chip->text, sizeof(text)))
    {
        chip->rds_changed = true;
    }

    return ret;
}

static void si4703_clear_rds(struct si4703_device *chip)
{
    memset(chip->name, 0, sizeof(chip->name));
    memset(chip->text, 0, sizeof(chip->text));
}

static int si4703_show_channel(struct si4703_device *chip)
{
    int ret = 0;
    uint16_t val;
    uint16_t rssi;
    uint32_t khz;
    float freq;

    rssi = si4703_get_rssi(chip);
    val = si4703_get_chan(chip) & 0x3ff;

    khz = FREQ_MIN + val * CHANNEL_SPACE;
    freq = (float)khz / 1000;

    LOG_INF("Channel: %d  %4.1f MHz %u %s rds:%s",
            val, freq, rssi & 0xFF,
            (rssi & 0x100) ? "stereo" : "mono",
            (rssi & 0x8000) ? "rdy" : "");

    return ret;
}

static int si4703_unmute(struct si4703_device *chip)
{
    int ret;
    uint16_t val;

    ret = si4703_read(chip, REG_SYSCONFIG2, &val);
    if (ret)
    {
        return ret;
    }

    val &= ~0xf;
    val |= chip->curvol;

    ret = si4703_write(chip, REG_SYSCONFIG2, val);
    chip->muted = false;
    return ret;
}

static int si4703_mute(struct si4703_device *chip)
{
    int ret;
    uint16_t val;

    ret = si4703_read(chip, REG_SYSCONFIG2, &val);
    if (ret)
    {
        return ret;
    }

    chip->curvol = val & 0xf;
    val &= ~0xf;
    ret = si4703_write(chip, REG_SYSCONFIG2, val);
    if (ret)
    {
        return ret;
    }

    chip->muted = true;
    return 0;
}

static int si4703_set_vol(struct si4703_device *chip, uint8_t vol)
{
    int ret = -EINVAL;
    uint16_t val;

    if (vol > 15)
    {
        return ret;
    }

    ret = si4703_read(chip, REG_SYSCONFIG2, &val);
    if (ret)
    {
        return ret;
    }

    val &= ~0xf;
    ret = si4703_write(chip, REG_SYSCONFIG2, val | vol);
    if (ret)
    {
        return ret;
    }

    if (vol == 0 && !chip->muted)
    {
        ret = si4703_mute(chip);
    }
    else if (vol != 0 && chip->muted)
    {
        ret = si4703_unmute(chip);
    }

    chip->curvol = vol;
    return ret;
}

int si4703_set_chan(struct si4703_device *chip, uint16_t channel)
{
    int ret = -EINVAL;
    uint16_t val;
    uint16_t rssi;
    int station;

    if (chip->state < TUNER_DISCOVERY)
    {
        return -EAGAIN;
    }

    if (chip->state == TUNER_READY)
    {
        chip->state = TUNER_TUNED;
    }

    chip->cur_station = MAX_CHANNELS;

    for (station = 0; station < MAX_CHANNELS; station++)
    {
        if (chip->stations[station].rssi > 0)
        {
            if (chip->stations[station].channel == channel)
            {
                chip->cur_station = station;
                break;
            }
        }
    }

    si4703_clear_rds(chip);

    if (si4703_write(chip, REG_CHANNEL, channel))
    {
        return -EIO;
    }

    if (si4703_write(chip, REG_CHANNEL, channel | TUNE))
    {
        return -EIO;
    }

    /* wait for STC to be set */
    ret = si4703_wait_stc(chip, 550, 1);
    if (ret)
    {
        return -EIO;
    }

    si4703_show_channel(chip);

    rssi = si4703_get_rssi(chip);

    /* clear tune bit */
    if (si4703_read(chip, REG_CHANNEL, &val))
    {
        return -EIO;
    }

    if (si4703_write(chip, REG_CHANNEL, ~TUNE & val))
    {
        return -EIO;
    }

    /* wait for STC to be clear */
    ret = si4703_wait_stc(chip, 200, 0);
    if (ret)
    {
        return -EIO;
    }

    if (si4703_get_rssi(chip) & AFCRL)
    {
        // not an error, just that there is prolly a better station close by
        LOG_INF("AFC railed on set chan");
    }

    return ret;
}

int si4703_set_freq(struct si4703_device *chip, unsigned int freq)
{
    int ret = -EINVAL;
    uint16_t channel;

    if (chip->state < TUNER_READY)
    {
        return -EAGAIN;
    }

    if (freq < FREQ_MIN || freq > FREQ_MAX)
    {
        return -EINVAL;
    }

    channel = (freq - FREQ_MIN) / CHANNEL_SPACE;

    LOG_DBG("Channel %d (freq: %d kHz  -> %d kHz)", channel, freq, channel * CHANNEL_SPACE + FREQ_MIN);

    ret = si4703_set_chan(chip, channel);
    return ret;
}

static int si4703_seek(struct si4703_device *chip, int up_down)
{
    int ret = 0;
    uint16_t val;
    uint16_t chan;
    uint16_t stat;

    si4703_clear_rds(chip);

    ret = si4703_read(chip, REG_POWERCFG, &val);
    if (ret)
    {
        return -EIO;
    }

    val &= ~0x100; // seek clear

    ret = si4703_write(chip, REG_POWERCFG, val);
    if (ret)
    {
        return -EIO;
    }

    if (up_down)
    {
        val |= 0x200; // seek up
    }
    else
    {
        val &= ~0x200;
    }

    val |= 0x100; // seek enable;
    ret = si4703_write(chip, REG_POWERCFG, val);
    if (ret)
    {
        return -EIO;
    }

    ret = si4703_wait_stc(chip, 250, 1);
    if (ret)
    {
        return -ENODEV;
    }

    stat = si4703_get_rssi(chip);
    if (stat & SFBL)
    {
        LOG_INF("Seek limit");
    }

    val &= ~0x100; // seek disable
    ret = si4703_write(chip, REG_POWERCFG, val);
    if (ret)
    {
        return -EIO;
    }

    ret = si4703_wait_stc(chip, 100, 0);
    if (ret)
    {
        return -ENODEV;
    }

    int bumps = 0;
    uint16_t newchan;

    do
    {
        if (!(si4703_get_rssi(chip) & AFCRL))
        {
            break;
        }

        // AFC railed means we are close to a stronger signal, so
        // go that way to get the clear station
        //
        chan = si4703_get_chan(chip);
        newchan = chan + (up_down ? -1 : 1);

        LOG_INF("AFC railed, bump %s  from %u to %u",
                    up_down ? "up" : "down", chan, newchan);
        si4703_set_chan(chip, newchan);
    }
    while (bumps < 10);

    si4703_show_channel(chip);

    return ret;
}

static int si4703_disco_slice(struct si4703_device *chip, bool *complete)
{
    int ret;
    uint16_t chan;
    uint16_t rssi;

    if  (complete)
    {
        *complete = false;
    }

    if (chip->disco_station >= MAX_CHANNELS)
    {
        LOG_INF("Scanning for channels");

        chip->disco_station = 0;
        chip->disco_chan = 0;

        si4703_clear_rds(chip);

        // clear out all channel info
        //
        for (chan = 0; chan < MAX_CHANNELS; chan++)
        {
            chip->stations[chip->disco_chan].channel = 0;
            chip->stations[chip->disco_chan].freq_kHz = 0;
            chip->stations[chip->disco_chan].rssi = 0;
            memset(chip->stations[chip->disco_chan].name, 0, sizeof(chip->stations[chip->disco_chan].name));
            memset(chip->stations[chip->disco_chan].text, 0, sizeof(chip->stations[chip->disco_chan].text));
        }
    }

    // get current tuned chan state and signal
    ret = si4703_set_chan(chip, chip->disco_chan);
    if (ret)
    {
        return ret;
    }

    k_msleep(10);
    rssi = si4703_get_rssi(chip);
    chan = si4703_get_chan(chip);

    LOG_DBG("disco check chan %u at %u 0x%04X", chip->disco_chan, chan, rssi);

    // for stations with signal, add to our database
    if ((rssi & 0xFF) >= chip->seek_thr && !(rssi & AFCRL))
    {
        LOG_INF("Disco add %d chan %d rssi 0x%04X", chip->disco_station, chan, rssi);

        chip->stations[chip->disco_station].channel = chan;
        chip->stations[chip->disco_station].freq_kHz = chan * CHANNEL_SPACE + FREQ_MIN;
        chip->stations[chip->disco_station].rssi = rssi & 0xFF;

        chip->disco_station++;
        if (chip->disco_station >= MAX_CHANNELS)
        {
            // TODO evict weakest and move slots up?
            LOG_ERR("too many channels");
            if (complete)
            {
                *complete = true;
            }
        }
    }

    chip->disco_chan = chan;

    if (rssi & AFCRL)
    {
        uint16_t max_chan = (FREQ_MAX - FREQ_MIN) / CHANNEL_SPACE;

        // if AFCRL was set in rssi, it means we are close to a strong station
        // so just bump up one channel
        //
        LOG_INF("Fudge at chan %u 0x%04X", chan, rssi);
        chip->disco_chan = chan + 1;

        if (chip->disco_chan > max_chan)
        {
            if (complete)
            {
                *complete = true;
            }
        }
    }
    else
    {
        // seek up to next chan
        //
        ret = si4703_seek(chip, 1);
        if (ret)
        {
            LOG_INF("seek fail, recheck from %u", chip->disco_chan);
            chip->disco_chan = chan + 1;
        }
        else
        {
            chan = si4703_get_chan(chip);
            LOG_INF("Seek up to chan %u 0x%04X", chan, rssi);
            if (chan < chip->disco_chan)
            {
                //wrapped, all done
                if (complete)
                {
                    *complete = true;
                }
            }

            chip->disco_chan = chan;
        }
    }

    return 0;
}

static int si4703_config(struct si4703_device *chip)
{
    int ret;

    ret  = si4703_write(chip, REG_CHANNEL, 0x0000);
    ret += si4703_write(chip, REG_SYSCONFIG1, STCIEN | RDSIEN | RDS | GPIO2_INTERRUPT);
    ret += si4703_write(chip, REG_SYSCONFIG2, (chip->seek_thr << 8) | SPACE | BAND);
    ret += si4703_write(chip, REG_SYSCONFIG3, (chip->sksnr << 4) | (chip->skcnt));
    ret += si4703_set_vol(&m_chip, 0);

    return ret;
}

static int si4703_power_up(struct si4703_device *chip)
{
    int ret;

    ret = si4703_write(chip, REG_POWERCFG, DMUTE | ENABLE);
    return ret;
}

static int si4703_power_down(struct si4703_device *chip)
{
    return si4703_write(chip, REG_POWERCFG, ENABLE | DISABLE);
}

static int si4703_reset(struct si4703_device *chip, int longloop)
{
    int ret;

    ret = gpio_pin_configure_dt(m_chip.rst_spec, GPIO_OUTPUT);
    if (ret)
    {
        LOG_ERR("Can't configure rst gpio");
        return ret;
    }
    // reset chip
    ret = gpio_pin_set_dt(m_chip.rst_spec, true);
    k_msleep(10);
    ret = gpio_pin_set_dt(m_chip.rst_spec, false);
    return ret;
}

int SI4703TuneTo(uint32_t freq_kHz)
{
    int ret = -EINVAL;

    if (freq_kHz < FREQ_MIN || freq_kHz > FREQ_MAX)
    {
        return ret;
    }

    if (m_chip.state < TUNER_READY)
    {
        return -EAGAIN;
    }

    ret = si4703_set_freq(&m_chip, freq_kHz);
    if (!ret)
    {
        ret = si4703_set_vol(&m_chip, 15);
    }
    return ret;
}

int SI4703DiscoverStations(void)
{
    if (m_chip.state < TUNER_READY)
    {
        return -EAGAIN;
    }

    m_chip.disco_station = MAX_CHANNELS;
    m_chip.state = TUNER_DISCOVERY;
    return 0;
}

int SI4703GetStations(struct station_info **stations, uint32_t *num_stations)
{
    int ret = -EINVAL;
    uint32_t nstations;

    if (stations)
    {
        *stations = m_chip.stations;
    }

    if (num_stations)
    {
        *num_stations = 0;
    }

    if (m_chip.state < TUNER_READY)
    {
        return -EAGAIN;
    }

    for (nstations = 0; nstations < MAX_CHANNELS; nstations++)
    {
        if (m_chip.stations[nstations].rssi == 0)
        {
            break;
        }
    }

    if (num_stations)
    {
        *num_stations = nstations;
    }

    ret = 0;
    return ret;
}

int SI4703Slice(uint32_t *delay, tuner_state_t *state)
{
    uint64_t now = k_uptime_get();
    uint16_t val;
    uint16_t rssi;
    bool complete;
    int ret;

    switch (m_chip.state)
    {
    case TUNER_INIT:
        si4703_reset(&m_chip, 0);

        // enable osc
        ret = si4703_write(&m_chip, REG_TEST1, 0x8100);
        if (ret)
        {
            break;
        }

        m_chip.state = TUNER_RESET;
        m_chip.state_time = now + POST_RESET_MS;
        *delay = 50;
        break;

    case TUNER_RESET:
        if (now > m_chip.state_time)
        {
            // get intitial val for all regs
            ret = si4703_read(&m_chip, REG_READ_START - 1, &val);
            if (ret)
            {
                m_chip.state = TUNER_INIT;
                break;
            }

            // turn on power
            ret = si4703_power_up(&m_chip);
            if (ret)
            {
                m_chip.state = TUNER_INIT;
                break;
            }

            m_chip.state = TUNER_CONFIG;
            m_chip.state_time = now + POST_POWERUP_MS;
        }

        *delay = 50;
        break;

    case TUNER_CONFIG:
        if (now > m_chip.state_time)
        {
            si4703_read(&m_chip, REG_CHIPID, &val);
            if (!(val & 0xFF))
            {
                // no f/w version in chip-id, re-init
                m_chip.state = TUNER_INIT;
                break;
            }

            LOG_INF("Chip ID: 0x%04X", val);

            ret = si4703_config(&m_chip);
            if (ret)
            {
                m_chip.state = TUNER_INIT;
                break;
            }

            m_chip.state = TUNER_READY;
            m_chip.state_time = now + POST_CONFIG_MS;
        }

        *delay = 20;
        break;

    case TUNER_DISCOVERY:
        ret = si4703_disco_slice(&m_chip, &complete);
        if (ret)
        {
            m_chip.state = TUNER_INIT;
            break;
        }

        if (complete)
        {
            m_chip.state = TUNER_READY;
        }

        *delay = 20;
        break;

    case TUNER_READY:
        *delay = 100;
        break;

    case TUNER_TUNED:
        rssi = si4703_get_rssi(&m_chip);
        if (rssi & 0x8000)
        {
            si4703_decode_rds(&m_chip);

            if (m_chip.rds_changed && m_chip.cur_station < MAX_CHANNELS)
            {
                float freq = (float)(m_chip.stations[m_chip.cur_station].freq_kHz) / 1000.0;

                m_chip.stereo = (rssi & 0x100) ? true : false;
                m_chip.stations[m_chip.cur_station].rssi = rssi & 0xFF;

                LOG_INF("%2d  %4.1f %s S:%8s  T:%s",
                        m_chip.cur_station, freq,
                        m_chip.stereo ? "stereo" : "mono", m_chip.name, m_chip.text);

                memcpy(m_chip.stations[m_chip.cur_station].name, m_chip.name, sizeof(m_chip.stations[m_chip.cur_station].name));
                memcpy(m_chip.stations[m_chip.cur_station].text, m_chip.text, sizeof(m_chip.stations[m_chip.cur_station].text));
            }
        }

        *delay = 20;
        break;
    }

    if (state)
    {
        *state = m_chip.state;
    }

    return 0;
}

int SI4703Init(tuner_seek_threshold_t seek_threshold)
{
    int ret = -EINVAL;

    m_chip.rst_spec = &gpio_rst;
    m_chip.i2c_spec = &i2c_dev;

    m_chip.state = TUNER_INIT;

    // translate generic seek type to individual thresholds
    switch (seek_threshold)
    {
    case TUNER_SEEK_ALL:
        m_chip.seek_thr = 0;
        m_chip.sksnr    = 4;
        m_chip.skcnt    = 15;
        break;
    case TUNER_SEEK_MOST:
        m_chip.seek_thr = 12;
        m_chip.sksnr    = 4;
        m_chip.skcnt    = 8;
        break;
    case TUNER_SEEK_BETTER:
        m_chip.seek_thr = 25;
        m_chip.sksnr    = 4;
        m_chip.skcnt    = 8;
        break;
    case TUNER_SEEK_BEST:
        m_chip.seek_thr = 12;
        m_chip.sksnr    = 7;
        m_chip.skcnt    = 15;
        break;
    case TUNER_SEEK_DEFAULT:
    default:
        m_chip.seek_thr = 25;
        m_chip.sksnr    = 0;
        m_chip.skcnt    = 0;
    }

    LOG_INF("Start tuner  seek_thr=%u  sksnr=%u  skcnt=%u",
                m_chip.seek_thr, m_chip.sksnr, m_chip.skcnt);
    ret = 0;
    return ret;
}

#if CONFIG_SHELL

#include <zephyr/shell/shell.h>
#include <stdlib.h>

static int _CommandSetVol(const struct shell *s, size_t argc, char **argv)
{
    uint32_t vol = 15;

    if (argc > 1)
    {
        vol = strtoul(argv[1], NULL, 0);
    }

    si4703_set_vol(&m_chip, vol);

    return 0;
}

static int _CommandSetFreq(const struct shell *s, size_t argc, char **argv)
{
    uint32_t freq = 89700;

    if (argc > 1)
    {
        freq = strtoul(argv[1], NULL, 0);
    }

    si4703_set_freq(&m_chip, freq);
    si4703_set_vol(&m_chip, 15);

    return 0;
}

static int _CommandSetStation(const struct shell *s, size_t argc, char **argv)
{
    uint32_t station = 0;

    if (argc > 1)
    {
        station = strtoul(argv[1], NULL, 0);
    }

    if (station >= MAX_CHANNELS)
    {
        shell_print(s, "station should be < %u", MAX_CHANNELS);
        return 0;
    }

    if (m_chip.stations[station].rssi == 0)
    {
        shell_print(s, "station %u wasn't discovered", station);
        return 0;
    }

    si4703_set_freq(&m_chip, m_chip.stations[station].freq_kHz);
    si4703_set_vol(&m_chip, 15);

    return 0;
}

static int _CommandRst(const struct shell *s, size_t argc, char **argv)
{
    si4703_reset(&m_chip, 1);
    return 0;
}

static int _CommandSeekUp(const struct shell *s, size_t argc, char **argv)
{
    si4703_seek(&m_chip, 1);
    si4703_set_vol(&m_chip, 15);
    return 0;
}

static int _CommandSeekDown(const struct shell *s, size_t argc, char **argv)
{
    si4703_seek(&m_chip, 0);
    si4703_set_vol(&m_chip, 15);
    return 0;
}

static int _CommandTuneUp(const struct shell *s, size_t argc, char **argv)
{
    uint16_t chan = si4703_get_chan(&m_chip);
    uint32_t freq;

    freq = chan * CHANNEL_SPACE + FREQ_MIN;
    if (freq >= FREQ_MAX)
    {
        freq = FREQ_MIN;
    }
    else
    {
        freq += CHANNEL_SPACE;
    }

    si4703_set_freq(&m_chip, freq);
    si4703_set_vol(&m_chip, 15);
    return 0;
}

static int _CommandTuneDown(const struct shell *s, size_t argc, char **argv)
{
    uint16_t chan = si4703_get_chan(&m_chip);
    uint32_t freq;

    freq = chan * CHANNEL_SPACE + FREQ_MIN;
    if (freq <= FREQ_MIN)
    {
        freq = FREQ_MAX;
    }
    else
    {
        freq -= CHANNEL_SPACE;
    }

    si4703_set_freq(&m_chip, freq);
    si4703_set_vol(&m_chip, 15);
    return 0;
}

static int _CommandPrint(const struct shell *s, size_t argc, char **argv)
{
    si4703_show_channel(&m_chip);

    shell_print(s, "Station List:");

    for (int station = 0; station < MAX_CHANNELS; station++)
    {
        if (m_chip.stations[station].rssi == 0)
        {
            break;
        }

        shell_print(s, "%2d %4.1f  rssi:%u  S:%8s T:%s",
                station,
                ((float)(m_chip.stations[station].channel * CHANNEL_SPACE + FREQ_MIN)) / 1000.0,
                m_chip.stations[station].rssi,
                m_chip.stations[station].name,
                m_chip.stations[station].text);
    }

    return 0;
}

static int _CommandPoll(const struct shell *s, size_t argc, char **argv)
{
    int timeout = 30;
    uint16_t rssi;

    if (argc > 1)
    {
        timeout = strtol(argv[1], NULL, 0);
    }

    timeout *= 1000;

    while (timeout > 0)
    {
        k_msleep(2);
        timeout -= 2;

        // wait for rds ready
        rssi = si4703_get_rssi(&m_chip);
        if (rssi & 0x8000)
        {
            si4703_decode_rds(&m_chip);

            if (m_chip.rds_changed)
            {
                LOG_INF("S:%8s  T:%s", m_chip.name, m_chip.text);
            }
        }
    }
    return 0;
}

static int _CommandDisco(const struct shell *s, size_t argc, char **argv)
{
    int timeout = 30;
    bool complete = false;

    if (argc > 1)
    {
        timeout = strtol(argv[1], NULL, 0);
    }

    timeout *= 1000;

    m_chip.disco_station = MAX_CHANNELS;

    do
    {
        si4703_disco_slice(&m_chip, &complete);
    }
    while(!complete && timeout-- > 0);

    LOG_INF("Stations:");
    for (int curchan = 0; curchan < MAX_CHANNELS; curchan++)
    {
        if (m_chip.stations[curchan].rssi == 0)
        {
            break;
        }

        LOG_INF("%d %4.1f  rssi:%u  S:%8s T:%s",
                curchan,
                ((float)(m_chip.stations[curchan].channel * CHANNEL_SPACE + FREQ_MIN)) / 1000.0,
                m_chip.stations[curchan].rssi,
                m_chip.stations[curchan].name,
                m_chip.stations[curchan].text);
    }

    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(_sub_tuner_commands,
       SHELL_CMD_ARG(vol,       NULL,  "Set Volume [vol  0-15 (15)]", _CommandSetVol, 1, 2),
       SHELL_CMD_ARG(freq,      NULL,  "Set Frequencey [kHz (89700)]", _CommandSetFreq, 1, 2),
       SHELL_CMD_ARG(station,   NULL,  "Set Station [0-max (0)]", _CommandSetStation, 1, 2),
       SHELL_CMD(seeku,         NULL,  "Seek Up", _CommandSeekUp),
       SHELL_CMD(seekd,         NULL,  "Seek Down", _CommandSeekDown),
       SHELL_CMD(tuneu,         NULL,  "Tune Up", _CommandTuneUp),
       SHELL_CMD(tuned,         NULL,  "Tune Down", _CommandTuneDown),
       SHELL_CMD(print,         NULL,  "Print out info", _CommandPrint),
       SHELL_CMD_ARG(poll,      NULL,  "Poll staton rds [seconds (30)]", _CommandPoll, 1, 2),
       SHELL_CMD_ARG(disco,     NULL,  "Discover Stations [seconds (30)]", _CommandDisco, 1, 2),
       SHELL_CMD(rst,           NULL,  "Reset", _CommandRst),
       SHELL_SUBCMD_SET_END );

SHELL_CMD_REGISTER(tuner, &_sub_tuner_commands, "Tuner commands", NULL);

#endif

