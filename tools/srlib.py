"""Street Rod picture libraries (LIB1, LIB2 = 16 colours; LIB1C, LIB2C = CGA 4 colours).

File: u16 count, count x 30-byte directory records, then the packed pictures.
Record: u16 width, u16 height, u16 raw_size, u8 masked, u8 tokens[16], u8 0, u32 offset, u16 size.
  raw_size = width*height/2 (16 colours) or /4 (CGA).
  offset is relative to the end of the directory; the packed data is `size` bytes and the byte after
  it (the next picture's first byte, or the file's last byte) is read too: every picture starts
  with 0xBC and the loader checks that data[0] and data[size] are both 0xBC.
  masked (1): colour 0 is transparent when the picture is drawn (2634:00c1 builds the mask).
Packing (unpacker 0e92:0006, input = data[1:size]):
  00 n / FF n    -> n+1 bytes of 00 / FF
  tokens[k] (k=0..15) -> RUN_COUNT[k] bytes of RUN_VALUE[k]  (fixed tables DS:4EEE / DS:4EFE)
  other byte     -> itself
16-colour pictures are planar: 4 planes one after the other (plane 0 = blue bit ... 3 = intensity),
each height rows of width/8 bytes, MSB = leftmost pixel. CGA pictures: 2 bits per pixel, linear.

usage: srlib.py Game/LIB2 work/lib2 [--palette ega|vga]   -> PNG per picture + contact sheet
"""
import os, struct, sys, zlib

RUN_COUNT = [1, 1, 2, 3, 4, 2, 3, 5, 6, 6, 4, 7, 7, 5, 8, 8]
RUN_VALUE = [0x00, 0xFF, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0x00]

EGA16 = [(0, 0, 0), (0, 0, 170), (0, 170, 0), (0, 170, 170), (170, 0, 0), (170, 0, 170), (170, 85, 0),
         (170, 170, 170), (85, 85, 85), (85, 85, 255), (85, 255, 85), (85, 255, 255), (255, 85, 85),
         (255, 85, 255), (255, 255, 85), (255, 255, 255)]
CGA4 = [(0, 0, 0), (85, 255, 255), (255, 85, 255), (255, 255, 255)]


def read_lib(path):
    d = open(path, 'rb').read()
    n = struct.unpack_from('<H', d, 0)[0]
    base = 2 + n * 30
    out = []
    for i in range(n):
        p = 2 + i * 30
        w, h, raw, masked = struct.unpack_from('<HHHB', d, p)
        tokens = d[p + 7:p + 23]
        off, size = struct.unpack_from('<IH', d, p + 24)
        out.append(dict(index=i, w=w, h=h, raw=raw, masked=masked, tokens=tokens,
                        data=d[base + off:base + off + size + 1]))
    return out


def unpack(data, tokens, raw):
    assert data[0] == 0xBC, 'missing BC marker'
    tok = {t: k for k, t in enumerate(tokens) if t}   # zero entries = unused (LIB1 #0)
    out = bytearray()
    src = data[1:-1]
    i = 0
    while i < len(src):
        b = src[i]; i += 1
        if b in (0x00, 0xFF):
            out += bytes([b]) * (src[i] + 1); i += 1
        elif b in tok:
            k = tok[b]
            out += bytes([RUN_VALUE[k]]) * RUN_COUNT[k]
        else:
            out.append(b)
    if len(out) < raw:
        out += bytes(raw - len(out))
    return bytes(out[:raw])


def to_indices(pic, pix, cga=False):
    w, h = pic['w'], pic['h']
    idx = bytearray(w * h)
    if cga:
        bpr = w // 4
        for y in range(h):
            for x in range(w):
                idx[y * w + x] = (pix[y * bpr + x // 4] >> (6 - 2 * (x % 4))) & 3
        return idx
    bpr = w // 8
    plane = bpr * h
    for pl in range(4):
        for y in range(h):
            row = pl * plane + y * bpr
            for x in range(w):
                if pix[row + x // 8] & (0x80 >> (x % 8)):
                    idx[y * w + x] |= 1 << pl
    return idx


def png(path, w, h, rgb):
    rows = b''.join(b'\0' + bytes(rgb[y * w * 3:(y + 1) * w * 3]) for y in range(h))
    chunk = lambda t, c: struct.pack('>I', len(c)) + t + c + struct.pack('>I', zlib.crc32(t + c))
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
                           + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))


def main():
    src, out = sys.argv[1], sys.argv[2]
    pal_name = sys.argv[sys.argv.index('--palette') + 1] if '--palette' in sys.argv else 'ega'
    os.makedirs(out, exist_ok=True)
    pics = read_lib(src)
    cga = src.upper().endswith('C')
    pal = CGA4 if cga else EGA16
    if pal_name == 'vga' and not cga:
        import srpal
        pal = srpal.VGA
    sheet = []
    for p in pics:
        pix = unpack(p['data'], p['tokens'], p['raw'])
        idx = to_indices(p, pix, cga)
        rgb = bytearray()
        for c in idx:
            rgb += bytes(pal[c])
        png(os.path.join(out, '%03d_%dx%d%s.png' % (p['index'], p['w'], p['h'], 'm' if p['masked'] else '')),
            p['w'], p['h'], rgb)
        sheet.append((p, rgb))
    # contact sheet: pictures packed in rows 640 wide
    W, x, y, rowh, place = 640, 0, 0, 0, []
    for p, rgb in sheet:
        if x + p['w'] > W:
            x, y, rowh = 0, y + rowh + 4, 0
        place.append((x, y, p, rgb))
        x += p['w'] + 4
        rowh = max(rowh, p['h'])
    H = y + rowh
    canvas = bytearray([40, 40, 60] * W * H)
    for x0, y0, p, rgb in place:
        for yy in range(p['h']):
            o = ((y0 + yy) * W + x0) * 3
            canvas[o:o + p['w'] * 3] = rgb[yy * p['w'] * 3:(yy + 1) * p['w'] * 3]
    png(os.path.join(out, '_sheet.png'), W, H, canvas)
    print('%d pictures -> %s' % (len(pics), out))


if __name__ == '__main__':
    main()
