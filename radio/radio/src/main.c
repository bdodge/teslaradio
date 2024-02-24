
#include <zephyr/kernel.h>
#include "autoconf.h"
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#if CONFIG_BT
#include "tr_ble.h"
#endif
#include "si4703.h"
#include "vfs.h"

static uint32_t s_requested_freq;

int RequestTuneTo(uint32_t freq_kHz)
{
    s_requested_freq = freq_kHz;
    return 0;
}

int main(void)
{
    int ret;

    printk("-- Hello --");

    uint32_t delay = 2000;
    uint32_t min_delay;

    do // try
    {
        ret = SI4703Init(TUNER_SEEK_BETTER);
        if (ret)
        {
            break;
        }

#if CONFIG_BT
        ret = BLEinit(CONFIG_BT_DEVICE_NAME);
        if (ret)
        {
            break;
        }

#endif
    }
    while (0); // catch

    bool got_stations = false;
    bool setup_vdisk = false;
    tuner_state_t tuner_state;

    while (true)
    {
        min_delay = delay;
        delay = 2000;

#if CONFIG_BT
        ret = BLEslice(&delay);

        if (delay < min_delay)
        {
            min_delay = delay;
        }
#endif
        ret = SI4703Slice(&delay, &tuner_state);

        if (delay < min_delay)
        {
            min_delay = delay;
        }

        k_msleep(min_delay);

        if (tuner_state == TUNER_READY && !got_stations)
        {
            got_stations = true;
            SI4703DiscoverStations();
        }
        else if (tuner_state == TUNER_READY && got_stations && !setup_vdisk)
        {
            struct station_info *station_list;
            uint32_t num_stations;

            ret = SI4703GetStations(&station_list, &num_stations);
            if (!ret)
            {
                setup_vdisk = true;

                ret = vfs_init(station_list, num_stations);
                if (ret)
                {
                    break;
                }

            }
        }
        else if (tuner_state == TUNER_READY || tuner_state == TUNER_TUNED)
        {
            if (s_requested_freq != 0)
            {
                LOG_INF("TUNE --------- Requested freg %u", s_requested_freq);
                SI4703TuneTo(s_requested_freq);
                s_requested_freq = 0;
            }
        }
    }

    while (true)
    {
        k_sleep(K_MSEC(3000));
    }

    return ret;
}

