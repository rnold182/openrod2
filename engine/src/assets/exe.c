#include "exe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *image;
static size_t   image_size;

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

/* Microsoft EXEPACK: data is decompressed backwards from the end of the packed image. */
static int unexepack(const uint8_t *file, size_t len)
{
    if (len < 28 || file[0] != 'M' || file[1] != 'Z') return -1;
    size_t hdr = (size_t)rd16(file + 8) * 16;
    size_t flen = rd16(file + 2) ? (size_t)(rd16(file + 4) - 1) * 512 + rd16(file + 2) : (size_t)rd16(file + 4) * 512;
    if (flen > len || hdr >= flen) return -1;
    const uint8_t *img = file + hdr;
    size_t img_len = flen - hdr;
    size_t ep = (size_t)rd16(file + 22) * 16;           /* CS of the unpacker stub */
    if (ep + 18 > img_len) return -1;
    const uint8_t *eh = img + ep;
    size_t sig = (eh[14] == 'R' && eh[15] == 'B') ? 16 : 18;
    if (eh[sig - 2] != 'R' || eh[sig - 1] != 'B') return -1;
    size_t dest_len = (size_t)rd16(eh + 12) * 16;

    size_t cap = dest_len > ep ? dest_len : ep;
    uint8_t *buf = calloc(cap, 1);
    if (!buf) return -1;
    memcpy(buf, img, ep);
    size_t src = ep, dst = dest_len;
    while (src > 0 && buf[src - 1] == 0xFF) src--;
    for (;;) {
        if (src < 3) { free(buf); return -1; }
        uint8_t cmd = buf[--src];
        size_t n = (size_t)buf[src - 1] << 8 | buf[src - 2];
        src -= 2;
        if ((cmd & 0xFE) == 0xB0) {
            uint8_t fill = buf[--src];
            if (n > dst) { free(buf); return -1; }
            memset(buf + dst - n, fill, n);
            dst -= n;
        } else if ((cmd & 0xFE) == 0xB2) {
            if (n > dst || n > src) { free(buf); return -1; }
            memmove(buf + dst - n, buf + src - n, n);
            dst -= n;
            src -= n;
        } else {
            free(buf);
            return -1;
        }
        if (cmd & 1) break;
    }
    image = buf;
    image_size = cap;
    return 0;
}

int exe_load(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc((size_t)len);
    if (!d || fread(d, 1, (size_t)len, f) != (size_t)len) { fclose(f); free(d); return -1; }
    fclose(f);
    int r = unexepack(d, (size_t)len);
    free(d);
    return r;
}

void exe_free(void)
{
    free(image);
    image = NULL;
    image_size = 0;
}

const uint8_t *exe_ptr(uint16_t seg, uint16_t off, size_t len)
{
    size_t lin = (size_t)seg * 16 + off;
    if (!image || lin + len > image_size) return NULL;
    return image + lin;
}

const uint8_t *exe_ds(uint16_t off, size_t len) { return exe_ptr(EXE_DGROUP, off, len); }

uint16_t exe_ds_u16(uint16_t off)
{
    const uint8_t *p = exe_ds(off, 2);
    return p ? rd16(p) : 0;
}

const char *exe_ds_str(uint16_t off)
{
    const uint8_t *p = exe_ds(off, 1);
    if (!p || off == 0) return "";
    size_t max = image_size - (size_t)(p - image);
    return memchr(p, 0, max) ? (const char *)p : "";
}
