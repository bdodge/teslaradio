
#include <zephyr/kernel.h>
#include "autoconf.h"
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#if CONFIG_OPTION_AUDIO
    #include "tones.h"
#endif
#if CONFIG_BT
    #include "tr_ble.h"
#endif

int main(void)
{
    int ret;

    printk("-- Hello --");

    uint32_t delay = 2000;
    uint32_t min_delay;

    vdisk_init();

    do // try
    {
#if CONFIG_OPTION_AUDIO
        ret = ToneInit(g_tone_registry, g_tone_registry_count);
        if (ret)
        {
            break;
        }
#endif
#if CONFIG_BT
        ret = BLEinit(CONFIG_BT_DEVICE_NAME);
        if (ret)
        {
            break;
        }
#endif
    }
    while(0); // catcj
#if CONFIG_OPTION_AUDIO
    TonePlayToneAtVolume("boot", 50);
#endif
    // Run the application
    //
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
        k_msleep(min_delay);
    }

    while (true)
    {
        k_sleep(K_MSEC(3000));
    }
    return ret;
}

