
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ff.h"
#include "hacksect.h"

#define NUM_ROOT_FILES	32
#define FILE_NAME_MAX	64

int main(int argc, char **argv)
{
	int ret;
	MKFS_PARM mkopts;

	mkopts.fmt = FM_FAT32; /* FM_EXFAT;  */
	mkopts.n_fat = 1;
	mkopts.align = 1;
	mkopts.n_root = NUM_ROOT_FILES;
	mkopts.au_size = 0;

	do
	{
		ret = f_mkfs((TCHAR*)"0:/", &mkopts, malloc(65536), 65536);
		printf("mkfs=%d\n", ret);
		if (ret)
		{
			break;
		}

		FATFS fs;
		memset(&fs, 0, sizeof(fs));
		ret = f_mount(&fs, "0:/", 1);
		printf("f_mount %d\n", ret);
		if (ret)
		{
			break;
		}


		FIL file;
		char fxbuf[100];
		char fname[128];

		for (int f = 0; f < NUM_ROOT_FILES; f++)
		{
			memset(&file, 0, sizeof(file));

			int totlen = 0;
			int len;

			while (totlen < (FILE_NAME_MAX - 8))
			{
				len = snprintf(fxbuf + totlen, sizeof(fxbuf) - totlen, "%02d", f);
				totlen += len;
			}

			fxbuf[FILE_NAME_MAX - 8] = '\0';
			snprintf(fname, sizeof(fname), "0:/file%s.wav", fxbuf);
			ret = f_open(&file, fname, FA_CREATE_NEW | FA_WRITE);
			printf("f_open%d %d\n", f, ret);
			if (ret)
			{
				break;
			}
			uint8_t buf[512];
			uint32_t wc;
			uint32_t fz;

			if (f == 0)
			{
				// 2 Gb
				fz =  4U * 512 * 1024 * 1024;
			}
			else
			{
				fz = 512;
			}
			for (int s = 0; s < fz; s += 512)
			{
				memset(buf, 0, sizeof(buf));
	//			memset(buf, (s/512 + 1) & 0xFF, sizeof(buf));
				ret = f_write(&file, buf, sizeof(buf), &wc);
				if (ret || (wc != sizeof(buf)))
				{
					printf("write filed at sec %d  ret=%d wc=%u\n", s, ret, wc);
					break;
				}
			}
			f_close(&file);
		}

		dump_sections(NUM_ROOT_FILES, FILE_NAME_MAX);
	}
	while (0);

	return ret;
}

