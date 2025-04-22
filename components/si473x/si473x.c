
#include "si473x.h"
#include <tuner.h>
#include <asserts.h>
#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(si473x, LOG_LEVEL_INF);

#define CONFIG_RADIO_FM_USA
#include "si473xregs.h"

static const struct i2c_dt_spec i2c_dev = I2C_DT_SPEC_GET(DT_NODELABEL(si473x));
static const struct gpio_dt_spec gpio_rst = GPIO_DT_SPEC_GET(DT_NODELABEL(gpio_si473x_rst), gpios);

#define MAX_CHANNELS    (32)

#define POST_RESET_MS       (510)
#define POST_POWERUP_MS     (110)
#define POST_CONFIG_MS      (50)

static struct si473x_device
{
    const struct i2c_dt_spec *i2c_spec;
    const struct gpio_dt_spec *rst_spec;

    tuner_state_t   state;

    bool        rds_changed;
    char        name[9];
    char        text[65];

    uint16_t    seek_thr;
    uint16_t    sksnr;
}
m_chip;

static int si473x_read(struct si473x_device *chip, size_t in_byte_count, uint8_t *out_bytes)
{
    int ret;

    ret = i2c_read_dt(chip->i2c_spec, out_bytes, in_byte_count);
    if (ret)
    {
        LOG_ERR("i2c read failed!");
        return -EIO;
    }

    return ret;
}

static int si473x_wait_cts(struct si473x_device *chip, const int32_t in_timeout_ms)
{
    int ret;
    uint8_t status;
    int32_t timeout = in_timeout_ms;

    k_usleep(300);

    do
    {
        ret = i2c_read_dt(chip->i2c_spec, &status, 1);
        if (ret)
        {
            LOG_ERR("i2c read failed");
            return -EIO;
        }

        if (!(ret & STATUS_CTS) && (timeout > 0))
        {
            k_msleep(10);
            timeout -= 10;
        }
    }
    while (!ret && !(status & STATUS_CTS) && (timeout > 0));

    if (!(status & STATUS_CTS))
    {
        LOG_ERR("Timeout waiting for CTS");
        ret = -ETIMEDOUT;
    }

    return ret;
}

static int si473x_write(struct si473x_device *chip, uint8_t *in_bytes, size_t in_count)
{
    int ret;

    ret = i2c_write_dt(chip->i2c_spec, in_bytes, in_count);
    if (ret)
    {
        LOG_ERR("i2c write failed!");
        return -EIO;
    }

    return ret;
}

static int si473x_send_cmd(struct si473x_device *chip, const uint8_t in_cmd, const uint32_t in_arg_count, uint8_t *in_args)
{
    int ret = -EINVAL;
    uint8_t buf[32];

    require(in_arg_count < sizeof(buf), exit);

    buf[0] = in_cmd;
    for (int i = 0; i < in_arg_count; i++)
    {
        buf[i + 1] = in_args[i];
    }

    // wait cts
    ret = si473x_wait_cts(chip, 600);
    require_noerr(ret, exit);

    // write cmd and args
    ret = si473x_write(chip, buf, 1 + in_arg_count);
    require_noerr(ret, exit);

    // wait cts
    ret = si473x_wait_cts(chip, 600);
    require_noerr(ret, exit);

exit:
    return ret;
}

static int si473x_send_prop(struct si473x_device *chip, const uint16_t in_prop, const uint16_t in_value)
{
    int ret = -EINVAL;
    uint8_t buf[5];

    buf[0] = 0;

    buf[1] = in_prop >> 8;
    buf[2] = in_prop & 0xFF;

    buf[3] = in_value >> 8;
    buf[4] = in_value & 0xFF;

    ret = si473x_send_cmd(chip, SET_PROPERTY, 5, buf);
    return ret;
}

static int si473x_set_vol(struct si473x_device *chip, uint8_t vol)
{
    int ret = -EINVAL;

    if (vol > 63)
    {
        return ret;
    }

    ret = si473x_send_prop(chip, RX_VOLUME, vol);
    return ret;
}

static int si473x_get_int_status(struct si473x_device *chip, uint8_t *out_status)
{
    int ret;

    ret = si473x_send_cmd(chip, GET_INT_STATUS, 0, NULL);
    require_noerr(ret, exit);

    ret = si473x_read(chip, 1, out_status);
    require_noerr(ret, exit);
exit:
    return ret;
}

static int si473x_wait_stc(struct si473x_device *chip, const uint32_t in_timeout_ms)
{
    int ret;
    int timeout = in_timeout_ms;
    uint8_t status;

    do
    {
        ret = si473x_get_int_status(chip, &status);
        require_noerr(ret, exit);
        if (!(status & 0x1))
        {
            timeout -= 4;
            k_msleep(4);
        }
    }
    while (!(status & 0x1) && (timeout > 0));

    if (!(status & 0x1))
    {
        LOG_WRN("no STC");
        ret = -ETIMEDOUT;
    }
exit:
     return ret;
}

static void si473x_clear_rds(struct si473x_device *chip)
{
    memset(chip->name, 0, sizeof(chip->name));
    memset(chip->text, 0, sizeof(chip->text));
}

int si473x_set_freq(struct si473x_device *chip, uint32_t in_freq)
{
    int ret = -EINVAL;
    uint16_t freq;
    uint8_t args[4];

    if (chip->state < TUNER_READY)
    {
        return -EAGAIN;
    }

    if (in_freq < FREQ_MIN || in_freq > FREQ_MAX)
    {
        return -EINVAL;
    }

    si473x_clear_rds(chip);

    freq = in_freq / 10;
    args[0] = 0;
    args[1] = freq >> 8;
    args[2] = freq & 0xFF;
    args[3] = 0;

    ret = si473x_send_cmd(chip, FM_TUNE_FREQ, 4, args);
    require_noerr(ret, exit);
    ret = si473x_wait_stc(&m_chip, 250);
    require_noerr(ret, exit);
exit:
    return ret;
}

static int si473x_get_tune_status(
                                struct si473x_device *chip,
                                bool in_clr_stc,
                                uint32_t *out_freq,
                                uint8_t *out_rssi,
                                bool *out_afc_railed
                                )
{
    int ret = -EINVAL;
    uint8_t args[1];
    uint8_t response[8];

    if (chip->state < TUNER_READY)
    {
        return -EAGAIN;
    }

    args[0] = in_clr_stc ? 0x3 : 0x0;
    ret = si473x_send_cmd(chip, FM_TUNE_STATUS, 1, args);
    require_noerr(ret, exit);

    ret = si473x_read(chip, 8, response);
    require_noerr(ret, exit);

    if (out_freq)
    {
        uint32_t freq = ((uint32_t)response[2] << 8) | (uint32_t)response[3];

        *out_freq = freq * 10;
    }

    if (out_rssi)
    {
        *out_rssi = response[4];
    }

    if (out_afc_railed)
    {
        *out_afc_railed = (response[1] & 0x2) ? true : false;
    }

exit:
    return ret;
}

static int si473x_get_rssi(struct si473x_device *chip, uint8_t *out_rssi, bool *out_stereo, bool *out_afc_railed)
{
    int ret = -EINVAL;
    uint8_t args[1];
    uint8_t response[8];

    if (chip->state < TUNER_READY)
    {
        return -EAGAIN;
    }

    args[0] = 0;
    ret = si473x_send_cmd(chip, FM_RSQ_STATUS, 1, args);
    require_noerr(ret, exit);

    ret = si473x_read(chip, 8, response);
    require_noerr(ret, exit);

    if (out_rssi)
    {
        *out_rssi = response[4];
    }

    if (out_stereo)
    {
        *out_stereo = (response[3] & 0x8) ? true : false;
    }

    if (out_stereo)
    {
        *out_stereo = (response[2] & 0x2) ? true : false;
    }
exit:
    return ret;
}

static int si473x_bump_afcrailed(struct si473x_device *chip, bool in_up, bool in_wrap)
{
    int ret;
    uint32_t cur_freq;
    uint32_t next_freq;
    uint8_t rssi;
    bool railed = false;
    int bumps = 0;

    ret = si473x_get_tune_status(chip, false, &cur_freq, NULL, NULL);
    require_noerr(ret, exit);

    do
    {
        ret = si473x_get_rssi(chip, &rssi, NULL, &railed);
        require_noerr(ret, exit);

        if (!railed)
        {
            break;
        }
        // AFC railed means we are close to a stronger signal, so
        // go that way to get the clear station
        //
        next_freq = cur_freq;

        if (in_up)
        {
            if (next_freq >= FREQ_MAX)
            {
                if (in_wrap)
                {
                    next_freq = FREQ_MIN;
                }
            }
            else
            {
                next_freq += CHANNEL_SPACE;
            }
        }
        else
        {
            if (next_freq <= FREQ_MIN)
            {
                if (in_wrap)
                {
                    next_freq = FREQ_MAX;
                }
            }
            else
            {
                next_freq -= CHANNEL_SPACE;
            }
        }

        LOG_INF("AFC railed, bump %s  from %5.1f to %5.1f",
                    in_up ? "up" : "down", (float)cur_freq / 1000.0, (float)next_freq / 1000.0);
        ret = si473x_set_freq(chip, next_freq);
        cur_freq = next_freq;
    }
    while (bumps < 10);

exit:
    return ret;
}

static int si473x_seek(struct si473x_device *chip, bool in_up, bool in_wrap)
{
    int ret = -EINVAL;
    uint8_t args[1];

    ret = si473x_get_tune_status(chip, true, NULL, NULL, NULL);
    require_noerr(ret, exit);

    args[0] = in_up ? 0x8 : 0x0;
    args[0] |= in_wrap ? 0x4 : 0x0;
    ret = si473x_send_cmd(chip, FM_SEEK_START, 4, args);
    require_noerr(ret, exit);
    ret = si473x_wait_stc(&m_chip, 250);
    require_noerr(ret, exit);
exit:
    return ret;
}

static int si473x_show_channel(struct si473x_device *chip)
{
    int ret = 0;
    bool railed;
    bool stereo;
    uint8_t rssi;
    uint32_t khz;
    uint32_t chan;
    float freq;

    ret = si473x_get_tune_status(chip, true, &khz, &rssi, &railed);
    require_noerr(ret, exit);

    ret = si473x_get_rssi(chip, &rssi, &stereo, NULL);
    require_noerr(ret, exit);

    freq = (float)khz / 1000;
    chan = (khz - FREQ_MIN) / CHANNEL_SPACE;
    LOG_INF("Channel: %d  %4.1f MHz %u %s",
            chan, freq, rssi, stereo ? "st" : "mono");
exit:
    return ret;
}


#if 0
static int si473x_decode_rds(struct si473x_device *chip)
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

    ret  = si473x_read(chip, REG_RDSA, &rds[0]);
    ret += si473x_read(chip, REG_RDSB, &rds[1]);
    ret += si473x_read(chip, REG_RDSC, &rds[2]);
    ret += si473x_read(chip, REG_RDSD, &rds[3]);
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
#endif

static int si473x_config(struct si473x_device *chip)
{
    int ret;

    ret = si473x_send_prop(chip, FM_SEEK_BAND_BOTTOM, FREQ_MIN / 10);
    require_noerr(ret, exit);
    ret = si473x_send_prop(chip, FM_SEEK_BAND_TOP, FREQ_MAX / 10);
    require_noerr(ret, exit);
    ret = si473x_send_prop(chip, FM_SEEK_FREQ_SPACING, CHANNEL_SPACE / 10);
    require_noerr(ret, exit);
    ret = si473x_send_prop(chip, FM_SEEK_TUNE_RSSI_THRESHOLD, chip->seek_thr);
    require_noerr(ret, exit);
    ret = si473x_send_prop(chip, FM_SEEK_TUNE_SNR_THRESHOLD, chip->sksnr);
    require_noerr(ret, exit);
exit:
    return ret;
}

static int si473x_power_up(struct si473x_device *chip)
{
    int ret = -ENOENT;
    uint8_t args[2];
    uint8_t response[16];

    args[0] = 0xD0;
    args[1] = 0x05;

    ret = si473x_send_cmd(chip, POWER_UP, 2, args);
    require_noerr(ret, exit);

    ret = si473x_send_cmd(chip, GET_REV, 0, NULL);
    require_noerr(ret, exit);

    // read 16 bytes of chip rev
    ret = si473x_read(chip, 16, response);
    require_noerr(ret, exit);

    LOG_HEXDUMP_INF(response, 16, "chip rev");
exit:
    return ret;
}

static int si473x_power_down(struct si473x_device *chip)
{
    int ret = -ENOENT;

    ret = si473x_send_cmd(chip, POWER_DOWN, 0, NULL);
    require_noerr(ret, exit);

exit:
    return ret;
}

static int si473x_reset(struct si473x_device *chip, int longloop)
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

static int SI473XGetBandInfo(
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

static int SI473XGetTunedFreq(
                        tuner_t *in_tuner,
                        uint32_t *out_freq_kHz
                        )
{
    int ret = -EAGAIN;

    ret = si473x_get_tune_status(&m_chip, false, out_freq_kHz, NULL, NULL);
    return ret;
}

static int SI473XGetRSSI(
                        tuner_t *in_tuner,
                        uint8_t *out_rssi,
                        bool    *out_stereo,
                        bool    *out_afc_railed
                        )
{
    int ret = -EAGAIN;

    ret = si473x_get_rssi(&m_chip, out_rssi, out_stereo, out_afc_railed);
    return ret;
}

static int SI473XGetRDS(
                        tuner_t *in_tuner,
                        bool *out_rds_changed,
                        const char **out_rds_short,
                        const char **out_rds_long
                        )
{
    int ret = -EAGAIN;

    if (out_rds_changed)
    {
        *out_rds_changed = false;
    }

    /*
    rssi = si473x_get_rssi(&m_chip);
    if (rssi & 0x8000)
    {
        si473x_decode_rds(&m_chip);

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
    */
    return ret;
}

int SI473XTuneTo(
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

    ret = si473x_set_freq(&m_chip, freq_kHz);
    return ret;
}

int SI473XSetVolume(
                        tuner_t *in_tuner,
                        const uint32_t in_volume_percent
                )
{
    int ret = -EINVAL;
    uint32_t vol = in_volume_percent * 63 / 100;

    if (vol <= 63)
    {
        ret = si473x_set_vol(&m_chip, vol);
    }

    return ret;
}

int SI473XTune(
                        tuner_t *in_tuner,
                        const bool in_up,
                        const bool in_wrap
                )
{
    int ret = -EINVAL;
    uint32_t freq;

    ret = si473x_get_tune_status(&m_chip, true, &freq, NULL, NULL);
    require_noerr(ret, exit);

    if (in_up)
    {
        if (freq >= FREQ_MAX)
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

    ret = si473x_set_freq(&m_chip, freq);
    require_noerr(ret, exit);

    ret = si473x_show_channel(&m_chip);
exit:
    return ret;
}

int SI473XSeek(
                        tuner_t *in_tuner,
                        const bool in_up,
                        const bool in_wrap
                )
{
    int ret = -EINVAL;

    ret = si473x_seek(&m_chip, in_up, in_wrap);
    require_noerr(ret, exit);

    ret = si473x_wait_stc(&m_chip, 250);
    require_noerr(ret, exit);

    ret = si473x_bump_afcrailed(&m_chip, in_up, in_wrap);
    require_noerr(ret, exit);

    si473x_show_channel(&m_chip);
exit:
    return ret;
}

int SI473XInit(tuner_t *in_tuner, tuner_seek_threshold_t seek_threshold)
{
    int ret = -EINVAL;

    m_chip.rst_spec = &gpio_rst;
    m_chip.i2c_spec = &i2c_dev;

    // translate generic seek type to individual thresholds
    switch (seek_threshold)
    {
    case TUNER_SEEK_ALL:
        m_chip.seek_thr = 0;
        m_chip.sksnr    = 3;
        break;
    case TUNER_SEEK_MOST:
        m_chip.seek_thr = 12;
        m_chip.sksnr    = 3;
        break;
    case TUNER_SEEK_BETTER:
        m_chip.seek_thr = 16;
        m_chip.sksnr    = 3;
        break;
    case TUNER_SEEK_BEST:
        m_chip.seek_thr = 20;
        m_chip.sksnr    = 7;
        break;
    case TUNER_SEEK_DEFAULT:
    default:
        m_chip.seek_thr = 18;
        m_chip.sksnr    = 3;
    }

    LOG_INF("Start tuner  seek_thr=%u  sksnr=%u",
                m_chip.seek_thr, m_chip.sksnr);

    // h/w reset
    ret = si473x_reset(&m_chip, 0);
    require_noerr(ret, exit);
    k_msleep(POST_RESET_MS);

    // turn on power
    ret = si473x_power_up(&m_chip);
    require_noerr(ret, exit);
    k_msleep(POST_POWERUP_MS);

    // setup config
    ret = si473x_config(&m_chip);
    require_noerr(ret, exit);
    k_msleep(POST_CONFIG_MS);

    m_chip.state = TUNER_READY;
exit:
    return ret;
}

static tuner_t m_si473x =
{
    .init = SI473XInit,
    .get_band_info = SI473XGetBandInfo,
    .set_volume = SI473XSetVolume,
    .set_tune = SI473XTuneTo,
    .get_tune = SI473XGetTunedFreq,
    .get_rssi = SI473XGetRSSI,
    .get_rds  = SI473XGetRDS,
    .tune     = SI473XTune,
    .seek     = SI473XSeek
};

tuner_t *SI473XGetRadio(void)
{
    return &m_si473x;
}


#if CONFIG_SHELL

#include <zephyr/shell/shell.h>
#include <stdlib.h>

static int _CommandRst(const struct shell *s, size_t argc, char **argv)
{
    si473x_reset(&m_chip, 1);
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(_sub_si473x_commands,
       SHELL_CMD(rst,           NULL,  "Reset", _CommandRst),
       SHELL_SUBCMD_SET_END );

SHELL_CMD_REGISTER(si473x, &_sub_si473x_commands, "SI473X Tuner commands", NULL);

#endif

