/* Read-only access to data tables inside the original SR.EXE (EXEPACK is unpacked in memory).
 * Offsets are the same ones used in NOTES.md: DS = DGROUP segment 0x3e08 of the unpacked image. */
#ifndef SR_EXE_H
#define SR_EXE_H

#include <stddef.h>
#include <stdint.h>

#define EXE_DGROUP 0x3e08

int  exe_load(const char *path);
void exe_free(void);

/* Pointer to `seg:off` inside the unpacked load image, or NULL if out of range. */
const uint8_t *exe_ptr(uint16_t seg, uint16_t off, size_t len);
const uint8_t *exe_ds(uint16_t off, size_t len);
uint16_t       exe_ds_u16(uint16_t off);
const char    *exe_ds_str(uint16_t off);       /* NUL-terminated string in DGROUP ("" if invalid) */

#endif
