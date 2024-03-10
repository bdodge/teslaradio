
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
#include "vfs.h"

static uint32_t s_requested_freq;

int RequestTuneTo(uint32_t freq_kHz)
{
    s_requested_freq = freq_kHz;
    return 0;
}

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

    ret = SI4703GetTunedStation(
                                &curfreq,
                                &currssi,
                                &curstereo,
                                &rdsn,
                                &rdst
                                );

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
#endif
    }
}

int main(void)
{
    int ret;

    printk("-- Hello --");

    uint32_t delay = 2000;
    uint32_t min_delay;

    ret = SettingsInit();
    require_noerr(ret, exit);
#if CONFIG_DISPLAY
    ret = DisplayInit();
    require_noerr(ret, exit);
#endif
    ret = AudioInit();
    require_noerr(ret, exit);

    ret = SI4703Init(TUNER_SEEK_BETTER);
    require_noerr(ret, exit);

#if CONFIG_BT
    ret = BLEinit(CONFIG_BT_DEVICE_NAME);
    require_noerr(ret, exit);
#endif

    bool got_stations = false; // set to false to do a disco on startup
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

                ret = SettingsReadUint32("Freg", 0, &s_requested_freq);
                if (ret)
                {
                    // no saved last station, use best amongst scan (TODO)
                    LOG_INF("No Last Station");
                    s_requested_freq = 0;
                }
                else
                {
                    LOG_INF("Last Station --------- Requested freg %u", s_requested_freq);
                }
            }
        }
        else if (tuner_state == TUNER_READY || tuner_state == TUNER_TUNED)
        {
            if (s_requested_freq != 0)
            {
                LOG_INF("TUNE --------- Requested freg %u", s_requested_freq);
                SettingsWriteUint32("Freq", 0, s_requested_freq);
                SI4703TuneTo(s_requested_freq);
                s_requested_freq = 0;
            }
            else if (tuner_state == TUNER_TUNED)
            {
                UpdateDisplay();
            }

        }
    }
exit:
    while (true)
    {
        k_sleep(K_MSEC(3000));
    }

    return ret;
}

