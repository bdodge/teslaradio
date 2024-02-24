
#include "vdisk.h"
#include <zephyr/types.h>
#include <zephyr/drivers/disk.h>
#include <zephyr/init.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <errno.h>

LOG_MODULE_REGISTER(vdisk, CONFIG_DISK_LOG_LEVEL);

#include "fs_sects.c"

//#define SECTION_MAX (sizeof(section_sectors)/sizeof(struct sect_desc))

#define VFAT_SECTOR_COUNT ((uint32_t)(VFAT_VOLUME_SIZE / VFAT_SECTOR_SIZE))

#include "taunt.h"

static uint8_t s_wav_header[] = {
  0x52, 0x49, 0x46, 0x46, 0x8c, 0x64, 0x01, 0x00, 0x57, 0x41, 0x56, 0x45,
  0x66, 0x6d, 0x74, 0x20, 0x10, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00,
  0xf1, 0x56, 0x00, 0x00, 0xf1, 0x56, 0x00, 0x00, 0x01, 0x00, 0x08, 0x00,
  0x64, 0x61, 0x74, 0x61, 0x00, 0x00, 0x00, 0x80
};

#define WAV_HEADER_SIZE (sizeof(s_wav_header))

static uint32_t s_start_sectors[VFAT_ROOT_DIR_COUNT];
static uint32_t s_start_stations[VFAT_ROOT_DIR_COUNT];

static int disk_ram_access_status(struct disk_info *disk)
{
    return DISK_STATUS_OK;
}

static int disk_ram_access_init(struct disk_info *disk)
{
    return 0;
}

static int disk_ram_access_read(struct disk_info *disk, uint8_t *buff,
                                uint32_t sector, uint32_t count)
{
    uint32_t last_sector = sector + count;
    uint32_t remain;
    uint32_t section;

    if (last_sector < sector || last_sector > VFAT_SECTOR_COUNT)
    {
        LOG_ERR("Sector %u is outside the range %u",
                last_sector, VFAT_SECTOR_COUNT);
        return -EIO;
    }

    // search through section description to get section the start sector is in
    //
    for (section = 0; section < SECTION_MAX; section++)
    {
        if (sector >= section_sectors[section].start_sector && sector < (section_sectors[section].start_sector + section_sectors[section].count))
        {
            break;
        }
    }

    if (section >= SECTION_MAX)
    {
        LOG_ERR("sector %u isn't in our fs", sector);
        memset(buff, 0, count * VFAT_SECTOR_SIZE);
        return 0;
    }

    //LOG_INF("read %u %d", sector, count);

    sector -= section_sectors[section].start_sector;
    remain = section_sectors[section].count - sector;

    if (section == SECTION_FAT)
    {
        // synthesize FAT sectors
        while (count > 0 && remain > 0)
        {
            uint32_t *fatptr;
            uint32_t fatdex;
            uint32_t cluster;

            fatptr = (uint32_t *)buff;

            cluster = 1 + sector * VFAT_SECTOR_SIZE / 4;

            if (sector <= 512)
            {
                for (fatdex = 0; fatdex < VFAT_SECTOR_SIZE / 4; fatdex++)
                {
                    fatptr[fatdex] = cluster++;
                }

                if (sector == 0)
                {
                    // first three fat entries fixed/reserved
                    fatptr[0] = 0xFFFFFFF8;
                    fatptr[1] = 0xFFFFFFFF;
                    fatptr[2] = 0x0FFFFFFF;
                }
                else if (sector == 512) /* 2mb file fat ends 2 entries into sector 512 */
                {
                    int filedex = 1;

                    // there should be N cluster ptrs after the last 2 for the first file
                    // so point all those that are not EOC markers at the second cluster
                    // of the first file so reading will continue from there
                    //
                    for (fatdex = 3; fatdex < VFAT_SECTOR_SIZE / 4; fatdex+= 2)
                    {
                        if (filedex < VFAT_ROOT_DIR_COUNT)
                        {
                            fatptr[fatdex] =  129;
                            fatptr[fatdex + 1] = 0x0FFFFFFF; // EOC for safety
                        }

                        filedex++;
                    }
                }
            }
            else
            {
                memset(fatptr, 0, VFAT_SECTOR_SIZE);
            }

            fatptr += VFAT_SECTOR_SIZE / 4;
            sector++;
            count--;
            remain--;
        }
    }
    else if (section == SECTION_DATA)
    {
        uint32_t *src_ptr;
        uint32_t *dst_ptr;
        uint32_t sector_offset = 0;
        int srcdex;
        int dstdex;
        int copy_cnt;
        bool is_start_sector = false;

        dst_ptr = (uint32_t *)buff;
        dstdex = 0;

        for (int ss = 0; ss < VFAT_ROOT_DIR_COUNT; ss++)
        {
            if (sector == s_start_sectors[ss])
            {
                LOG_INF("data read at sec %u starts wav file index %d", sector, ss);

                // tune to this station
                if (s_start_stations[ss] != 0)
                {
                   RequestTuneTo(s_start_stations[ss]);
                }

                is_start_sector = true;
                break;
            }
            if (
                    ss > 0
                &&  sector > s_start_sectors[ss]
                && (
                        (ss == VFAT_ROOT_DIR_COUNT - 1)
                    ||  (sector < s_start_sectors[ss + 1])
                )
            )
            {
                // the sector is within the first cluster of a file above the first file, after the start,
                // so map that as if its the same relative sector in the first file
                //
                sector_offset = s_start_sectors[ss] + 16;
                //LOG_INF("in sector %u  offset=%u", sector, sector_offset);
                break;
            }
        }

        while (count > 0 && remain > 0)
        {
            // synthesize data
            if (is_start_sector)
            {
                int hdrdex = 0;

                is_start_sector = false;

                src_ptr = (uint32_t *)s_wav_header;

                // prepend wave header
                while (dstdex < WAV_HEADER_SIZE / 4)
                {
                    dst_ptr[dstdex++] = src_ptr[hdrdex++];
                }

                srcdex = 0;
                copy_cnt = VFAT_SECTOR_SIZE - WAV_HEADER_SIZE;
            }
            else
            {
                srcdex = ((sector - sector_offset) * VFAT_SECTOR_SIZE - WAV_HEADER_SIZE) % taunt_wav_len;
                copy_cnt = VFAT_SECTOR_SIZE;
            }

            src_ptr = (uint32_t *)taunt_wav;
            srcdex /= 4;
            copy_cnt /= 4;

            while (copy_cnt > 0)
            {
                dst_ptr[dstdex++] = src_ptr[srcdex++];

                if (srcdex >= taunt_wav_len / 4)
                {
                    srcdex = 0;
                }

                copy_cnt--;
            }

            sector++;
            count--;
            remain--;
        }
    }
    else
    {
        while (count > 0 && remain > 0)
        {
            if (section_sectors[section].sectors)
            {
                const uint8_t *psect = section_sectors[section].sectors[sector];

                if (psect)
                {
                    memcpy(buff, psect, VFAT_SECTOR_SIZE);
                }
                else
                {
                    memset(buff, 0, VFAT_SECTOR_SIZE);
                }
            }
            else
            {
                memset(buff, 0, VFAT_SECTOR_SIZE);
            }

            sector++;
            count--;
            remain--;
        }
    }

    if (count > 0)
    {
        LOG_ERR("sector read crossed section boundary at %u", sector);
    }
    return 0;
}

static int disk_ram_access_write(struct disk_info *disk, const uint8_t *buff,
                                 uint32_t sector, uint32_t count)
{
    return -EIO;
}

static int disk_ram_access_ioctl(struct disk_info *disk, uint8_t cmd, void *buff)
{
    switch (cmd)
    {
    case DISK_IOCTL_CTRL_SYNC:
        break;

    case DISK_IOCTL_GET_SECTOR_COUNT:
        *(uint32_t *)buff = VFAT_VOLUME_SIZE / VFAT_SECTOR_SIZE;
        break;

    case DISK_IOCTL_GET_SECTOR_SIZE:
        *(uint32_t *)buff = VFAT_SECTOR_SIZE;
        break;

    case DISK_IOCTL_GET_ERASE_BLOCK_SZ:
        *(uint32_t *)buff  = 1U;
        break;

    default:
        return -EINVAL;
    }

    return 0;
}

static const struct disk_operations ram_disk_ops =
{
    .init = disk_ram_access_init,
    .status = disk_ram_access_status,
    .read = disk_ram_access_read,
    .write = disk_ram_access_write,
    .ioctl = disk_ram_access_ioctl,
};

static struct disk_info ram_disk =
{
    .name = CONFIG_MASS_STORAGE_DISK_NAME,
    .ops = &ram_disk_ops,
};

#define AT_READ_ONLY    0x01
#define AT_HIDDEN       0x02
#define AT_SYSTEM       0x04
#define AT_VOLUME_ID    0x08
#define AT_DIRECTORY    0x10
#define AT_ARCHIVE      0x20
#define AT_LFN          (AT_READ_ONLY|AT_HIDDEN|AT_SYSTEM|AT_VOLUME_ID) /* 0x0F */

#define AT_OFF          11
#define CLUST_HIGH      20
#define CLUST_LOW       26
#define SIZE_OFF        28
#define CSUM_OFF        13

uint8_t vdisk_csum_lfn(const char *name)
{
    uint8_t sum;
    uint8_t i;

    for (sum = i = 0; i < 11; i++)
    {
        sum = (((sum & 1) << 7) | ((sum & 0xFE) >> 1)) + (uint8_t)name[i];
    }

    return sum;
}

void vdisk_get_newfilename(struct station_info *stations, uint32_t num_stations, int fileindex, char *newlfn, uint32_t newlfn_size)
{
    int len;

    memset(newlfn, '_', VFAT_MAX_FILENAME - 4);
    snprintf(newlfn + VFAT_MAX_FILENAME - 4, 5, ".wav");

    if (stations && num_stations)
    {
        if (fileindex < num_stations)
        {
            uint32_t freqmhz = stations[fileindex].freq_kHz / 1000;

            len = snprintf(newlfn, newlfn_size - 1, "%u_%1u_MHz", freqmhz,
                        stations[fileindex].freq_kHz / 100 - freqmhz * 10);
        }
        else
        {
            len = snprintf(newlfn, newlfn_size - 1, "-");
        }

        newlfn[len] = '_';

       // LOG_INF("Change name %d =%s=", fileindex, newlfn);
    }
}

void vdisk_setup_dir(struct station_info *stations, uint32_t num_stations)
{
    char lfnbuf[VFAT_MAX_FILENAME + 14];
    char newlfn[VFAT_MAX_FILENAME + 14];
    uint8_t entry[32];
    uint32_t ent_off = 0;
    uint32_t slen = section_sectors[SECTION_ROOTDIR].count * VFAT_SECTOR_SIZE;
    uint8_t attr;
    uint32_t lfndex = 0;
    uint32_t newdex = 0;
    uint32_t filenum = 0;

    uint32_t first_cluster = 0;
    uint32_t first_size = 0;

    memset(s_start_sectors, 0, sizeof(s_start_sectors));
    memset(s_start_stations, 0, sizeof(s_start_stations));

    vdisk_get_newfilename(stations, num_stations, filenum, newlfn, sizeof(newlfn));

    while (ent_off < slen && filenum < VFAT_ROOT_DIR_COUNT)
    {
        // read dir entry
        memcpy(entry, section_sectors[SECTION_ROOTDIR].sectors[0] + ent_off, 32);

        attr = entry[AT_OFF];
        if (attr == AT_LFN)
        {
            bool is_last = (entry[0] & 0x40) != 0;
            uint8_t order = (entry[0] & 0x0F);

            if (order < 1)
            {
                LOG_ERR("fname order issue");
                order = 1;
            }

            if (is_last)
            {
                memset(lfnbuf, 0, sizeof(lfnbuf));
            }

            lfndex = (order - 1) * 13;
            newdex = lfndex;

            if ((lfndex + 13) >= sizeof(lfnbuf))
            {
                LOG_ERR("fname overflow");
            }
            else
            {
                int sdex = lfndex;

                lfnbuf[lfndex++] = entry[1];
                lfnbuf[lfndex++] = entry[3];
                lfnbuf[lfndex++] = entry[5];
                lfnbuf[lfndex++] = entry[7];
                lfnbuf[lfndex++] = entry[9];
                lfnbuf[lfndex++] = entry[14];
                lfnbuf[lfndex++] = entry[16];
                lfnbuf[lfndex++] = entry[18];
                lfnbuf[lfndex++] = entry[20];
                lfnbuf[lfndex++] = entry[22];
                lfnbuf[lfndex++] = entry[24];
                lfnbuf[lfndex++] = entry[28];
                lfnbuf[lfndex++] = entry[30];

                for (int x = sdex; x < lfndex; x+= 2)
                {
                    if (lfnbuf[x] == 0xFF)
                    {
                        if (lfnbuf[x + 1] == 0xFF)
                        {
                            lfnbuf[x] = lfnbuf[x + 1] = 0;
                            newlfn[x] = 0xFF;
                            newlfn[x + 1] = 0xFF;
                        }
                    }
                }

                entry[1] = newlfn[newdex++];
                entry[3] = newlfn[newdex++];
                entry[5] = newlfn[newdex++];
                entry[7] = newlfn[newdex++];
                entry[9] = newlfn[newdex++];
                entry[14] = newlfn[newdex++];
                entry[16] = newlfn[newdex++];
                entry[18] = newlfn[newdex++];
                entry[20] = newlfn[newdex++];
                entry[22] = newlfn[newdex++];
                entry[24] = newlfn[newdex++];
                entry[28] = newlfn[newdex++];
                entry[30] = newlfn[newdex++];

                // replace dir entry
                memcpy((uint8_t*)section_sectors[SECTION_ROOTDIR].sectors[0] + ent_off, entry, 32);
            }
        }
        else
        {
            uint32_t cluster;
            uint32_t size;
            char sfn[16];

            for (int i = 0; i < 8; i++)
            {
                sfn[i] = entry[i];
            }
            sfn[8] = '.';
            for (int i = 0; i < 3; i++)
            {
                sfn[i + 9] = entry[i + 8];
            }
            sfn[12] = '\0';
            lfndex = 0;

            cluster  = (uint32_t)entry[CLUST_HIGH] << 16;
            cluster += (uint32_t)entry[CLUST_HIGH + 1] << 24;
            cluster += (uint32_t)entry[CLUST_LOW];
            cluster += (uint32_t)entry[CLUST_LOW + 1] << 8;

            size  = (uint32_t)entry[SIZE_OFF];
            size += (uint32_t)entry[SIZE_OFF + 1] << 8;
            size += (uint32_t)entry[SIZE_OFF + 2] << 16;
            size += (uint32_t)entry[SIZE_OFF + 3] << 24;

            LOG_INF("File %d of %08X at %08X =%s= =%s=", filenum, size, cluster, sfn, lfnbuf);

            if (filenum == 0)
            {
                first_cluster = cluster;
                first_size = size;
            }
            else
            {
                if (0) // point all file's to the first files cluster chain (i.e. link) but add 1 for filenum
                {
                    entry[CLUST_LOW] = (first_cluster + 0) & 0xFF;
                    entry[CLUST_LOW + 1] = (first_cluster >> 8) & 0xFF;
                    entry[CLUST_HIGH] = (first_cluster >> 16) & 0xFF;
                    entry[CLUST_HIGH] = (first_cluster >> 24) & 0xFF;
                }
                if (1) // make it appear as same size as first files size
                {
                    entry[SIZE_OFF] = size & 0xFF;
                    entry[SIZE_OFF + 1] = (first_size >> 8) & 0xFF;
                    entry[SIZE_OFF + 2] = (first_size >> 16) & 0xFF;
                    entry[SIZE_OFF + 3] = (first_size >> 24) & 0xFF;
                }
                if (0) // make it read-only
                {
                    entry[AT_OFF] |= AT_READ_ONLY;
                }

                // replace dir entry
                memcpy((uint8_t*)section_sectors[SECTION_ROOTDIR].sectors[0] + ent_off, entry, 32);
            }

            // remember first sector of each file entry
            LOG_INF("Set starting sector %08X for filenum %d", (cluster - 3) * 64, filenum);
            LOG_INF("Set freq kHz %d for filenum %d", stations[filenum].freq_kHz, filenum);
            k_msleep(150);

            s_start_sectors[filenum] = (cluster - 3) * 64;
            s_start_stations[filenum] = stations[filenum].freq_kHz;

            filenum++;
            vdisk_get_newfilename(stations, num_stations, filenum, newlfn, sizeof(newlfn));
        }

        ent_off += 32;
    }
}

int vdisk_init(struct station_info *stations, uint32_t num_stations)
{
    vdisk_setup_dir(stations, num_stations);

    return disk_access_register(&ram_disk);
}

//SYS_INIT(disk_ram_init, APPLICATION, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);

