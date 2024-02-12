
#include <zephyr/kernel.h>
#include "autoconf.h"
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#if CONFIG_BT
#include "tr_ble.h"
#endif

#include "vfs.h"

int main(void)
{
    int ret;

    printk("-- Hello --");

    uint32_t delay = 2000;
    uint32_t min_delay;

    do // try
    {
        ret = vfs_init();

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

