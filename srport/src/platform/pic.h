#pragma once
/* Picture libraries LIB1 / LIB2 (FORMATS.md). */
#include "types.h"

/* The unpacker 0e92:0006: expands `len` packed bytes from src into dst. tokens[0..ntok-1] are the
 * picture's run tokens (directory bytes 7..22); token k stands for RUN_COUNT[k] bytes of RUN_VALUE[k]
 * (DS:4EEE / DS:4EFE), 00 n / FF n for n+1 bytes of 00 / FF. Returns the number of bytes written. */
u16 pic_unpack(const u8 *src, u8 *dst, u16 len, const u8 *tokens, u16 ntok);
