
#include "i2c_zephyr.h"

#include <zephyr/drivers/i2c.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>

#include <strings.h>

#define I2C_HANDLE_TO_SPEC(h) ((struct i2c_dt_spec *)(h))
#define I2C_SPEC_TO_HANDLE(s) ((i2c_handle_t)(s))

#define DT_DRV_COMPAT i2c_device

#define _GET_SPEC(_n_)   \
    I2C_DT_SPEC_GET(DT_DRV_INST(_n_)),

static const struct i2c_dt_spec s_i2c_specs[] = {
    DT_INST_FOREACH_STATUS_OKAY(_GET_SPEC)
};

#define _GET_NAME(_n_)   \
    DEVICE_DT_NAME(DT_DRV_INST(_n_)),

static const char *s_i2c_names[] = {
    DT_INST_FOREACH_STATUS_OKAY(_GET_NAME)
};

const struct i2c_dt_spec *_I2CFromName(const char * const in_device_name)
{
    const struct i2c_dt_spec *spec = NULL;
    const char *dev_name;
    size_t name_len;
    size_t dev_name_len;
    int devidx;

    if (!in_device_name || !in_device_name[0])
    {
        goto exit;
    }

    name_len = strlen(in_device_name);

    for (devidx = 0; devidx < ARRAY_SIZE(s_i2c_names); devidx++)
    {
        dev_name = s_i2c_names[devidx];

        for (dev_name_len = 0; dev_name[dev_name_len]; dev_name_len++)
        {
            if (dev_name[dev_name_len] == '@')
            {
                break;
            }
        }

        if (dev_name_len == name_len)
        {
            if (!strncasecmp(s_i2c_names[devidx], in_device_name, name_len))
            {
                spec = &s_i2c_specs[devidx];
                break;
            }
        }
    }

exit:
    return spec;
}

int I2CWrite(const i2c_handle_t in_h_i2c, const uint8_t in_reg_addr, const uint8_t * const in_data, const size_t in_count)
{
    const struct i2c_dt_spec *spec = I2C_HANDLE_TO_SPEC(in_h_i2c);
    int ret = -EINVAL;

    if (spec && in_data && in_count)
    {
        if (in_count > 1)
        {
            ret = i2c_burst_write_dt(spec, in_reg_addr, in_data, in_count);
        }
        else if (in_count == 1)
        {
            ret = i2c_reg_write_byte_dt(spec, in_reg_addr, in_data[0]);
        }
        else
        {
            ret = i2c_write_dt(spec, &in_reg_addr, 1);
        }
    }

    return ret;
}

int I2CWriteByte(const i2c_handle_t in_h_i2c, const uint8_t in_reg_addr, const uint8_t in_data)
{
    return I2CWrite(in_h_i2c, in_reg_addr, &in_data, 1);
}

int I2CRead(const i2c_handle_t in_h_i2c, const uint8_t in_reg_addr, uint8_t * const out_data, const size_t in_count)
{
    const struct i2c_dt_spec *spec = I2C_HANDLE_TO_SPEC(in_h_i2c);
    int ret = -EINVAL;

    if (spec && out_data && in_count)
    {
        ret = i2c_write_read_dt(spec, &in_reg_addr, sizeof(uint8_t), out_data, in_count);
    }

    return ret;
}

i2c_handle_t I2CGetHandle(const char *in_i2c_name)
{
    return I2C_SPEC_TO_HANDLE(_I2CFromName(in_i2c_name));
}

#ifdef CONFIG_SHELL
#include <zephyr/shell/shell.h>

static int _CommandI2CList(const struct shell *s, size_t argc, char **argv)
{
    int devidx;

    for (devidx = 0; devidx < ARRAY_SIZE(s_i2c_names); devidx++)
    {
        shell_print(s, "%s", s_i2c_names[devidx]);
    }

    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(_sub_I2C_commands,
                               SHELL_CMD(list,  NULL,  "List all available I2C devices", _CommandI2CList),
                               SHELL_SUBCMD_SET_END );

SHELL_CMD_REGISTER(li2c, &_sub_I2C_commands, "Local I2C commands", NULL);

#endif

