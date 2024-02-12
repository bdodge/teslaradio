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

static int mount_app_fs(struct fs_mount_t *mnt)
{
    int ret = -EINVAL;

    static FATFS fat_fs;

    mnt->type = FS_FATFS;
    mnt->fs_data = &fat_fs;

    if (IS_ENABLED(CONFIG_DISK_DRIVER_RAM))
    {
        mnt->mnt_point = "/RAM:";
    }
    else if (IS_ENABLED(CONFIG_DISK_DRIVER_SDMMC))
    {
        mnt->mnt_point = "/SD:";
    }
    else
    {
        mnt->mnt_point = "/NAND:";
    }

    ret = fs_mount(mnt);
    return ret;
}

static int setup_disk(void)
{
    int ret = -ENODEV;
    struct fs_mount_t *mp = &fs_mnt;
    struct fs_dir_t dir;
    struct fs_statvfs sbuf;

    fs_dir_t_init(&dir);

    if (!IS_ENABLED(CONFIG_FAT_FILESYSTEM_ELM))
    {
        LOG_INF("No file system selected");
        return ret;
    }

    ret = mount_app_fs(mp);

    if (ret < 0)
    {
        LOG_ERR("Failed to mount filesystem");
        return ret;
    }

    /* Allow log messages to flush to avoid interleaved output */
    k_sleep(K_MSEC(50));

    LOG_INF("Mount %s: %d\n", fs_mnt.mnt_point, ret);

    ret = fs_statvfs(mp->mnt_point, &sbuf);

    if (ret < 0)
    {
        LOG_ERR("FAIL: statvfs: %d\n", ret);
        return ret;
    }

    LOG_INF("%s: bsize = %lu ; frsize = %lu ; blocks = %lu ; bfree = %lu\n",
            mp->mnt_point,
            sbuf.f_bsize, sbuf.f_frsize,
            sbuf.f_blocks, sbuf.f_bfree);

    ret = fs_opendir(&dir, mp->mnt_point);
    printk("%s opendir: %d\n", mp->mnt_point, ret);

    if (ret < 0)
    {
        LOG_ERR("Failed to open directory");
    }

    while (ret >= 0)
    {
        struct fs_dirent ent = { 0 };

        ret = fs_readdir(&dir, &ent);

        if (ret < 0)
        {
            LOG_ERR("Failed to read directory entries");
            break;
        }

        if (ent.name[0] == 0)
        {
            ret = 0;
            break;
        }

        LOG_INF("  %c %u %s",
                (ent.type == FS_DIR_ENTRY_FILE) ? 'F' : 'D',
                ent.size,
                ent.name);
    }

    (void)fs_closedir(&dir);

    return ret;
}

static int create_base_file(const char *filename)
{
    struct fs_file_t nf;
    char fname_buff[128];
    char mnt_pt_buff[32];
    int ret;
    int text_len;

    snprintf(mnt_pt_buff, sizeof(mnt_pt_buff), "/%s:", CONFIG_MASS_STORAGE_DISK_NAME);
    memset(&nf, 0, sizeof(nf));

    do
    {
        snprintf(fname_buff, sizeof(fname_buff), "%s/%s", mnt_pt_buff, filename);
        ret = fs_open(&nf, fname_buff, FS_O_RDWR | FS_O_CREATE);
        if (ret)
        {
            LOG_ERR("Can't create file");
            break;
        }

        text_len = snprintf(fname_buff, sizeof(fname_buff), "Hello %s\n", filename);
        ret = fs_write(&nf, fname_buff, text_len);
        fs_close(&nf);
        if (ret != text_len)
        {
            LOG_ERR("Can't write file");
            break;
        }

        ret = 0;
    }
    while (0);

    return ret;
}

static int setup_files(void)
{
    int ret;

    do
    {
        ret = create_base_file("88.1");
        if (ret)
        {
            break;
        }
        ret = create_base_file("89.7");
        if (ret)
        {
            break;
        }
        ret = create_base_file("90.9");
        if (ret)
        {
            break;
        }
        ret = create_base_file("101.3");
        if (ret)
        {
            break;
        }
        ret = create_base_file("107.9");
        if (ret)
        {
            break;
        }
    }
    while (0);

    return ret;
}

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

        ret = setup_disk();
        if (ret)
        {
            LOG_ERR("Can't create FATFS");
            break;
        }

        ret = setup_files();
        if (ret)
        {
            LOG_ERR("Can't create base file(s)");
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

