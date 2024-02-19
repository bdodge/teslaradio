
#pragma once

enum
{
	SECTION_MBR,
	SECTION_VBR1,
	SECTION_VBR2,
	SECTION_FSINFO,
	SECTION_ROOTDIR,
	SECTION_FAT,
	SECTION_ALLOCBMAP,
	SECTION_UPCASE,
	SECTION_DATA,
	SECTION_MAX
}
section_type_t;

int start_section(int section_type);
int end_section(int section_type);
int dump_sections(int root_dir_cnt, int max_filename);

