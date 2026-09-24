"""Street Rod PC-speaker music and effects: decoder and square-wave renderer (port/spec/sound.md).

Emulates, per 72.8 Hz timer tick, the music sequencer of the timer ISR 0000:1fe2 (sound part) on the
song tables in SR.EXE, the driving-ISR sounds of 0000:238c (engine, siren, squeal), and the busy-loop
effects of 0f38:7122-7998 with an approximate CPU timing model. Writes to <out_dir>:

  songs.txt            event list of every song (duration ticks, PIT divisor, Hz, vibrato record)
  song_<name>.wav      the background tune (one full pass) and the looping "jingles" (3 passes)
  sfx_<name>.wav       the busy-loop effects (timing model constants below)
  siren.wav, engine.csv, engine_sweep.wav

usage: python tools/srsnd.py work/SR_unp.exe work/snd
"""
import os, random, struct, sys, wave

import numpy as np

PIT_HZ = 1193182
TICK_HZ = PIT_HZ / 0x4000          # 72.8227 Hz (0000:1111 programs divisor 4000h)
DS = 0x3E96 * 16                   # DGROUP image offset
RATE = 44100

# Busy-loop timing model (sound.md section 7): delay unit and per-call overheads, microseconds.
DELAY_UNIT_US = 4.47               # 2196:0002 after 0f38:703e calibration (DOSBox reference)
TONE_CALL_US = 20.0                # one 0f38:714c call without its delay (aFldiv, outs, calls)
NOISE_ITER_US = 25.0               # one 0f38:7464 iteration without its delay (5eb6 random)

# Songs: name, far pointer (seg, off), loop-count table, vibrato-index table, 6fbd args or None, caller
SONGS = [
    ('background', (0x2E6B, 0x0000), 0x5858, 0x5882, None, '0000:39c0 start, 1a05(2,10)'),
    ('loop_7613', (0x2F93, 0x0000), 0x587E, 0x58CC, (1, 1, 8, 0x1E, 1), '0f38:7613'),
    ('loop_7661', (0x2F93, 0x002C), 0x587E, 0x58CC, (1, 1, 8, 0x14, 1), '0f38:7661'),
    ('loop_76af', (0x2F93, 0x0050), 0x587E, 0x58CC, 'keep', '0f38:76af (no callers)'),
    ('loop_76da', (0x2F93, 0x007C), 0x587E, 0x58CC, (1, 1, 8, 0x0E, 1), '0f38:76da'),
    ('loop_7573', (0x2F93, 0x00A0), 0x587E, 0x58CC, (1, 0, 8, 0x0A, 1), '0f38:7573'),
    ('loop_75c3', (0x2F93, 0x00B0), 0x587E, 0x58CC, (1, 0, 8, 0x05, 1), '0f38:75c3 (no callers)'),
    ('loop_7548', (0x2F93, 0x00C0), 0x587E, 0x58CC, 'keep', '0f38:7548'),
]


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


class Mem:
    def __init__(self, img):
        self.m = bytearray(img) + bytearray(0x20000)

    def w(self, lin):
        return struct.unpack_from('<H', self.m, lin)[0]

    def setw(self, lin, v):
        struct.pack_into('<H', self.m, lin, v & 0xFFFF)

    def dw(self, off):
        return self.w(DS + (off & 0xFFFF))

    def setdw(self, off, v):
        self.setw(DS + (off & 0xFFFF), v)


class Sequencer:
    """The song player of ISR 0000:1fe2 (lines 20b8-2295) on one song structure (sound.md 4.3)."""

    def __init__(self, mem, seg, off, loop_tbl, vib_tbl):
        self.mem = mem
        self.seg = seg
        self.base = off
        self.tbl14 = loop_tbl      # +14 loop-count table start
        self.loopptr = loop_tbl    # +16
        self.tbl18 = vib_tbl       # +18 vibrato-index table start
        self.vibptr = vib_tbl      # +1a
        self.note = 0              # +0
        self.dur = 0               # +2
        self.rep = 0               # +4
        self.playing = 0           # +6
        self.regate = 0            # +7
        self.loopstart = off       # +c
        self.cur = off             # +10
        self.vib_on = 0            # DS:72EC
        self.rec = 0x5826          # DS:82C4
        self.g6e2 = 0              # DS:06E2
        self.g6c52 = 0             # DS:6C52
        self.divisor = 0x10000
        self.gate = 0              # 47CF
        self.sound_on = 1          # 47CE
        self.events = []
        self.section_plays = 0
        self.wrapped = False

    def rd(self):
        v = self.mem.w(self.seg * 16 + self.cur)
        self.cur = (self.cur + 2) & 0xFFFF
        return v

    def start(self, word_off, delay):          # 0000:1a05
        self.playing = 0
        self.gate = 0
        self.rep = 0
        self.regate = 1
        self.cur = self.loopstart = (self.base + 2 * word_off) & 0xFFFF
        self.dur = (delay + 1) & 0xFFFF
        self.vibptr = self.tbl18
        self.rec = (0x5826 + 10 * self.mem.dw(self.vibptr)) & 0xFFFF
        self.vib_on = 1
        self.playing = 1

    def out(self, div):
        self.divisor = div if div else 0x10000

    def tick(self):
        if self.sound_on == 0 and self.gate:
            self.gate = 0
        if not self.playing:
            return
        self.dur = (self.dur - 1) & 0xFFFF
        if self.dur != 0:
            if self.vib_on and self.note != 0:
                self.g6e2 = (self.g6e2 - 1) & 0xFFFF
                if self.g6e2 == 0:
                    r = self.rec
                    period = s16(self.mem.dw(r + 6))
                    phase = s16(self.mem.dw(r))
                    rem = abs(phase) % abs(period) if period else 0
                    if phase < 0:
                        rem = -rem
                    self.mem.setdw(r, rem + 1)
                    idx = rem
                    row = self.mem.dw(r + 2)
                    wv = s16(self.mem.dw((((row << 4) + 2 * idx) & 0xFFFF) + 0x57F6))
                    self.out((wv * s16(self.g6c52) + self.note) & 0xFFFF)
                    self.g6e2 = self.mem.dw(r + 8)
            return
        self.fetch()

    def fetch(self):
        while True:
            self.dur = self.rd()
            if self.dur != 0:
                break
            w = self.rd()
            if w == 0:                                  # end of song
                self.gate = 0
                self.playing = 0
                self.regate = 1
                self.cur = self.loopstart = self.base
                self.events.append(('end',))
                return
            self.section_plays += 1
            self.vibptr += 2
            self.rep += 1
            if s16(self.mem.dw(self.loopptr)) >= self.rep:
                self.cur = self.loopstart               # repeat the section
            else:
                self.loopptr += 2
                if s16(self.mem.dw(self.loopptr)) < 0:  # whole song again
                    self.cur = (self.base + 4) & 0xFFFF
                    self.loopptr = self.tbl14
                    self.vibptr = self.tbl18
                    self.wrapped = True
                    self.events.append(('wrap',))
                self.loopstart = self.cur
                self.rep = 0
            self.rec = (0x5826 + 10 * self.mem.dw(self.vibptr)) & 0xFFFF
            self.events.append(('section', self.section_plays, (self.rec - 0x5826) // 10))
        self.note = self.rd()
        if self.note == 0:
            self.regate = 1
            self.gate = 0
            self.events.append(('rest', self.dur))
            return
        if self.vib_on:
            self.g6e2 = self.mem.dw(self.rec + 8)
            prod = (self.mem.dw(self.rec + 4) * self.note) & 0xFFFF     # dx cleared before div
            self.g6c52 = prod // 1000
        self.out(self.note)
        if self.sound_on and (self.gate != self.sound_on or self.regate):
            self.gate = 1
            self.regate = 0
        self.events.append(('note', self.dur, self.note, (self.rec - 0x5826) // 10))


class Speaker:
    """1-bit speaker model: level = bit1 AND (gate ? OUT2 : 1), OUT2 = PIT ch2 mode-3 square."""

    def __init__(self):
        self.t = 0.0          # seconds
        self.div = 0x10000
        self.gate = 0
        self.data = 0
        self.phase0 = 0.0
        self.segs = []        # (t_start, div, gate, data, phase0)

    def mark(self):
        self.segs.append((self.t, self.div, self.gate, self.data, self.phase0))

    def set_div(self, d):
        self.div = d if d else 0x10000
        self.phase0 = self.t
        self.mark()

    def set61(self, gate, data):
        if gate and not self.gate:
            self.phase0 = self.t
        self.gate, self.data = gate, data
        self.mark()

    def wait(self, sec):
        self.t += sec

    def render(self, extra=0.05):
        # 16x oversampled 1-bit signal, box-filtered down to RATE (segments shorter than one
        # output sample, e.g. the bit-banged noise, are kept by the oversampling)
        sub = 16
        fs = RATE * sub
        n = int((self.t + extra) * fs)
        hi = np.zeros(n, dtype=np.float32)
        segs = self.segs + [(self.t + extra, 0, 0, 0, 0)]
        for (t0, div, gate, data, ph0), (t1, *_r) in zip(segs, segs[1:]):
            i0, i1 = int(round(t0 * fs)), min(n, int(round(t1 * fs)))
            if not data or i1 <= i0:
                continue
            if not gate:
                hi[i0:i1] = 1.0
                continue
            per = div / PIT_HZ
            tt = np.arange(i0, i1) / fs - ph0
            hi[i0:i1] = ((tt % per) < per / 2).astype(np.float32)
        m = n // sub
        return hi[:m * sub].reshape(m, sub).mean(axis=1)


def write_wav(path, samples):
    # remove DC (1-pole high-pass ~ 20 Hz, done as x - running mean), 16-bit mono
    x = np.asarray(samples, dtype=np.float64)
    a = 0.997
    mean = np.empty_like(x)
    acc = 0.0
    for i in range(0, len(x), 256):             # block-wise running mean (fast enough)
        blk = x[i:i + 256]
        acc = a ** len(blk) * acc + (1 - a ** len(blk)) * blk.mean() if len(blk) else acc
        mean[i:i + 256] = acc
    y = np.clip((x - mean) * 20000, -32767, 32767).astype('<i2')
    with wave.open(path, 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(y.tobytes())


def render_song(mem, name, ptr, loop_tbl, vib_tbl, fbd, passes):
    seq = Sequencer(mem, ptr[0], ptr[1], loop_tbl, vib_tbl)
    if fbd not in (None, 'keep'):
        en, row, period, depth, step = fbd                      # 0f38:6fbd
        mem.setdw(0x5826, 0)
        mem.setdw(0x5828, row)
        mem.setdw(0x582A, depth)
        mem.setdw(0x582C, period)
        mem.setdw(0x582E, step)
    seq.start(2, 10 if name == 'background' else 0)
    sp = Speaker()
    wraps, ticks = 0, 0
    last = None
    while ticks < 20 * 60 * 73:
        seq.tick()
        st = (seq.divisor, seq.gate)
        if st != last:
            sp.set_div(seq.divisor) if last is None or st[0] != last[0] else None
            sp.set61(seq.gate, seq.gate)
            last = st
        sp.wait(1 / TICK_HZ)
        ticks += 1
        if not seq.playing:
            break
        if seq.wrapped:
            seq.wrapped = False
            wraps += 1
            if wraps >= passes:
                break
    return seq, sp, ticks


# ---- busy-loop effects (0f38:7122-7998) ------------------------------------------------------
class Fx:
    def __init__(self, rng):
        self.sp = Speaker()
        self.rng = rng

    def delay(self, n):                      # 2196:0002
        self.sp.wait(n * DELAY_UNIT_US * 1e-6)

    def tone(self, hz, d):                   # 0f38:714c via 70d5 (divisor = 1234DCh / hz)
        self.sp.set_div((0x1234DC // hz) & 0xFFFF if hz else 0)
        self.sp.wait(TONE_CALL_US * 1e-6)
        self.delay(d)

    def gate_on(self):                       # 0f38:7122
        self.sp.set_div(0x1234DC // 25000)
        self.sp.set61(1, 1)

    def gate_off(self):                      # 0f38:713d
        self.sp.set61(0, 0)

    def rnd(self, n):                        # 0f38:5eb6 (stand-in)
        return self.rng.randrange(n) if n > 0 else 0

    def burst(self, lo, hi, d, cnt):         # 0f38:7169
        for _ in range(cnt):
            self.tone(self.rnd(hi - lo) + lo, d)

    def sweep_down(self, start, end, step, d):   # 0f38:71b2
        f = start
        while f >= end:
            self.tone(f, d)
            f -= step

    def warble(self, center, dev, step, d, cnt):  # 0f38:71e4
        for _ in range(cnt):
            i, f = 0, center - dev
            while i < 2 * dev:
                self.tone(f, d)
                f += step
                i += step
            i, f = 0, center + dev
            while i < 2 * dev:
                self.tone(f, d)
                f -= step
                i += step

    def noise(self, rng_range, ormask, cnt):    # 0f38:7464
        self.gate_off()
        data = 0
        for _ in range(cnt):
            data ^= 1
            self.sp.set61(0, data)
            self.sp.wait(NOISE_ITER_US * 1e-6)
            self.delay(self.rnd(rng_range) | ormask)

    def noise_down(self, a, b, sub, step, cnt):  # 0f38:74b0
        if b < a:
            x = a
            while True:
                self.noise(a, a - sub, cnt)
                a -= step
                x -= step
                if not b < x:
                    break

    def noise_up(self, a, b, sub, step, cnt):    # 0f38:74fc
        if a < b:
            x = a
            while True:
                self.noise(a, a - sub, cnt)
                a += step
                x += step
                if not x < b:
                    break


def fx_77ba(f):
    f.gate_on(); f.warble(3000, 10, 10, 60, 9); f.gate_off()


def fx_77e1(f):
    f.gate_on(); f.warble(f.rnd(100) + 6200, 40, 10, 1, 10); f.gate_off()


def fx_7817(f):
    f.gate_on(); f.warble(5200, 40, 10, 1, 10); f.gate_off()


def fx_7841(f):
    f.gate_on(); f.noise(200, 10, 60); f.gate_off()


def fx_7863(f):
    for _ in range(3):
        f.gate_on()
        f.burst(3000, 8000, 60, 10)
        f.noise_down(200, f.rnd(30) + 18, 15, 5, 2)
        f.noise(f.rnd(300) + 40, 2, 30)
        f.noise_up(16, f.rnd(100) + 50, 15, 5, 4)
    f.gate_off()


def fx_790f(f):
    f.gate_on(); f.burst(2000, 8000, 100, 10); f.noise(120, 2, 30)
    f.gate_on(); f.burst(4000, 9000, 100, 10); f.noise(220, 2, 20)
    f.gate_off()


def fx_7973(f):
    f.gate_on(); f.sweep_down(130, 90, 4, 1000); f.gate_off()


EFFECTS = [('77ba_click', fx_77ba), ('77e1_chirp_hi', fx_77e1), ('7817_chirp_lo', fx_7817),
           ('7841_noise', fx_7841), ('7863_crash', fx_7863), ('790f_hit', fx_790f),
           ('7973_thud', fx_7973)]


def engine_divisor(rpm):                     # 0000:2404
    sq = (rpm * rpm) & 0xFFFFFFFF
    if sq & 0x80000000:
        sq -= 1 << 32
    t = (sq >> 10) & 0xFFFF
    sh = ((t & 0xFF) + 2) & 0xFF
    sh &= 0x1F                               # 286+ shift-count masking (DOSBox)
    x = 0 if sh >= 16 else t >> sh
    x >>= 3
    t = (t + x) & 0xFFFF
    bonus = 0x2710 if rpm == 0x1644 else 0
    return (0xC350 - ((t + bonus) & 0xFFFF)) & 0xFFFF


def main():
    exe, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    d = open(exe, 'rb').read()
    img = d[struct.unpack_from('<H', d, 8)[0] * 16:]
    lines = []
    for name, ptr, lt, vt, fbd, caller in SONGS:
        mem = Mem(img)
        seq, sp, ticks = render_song(mem, name, ptr, lt, vt, fbd, 1 if name == 'background' else 3)
        lines.append('== %s  %04x:%04x  loops DS:%04x  vib DS:%04x  6fbd %s  (%s)  %d ticks = %.2f s'
                     % (name, ptr[0], ptr[1], lt, vt, fbd, caller, ticks, ticks / TICK_HZ))
        for e in seq.events:
            if e[0] == 'note':
                lines.append('  note %3d ticks  div %5d  %8.2f Hz  vib rec %d' % (e[1], e[2], PIT_HZ / e[2], e[3]))
            elif e[0] == 'rest':
                lines.append('  rest %3d ticks' % e[1])
            else:
                lines.append('  -- %s' % (e,))
        write_wav(os.path.join(out, 'song_%s.wav' % name), sp.render())
    open(os.path.join(out, 'songs.txt'), 'w').write('\n'.join(lines) + '\n')

    rng = random.Random(1)
    for name, fn in EFFECTS:
        f = Fx(rng)
        fn(f)
        write_wav(os.path.join(out, 'sfx_%s.wav' % name), f.sp.render())
        print('sfx %-14s %.1f ms (model)' % (name, f.sp.t * 1000))

    # siren (0000:2490): two tones on 2 of every 4 ticks, 6E4 counter
    sp = Speaker(); c = 0; sp.set61(1, 1)
    for tick in range(4 * 73):
        ph = tick % 4
        if ph in (0, 2):
            old = c; c += 1
            sp.set_div(0x54B if old < 10 else 0x712)
            if c > 20:
                c = 0
        elif ph == 1:
            sp.set_div(engine_divisor(0x5DC))
        sp.wait(1 / TICK_HZ)
    write_wav(os.path.join(out, 'siren.wav'), sp.render())

    with open(os.path.join(out, 'engine.csv'), 'w') as fcsv:
        fcsv.write('rpm,divisor,hz\n')
        for rpm in range(0, 0x1645, 25):
            dv = engine_divisor(rpm)
            fcsv.write('%d,%d,%.2f\n' % (rpm, dv, PIT_HZ / (dv or 0x10000)))
        dv = engine_divisor(0x1644)
        fcsv.write('%d,%d,%.2f\n' % (0x1644, dv, PIT_HZ / dv))
    sp = Speaker(); sp.set61(1, 1)
    for i in range(4 * 73 * 4):                 # 4 s sweep 1500 -> 5700 rpm, engine tick 1 of 4
        if i % 4 == 1:
            rpm = 0x5DC + (0x1644 - 0x5DC) * i // (4 * 73 * 4 - 1)
            sp.set_div(engine_divisor(rpm))
        sp.wait(1 / TICK_HZ)
    write_wav(os.path.join(out, 'engine_sweep.wav'), sp.render())
    print('written to', out)


if __name__ == '__main__':
    main()
