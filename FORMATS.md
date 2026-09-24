# Street Rod file formats

Decoders are in `tools/`; they write to `work/` (ignored). Code addresses are `SSSS:OOOO` in the
unpacked `SR.EXE` (see `port/RE_GUIDE.md`).

## Picture libraries: `LIB1`, `LIB2` (16 colours), `LIB1C`, `LIB2C` (CGA)

Decoder: `tools/srlib.py Game/LIB2 work/lib2` (one PNG per picture + `_sheet.png`).
Loader: `0f38:6943` opens the libraries (name table DS:4EDC → `?:lib1`, `?:lib1c`, `?:lib2`,
`?:lib2c`) and reads the directories into segment `389b` (48-byte entries in memory);
`0f38:62ba` loads one picture, `0f38:65ae` draws it (unpacker `0e92:0006`, planar blit
`2462:*` / mask `2634:00c1`).

```
u16 count
count × 30-byte records:
  u16 width            multiple of 8
  u16 height
  u16 raw_size         width*height/2 (16 colours) or /4 (CGA)
  u8  masked           1: colour 0 is transparent when drawn
  u8  tokens[16]       run tokens of this picture (all 0 = none)
  u8  0
  u32 offset           from the end of the directory (2 + count*30)
  u16 size             packed size
packed pictures
```

Every packed picture starts with `BC`; the loader reads `size + 1` bytes and checks that the first and
the last (= the next picture's first byte, or the file's final byte) are `BC`. The unpacker runs over
bytes `1 … size-1`:

| Byte | Output |
|---|---|
| `00 n` / `FF n` | n+1 bytes `00` / `FF` |
| `tokens[k]` | `RUN_COUNT[k]` bytes of `RUN_VALUE[k]` (fixed tables DS:4EEE / DS:4EFE) |
| anything else | itself |

`RUN_COUNT = 1 1 2 3 4 2 3 5 6 6 4 7 7 5 8 8`, `RUN_VALUE = 00 FF 00 00 00 FF FF 00 FF 00 FF FF 00 FF FF 00`.

16-colour pictures are stored plane after plane (plane 0 = blue … plane 3 = intensity), each
`height` rows of `width/8` bytes, MSB = leftmost pixel, i.e. ready for EGA planar writes. CGA
pictures are 2 bits per pixel, linear rows.

Four LIB2 pictures (96, 98, 140, 226) unpack to a quarter of `raw_size`: one plane only (to check in
the draw code). LIB1 has 9 pictures (title, California Dreams logo, credits, the "King Street
Rodders" wall, the girlfriend's faces and lips, the juke box, the ending text); LIB2 268 (cars,
dashboard, shops, newspaper, Bob's Drive-In, parts, people, map).

### Colours

The 16-colour modes use BIOS mode 0Dh with the **default EGA palette**: there is no palette or DAC
code in the program (no INT 10h/10xxh, no port 3C0h/3C8h). "VGA" in the mode menu differs from EGA
only in the CRTC split screen used for the dashboard (`21a0:0014`, line compare with the VGA's
doubled scan lines; `README.VGA`).
