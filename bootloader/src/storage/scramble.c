#include "scramble.h"

static unsigned int scramble_seed;

void scramble_srand(unsigned int n) {
    scramble_seed = n & 0xffff;
}

unsigned int scramble_rand(void) {
    scramble_seed = (scramble_seed * 2109 + 9273) & 0x7fff;
    return (scramble_seed + 0xc000) & 0xffff;
}

void gdrom_descramble(const uint8_t *src, uint8_t *dst, uint32_t filesz) {
    uint32_t src_pos = 0;
    uint32_t remaining = filesz;
    uint8_t *cur_dst = dst;
    uint16_t *idx = (uint16_t *)0x8CF00000UL;

    scramble_srand(filesz);

    for(uint32_t chunksz = (2048 * 1024); chunksz >= 32; chunksz >>= 1) {
        while(remaining >= chunksz) {
            uint32_t sz = chunksz / 32;
            for(uint32_t i = 0; i < sz; i++) {
                idx[i] = (uint16_t)i;
            }
            for(int i = (int)sz - 1; i >= 0; --i) {
                int x = (int)((scramble_rand() * (uint32_t)i) >> 16);
                uint16_t tmp = idx[i];
                idx[i] = idx[x];
                idx[x] = tmp;

                uint8_t *slice_dst = cur_dst + (32 * (uint32_t)idx[i]);
                const uint8_t *slice_src = src + src_pos;
                for(int b = 0; b < 32; b++) {
                    slice_dst[b] = slice_src[b];
                }
                src_pos += 32;
            }
            remaining -= chunksz;
            cur_dst += chunksz;
        }
    }

    if(remaining) {
        for(uint32_t b = 0; b < remaining; b++) {
            cur_dst[b] = src[src_pos + b];
        }
    }
}
