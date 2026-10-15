/* Loader for the original LIB1/LIB2/LIB3 EGA image libraries. Format notes in NOTES.md. */
#ifndef SR_LIB_H
#define SR_LIB_H

#include <stdint.h>

typedef struct {
    int      w, h;
    uint16_t flags;
    uint8_t *pixels;        /* w*h palette indices 0..15 */
} Image;

#define IMAGE_SPRITE 0x100  /* flags: drawn with colour 0 transparent */

typedef struct {
    int       count;
    uint16_t *w, *h, *flags;
    uint8_t (*table)[16];
    uint32_t *offset;
    uint16_t *size;
    uint8_t  *blob;         /* compressed data area */
    uint32_t  blob_size;
    Image    *cache;        /* decoded on first use */
} Lib;

int  lib_open(Lib *lib, const char *path);
void lib_close(Lib *lib);
/* Returns a decoded image (cached), or NULL on bad index/corrupt data. */
const Image *lib_image(Lib *lib, int index);

#endif
