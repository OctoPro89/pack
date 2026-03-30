#define _CRT_SECURE_NO_WARNINGS

#define PACK_VERSION_LO 1
#define PACK_VERSION_HI 0

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "common.h"

typedef struct {
	char magic[10];
	u16 version;
	u16 file_count;
} PACK_HEADER;

typedef struct {
	u16 name_len;
	u16 flags; // reserved
	u32 data_size;
} PACK_FILEHEADER;

b32 pack_files(u16 num_files, const char** filenames) {
	FILE* base_pack = fopen("out.pack", "wb");

	if (!base_pack) {
		printf("Failed to open base pack!\n");
		return false;
	}

	{
		PACK_HEADER header;
		// "!?/PACK?/!"
		header.magic[0] = '!';
		header.magic[1] = '?';
		header.magic[2] = '/';
		header.magic[3] = 'P';
		header.magic[4] = 'A';
		header.magic[5] = 'C';
		header.magic[6] = 'K';
		header.magic[7] = '?';
		header.magic[8] = '/';
		header.magic[9] = '!';

		header.version = (PACK_VERSION_HI << 8) | PACK_VERSION_LO;

		header.file_count = num_files;

		fwrite(&header, 14, 1, base_pack);
	}

	for (u16 i = 0; i < num_files; ++i) {
		FILE* f = fopen(filenames[i], "rb");
		if (!f) {
			printf("Failed to open file '%s'\n", filenames[i]);
			fclose(base_pack);
			return false;
		}

		fseek(f, 0, SEEK_END);
		long fsize = ftell(f);
		fseek(f, 0, SEEK_SET);  /* same as rewind(f); */

		u8* bytes = malloc(fsize);
		if (!bytes) {
			printf("Failed to allocate memory for file '%s'!\n", filenames[i]);
			fclose(base_pack);
			return false;
		}
		fread(bytes, fsize, 1, f);

		PACK_FILEHEADER file_header;
		file_header.data_size = fsize;
		file_header.flags = 0;
		file_header.name_len = (u16)strnlen(filenames[i], 256);

		fwrite(&file_header, 8, 1, base_pack);
		fwrite(filenames[i], file_header.name_len, 1, base_pack);
		fwrite(&bytes[0], file_header.data_size, 1, base_pack);

		free(bytes);

		fclose(f);
	}

	fclose(base_pack);

	return true;
}

b32 unpack_files(const char* pack_base) {
	FILE* base_pack = fopen(pack_base, "rb");

	if (!base_pack) {
		printf("Failed to open base pack!\n");
		return false;
	}

	PACK_HEADER base_header;
	memset(&base_header, 0, 14);

	fread(&base_header, 14, 1, base_pack);

	for (u16 i = 0; i < base_header.file_count; ++i) {
		PACK_FILEHEADER file_header;
		memset(&file_header, 0, 8);

		fread(&file_header, 8, 1, base_pack);

		char* name = malloc(file_header.name_len + 1);
		if (!name) {
			printf("Failed to allocate memory for filenames!\n");
			return false;
		}

		fread(&name[0], file_header.name_len, 1, base_pack);
		name[file_header.name_len] = '\0';

		u8* bytes = malloc(file_header.data_size);
		if (!bytes) {
			printf("Failed to allocate memory for reading files!\n");
			return false;
		}

		fread(&bytes[0], file_header.data_size, 1, base_pack);

		FILE* fp = fopen(name, "wb");
		if (!fp) {
			printf("Failed to open unpacked file for write!\n");
			return false;
		}

		fwrite(&bytes[0], file_header.data_size, 1, fp);

		fclose(fp);

		free(bytes);
		free(name);
	}

	fclose(base_pack);

	return true;
}

int main(int argc, char* argv[]) {
	if (argc == 3) {
		if (strncmp(argv[1], "--unpack", sizeof("--unpack")) == 0) {
			if (!unpack_files(argv[2])) {
				return 1;
			}

			return 0;
		}
	}
	
	if (!pack_files(argc - 1, &argv[1])) {
		return 1;
	}

	return 0;
}