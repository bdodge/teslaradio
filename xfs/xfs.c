
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ff.h"
#include "hacksect.h"

#define NUM_ROOT_FILES_MAX	32
#define FILE_NAME_MAX		26

// Create a "C" header file that describes a FAT32 file system
// with a fixed directory of N files, where N defaults to 1
//
int main(int argc, char **argv)
{
	int ret;
	int num_root_files;
	char *root_file;
	int root_file_name_length;
	MKFS_PARM mkopts;

	if (argc < 2)
	{
		fprintf(stderr, "Usage %s <root filename>  OR  %s <number of root files>\n",
				*argv, *argv);
		return -1;
	}

	argv++;
	if (*argv[0] >= '0' && *argv[0] <= '9')
	{
		num_root_files = strtoul(*argv, NULL, 0);
		root_file = "";
	}
	else
	{
		num_root_files = 1;
		root_file = *argv;
	}
	// Build a FAT32 file system
	//
	mkopts.fmt = FM_FAT32; /* FM_EXFAT;  */
	mkopts.n_fat = 1;
	mkopts.align = 1;
	mkopts.n_root = num_root_files;
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

		for (int f = 0; f < num_root_files; f++)
		{
			memset(&file, 0, sizeof(file));

			if (num_root_files > 1)
			{
				int len;
				int totlen = 0;

				while (totlen < (FILE_NAME_MAX - 8))
				{
					len = snprintf(fxbuf + totlen, sizeof(fxbuf) - totlen, "%02d", f);
					totlen += len;
				}

				fxbuf[FILE_NAME_MAX - 8] = '\0';
				root_file_name_length = snprintf(fname, sizeof(fname), "0:/file%s.wav", fxbuf);
			}
			else
			{
				root_file_name_length = snprintf(fname, sizeof(fname), "0:/%s", root_file);
			}

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
				// 128 sectors, to force two clusters (each is 64 sectors)  the first
				// cluster is in the file entry, the second in the fat
				//
				fz = 64 * 2 * 512;
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

		dump_sections(num_root_files, root_file_name_length);
	}
	while (0);

	return ret;
}

