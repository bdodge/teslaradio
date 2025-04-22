
#include <stdio.h>
#include <zephyr/kernel.h>
#include "autoconf.h"
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#include "asserts.h"
#if CONFIG_DISPLAY
#include "display.h"
#endif
#if CONFIG_BT
#include "tr_ble.h"
#endif
#include "settings.h"
#include "audioin.h"
#include "si4703.h"
#include "si473x.h"
#include "vfs.h"

static uint32_t s_requested_freq;
static bool s_have_display;
static bool s_have_tuner;

int TunerRequestTuneTo(uint32_t freq_kHz)
{
    s_requested_freq = freq_kHz;
    return 0;
}

#if CONFIG_DISPLAY
static void UpdateDisplay(void)
{
    //static char rds_name[65];
    static char rds_text[65];
    static uint32_t rds_offset;
    static uint64_t rds_scroll;
    static uint32_t freq;
    static bool stereo;

    char rds_display[65];
    uint32_t curfreq;
    uint8_t  currssi;
    bool     curstereo;
    const char *rdsn;
    const char *rdst;

    int ret;

    if (!s_have_display)
    {
        return;
    }

    ret = TunerGetTunedStationFreq(&curfreq);
    ret = TunerGetTunedStationRSSI(&currssi, &curstereo);
    ret = TunerGetTunedStationRDS(&rdsn, &rdst);

    if (!ret)
    {
        char freqtext[8];
        bool rds_dirty = false;
        int i;

        snprintf(freqtext, sizeof(freqtext), "%5.1f", (float)curfreq / 1000.0);

        if (curfreq != freq)
        {
            DisplaySetFont(32);
            DisplayText(0, 4, "      ");
            DisplayText(0, 4, freqtext);
            freq = curfreq;
        }
        if (curstereo != stereo)
        {
            DisplaySetFont(8);
            DisplayText(100, 10, curstereo ? "ST" : "  ");
            stereo = curstereo;
        }

        if (true)
        {
#if 0
            if (memcmp(rdsn, rds_name, sizeof(rds_name)))
            {
                LOG_INF("New RDSN %s", rdsn);
                memcpy(rds_name, rdsn, sizeof(rds_name));
                DisplaySetFont(8);
                DisplayText(0, 0, "                      ");
                DisplayText(8, 0, rds_name);
            }
#endif
#if 1
            uint64_t now = k_uptime_get();
            int rds_room = 24;

            if (memcmp(rdst, rds_text, sizeof(rds_text)))
            {
                LOG_INF("New RDST %s", rdst);
                memcpy(rds_text, rdst, sizeof(rds_text));
                rds_offset = 0;
                rds_dirty = true;
            }

            int rds_len = strlen(rds_text);

            if (rds_len > rds_room)
            {
                if ((now - rds_scroll) > 600)
                {
                    rds_dirty = true;
                    rds_offset++;
                }
            }

            if (rds_dirty)
            {
                uint32_t offset;

                if (rds_len <= rds_room)
                {
                    rds_offset = 0;
                }
                else
                {
                    rds_offset %= rds_len;
                }

                offset = rds_offset;

                for (i = 0; i < rds_room; i++)
                {
                    if (offset >= rds_len)
                    {
                        offset = 0;
                    }

                    if (offset >= rds_len)
                    {
                        rds_display[i] = ' ';
                    }
                    else
                    {
                        rds_display[i] = rds_text[offset];
                        offset++;
                    }
                }

                rds_display[i] = '\0';
                DisplaySetFont(8);
                DisplayText(8, 0, rds_display);
                rds_dirty = false;
            }
        }
#endif
    }
}
#endif

int main(void)
{
    int ret;
    struct station_info *station_list;

    printk("-- Hello --");

    uint32_t delay = 2000;
    uint32_t min_delay;

    ret = SettingsInit();
    require_noerr(ret, exit);

    s_have_display = false;
#if CONFIG_DISPLAY
    ret = DisplayInit();
    if (!ret)
    {
        s_have_display = true;
    }
//    require_noerr(ret, exit);
#endif
    s_have_tuner = false;

    ret = AudioInit();
    require_noerr(ret, exit);

    ret = TunerInit(SI473XGetRadio(), TUNER_SEEK_BETTER);
    if (ret)
    {
        ret = TunerInit(SI4703GetRadio(), TUNER_SEEK_BETTER);
    }

    if (ret)
    {
        LOG_ERR("No radio found");
    }
    else
    {
        s_have_tuner = true;
    }
    //require_noerr(ret, exit);

#if CONFIG_BT
    ret = BLEinit(CONFIG_BT_DEVICE_NAME);
    require_noerr(ret, exit);
#endif

    bool got_stations = false; // set to false to do a disco on startup
    bool setup_vdisk = false;
    bool tuner_ready = false;
    tuner_state_t tuner_state;

    if (!s_have_tuner)
    {
        ret = vfs_init(station_list, 0, s_have_tuner);
        setup_vdisk = true;
    }

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
        if (s_have_tuner)
        {
            ret = TunerSlice(&delay, &tuner_state);

            if (delay < min_delay)
            {
                min_delay = delay;
            }

            tuner_ready = tuner_state == TUNER_READY || tuner_state == TUNER_TUNED;

            if (tuner_ready && !got_stations)
            {
                got_stations = true;

                TunerDiscoverStations();
            }
            else if (tuner_ready && got_stations && !setup_vdisk)
            {
                uint32_t num_stations;

                ret = TunerGetStations(&station_list, &num_stations);
                if (!ret)
                {
                    setup_vdisk = true;

                    ret = vfs_init(station_list, num_stations, s_have_tuner);
                    if (ret)
                    {
                        break;
                    }
                }
            }
            else if (tuner_ready)
            {
                if (s_requested_freq != 0)
                {
                    LOG_INF("TUNE --------- Requested freg %u", s_requested_freq);
                    TunerTuneTo(s_requested_freq);
                    s_requested_freq = 0;
                }
#if CONFIG_DISPLAY
                else if (s_have_display && tuner_state == TUNER_TUNED)
                {
                    UpdateDisplay();
                }
#endif
            }
        }

        k_msleep(min_delay);
    }
exit:
    while (true)
    {
        k_sleep(K_MSEC(3000));
    }

    return ret;
}

