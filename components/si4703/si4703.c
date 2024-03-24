
#include "si4703.h"
#include <tuner.h>
#include <asserts.h>
#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
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
    uint32_t    cur_channel;
    uint8_t     curvol;
    bool        stereo;
    bool        muted;
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

static int si4703_get_rssi(struct si4703_device *chip, uint8_t *out_rssi, bool *out_afcrailed)
{
    int ret = -EINVAL;
    uint16_t val;

    ret = si4703_read(chip, REG_RSSI, &val);
    require_noerr(ret, exit);

    if (out_rssi)
    {
        *out_rssi = val & 0xFF;
    }

    if (out_afcrailed)
    {
        *out_afcrailed = (val & AFCRL) ? true : false;
    }
exit:
    return ret;
}

static int si4703_get_int_status(struct si4703_device *chip, uint16_t *out_status)
{
    int ret = -EINVAL;
    uint16_t val;

    require(out_status, exit);

    ret = si4703_read(chip, REG_RSSI, &val);
    require_noerr(ret, exit);

    *out_status = val & 0xFF00;
exit:
    return ret;
}

static int si4703_wait_stc(struct si4703_device *chip, int timeoutms, int on_off)
{
    int ret;
    int timeout = timeoutms;
    uint16_t val;

    do
    {
        ret = si4703_get_int_status(chip, &val);
        require_noerr(ret, exit);

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

    ret = 0;
exit:
    return ret;
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
    uint16_t status;
    uint16_t chan;
    uint8_t rssi;
    uint32_t khz;
    float freq;

    ret = si4703_get_rssi(chip, &rssi, NULL);
    require_noerr(ret, exit);

    ret = si4703_get_int_status(chip, &status);
    require_noerr(ret, exit);

    chan = si4703_get_chan(chip) & 0x3ff;

    khz = FREQ_MIN + chan * CHANNEL_SPACE;
    freq = (float)khz / 1000;

    LOG_INF("Channel: %d  %4.1f MHz %u %s rds:%s",
            chan, freq, rssi & 0xFF,
            (status & 0x100) ? "stereo" : "mono",
            (status & 0x8000) ? "rdy" : "");
exit:
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
    uint8_t rssi;
    bool afcrailed;

    if (chip->state < TUNER_DISCOVERY)
    {
        return -EAGAIN;
    }

    if (chip->state == TUNER_READY)
    {
        chip->state = TUNER_TUNED;
    }

    chip->cur_station = MAX_CHANNELS;

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

    ret = si4703_get_rssi(chip, &rssi, &afcrailed);
    require_noerr(ret, exit);

    if (afcrailed)
    {
        // not an error, just that there is prolly a better station close by
        LOG_INF("AFC railed on set chan");
    }

    chip->cur_channel = channel;
exit:
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

    chip->state = TUNER_TUNED;
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

    ret = si4703_get_int_status(chip, &stat);
    require_noerr(ret, exit);
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
        ret = si4703_get_int_status(chip, &stat);
        require_noerr(ret, exit);
        if (!(stat & AFCRL))
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

    chip->cur_channel = si4703_get_chan(chip);
    si4703_show_channel(chip);
exit:
    return ret;
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

static int SI4703GetBandInfo(
                        tuner_t *in_tuner,
                        const tuner_band_t in_band,
                        uint32_t *out_min_freq,
                        uint32_t *out_max_freq,
                        uint32_t *out_kHz_per_channel
                        )
{
    int ret = -EINVAL;

    require(in_band == TUNER_FM, exit);

    *out_min_freq = FREQ_MIN;
    *out_max_freq = FREQ_MAX;
    *out_kHz_per_channel = CHANNEL_SPACE;
    ret = 0;
exit:
    return ret;
}

static int SI4703GetTunedFreq(
                        tuner_t *in_tuner,
                        uint32_t *out_freq_kHz
                        )
{
    int ret = -EAGAIN;

    if (m_chip.state != TUNER_TUNED)
    {
        return ret;
    }

    if  (out_freq_kHz)
    {
        *out_freq_kHz = m_chip.cur_channel * CHANNEL_SPACE + FREQ_MIN;
    }
    ret = 0;
    return ret;
}

static int SI4703GetRSSI(
                        tuner_t *in_tuner,
                        uint8_t *out_rssi,
                        bool    *out_stereo,
                        bool    *out_afc_railed
                        )
{
    int ret = -EAGAIN;
    uint16_t status;

    if (m_chip.state != TUNER_TUNED)
    {
        return ret;
    }

    ret = si4703_get_rssi(&m_chip, out_rssi, out_afc_railed);
    require_noerr(ret, exit);
    ret = si4703_get_int_status(&m_chip, &status);
    require_noerr(ret, exit);
    *out_stereo = (status & 0x100) ? true : false;
exit:
    return ret;
}

static int SI4703GetRDS(
                        tuner_t *in_tuner,
                        bool *out_rds_changed,
                        const char **out_rds_short,
                        const char **out_rds_long
                        )
{
    int ret = -EAGAIN;
    uint16_t status;

    if (m_chip.state != TUNER_TUNED)
    {
        return ret;
    }

    if (out_rds_changed)
    {
        *out_rds_changed = false;
    }

    ret = si4703_get_int_status(&m_chip, &status);
    require_noerr(ret, exit);
    if (status & 0x8000)
    {
        si4703_decode_rds(&m_chip);

        if (m_chip.rds_changed)
        {
            if (out_rds_short)
            {
                *out_rds_short = m_chip.name;
            }
            if (out_rds_long)
            {
                *out_rds_long = m_chip.text;
            }
            if (out_rds_changed)
            {
                *out_rds_changed = true;
            }
        }
    }

    ret = 0;
exit:
    return ret;
}

int SI4703TuneTo(
                        tuner_t *in_tuner,
                        uint32_t freq_kHz
                )
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
    return ret;
}

int SI4703SetVolume(
                        tuner_t *in_tuner,
                        const uint32_t in_volume_percent
                )
{
    int ret = -EINVAL;
    uint32_t vol = in_volume_percent * 15 / 100;

    if (vol <= 15)
    {
        ret = si4703_set_vol(&m_chip, vol);
    }

    return ret;
}

int SI4703Tune(
                        tuner_t *in_tuner,
                        const bool in_up,
                        const bool in_wrap
                )
{
    int ret = -EINVAL;
    uint16_t chan = si4703_get_chan(&m_chip);
    uint32_t freq;

    freq = chan * CHANNEL_SPACE + FREQ_MIN;

    if (in_up)
    {
        if (freq >= FREQ_MIN)
        {
            if (in_wrap)
            {
                freq = FREQ_MIN;
            }
        }
        else
        {
            freq += CHANNEL_SPACE;
        }
    }
    else
    {
        if (freq <= FREQ_MIN)
        {
            if (in_wrap)
            {
                freq = FREQ_MAX;
            }
        }
        else
        {
            freq -= CHANNEL_SPACE;
        }
    }

    ret = si4703_set_freq(&m_chip, freq);
    return ret;
}

int SI4703Seek(
                        tuner_t *in_tuner,
                        const bool in_up,
                        const bool in_wrap
                )
{
    int ret = -EINVAL;

    ret = si4703_seek(&m_chip, in_up);
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

int SI4703Init(tuner_t *in_tuner, tuner_seek_threshold_t seek_threshold)
{
    int ret = -EINVAL;
    uint16_t val;

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

    // h/w reset
    si4703_reset(&m_chip, 0);

    // enable osc
    ret = si4703_write(&m_chip, REG_TEST1, 0x8100);
    require_noerr(ret, exit);
    k_msleep(POST_RESET_MS);

    // turn on power
    ret = si4703_power_up(&m_chip);
    require_noerr(ret, exit);
    k_msleep(POST_POWERUP_MS);

    ret  = si4703_read(&m_chip, REG_CHIPID, &val);
    require_noerr(ret, exit);
    if (!(val & 0xFF))
    {
        // no f/w version in chip-id, re-init
        LOG_ERR("No f/w version, fail");
    }

    LOG_INF("Chip ID: 0x%04X", val);

    ret = si4703_config(&m_chip);
    require_noerr(ret, exit);
    k_msleep(POST_CONFIG_MS);
    m_chip.state = TUNER_READY;
exit:
    return ret;
}

static tuner_t m_si4703 =
{
    .init = SI4703Init,
    .get_band_info = SI4703GetBandInfo,
    .set_volume = SI4703SetVolume,
    .set_tune = SI4703TuneTo,
    .get_tune = SI4703GetTunedFreq,
    .get_rssi = SI4703GetRSSI,
    .get_rds  = SI4703GetRDS,
    .tune     = SI4703Tune,
    .seek     = SI4703Seek
};

tuner_t *SI4703GetRadio(void)
{
    return &m_si4703;
}


#if CONFIG_SHELL

#include <zephyr/shell/shell.h>
#include <stdlib.h>

static int _CommandRst(const struct shell *s, size_t argc, char **argv)
{
    si4703_reset(&m_chip, 1);
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(_sub_si4703_commands,
       SHELL_CMD(rst,           NULL,  "Reset", _CommandRst),
       SHELL_SUBCMD_SET_END );

SHELL_CMD_REGISTER(si4703, &_sub_si4703_commands, "SI4703 Tuner commands", NULL);

#endif

