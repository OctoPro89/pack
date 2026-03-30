#define _CRT_SECURE_NO_WARNINGS

#define PACK_VERSION_LO 2
#define PACK_VERSION_HI 0

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "lzss.h"
#include "common.h"

typedef struct {
	char magic[10];
	u16 version;
	u16 file_count;
} PACK_HEADER;

#pragma pack(push, 1)
typedef struct {
	u16 name_len;
	u16 flags; // reserved
	u64 original_size;
	u64 compressed_size;
} PACK_FILEHEADER;
#pragma pack(pop)

#define PACK_FILEHEADER_SIZE 20 // u16+u16+u64+u64

typedef enum {
	PACK_FLAGS_COMPRESSION_LZSS = 0x0001
} PACK_FLAGS;

uint64_t ftell64(FILE* fp) {
	fpos_t pos;
	if (fgetpos(fp, &pos) != 0) {
		return (uint64_t)-1; // error
	}
	// fpos_t may not be an integer, so convert carefully
	return (uint64_t)pos;
}

b32 pack_files(u16 flags, u16 num_files, const char** filenames) {
	FILE* base_pack = fopen("out.pack", "wb");

	if (!base_pack) {
		printf("Failed to open base pack!\n");
		return false;
	}

	// Write header
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

	// Write filedata
	for (u16 i = 0; i < num_files; ++i) {
		FILE* f = fopen(filenames[i], "rb");
		if (!f) {
			printf("Failed to open file '%s'\n", filenames[i]);
			fclose(base_pack);
			return false;
		}

		fseek(f, 0, SEEK_END);
		u64 fsize = ftell64(f);
		fseek(f, 0, SEEK_SET);  /* same as rewind(f); */

		u8* bytes = malloc(fsize);
		if (!bytes) {
			printf("Failed to allocate memory for file '%s' [Read size: %llu]!\n", filenames[i], (unsigned long long)fsize);
			fclose(base_pack);
			return false;
		}
		fread(bytes, fsize, 1, f);

		PACK_FILEHEADER file_header;
		file_header.name_len = (u16)strnlen(filenames[i], 256);
		file_header.flags = 0;

		// Handle compression
		{
			u8* compressed = NULL;
			printf("Compressing file %s\n", filenames[i]);
			u64 compressed_size = lzss_compress(bytes, fsize, &compressed);
			printf("Compressed successfully [size: %llu, original: %llu]\n", compressed_size, fsize);

			if (flags & PACK_FLAGS_COMPRESSION_LZSS) {
				file_header.flags |= PACK_FLAGS_COMPRESSION_LZSS;
				file_header.compressed_size = compressed_size;
				file_header.original_size = fsize;
				fwrite(&file_header, PACK_FILEHEADER_SIZE, 1, base_pack);
				fwrite(filenames[i], file_header.name_len, 1, base_pack);
				fwrite(&compressed[0], compressed_size, 1, base_pack);
			}
			else {
				file_header.compressed_size = 0;
				file_header.original_size = fsize;
				fwrite(&file_header, PACK_FILEHEADER_SIZE, 1, base_pack);
				fwrite(filenames[i], file_header.name_len, 1, base_pack);
				fwrite(&bytes[0], fsize, 1, base_pack);
			}

			free(compressed);
		}

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

	u16 ver_hi = base_header.version >> 8;
	u16 ver_lo = base_header.version & 0xFF;
	b32 is_old = (ver_hi == 0 && ver_lo == 1); // v0.1

	for (u16 i = 0; i < base_header.file_count; ++i) {
		PACK_FILEHEADER file_header;
		memset(&file_header, 0, PACK_FILEHEADER_SIZE);

		if (is_old) {
			// Old v0.1 file: read u32 sizes
			struct {
				u16 name_len;
				u16 flags;
				u32 original_size;
				u32 compressed_size;
			} old_header;
			fread(&old_header, 12, 1, base_pack);

			if (file_header.flags & PACK_FLAGS_COMPRESSION_LZSS) {
				printf("Detected old v0.1 pack file [size: %llu]\n", (unsigned long long)old_header.compressed_size);
			}
			else {
				printf("Detected old v0.1 pack file [size: %llu]\n", (unsigned long long)old_header.original_size);
			}

			file_header.name_len = old_header.name_len;
			file_header.flags = old_header.flags;
			file_header.original_size = old_header.original_size;
			file_header.compressed_size = old_header.compressed_size;
		}
		else {
			// New file: read u64 sizes
			fread(&file_header, PACK_FILEHEADER_SIZE, 1, base_pack);
		}

		char* name = malloc(file_header.name_len + 1);
		if (!name) {
			printf("Failed to allocate memory for filenames!\n");
			return false;
		}

		fread(&name[0], file_header.name_len, 1, base_pack);
		name[file_header.name_len] = '\0';

		u8* bytes = NULL;
		u64 size = 0;

		if (file_header.flags & PACK_FLAGS_COMPRESSION_LZSS) {
			bytes = malloc(file_header.compressed_size);
			size = file_header.compressed_size;
		}
		else {
			bytes = malloc(file_header.original_size);
			size = file_header.original_size;
		}

		if (!bytes) {
			printf("Failed to allocate memory for reading files! [size: %llu]\n", size);
			return false;
		}

		printf("Unpacking file '%s' [size: %llu]\n", name, (unsigned long long)size);

		if (file_header.flags & PACK_FLAGS_COMPRESSION_LZSS) {
			fread(&bytes[0], file_header.compressed_size, 1, base_pack);

			u8* decompressed = NULL;
			u64 out_size = lzss_decompress(bytes, file_header.compressed_size, &decompressed, file_header.original_size);

			if (out_size != file_header.original_size) { __debugbreak(); }
			
			free(bytes);
			bytes = decompressed;
		}
		else {
			fread(&bytes[0], file_header.original_size, 1, base_pack);
		}

		FILE* fp = fopen(name, "wb");
		if (!fp) {
			printf("Failed to open unpacked file for write!\n");
			return false;
		}

		fwrite(&bytes[0], file_header.original_size, 1, fp);

		fclose(fp);

		free(bytes);
		free(name);
	}

	fclose(base_pack);

	return true;
}

b32 view_files(const char* pack_base) {
	FILE* base_pack = fopen(pack_base, "rb");

	if (!base_pack) {
		printf("Failed to open base pack!\n");
		return false;
	}

	PACK_HEADER base_header;
	memset(&base_header, 0, 14);

	fread(&base_header, 14, 1, base_pack);

	u16 ver_hi = base_header.version >> 8;
	u16 ver_lo = base_header.version & 0xFF;
	b32 is_old = (ver_hi == 0 && ver_lo == 1); // v0.1

	printf("PACK FILE INFO:\n");
	printf("\tPack version [Version low: %u Version high: %u] (current: [Version low: %u Version high: %u])\n", (u32)ver_lo, (u32)ver_hi, PACK_VERSION_LO, PACK_VERSION_HI);
	printf("\tFile Count: %u\n", (u32)base_header.file_count);
	printf("\tFile MAGIC:\n");
	printf("\t");
	for (u8 i = 0; i < 10; ++i) {
		printf("%c", base_header.magic[i]);
	}
	printf("\n");

	if (strncmp(base_header.magic, "!?/PACK?/!", 10) != 0) {
		printf("\tInvalid file magic, continuing but file may be corrupt\n");
	}
	else {
		printf("\tValid file magic\n");
	}
	printf("\n");
	printf("ARCHIVED FILES:\n");

	for (u16 i = 0; i < base_header.file_count; ++i) {
		PACK_FILEHEADER file_header;
		memset(&file_header, 0, PACK_FILEHEADER_SIZE);

		if (is_old) {
			// Old v0.1 file: read u32 sizes
			struct {
				u16 name_len;
				u16 flags;
				u32 original_size;
				u32 compressed_size;
			} old_header;
			fread(&old_header, 12, 1, base_pack);

			file_header.name_len = old_header.name_len;
			file_header.flags = old_header.flags;
			file_header.original_size = old_header.original_size;
			file_header.compressed_size = old_header.compressed_size;
		}
		else {
			fread(&file_header, PACK_FILEHEADER_SIZE, 1, base_pack);
		}

		char* name = malloc(file_header.name_len + 1);
		if (!name) {
			printf("Failed to allocate memory for filenames!\n");
			return false;
		}

		fread(&name[0], file_header.name_len, 1, base_pack);
		name[file_header.name_len] = '\0';

		u64 size = 0;
		b32 compressed = false;

		if (file_header.flags & PACK_FLAGS_COMPRESSION_LZSS) {
			size = file_header.compressed_size;
			compressed = true;
		}
		else {
			size = file_header.original_size;
			compressed = false;
		}

		printf("\tFILE %u\n", (u32)i);
		printf("\t\tFilename: %s\n", name);
		u64 remaining = size;
		while (remaining > 0) {
			long chunk = remaining > LONG_MAX ? LONG_MAX : (long)remaining;
			if (fseek(base_pack, chunk, SEEK_CUR) != 0) {
				printf("Failed to skip file data!\n");
				free(name);
				fclose(base_pack);
				return false;
			}
			remaining -= chunk;
		}
		printf("\t\tReported original size (bytes): %llu\n", (unsigned long long)file_header.original_size);
		printf("\t\tReported compressed size (bytes): %llu\n", (unsigned long long)file_header.compressed_size);
		printf("\t\tReported filename length (bytes): %u\n", (u32)file_header.name_len);
		printf("\t\tIs compressed: %u\n", (u32)(file_header.flags & PACK_FLAGS_COMPRESSION_LZSS));
		printf("\t\tFlags: [u16 bytes: 0x%04hx] %s\n", (u32)file_header.flags, file_header.flags & PACK_FLAGS_COMPRESSION_LZSS ? "PACK_FLAGS_COMPRESSION_LZSS" : "None");
		printf("\n");
		
		free(name);
	}

	fclose(base_pack);

	return true;
}

int main(int argc, char* argv[]) {
	if (argc == 1) {
		printf("pack v0.2\n");
		printf("usage: pack [filenames]\n");
		printf("options:\n");
		printf("\t--unpack Unpacks input .pack file\n");
		printf("\t--view Lists info from input .pack file\n");
		return 0;
	}

	if (argc == 3) {
		if (strncmp(argv[1], "--unpack", sizeof("--unpack")) == 0) {
			if (!unpack_files(argv[2])) {
				return 1;
			}

			return 0;
		}
		if (strncmp(argv[1], "--view", sizeof("--view")) == 0) {
			if (!view_files(argv[2])) {
				return 1;
			}

			return 0;
		}
	}

	if (!pack_files(PACK_FLAGS_COMPRESSION_LZSS, argc - 1, &argv[1])) {
		return 1;
	}

	return 0;
}