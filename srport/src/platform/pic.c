#include "platform/pic.h"

#include <string.h>

#include "mem.h"

/* Run tables DS:4EEE (counts) and DS:4EFE (values), read from the loaded image. */
#define DS_run_count 0x4EEE
#define DS_run_value 0x4EFE

u16 pic_unpack(const u8 *src, u8 *dst, u16 len, const u8 *tokens, u16 ntok)
{
    u8 token_of[256];                        /* byte -> token number 1..ntok, 0 = literal ([bp-12Ah]) */
    memset(token_of, 0, sizeof token_of);
    for (u16 k = 0; k < ntok; k++) token_of[tokens[k]] = (u8)(k + 1);
    const u8 *count = mp(DGROUP, DS_run_count), *value = mp(DGROUP, DS_run_value);

    u16 out = len;                           /* [bp-0Ah]: grows by run length - 1 per run */
    s16 left = (s16)len;
    while (left > 0) {
        left--;
        u8 b = *src++;
        u16 n;
        if (b == 0x00 || b == 0xFF) {
            left--;
            n = (u16)(*src++ + 1);
            out = (u16)(out + n - 2);        /* the count byte and the run byte: n bytes from 2 */
        } else if (token_of[b]) {
            u8 k = token_of[b];
            n = count[k - 1];
            b = value[k - 1];
            out = (u16)(out + n - 1);
        } else {
            *dst++ = b;
            continue;
        }
        while (n--) *dst++ = b;
    }
    return out;
}
