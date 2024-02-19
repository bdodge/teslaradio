/*-----------------------------------------------------------------------*/
/* Low level disk I/O module SKELETON for FatFs     (C)ChaN, 2019        */
/*-----------------------------------------------------------------------*/
/* If a working storage control module is available, it should be        */
/* attached to the FatFs via a glue function rather than modifying it.   */
/* This is an example of glue functions to attach various exsisting      */
/* storage control modules to the FatFs module with a defined API.       */
/*-----------------------------------------------------------------------*/

#include "ff.h"			/* Obtains integer types */
#include "diskio.h"		/* Declarations of disk functions */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "hacksect.h"

#define PRIu32 "u"

#define RAMDISK_SECTOR_SIZE 512
#define RAMDISK_VOLUME_SIZE (3U * 1024 * 1024 * 1024)
#define RAMDISK_SECTOR_COUNT (RAMDISK_VOLUME_SIZE / RAMDISK_SECTOR_SIZE)

static uint8_t *ramdisk_buf;

static void *lba_to_address(uint32_t lba)
{
    return &ramdisk_buf[lba * RAMDISK_SECTOR_SIZE];
}

static bool non_zero_sector(const uint8_t *buff, uint32_t count)
{
	for (uint32_t i = 0; i < count; i++)
	{
		if (buff[i] != 0)
		{
			return true;
		}
	}
	return false;
}

struct secmap
{
	int start;
	int end;
}
section_map[SECTION_MAX];

int cur_section;
int post_format;

DSTATUS disk_status (
	BYTE pdrv		/* Physical drive nmuber to identify the drive */
)
{
    return RES_OK;
}


/*-----------------------------------------------------------------------*/
/* Inidialize a Drive                                                    */
/*-----------------------------------------------------------------------*/

DSTATUS disk_initialize (
	BYTE pdrv				/* Physical drive nmuber to identify the drive */
)
{
	if (!ramdisk_buf)
	{
		ramdisk_buf = (uint8_t*)malloc(RAMDISK_VOLUME_SIZE);

		for (int s = 0; s < SECTION_MAX; s++)
		{
			section_map[s].start = RAMDISK_SECTOR_COUNT;
			section_map[s].end = 0;
		}
	}
	else
	{
		printf("post-format\n");
		post_format = 1;
	}

	if (ramdisk_buf)
	    return RES_OK;
	else
		return RES_ERROR;
}



/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */
/*-----------------------------------------------------------------------*/

DRESULT disk_read (
	BYTE pdrv,		/* Physical drive nmuber to identify the drive */
	BYTE *buff,		/* Data buffer to store read data */
	LBA_t sector,	/* Start sector in LBA */
	UINT count		/* Number of sectors to read */
)
{
	DRESULT res;
	int result;

    uint32_t last_sector = sector + count;

    if (last_sector < sector || last_sector > RAMDISK_SECTOR_COUNT)
    {
        printf("Sector %" PRIu32 " is outside the range %u\n",
                last_sector, RAMDISK_SECTOR_COUNT);
        return RES_ERROR;
    }

    //LOG_INF("read %u %d", sector, count);

    memcpy(buff, lba_to_address(sector), count * RAMDISK_SECTOR_SIZE);
    return RES_OK;

}

/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                       */
/*-----------------------------------------------------------------------*/

#if FF_FS_READONLY == 0

DRESULT disk_write (
	BYTE pdrv,			/* Physical drive nmuber to identify the drive */
	const BYTE *buff,	/* Data to be written */
	LBA_t sector,		/* Start sector in LBA */
	UINT count			/* Number of sectors to write */
)
{
    uint32_t last_sector = sector + count;
	int s;

    if (last_sector < sector || last_sector > RAMDISK_SECTOR_COUNT)
    {
        printf("Sector %" PRIu32 " is outside the range %u\n",
                last_sector, RAMDISK_SECTOR_COUNT);
        return RES_ERROR;
    }

	if (post_format)
	{
		for ( s = 0; s < SECTION_MAX; s++)
		{
			if (sector >= section_map[s].start && last_sector <= section_map[s].end)
			{
				cur_section = s;
				break;
			}
		}

		if (s >= SECTION_MAX)
		{
			cur_section = SECTION_DATA;
		}

		if (sector == 864)
		{
			volatile int a;
			a = sector;
		}
	}

	for (int s = sector; s < last_sector; s++)
	{
		if (s < section_map[cur_section].start)
		{
			section_map[cur_section].start = s;
		}
		if (s > section_map[cur_section].end)
		{
			section_map[cur_section].end = s;
		}
		/*
		if (non_zero_sector(buff, count * RAMDISK_SECTOR_SIZE))
		{
			printf("nz sect %u  in %u\n", s, cur_section);
		}
		*/
	}
    memcpy(lba_to_address(sector), buff, count * RAMDISK_SECTOR_SIZE);

    return RES_OK;
}

#endif


/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                               */
/*-----------------------------------------------------------------------*/

DRESULT disk_ioctl (
	BYTE pdrv,		/* Physical drive nmuber (0..) */
	BYTE cmd,		/* Control code */
	void *buff		/* Buffer to send/receive control data */
)
{
    switch (cmd)
    {
    case CTRL_SYNC:
        break;

    case GET_SECTOR_COUNT:
        *(uint32_t *)buff = RAMDISK_SECTOR_COUNT;
        break;

    case GET_SECTOR_SIZE:
        *(uint32_t *)buff = RAMDISK_SECTOR_SIZE;
        break;

    default:
        return RES_PARERR;
    }
	return RES_OK;
}

#include "fcntl.h"
#include "unistd.h"

const char *section_name(int section_type)
{
	switch (section_type)
	{
	case SECTION_MBR:	return "MBR";
	case SECTION_VBR1:	return "VBR";
	case SECTION_VBR2:	return "VBR_Backup";
	case SECTION_FSINFO:return "FSINFO";
	case SECTION_ROOTDIR:return "ROOTDIR";
	case SECTION_FAT:	return "FAT";
	case SECTION_ALLOCBMAP: return "ALLOCBMAP";
	case SECTION_UPCASE:	return "UPCASETAB";
	case SECTION_DATA:	return "DATA";
	default: return "???";
	}
}

const char *section_data_name(int section_type, int sector, char *buff, int buff_size)
{
	if (section_type == SECTION_ROOTDIR)
	{
		if (sector >= 0)
		{
			snprintf(buff, buff_size, "%s_sect + %d", section_name(section_type), sector * RAMDISK_SECTOR_SIZE);
		}
		else
		{
			snprintf(buff, buff_size, "%s_sect", section_name(section_type));
		}
	}
	else
	{
		snprintf(buff, buff_size, "%s_sect_%u", section_name(section_type), sector);
	}
	return buff;
}

int start_section(int section_type)
{
	cur_section = section_type;
	section_map[cur_section].start = RAMDISK_SECTOR_COUNT;
	section_map[cur_section].end = 0;
	return 0;
}

int end_section(int section_type)
{
	int count;

	count = section_map[section_type].end - section_map[section_type].start + 1;
	if (count <= 0)
	{
		printf("no data for section %s\n", section_name(section_type));
		return 0;
	}

	printf("End Section %s  %u sectors\n", section_name(section_type), count);
	return 0;
}


int dump_section(int section_type, int sf)
{
	char buf[256];
	char sname[256];
	int sect;
	int s;
	int len;
	int ilen;
	int count;

	count = section_map[section_type].end - section_map[section_type].start + 1;
	if (count <= 0)
	{
		printf("no data for section %s\n", section_name(section_type));
		return 0;
	}

	if (section_type == SECTION_FAT)
	{
		len = snprintf(buf, sizeof(buf), "/* FAT, can be synthesized so not compiled */\n/*\n");
		write(sf, buf, len);
	}

	if (section_type == SECTION_ROOTDIR)
	{
		len = snprintf(buf, sizeof(buf), "\n// Section %s sectors %u to %u at %u\n//\nstatic uint8_t %s[] = {\n",
						section_name(section_type),
						0,
						count,
						section_map[section_type].start,
						section_data_name(section_type, -1, sname, sizeof(sname)));
		write(sf, buf, len);

		int null_sectors = 0;

		for (sect = 0; sect < count; sect++)
		{
			uint8_t *sector_data = lba_to_address(section_map[section_type].start + sect);

			if (non_zero_sector(sector_data, RAMDISK_SECTOR_SIZE))
			{
				if (null_sectors > 0)
				{
					printf("--------- got internal 0 sector in root dir section ?????????????\n");

					for (s = 0; s < null_sectors * RAMDISK_SECTOR_SIZE; s+= 16)
					{
						len = 0;
						for (int b = 0; b < 16; b++)
						{
							ilen = snprintf(buf + len, sizeof(buf) - len, "0x00%s", ", ");
							len += ilen;
						}
						ilen = snprintf(buf + len, sizeof(buf) - len, "\n");
						len += ilen;
						write(sf, buf, len);
					}
					null_sectors = 0;
				}

				for (s = 0; s < RAMDISK_SECTOR_SIZE; s+= 16)
				{
					len = 0;
					for (int b = 0; b < 16; b++)
					{
						ilen = snprintf(buf + len, sizeof(buf) - len, "0x%02X%s", sector_data[s + b], ", ");
						len += ilen;
					}
					ilen = snprintf(buf + len, sizeof(buf) - len, "\n");
					len += ilen;
					write(sf, buf, len);
				}
			}
			else
			{
				null_sectors++;
			}
		}
		len = snprintf(buf, sizeof(buf), "};\n\n");
		write(sf, buf, len);
	}
	else
	{
		for (sect = 0; sect < count; sect++)
		{
			uint8_t *sector_data = lba_to_address(section_map[section_type].start + sect);

			if (non_zero_sector(sector_data, RAMDISK_SECTOR_SIZE))
			{
				len = snprintf(buf, sizeof(buf), "\n// Section %s sector %u of %d at %u\n//\nstatic const uint8_t %s[] = {\n",
								section_name(section_type),
								sect,
								count,
								section_map[section_type].start + sect,
								section_data_name(section_type, sect, sname, sizeof(sname)));
				write(sf, buf, len);

				for (s = 0; s < RAMDISK_SECTOR_SIZE; s+= 16)
				{
					len = 0;
					for (int b = 0; b < 16; b++)
					{
						ilen = snprintf(buf + len, sizeof(buf) - len, "0x%02X%s", sector_data[s + b], ", ");
						len += ilen;
					}
					ilen = snprintf(buf + len, sizeof(buf) - len, "\n");
					len += ilen;
					write(sf, buf, len);
				}

				len = snprintf(buf, sizeof(buf), "};\n");
				write(sf, buf, len);
			}
		}
	}

	if (section_type == SECTION_FAT)
	{
		len = snprintf(buf, sizeof(buf), "*/\n");
		write(sf, buf, len);
	}

	return 0;
}

int dump_section_xref(int section_type, int sf)
{
	char buf[256];
	char sname[256];
	int sect;
	int len;
	int count;

	count = section_map[section_type].end - section_map[section_type].start + 1;
	if (count <= 0)
	{
		return 0;
	}

	if (!non_zero_sector(lba_to_address(section_map[section_type].start), RAMDISK_SECTOR_SIZE * count))
	{
		printf("no non-zero sectors in %s\n", section_name(section_type));
		return 0;
	}

	if (section_type == SECTION_FAT)
	{
		len = snprintf(buf, sizeof(buf), "/* FAT, can be synthesized so not compiled */\n/*\n");
		write(sf, buf, len);
	}

	len = snprintf(buf, sizeof(buf), "\n// Section %s. %u sectors at %u\n//\nstatic const uint8_t *%s_xref[] = {\n",
					section_name(section_type),
					count,
					section_map[section_type].start,
					section_name(section_type));
	write(sf, buf, len);

	for (sect = 0; sect < count; sect++)
	{
		uint8_t *sector_data = lba_to_address(section_map[section_type].start + sect);

		if (non_zero_sector(sector_data, RAMDISK_SECTOR_SIZE))
		{
			len = snprintf(buf, sizeof(buf), "    %s,\n",
							section_data_name(section_type, sect, sname, sizeof(sname)));
		}
		else
		{
			len = snprintf(buf, sizeof(buf), "    NULL,\n");
		}
		write(sf, buf, len);
	}

	len = snprintf(buf, sizeof(buf), "};\n\n");
	write(sf, buf, len);

	if (section_type == SECTION_FAT)
	{
		len = snprintf(buf, sizeof(buf), "*/\n");
		write(sf, buf, len);
	}

	return 0;
}

int dump_section_table(int section_type, int sf)
{
	char buf[256];
	char sname[256];
	int sect;
	int len;
	int count;

	if (section_map[section_type].end >= section_map[section_type].start)
	{
		count = section_map[section_type].end - section_map[section_type].start + 1;
	}
	else
	{
		count = 0;
	}

	if (
			section_type != SECTION_FAT
		&&	count
		&&	non_zero_sector(lba_to_address(section_map[section_type].start), RAMDISK_SECTOR_SIZE * count)
	)
	{
		snprintf(sname, sizeof(sname), "%s_xref", section_name(section_type));
	}
	else
	{
		snprintf(sname, sizeof(sname), "NULL");
	}

	len = snprintf(buf, sizeof(buf), "\n// Section %s\n{\n    .start_sector = %u,\n    .count = %u,\n    .sectors = %s\n},\n",
					section_name(section_type),
					section_map[section_type].start,
					count,
					sname);
	write(sf, buf, len);
	return 0;
}

int dump_sections(int root_dir_cnt, int max_fname_len)
{
	int section;
	int sect;
	int outf;
	char buf[256];
	int len;
	int count;

	outf = open("fs_sects.c", O_WRONLY | O_CREAT | O_TRUNC, 0664);
	if (outf < 0)
	{
		return -1;
	}

	len = snprintf(buf, sizeof(buf), "\n#include <stdint.h>\n#include <stdio.h>\n// Generated\n\n");
	write(outf, buf, len);

	len = snprintf(buf, sizeof(buf), "#define VFAT_VOLUME_SIZE (%u)\n", RAMDISK_VOLUME_SIZE);
	write(outf, buf, len);
	len = snprintf(buf, sizeof(buf), "#define VFAT_SECTOR_SIZE (%u)\n", RAMDISK_SECTOR_SIZE);
	write(outf, buf, len);
	len = snprintf(buf, sizeof(buf), "#define VFAT_ROOT_DIR_COUNT (%u)\n", root_dir_cnt);
	write(outf, buf, len);
	len = snprintf(buf, sizeof(buf), "#define VFAT_MAX_FILENAME (%u)\n", max_fname_len);
	write(outf, buf, len);

	len = snprintf(buf, sizeof(buf), "#define SECTION_MBR (%u)\n", SECTION_MBR);
	write(outf, buf, len);
	len = snprintf(buf, sizeof(buf), "#define SECTION_VBR1 (%u)\n", SECTION_VBR1);
	write(outf, buf, len);
	len = snprintf(buf, sizeof(buf), "#define SECTION_VBR2 (%u)\n", SECTION_VBR2);
	write(outf, buf, len);
	len = snprintf(buf, sizeof(buf), "#define SECTION_FSINFO (%u)\n", SECTION_FSINFO);
	write(outf, buf, len);
	len = snprintf(buf, sizeof(buf), "#define SECTION_ROOTDIR (%u)\n", SECTION_ROOTDIR);
	write(outf, buf, len);
	len = snprintf(buf, sizeof(buf), "#define SECTION_FAT (%u)\n", SECTION_FAT);
	write(outf, buf, len);
	len = snprintf(buf, sizeof(buf), "#define SECTION_ALLOCBMAP (%u)\n", SECTION_ALLOCBMAP);
	write(outf, buf, len);
	len = snprintf(buf, sizeof(buf), "#define SECTION_UPCASE (%u)\n", SECTION_UPCASE);
	write(outf, buf, len);
	len = snprintf(buf, sizeof(buf), "#define SECTION_DATA (%u)\n", SECTION_DATA);
	write(outf, buf, len);
	len = snprintf(buf, sizeof(buf), "#define SECTION_MAX (%u)\n", SECTION_MAX);
	write(outf, buf, len);

	for (section = 0; section < SECTION_MAX; section++)
	{
		dump_section(section, outf);
	}


	for (section = 0; section < SECTION_MAX; section++)
	{
		dump_section_xref(section, outf);
	}

	len = snprintf(buf, sizeof(buf), "\n\nstruct sect_desc {\n    int start_sector;\n    int count;\n    const uint8_t **sectors;\n};\n");
	write(outf, buf, len);

	len = snprintf(buf, sizeof(buf), "\nstruct sect_desc section_sectors[] = {\n");
	write(outf, buf, len);

	for (sect = 0; sect < SECTION_MAX; sect++)
	{
		dump_section_table(sect, outf);
	}

	len = snprintf(buf, sizeof(buf), "};\n");
	write(outf, buf, len);

	if (outf >= 0)
	{
		close(outf);
	}

	return 0;
}

