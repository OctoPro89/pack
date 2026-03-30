#pragma once

#include "common.h"

#define LZSS_WINDOW_SIZE 4095 // Max size is 4095
#define LZSS_MIN_MATCH 3
#define LZSS_MAX_MATCH (LZSS_MIN_MATCH + 15)

u64 lzss_compress(const u8* in, u64 in_size, u8** out);
u64 lzss_decompress(const u8* in, u64 in_size, u8** out, u64 expected_size);