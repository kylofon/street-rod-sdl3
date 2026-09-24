"""Street Rod: print the garage data tables (cars, parts, prices, compatibility, performance).

  python tools/srtables.py [EXE] [HOT_DATA]      (defaults: work/SR_unp.exe Game/HOT_DATA)

Builds DGROUP as the game sees it after start-up: the initialised data of the unpacked EXE
(DGROUP = segment 3E96) with the 13 HOT_DATA blocks copied over it (pointer table DS:4EA4,
size table DS:4EBE, loaded by 0f38:6b79).  See port/spec/garage.md.
"""
import struct
import sys

DGROUP = 0x3E96 * 16
TEXT_BASE = 0x2C02          # message / ad texts: DS:2C02 + id (byte before = line count / box style)
UI_TEXT_BASE = 0x239E       # hot-spot button labels: DS:239E + id


def load(exe, hot):
    d = open(exe, 'rb').read()
    d = d[struct.unpack_from('<H', d, 8)[0] * 16:]
    ds = bytearray(0x10000)
    init = d[DGROUP:]
    ds[:len(init)] = init
    h = open(hot, 'rb').read()
    ptrs = struct.unpack_from('<13H', ds, 0x4EA4)
    sizes = struct.unpack_from('<13H', ds, 0x4EBE)
    off = 0
    for p, n in zip(ptrs, sizes):
        ds[p:p + n] = h[off:off + n]
        off += n
    return ds, list(zip(ptrs, sizes))


def main():
    exe = sys.argv[1] if len(sys.argv) > 1 else 'work/SR_unp.exe'
    hot = sys.argv[2] if len(sys.argv) > 2 else 'Game/HOT_DATA'
    ds, blocks = load(exe, hot)
    w = lambda o: struct.unpack_from('<h', ds, o)[0]
    uw = lambda o: struct.unpack_from('<H', ds, o)[0]
    sb = lambda o: struct.unpack_from('<b', ds, o)[0]

    def s(o):
        e = ds.index(b'\0', o)
        return ds[o:e].decode('latin1')

    def ad(tid):
        """An ad / multi-line text: consecutive strings, count = byte before the first."""
        o = TEXT_BASE + tid
        n = max(ds[o - 1], 1)
        lines = [s(o)]
        o += len(lines[0]) + 1
        while True:                      # '+' continues the previous line (0000:09cb)
            t = s(o)
            if t.startswith('+'):
                lines[-1] += ' ' + t[1:].strip('`').strip()
            elif len(lines) < n and t and t[0] >= ' ':
                lines.append(t)
            else:
                break
            o += len(t) + 1
        return ' / '.join(lines)

    CAT = ['engine', 'trans', 'carb', 'manifold', 'tires']
    MAKE = {1: 'GM', 2: 'Ford', 4: 'Chrysler', 7: 'all'}

    print('HOT_DATA blocks (DS address, size):', ', '.join('%04X:%d' % b for b in blocks))
    print()
    print('== Part catalogue DS:4806, 8-byte records {i16 price, u8 grade, u8 make_mask, u8 category, u8 0, i16 text} ==')
    print(' idx  price grade make      category  text')
    for i in range(43):
        o = 0x4806 + i * 8
        print('%4d %6d %5d %-9s %-9s %s' % (i, w(o), ds[o + 2], MAKE.get(ds[o + 3], ds[o + 3]),
                                          CAT[ds[o + 4]], ad(w(o + 6))))
    print()
    print('Initial wear by category DS:4966:', [w(0x4966 + 2 * k) for k in range(5)],
          '(-128 = part does not wear)')
    print('Carburettors per manifold grade DS:5126:', [w(0x5126 + 2 * k) for k in range(5)])
    print('Manifold fits engine DS:4978[engine_grade*5 + manifold_grade]:')
    for e in range(3):
        print('   engine grade %d:' % e, list(ds[0x4978 + e * 5:0x4978 + e * 5 + 5]))
    print('Carb fits manifold DS:4988[manifold_grade*3 + carb_grade]:')
    for m in range(5):
        print('   manifold grade %d:' % m, list(ds[0x4988 + m * 3:0x4988 + m * 3 + 3]))
    print()
    print('== Car models DS:7D86 (HOT_DATA block 1), 10-byte records ==')
    print('{i16 price, u8 class, i8 next_for_sale, u16 spec, i16 ad_text, i16 picture}')
    print('spec: bits0-3 make mask, 4-5 tires, 6-7 trans grade, 8-10 engine grade, 11-13 manifold grade, 14-15 carb grade')
    print(' m  price cls next make  eng trn man carb tire  pic   roof  scoop rbump fbump b0  ad')
    for m in range(26):
        o = 0x7D86 + m * 10
        spec = uw(o + 4)
        p = 0x8DF0 + m * 10
        print('%2d %6d %3d %4d %-8s %d  %d   %d   %d    %d   %4d  %4d  %4d  %4d  %4d  %2d  %s' % (
            m, w(o), ds[o + 2], sb(o + 3), MAKE.get(spec & 15, spec & 15), (spec >> 8) & 7,
            (spec >> 6) & 3, (spec >> 11) & 7, spec >> 14, (spec >> 4) & 3, w(o + 8) - 1000,
            w(p + 2) - 1000 if w(p + 2) else -1, w(p + 4) - 1000 if w(p + 4) else -1,
            w(p + 6) - 1000 if w(p + 6) else -1, w(p + 8) - 1000 if w(p + 8) else -1,
            ds[p], ad(w(o + 6))))
    print('(pictures are LIB2 indices = id - 1000; -1 = none; DS:8DF0 block 2: b0 = byte +0 (race), '
          '+2 chopped roof, +4 hood scoop, +6 rear bumper stripped, +8 front bumper stripped)')
    head = w(0x7D16)
    order = []
    while head >= 0 and len(order) < 40:
        order.append(head)
        head = sb(0x7D86 + head * 10 + 3)
    print('Used-car ads order (head DS:7D16, next = byte +3):', order)
    print()
    print('== Stock top speed (0000:e218 with the stock parts, wear 0, ignition 0) ==')
    k556e = [w(0x556E + 2 * i) for i in range(9)]
    k552e = [w(0x552E + 2 * i) for i in range(15)]
    k554c = [w(0x554C + 2 * i) for i in range(3)]
    k5552 = [w(0x5552 + 2 * i) for i in range(4)]
    for m in range(25):
        o = 0x7D86 + m * 10
        spec = uw(o + 4)
        p = 0x8DF0 + m * 10
        f = 0.09 * (w(p + 2) == 0) + 0.05 * (w(p + 8) == 0) + 0.01 * (w(p + 6) == 0)
        f += k556e[ds[o + 2]] * 0.1 / 88.0
        f += k552e[(spec >> 14) * 5 + ((spec >> 11) & 7)] * 0.15 / 100.0
        f += k554c[(spec >> 8) & 7] * 0.25 / 100.0
        f += k5552[(spec >> 6) & 3] * 0.15 / 100.0
        f += (1.0 - 0.0) * 0.2
        f = f * (0.8 if ((spec >> 6) & 3) == 0 else 1.0) * 0.3 + 0.7
        v = min(f * 130.0, 130.0)
        print('  model %2d: %3d mph' % (m, int(v)))
    print('tables: class DS:556E', k556e, ' carb x manifold DS:552E', k552e,
          ' engine DS:554C', k554c, ' trans DS:5552', k5552)
    print()
    print('== Paint colours DS:50C2 (EGA palette values for registers 6, 7) ==')
    print('  ', [(ds[0x50C2 + 2 * i], ds[0x50C3 + 2 * i]) for i in range(6)])
    print('== Tyre wheel pictures DS:5030 + grade*8 (4 frames) ==')
    for g in range(3):
        print('   grade %d:' % g, [w(0x5030 + g * 8 + 2 * k) - 1000 for k in range(4)])
    print('== Stickers DS:7844 + n*6 {i16 picture, i16 dx, i16 dy} (n = 1..8) ==')
    for n in range(1, 9):
        o = 0x7844 + n * 6
        print('   %d: pic %d  dx %d dy %d' % (n, w(o) - 1000, w(o + 2), w(o + 4)))
    print('== Engine bay pictures DS:4998[slot*5 + grade] ==')
    for sl, name in enumerate(['engine', 'manifold', 'carb 1', 'carb 2', 'carb 3']):
        print('   %-8s' % name, [(w(0x4998 + sl * 10 + 2 * g) - 1000) if w(0x4998 + sl * 10 + 2 * g) else -1
                                   for g in range(5)])
    print('   bolt pictures DS:49CA[tightness]', [w(0x49CA + 2 * k) - 1000 if w(0x49CA + 2 * k) else -1 for k in range(4)],
          ' transmission pictures DS:49D2[grade]', [w(0x49D2 + 2 * k) - 1000 for k in range(4)])
    print('   bolts per layer DS:5130', [w(0x5130 + 2 * k) for k in range(5)],
          ' part category per layer DS:513C', [w(0x513C + 2 * k) for k in range(5)])
    print()
    print('== Opponents DS:7FF8 (HOT_DATA block 0), 18-byte records (raw) ==')
    for i in range(22):
        o = 0x7FF8 + i * 18
        nm = w(o + 6)
        print('  %2d %s  name=%s' % (i, ds[o:o + 18].hex(' '), s(TEXT_BASE + nm) if 0 < nm < 0x800 else nm))
    print()
    print('== Per-model layout, HOT_DATA block 3 DS:7682 (12 bytes) / block 5 DS:818C (6 bytes) / block 4 DS:70E4 (20 bytes) ==')
    for m in range(26):
        print('  %2d  %s | %s | %s' % (m, ds[0x7682 + m * 12:0x7682 + m * 12 + 12].hex(' '),
                                        ds[0x818C + m * 6:0x818C + m * 6 + 6].hex(' '),
                                        ds[0x70E4 + m * 20:0x70E4 + m * 20 + 20].hex(' ')))


if __name__ == '__main__':
    main()
