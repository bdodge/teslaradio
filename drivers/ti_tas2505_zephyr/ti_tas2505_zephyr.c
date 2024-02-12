/*
 * Copyright (c) 2024, Level Home Inc.
 *
 * All rights reserved.
 *
 * Proprietary and confidential. Unauthorized copying of this file,
 * via any medium is strictly prohibited.
 *
 */

#define DT_DRV_COMPAT level_tas2505

#define COMPONENT_NAME tas2505
#include "Logging.h"

#include <zephyr/pm/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/audio/codec.h>

#define TAS2505_PAGE_SHIFT      (8)

#define TAS2505_RATES   SNDRV_PCM_RATE_8000_96000
#define TAS2505_FORMATS SNDRV_PCM_FMTBIT_S24_LE

#define TAS2505_REG(page, reg)  ((page << TAS2505_PAGE_SHIFT) + reg)

#define TAS2505_PAGECTL         TAS2505_REG(0, 0x00)
#define TAS2505_RESET           TAS2505_REG(0, 0x01)
#define TAS2505_CLKMUX          TAS2505_REG(0, 0x04)
#define TAS2505_PLLPR           TAS2505_REG(0, 0x05)
#define TAS2505_PLLJ            TAS2505_REG(0, 0x06)
#define TAS2505_PLLDMSB         TAS2505_REG(0, 0x07)
#define TAS2505_PLLDLSB         TAS2505_REG(0, 0x08)
#define TAS2505_NDAC            TAS2505_REG(0, 0x0B)
#define TAS2505_MDAC            TAS2505_REG(0, 0x0C)
#define TAS2505_DOSRMSB         TAS2505_REG(0, 0x0D)
#define TAS2505_DOSRLSB         TAS2505_REG(0, 0x0E)
#define TAS2505_IFACE1          TAS2505_REG(0, 0x1B)
#define TAS2505_IFACE2          TAS2505_REG(0, 0x1C)
#define TAS2505_IFACE3          TAS2505_REG(0, 0x1D)
#define TAS2505_BCLKNDIV        TAS2505_REG(0, 0x1E)
#define TAS2505_DACFLAG1        TAS2505_REG(0, 0x25)
#define TAS2505_DACFLAG2        TAS2505_REG(0, 0x26)
#define TAS2505_STICKYFLAG1     TAS2505_REG(0, 0x2A)
#define TAS2505_INTFLAG1        TAS2505_REG(0, 0x2B)
#define TAS2505_STICKYFLAG2     TAS2505_REG(0, 0x2C)
#define TAS2505_INTFLAG2        TAS2505_REG(0, 0x2E)
#define TAS2505_DACINSTRSET     TAS2505_REG(0, 0x3C)
#define TAS2505_DACSETUP1       TAS2505_REG(0, 0x3F)
#define TAS2505_DACSETUP2       TAS2505_REG(0, 0x40)
#define TAS2505_DACVOL          TAS2505_REG(0, 0x41)
#define TAS2505_REF_POR_LDO_BGAP_CTRL   TAS2505_REG(1, 1)
#define TAS2505_LDO_CTRL        TAS2505_REG(1, 0x02)
#define TAS2505_PLAYBACKCONF1   TAS2505_REG(1, 0x03)
#define TAS2505_PGA_CTRL        TAS2505_REG(1, 0x08)
#define TAS2505_AINL_AINR_CTRL  TAS2505_REG(1, 0x09)
#define TAS2505_COMMON_CTRL     TAS2505_REG(1, 0x0A)
#define TAS2505_HPH_OVERCURRENT TAS2505_REG(1, 0x0B)
#define TAS2505_HPH_ROUTING     TAS2505_REG(1, 0x0C)
#define TAS2505_HPH_GAIN        TAS2505_REG(1, 0x10)
#define TAS2505_HPHDRIVER_CTRL  TAS2505_REG(1, 0x14)
#define TAS2505_HPH_VOL         TAS2505_REG(1, 0x16)
#define TAS2505_AINL_VOL        TAS2505_REG(1, 0x18)
#define TAS2505_AINR_VOL        TAS2505_REG(1, 0x19)
#define TAS2505_SPKAMPCTRL1     TAS2505_REG(1, 0x2D)
#define TAS2505_SPKVOL1         TAS2505_REG(1, 0x2E)
#define TAS2505_SPKVOL2         TAS2505_REG(1, 0x30)
#define TAS2505_DACANLGAINFLAG  TAS2505_REG(1, 0x3F)

#define TAS2505_PLLPR_P_MASK                0x70
#define TAS2505_PLLPR_R_MASK                0xf
#define TAS2505_PLL_DAC_MASK                0x7f
#define TAS2505_BCLKNDIV_MASK               0x7f
#define TAS2505_IFACE1_DATALEN_MASK         0x30
#define TAS2505_IFACE1_WCLKDIR_MASK         0x4
#define TAS2505_IFACE1_BCLKDIR_MASK         0x8
#define TAS2505_IFACE1_INTERFACE_MASK       0xc0
#define TAS2505_IFACE3_BDIVCLKIN_MASK       0x1
#define TAS2505_IFACE3_BCLKINV_MASK         0x8
#define TAS2505_DACSETUP1_POWER_MASK        0x80
#define TAS2505_DACSETUP2_MUTE_MASK         0x8
#define TAS2505_PM_MASK                     0x80
#define TAS2505_LDO_PLL_HP_LVL_MASK         0x8
#define TAS2505_REF_POR_LDO_BGAP_MASTER_REF_MASK    0x10

#define TAS2505_PLLPR_P_SHIFT       4
#define TAS2505_PLL_CLKIN_SHIFT     2

#define TAS2505_IFACE1_DATALEN_SHIFT    4
#define TAS2505_IFACE1_INTERFACE_SHIFT  6
#define TAS2505_IFACE3_BCLKINV_SHIFT    4

#define TAS2505_WORD_LEN_20BITS     1
#define TAS2505_WORD_LEN_24BITS     2
#define TAS2505_WORD_LEN_32BITS     3

#define TAS2505_DSP_MODE        1
#define TAS2505_RJF_MODE        2
#define TAS2505_LJF_MODE        3

#define TAS2505_PLL_CLKIN_MCLK      0
#define TAS2505_PLL_CLKIN_BCLK      1
#define TAS2505_PLL_CLKIN_GPIO      2
#define TAS2505_PLL_CLKIN_DIN       3
#define TAS2505_CODEC_CLKIN_PLL     3

struct tas2505_cfg
{
    struct gpio_dt_spec     reset_gpio;
    struct i2c_dt_spec      i2c;
};

struct tas2505_data
{
    uint8_t                 cur_page;
};

#define DEV_CFG(d) ((d)?((const struct tas2505_cfg *)((d)->config)):NULL)
#define DEV_DATA(d)((d)?((struct tas2505_data *)((d)->data)):NULL)

static int _TAS2505ReadReg(const struct device *device, uint16_t in_reg, uint8_t *out_data, const size_t in_count)
{
    int ret = -EINVAL;
    const struct tas2505_cfg *config = DEV_CFG(device);
    struct tas2505_data *data = DEV_DATA(device);
    uint8_t page = in_reg >> TAS2505_PAGE_SHIFT;
    uint8_t i2c_reg = in_reg & 0xFF;

    require(config && data && out_data, exit);
    if (page != data->cur_page)
    {
        //LOG_INF("Write page:%d reg%02X = %02X", page, TAS2505_PAGECTL, page);
        ret = i2c_reg_write_byte_dt(&config->i2c, TAS2505_PAGECTL, page);
        require_noerr(ret, exit);
        data->cur_page = page;
    }

    ret = i2c_write_read_dt(&config->i2c, &i2c_reg, 1, out_data, in_count);
    require_noerr(ret, exit);
    //LOG_INF("Read page:%d reg%02X = %02X", page, i2c_reg, out_data[0]);
exit:
    return ret;
}

static int _TAS2505WriteReg(const struct device *device, uint16_t in_reg, uint8_t in_val)
{
    int ret = -EINVAL;
    const struct tas2505_cfg *config = DEV_CFG(device);
    struct tas2505_data *data = DEV_DATA(device);
    uint8_t page = in_reg >> TAS2505_PAGE_SHIFT;
    uint8_t i2c_reg = in_reg & 0xFF;

    require(config && data, exit);
    if (page != data->cur_page)
    {
        //LOG_INF("Write page:%d reg%02X = %02X", page, TAS2505_PAGECTL, page);
        ret = i2c_reg_write_byte_dt(&config->i2c, TAS2505_PAGECTL, page);
        require_noerr(ret, exit);
        data->cur_page = page;
    }

    ret = i2c_reg_write_byte_dt(&config->i2c, i2c_reg, in_val);
    require_noerr(ret, exit);
    //LOG_INF("Write page:%d reg%02X = %02X", page, i2c_reg, in_val);
exit:
    return ret;
}

static int _TAS2505SetBits(const struct device *device, uint16_t in_reg, uint8_t in_mask, uint8_t in_bits)
{
    int ret = -EINVAL;
    uint8_t reg_val;

    ret = _TAS2505ReadReg(device, in_reg, &reg_val, 1);
    require_noerr(ret, exit);

    if (in_mask)
    {
        reg_val &= ~in_mask;
    }

    reg_val |= in_bits;
    ret = _TAS2505WriteReg(device, in_reg, reg_val);
    require_noerr(ret, exit);
exit:
    return ret;
}

static void _TAS2505StartOutput(const struct device *device)
{
    // power up DAC
    _TAS2505SetBits(device, TAS2505_DACSETUP1, TAS2505_DACSETUP1_POWER_MASK, TAS2505_DACSETUP1_POWER_MASK);
    // SPK powered up (P1, R45, D1=1)
    _TAS2505WriteReg(device, TAS2505_SPKAMPCTRL1, 0x02);            // page 1, 2D = 02

}

static void _TAS2505StopOutput(const struct device *device)
{
    // SPK powered down (P1, R45, D1=0)
    _TAS2505WriteReg(device, TAS2505_SPKAMPCTRL1, 0x00);            // page 1, 2D = 00
    // power down DAC
    _TAS2505SetBits(device, TAS2505_DACSETUP1, TAS2505_DACSETUP1_POWER_MASK, 0);
}

static int _TAS2505SetOutputVolume(const struct device *device, int in_volume)
{
    int ret;
    uint8_t vol;

    if (in_volume > 0)
    {
       in_volume = 0;
    }

    if (in_volume < -127)
    {
        in_volume = -127;
    }

    vol = -in_volume; // 1/2 decibles, 0 is loudest. 0x7F is quietest

    ret = _TAS2505WriteReg(device, TAS2505_SPKVOL1, vol);
    return ret;
}

static int _TAS2505MuteOutput(const struct device *device, bool in_mute)
{
    return _TAS2505SetBits(device, TAS2505_DACSETUP2, TAS2505_DACSETUP2_MUTE_MASK,
            in_mute ? TAS2505_DACSETUP2_MUTE_MASK : 0);
}

static int _TAS2505SetProperty(
                            const struct device *device,
                            audio_property_t property,
                            audio_channel_t channel,
                            audio_property_value_t val
                            )
{
    int ret = -EINVAL;
    int vol;

    // individual channel control not currently supported
    require(channel == AUDIO_CHANNEL_ALL, exit);

    switch (property)
    {
    case AUDIO_PROPERTY_OUTPUT_VOLUME:
        // input val is attenuation in 1/2 db -100 to 0.  tas2505 dac vol is -63 to +24db by 1/2 db
        vol = (val.vol * 87 / 100) + 24;
        ret = _TAS2505SetOutputVolume(device, val.vol);
        break;

    case AUDIO_PROPERTY_OUTPUT_MUTE:
        ret = _TAS2505MuteOutput(device, val.mute);
        break;

    default:
        ret = 0; //??
        break;
    }

exit:
    return ret;
}

static int _TAS2505ApplyProperties(const struct device *device)
{
    return 0;
}

#ifdef TAS2505_DUMP_REGS
static void _DumpRegs(const struct device *device)
{
    // Fetch and display current register values
    LOG_INF("TAS2505 Control Registers:");
    uint8_t byte_val;

    for(uint8_t page = 0; page < 2; page++)
    {
        LOG_INF( "TAS2505 Page %i", page);
        LOG_INF( "     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f \r\n");
        for(uint8_t row = 0; row < 8; row++)
        {
            char row_str[100] = "";
            char temp_str[5] = "AB ";

            snprintf(row_str, sizeof(row_str), "%02X: ", row * 16);
            for(uint8_t col = 0; col<16; col++)
            {
                _TAS2505ReadReg(device, (page << 8) + (row * 16) + col, &byte_val, 1);
                sprintf( temp_str, "%02X ", byte_val);
                strcat(row_str, temp_str);
                byte_val++;
            }
            LOG_INF("%s", row_str);
        }
    }
}
#endif

static int _TAS2505Configure(const struct device *device, struct audio_codec_cfg *cfg)
{
    int ret = -EINVAL;
    const struct tas2505_cfg *config = DEV_CFG(device);

    require(cfg->dai_type == AUDIO_DAI_TYPE_I2S, exit);

    if (config->reset_gpio.port)
    {
        // take out of reset
        require(device_is_ready(config->reset_gpio.port), exit);
        gpio_pin_configure_dt(&config->reset_gpio, GPIO_OUTPUT);
        gpio_pin_set_dt(&config->reset_gpio, 0);
    }

    ret = _TAS2505WriteReg(device, TAS2505_RESET, 1);
    require_noerr(ret, exit);

    // LDO output programmed as 1.8V and Level shifters powered up. (P1, R2, D5-D4=00, D3=0)
    _TAS2505WriteReg(device, TAS2505_LDO_CTRL, 0x04);               // page 1, 02 = 04

    // codec_clkin = MCLK
    _TAS2505WriteReg(device, TAS2505_CLKMUX, 0x00);                 // page 0, 04 = 00

    // Power DOWN PLL, not using since mclk is input
    _TAS2505WriteReg(device, TAS2505_PLLPR, 0x00);                  // page 0, 05 = 00

    // DAC NDAC Powered up, NDAC=4 (P0, R11, D7=1, D6-D0=0000100)
    _TAS2505WriteReg(device, TAS2505_NDAC, 0x84);                   // page 0, 0B = 84

    // DAC MDAC Powered up, MDAC=2 (P0, R12, D7=1, D6-D0=0000010)
    _TAS2505WriteReg(device, TAS2505_MDAC, 0x82);                   // page 0, 0C = 82

    // DAC OSR(9:0)-> DOSR=128 (P0, R12, D1-D0=00)
    _TAS2505WriteReg(device, TAS2505_DOSRMSB, 0x00);                // page 0, 0D = 00

    // DAC OSR(9:0)-> DOSR=128 (P0, R13, D7-D0=10000000)
    _TAS2505WriteReg(device, TAS2505_DOSRLSB, 0x80);                // page 0, 0E = 80

    // Codec Interface control Word length = 16bits, BCLK&WCLK inputs, I2S mode. (P0, R27, D7-D6=00, D5-D4=00, D3-D2=00)
    _TAS2505WriteReg(device, TAS2505_IFACE1, 0x00);                 // page 0, 1B = 00

    // Data slot offset 00 (P0, R28, D7-D0=0000)
    _TAS2505WriteReg(device, TAS2505_IFACE2, 0x00);                 // page 0, 1C = 00

    // Dac Instruction programming PRB #2 for Mono routing. Type interpolation (x8) and 3 programmable Biquads. (P0, R60, D4-D0=0010)
    _TAS2505WriteReg(device, TAS2505_DACINSTRSET, 0x02);            // page 0, 3C = 02

    // DAC powered down, Left channel only, Soft step 1 per Fs. (P0, R63, D7=1, D5-D4=01, D3-D2=01, D1-D0=00)
    _TAS2505WriteReg(device, TAS2505_DACSETUP1, 0x14);              // page 0, 3F = 14

    // DAC digital gain 0dB (P0, R65, D7-D0=00000000)
    _TAS2505WriteReg(device, TAS2505_DACVOL, 0x00);                 // page 0, 41 = 00

    // DAC volume auto-muted. (P0, R64, D6-D4=4, D3=1, D2=1)
    _TAS2505WriteReg(device, TAS2505_DACSETUP2, 0x4C);              // page 0, 40 = 4C

    // Master Reference Powered on (P1, R1, D4=1)
    _TAS2505WriteReg(device, TAS2505_REF_POR_LDO_BGAP_CTRL, 0x10);  // page 1, 01 = 10

    // Output common mode for DAC set to 0.9V (default) (P1, R10)
    _TAS2505WriteReg(device, TAS2505_COMMON_CTRL, 0x00);            // page 1, 0A = 00

    // No routing to headphones
    _TAS2505WriteReg(device, TAS2505_HPH_ROUTING, 0x00);            // page 1, 0C = 00

    // SPK attn. Gain = 0dB (max) (P1, R46, D6-D0=000000)
    _TAS2505WriteReg(device, TAS2505_SPKVOL1, 0x00);                // page 1, 2E = 00

    // SPK driver Gain=6.0dB (P1, R48, D6-D4=001)
    _TAS2505WriteReg(device, TAS2505_SPKVOL2, 0x10);                // page 1, 30 = 10

    // SPK powered down (P1, R45, D1=0)
    _TAS2505WriteReg(device, TAS2505_SPKAMPCTRL1, 0x00);            // page 1, 2D = 00

    #ifdef TAS2505_DUMP_REGS
    _DumpRegs(device);
    #endif
    ret = 0;
exit:
    return ret;
}

static int _TAS2505Init(const struct device *device)
{
    int ret = -ENODEV;
    const struct tas2505_cfg *config = DEV_CFG(device);

    require(config && i2c_is_ready_dt(&config->i2c), exit);
    ret = 0;
exit:
    return ret;
}

static const struct audio_codec_api tas2505_api = {
    .configure          = _TAS2505Configure,
    .start_output       = _TAS2505StartOutput,
    .stop_output        = _TAS2505StopOutput,
    .set_property       = _TAS2505SetProperty,
    .apply_properties   = _TAS2505ApplyProperties,
};

#define TAS2505_INIT(id) \
    static const struct tas2505_cfg s_tas2505_##id##_cfg = {            \
        .i2c = I2C_DT_SPEC_INST_GET(id),                                \
        .reset_gpio = GPIO_DT_SPEC_INST_GET_OR(id, reset_gpios, { 0 })  \
    };                                                                  \
    static struct tas2505_data s_tas2505_##id##_data = {                \
        .cur_page = 2,                                                  \
    };                                                                  \
    DEVICE_DT_INST_DEFINE(id, &_TAS2505Init, NULL,                      \
        &s_tas2505_##id##_data, &s_tas2505_##id##_cfg, POST_KERNEL,     \
        CONFIG_AUDIO_CODEC_INIT_PRIORITY, &tas2505_api);

DT_INST_FOREACH_STATUS_OKAY(TAS2505_INIT)

