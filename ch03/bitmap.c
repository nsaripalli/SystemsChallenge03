// based on cs3650 starter code

#include <stdio.h>
#include <assert.h>
#include "bitmap.h"


void printbits(unsigned long n) {
    char out[64];
    unsigned long i;
    i = 1UL << (sizeof(n) * __CHAR_BIT__ - 1);
    long okay = 0;
    while (i > 0) {
        if (n & i) {
            out[okay] = '1';
        } else {
            out[okay] = '0';
        }
        okay++;
        i >>= 1;
    }
    printf("%s\n", out);
}


int bitmap_get(void* bm, int ii) {
    //ii bits from bm
    //ii/64 longs + ii%64 bits from bm
    long numlongs = ii / (8 * sizeof(long));
    long leftOffset = ii % (8 * sizeof(long));
    long shiftLen = (8 * sizeof(long)) - (leftOffset + 1);
    unsigned long l = *(unsigned long*)(bm + (numlongs * sizeof(long)));

    return (l >> shiftLen) % 2;
}


void bitmap_put(void* bm, int ii, int vv) {
    assert(vv == 0 || vv == 1);
    long* cur = (long*)(bm + (ii/64));
    long leftOffsetBits = ii % (8*sizeof(long));
    long shiftLenBits = (8*sizeof(long)) - (leftOffsetBits + 1);
    unsigned long mask = 1UL << shiftLenBits;
    // clear the spot
    unsigned long clearmask = ~mask;
    *cur &= clearmask;
    // add the value we want
    unsigned long addmask = ((unsigned long) vv) << shiftLenBits;
    *cur |= addmask;
}


//size should be in bytes
void bitmap_print(void* bm, int size) {
    unsigned long* map = (unsigned long*)bm;
    for(int i = 0; i < (size / sizeof(unsigned long)); ++i) {
        printbits(map[i]);
    }
}
