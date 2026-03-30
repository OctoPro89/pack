#include "lzss.h"

#include <stdlib.h>

#define max(a,b) (((a) > (b)) ? (a) : (b))
#define min(a,b) (((a) < (b)) ? (a) : (b))

#define HASH_TABLE_SIZE 65536  // Define hash table size (adjust based on needs)

// Rolling hash state
static u64 rolling_hash(const u8* data, u64 length) {
    u64 hash = 0;
    for (u64 i = 0; i < length; ++i) {
        hash = (hash * 31 + data[i]) % HASH_TABLE_SIZE;
    }
    return hash;
}

static void find_best_match_hashing(const u8* in, u64 in_size, u64 i, u64* best_len, u64* best_offset) {
    *best_len = 0;
    *best_offset = 0;

    u64 window_start = (i > LZSS_WINDOW_SIZE) ? (i - LZSS_WINDOW_SIZE) : 0;

    static u64 hash_table[HASH_TABLE_SIZE] = { 0 };  // Hash table for substring positions
    u64 match_len = 0;

    // Hash the current substring at i
    u64 current_hash = rolling_hash(&in[i], LZSS_MIN_MATCH);

    // Check the hash table for previous occurrences of this substring
    u64 previous_position = hash_table[current_hash];

    // If the substring exists in the hash table, check for matching
    if (previous_position > window_start && previous_position < i) {
        u64 k = 0;
        while ((i + k < in_size) && (in[previous_position + k] == in[i + k])) {
            ++k;
            if (k >= LZSS_MAX_MATCH) break;  // Stop when max match is found
        }

        if (k > *best_len) {
            *best_len = k;
            *best_offset = i - previous_position;
        }
    }

    // Update the hash table for this position
    hash_table[current_hash] = i;
}


static void find_best_match( const u8* in, u64 in_size, u64 i, u64* best_len, u64* best_offset
) {
	*best_len = 0;
	*best_offset = 0;

	u64 window_start = (i > LZSS_WINDOW_SIZE) ? (i - LZSS_WINDOW_SIZE) : 0;

	for (u64 j = window_start; j < i; ++j) {
		u64 k = 0;
		while ((i + k < in_size) && (in[j + k] == in[i + k]) && (k < LZSS_MAX_MATCH)) {
			++k;
		}

		if (k > *best_len) {
			*best_len = k;
			*best_offset = i - j;
		}
	}
}


u64 lzss_compress(const u8* in, u64 in_size, u8** out) {
    u64 out_cap = in_size * 2;
    *out = malloc(out_cap);

    u64 i = 0;
    u64 out_pos = 0;

    while (i < in_size) {
        u64 flag_pos = out_pos;
        (*out)[out_pos++] = 0; // placeholder
        u8 flags = 0;
        u8 bits = 0;

        while (bits < 8 && i < in_size) {
            u64 best_len, best_offset;
            //find_best_match(in, in_size, i, &best_len, &best_offset);
            find_best_match_hashing(in, in_size, i, &best_len, &best_offset);

            flags <<= 1;

            if (best_len >= LZSS_MIN_MATCH) {
                // reference
                u16 ref = (u16)((best_offset << 4) | (best_len - LZSS_MIN_MATCH));
                (*out)[out_pos++] = (ref >> 8) & 0xFF;
                (*out)[out_pos++] = ref & 0xFF;
                i += best_len;
            }
            else {
                // literal
                flags |= 1;
                (*out)[out_pos++] = in[i++];
            }

            bits++;
        }

        flags <<= (8 - bits); // pad remaining bits
        (*out)[flag_pos] = flags;
    }

    return out_pos;
}

u64 lzss_decompress(const u8* in, u64 in_size, u8** out, u64 expected_size) {
    *out = malloc(expected_size);

    u64 in_pos = 0;
    u64 out_pos = 0;

    while (in_pos < in_size && out_pos < expected_size) {
        u8 flags = in[in_pos++];

        for (u8 bit = 0; bit < 8 && in_pos < in_size && out_pos < expected_size; ++bit) {
            if (flags & 0x80) {
                // literal
                (*out)[out_pos++] = in[in_pos++];
            }
            else {
                // reference
                u16 ref = (in[in_pos] << 8) | in[in_pos + 1];
                in_pos += 2;

                u64 offset = ref >> 4;
                u64 length = (ref & 0xF) + LZSS_MIN_MATCH;

                for (u64 k = 0; k < length && out_pos < expected_size; ++k) {
                    (*out)[out_pos] = (*out)[out_pos - offset];
                    out_pos++;
                }
            }

            flags <<= 1;
        }
    }

    return out_pos;
}