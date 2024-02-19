/*
 * Copyright (c) 2016 Intel Corporation.
 * Copyright (c) 2019-2020 Nordic Semiconductor ASA
 * Copyright (c) 2024 Brian Dodge
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "vfs.h"
#include "vdisk.h"
#include "usb_msc.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/usb/class/usbd_msc.h>
#include <zephyr/fs/fs.h>
#include <stdio.h>

LOG_MODULE_REGISTER(vfs);

#include <ff.h>

#define STORAGE_PARTITION       storage_partition
#define STORAGE_PARTITION_ID    FIXED_PARTITION_ID(STORAGE_PARTITION)

static struct fs_mount_t fs_mnt;

#if defined(CONFIG_USB_DEVICE_STACK_NEXT)
USBD_CONFIGURATION_DEFINE(config_1,
                          USB_SCD_SELF_POWERED,
                          200);

USBD_DESC_LANG_DEFINE(tr_vdisk_lang);
USBD_DESC_MANUFACTURER_DEFINE(tr_vdisk_mfr, "BDodge");
USBD_DESC_PRODUCT_DEFINE(tr_vdisk_product, "Teslaradio Disk");
USBD_DESC_SERIAL_NUMBER_DEFINE(tr_vdisk_sn, "0123456789ABCDEF");


USBD_DEVICE_DEFINE(tr_vdisk_usbd,
                   DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)),
                   0x2fe3, 0x0008);

#if CONFIG_DISK_DRIVER_RAM
USBD_DEFINE_MSC_LUN(RAM, "BDodge", "TeslaRadio", "1.00");
#endif

static int enable_usb_device_next(void)
{
    int err;

    err = usbd_add_descriptor(&tr_vdisk_usbd, &tr_vdisk_lang);

    if (err)
    {
        LOG_ERR("Failed to initialize language descriptor (%d)", err);
        return err;
    }

    err = usbd_add_descriptor(&tr_vdisk_usbd, &tr_vdisk_mfr);

    if (err)
    {
        LOG_ERR("Failed to initialize manufacturer descriptor (%d)", err);
        return err;
    }

    err = usbd_add_descriptor(&tr_vdisk_usbd, &tr_vdisk_product);

    if (err)
    {
        LOG_ERR("Failed to initialize product descriptor (%d)", err);
        return err;
    }

    err = usbd_add_descriptor(&tr_vdisk_usbd, &tr_vdisk_sn);

    if (err)
    {
        LOG_ERR("Failed to initialize SN descriptor (%d)", err);
        return err;
    }

    err = usbd_add_configuration(&tr_vdisk_usbd, &config_1);

    if (err)
    {
        LOG_ERR("Failed to add configuration (%d)", err);
        return err;
    }

    err = usbd_register_class(&tr_vdisk_usbd, "msc_0", 1);

    if (err)
    {
        LOG_ERR("Failed to register MSC class (%d)", err);
        return err;
    }

    err = usbd_init(&tr_vdisk_usbd);

    if (err)
    {
        LOG_ERR("Failed to initialize device support");
        return err;
    }

    err = usbd_enable(&tr_vdisk_usbd);

    if (err)
    {
        LOG_ERR("Failed to enable device support");
        return err;
    }

    LOG_DBG("USB device support enabled");

    return 0;
}
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK_NEXT) */

int vfs_init(void)
{
    int ret;

    do // try
    {
        ret = vdisk_init();

        if (ret)
        {
            LOG_ERR("Can't create virtual disk");
            break;
        }

#if defined(CONFIG_USB_DEVICE_STACK_NEXT)
        ret = enable_usb_device_next();
#else
        ret = usb_enable(NULL);
#endif
        if (ret)
        {
            LOG_ERR("Failed to enable USB");
            break;
        }

        ret = mass_storage_init();
        if (ret)
        {
            LOG_ERR("Can't start Mass-Storage");
            break;
        }

        LOG_INF("USB mass storage ready");
    }
    while (0); // catch

    return ret;
}

