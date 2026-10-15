#include "lib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Shorthand run table from SR.EXE (DS:4b72 counts, DS:4b82 values). */
static const uint8_t rle_counts[16] = { 1, 1, 2, 3, 4, 2, 3, 5, 6, 6, 4, 7, 7, 5, 8, 8 };
static const uint8_t rle_values[16] = { 0x00, 0xff, 0x00, 0x00, 0x00, 0xff, 0xff, 0x00,
                                        0xff, 0x00, 0xff, 0xff, 0x00, 0xff, 0xff, 0x00 };

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

int lib_open(Lib *lib, const char *path)
{
    memset(lib, 0, sizeof *lib);
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc((size_t)len);
    if (!d || fread(d, 1, (size_t)len, f) != (size_t)len) { fclose(f); free(d); return -1; }
    fclose(f);

    int n = rd16(d);
    long base = 2 + 28L * n;
    if (base > len) { free(d); return -1; }
    lib->count = n;
    lib->w = calloc((size_t)n, sizeof *lib->w);
    lib->h = calloc((size_t)n, sizeof *lib->h);
    lib->flags = calloc((size_t)n, sizeof *lib->flags);
    lib->table = calloc((size_t)n, sizeof *lib->table);
    lib->offset = calloc((size_t)n, sizeof *lib->offset);
    lib->size = calloc((size_t)n, sizeof *lib->size);
    lib->cache = calloc((size_t)n, sizeof *lib->cache);
    for (int i = 0; i < n; i++) {
        const uint8_t *r = d + 2 + 28 * i;
        lib->w[i] = rd16(r);
        lib->h[i] = rd16(r + 2);
        lib->flags[i] = rd16(r + 4);
        memcpy(lib->table[i], r + 6, 16);
        lib->offset[i] = rd32(r + 22);
        lib->size[i] = rd16(r + 26);
    }
    lib->blob_size = (uint32_t)(len - base);
    lib->blob = malloc(lib->blob_size);
    memcpy(lib->blob, d + base, lib->blob_size);
    free(d);
    return 0;
}

void lib_close(Lib *lib)
{
    if (lib->cache)
        for (int i = 0; i < lib->count; i++) free(lib->cache[i].pixels);
    free(lib->w); free(lib->h); free(lib->flags); free(lib->table);
    free(lib->offset); free(lib->size); free(lib->blob); free(lib->cache);
    memset(lib, 0, sizeof *lib);
}

/* Decompress into `out` (capacity `cap`); returns bytes produced. */
static size_t rle_decode(const uint8_t *src, size_t n, const uint8_t table[16], uint8_t *out, size_t cap)
{
    uint8_t lut[256] = { 0 };
    for (int k = 0; k < 16; k++) lut[table[k]] = (uint8_t)(k + 1);
    size_t o = 0, i = 0;
#define EMIT(v, cnt) do { for (int r_ = 0; r_ < (cnt) && o < cap; r_++) out[o++] = (v); } while (0)
    while (i < n) {
        uint8_t b = src[i++];
        if (b == 0x00 || b == 0xff) {
            if (i >= n) break;
            uint8_t c = src[i++];
            if (c == 0) EMIT(b == 0 ? table[0] : table[1], 1);
            else EMIT(b, c + 1);
        } else if (lut[b]) {
            int k = lut[b] - 1;
            EMIT(rle_values[k], rle_counts[k]);
        } else {
            EMIT(b, 1);
        }
    }
#undef EMIT
    return o;
}

const Image *lib_image(Lib *lib, int index)
{
    if (index < 0 || index >= lib->count) return NULL;
    Image *img = &lib->cache[index];
    if (img->pixels) return img;

    int w = lib->w[index], h = lib->h[index];
    uint32_t off = lib->offset[index], size = lib->size[index];
    if ((uint64_t)off + size + 1 > lib->blob_size) return NULL;
    const uint8_t *raw = lib->blob + off;
    if (raw[0] != 0xBC) return NULL;

    int bpr = (w + 7) / 8;
    size_t plane = (size_t)bpr * h, cap = plane * 4;
    uint8_t *planar = calloc(cap, 1);
    size_t got = rle_decode(raw + 1, size - 1, lib->table[index], planar, cap);
    int nplanes = (int)(got / plane);

    img->w = w;
    img->h = h;
    img->flags = lib->flags[index];
    img->pixels = calloc((size_t)w * h, 1);
    for (int p = 0; p < nplanes; p++)
        for (int y = 0; y < h; y++) {
            const uint8_t *row = planar + p * plane + (size_t)y * bpr;
            for (int x = 0; x < w; x++)
                if (row[x >> 3] & (0x80 >> (x & 7)))
                    img->pixels[y * w + x] |= (uint8_t)(1 << p);
        }
    if (nplanes == 1)   /* single-plane images are 1-bit masks/overlays */
        for (int i = 0; i < w * h; i++) img->pixels[i] = img->pixels[i] ? 15 : 0;
    free(planar);
    return img;
}
