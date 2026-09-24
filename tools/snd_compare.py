"""Compare the SDL port's sound sequencer with the Python model tools/srsnd.py (port/spec/sound.md).

usage: python tools/snd_compare.py work/SR_unp.exe PORT_OUT_DIR
  PORT_OUT_DIR = output of srport/build*/srsnd-dump GAME_DIR PORT_OUT_DIR

Checks, for the background tune (one pass) and every loop sound (3 passes):
  * the event list (note / rest lines of songs.txt) and the tick count,
  * per tick: the last vibrato divisor (DS:6C54) and the speaker-gate mirror (DS:47CF),
and that engine.csv (RPM -> divisor, 0000:238c @2404) is identical.
"""
import os, struct, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import srsnd  # noqa: E402


class TracingSeq(srsnd.Sequencer):
    """srsnd.Sequencer that also keeps DS:6C54 (the last vibrato divisor)."""

    def __init__(self, *a):
        super().__init__(*a)
        self.vib_last = 0
        self.in_fetch = False

    def out(self, div):
        if not self.in_fetch:
            self.vib_last = div & 0xFFFF
        super().out(div)

    def fetch(self):
        self.in_fetch = True
        try:
            super().fetch()
        finally:
            self.in_fetch = False


def model_trace(img, name, ptr, lt, vt, fbd, passes):
    mem = srsnd.Mem(img)
    seq = TracingSeq(mem, ptr[0], ptr[1], lt, vt)
    if fbd not in (None, 'keep'):
        en, row, period, depth, step = fbd
        for off, v in ((0x5826, 0), (0x5828, row), (0x582A, depth), (0x582C, period), (0x582E, step)):
            mem.setdw(off, v)
    seq.start(2, 10 if name == 'background' else 0)
    rows, wraps = [], 0
    while len(rows) < 20 * 60 * 73:
        seq.tick()
        rows.append('%d %d' % (seq.vib_last, seq.gate))
        if not seq.playing:
            break
        if seq.wrapped:
            seq.wrapped = False
            wraps += 1
            if wraps >= passes:
                break
    return rows


def events(path):
    songs, cur = {}, None
    for line in open(path):
        if line.startswith('=='):
            cur = line.split()[1]
            songs[cur] = []
        elif line.startswith('  note') or line.startswith('  rest'):
            songs[cur].append(' '.join(line.split()))
    return songs


def main():
    exe, port = sys.argv[1], sys.argv[2]
    d = open(exe, 'rb').read()
    img = d[struct.unpack_from('<H', d, 8)[0] * 16:]
    ok = True
    with tempfile.TemporaryDirectory() as tmp:
        subprocess.run([sys.executable, srsnd.__file__, exe, tmp], check=True, stdout=subprocess.DEVNULL)
        ev_model, ev_port = events(os.path.join(tmp, 'songs.txt')), events(os.path.join(port, 'songs.txt'))
        same_engine = open(os.path.join(tmp, 'engine.csv')).read() == open(os.path.join(port, 'engine.csv')).read()
    print('engine.csv', 'identical' if same_engine else 'DIFFERENT')
    ok &= same_engine
    for name, ptr, lt, vt, fbd, _caller in srsnd.SONGS:
        passes = 1 if name == 'background' else 3
        model = model_trace(img, name, ptr, lt, vt, fbd, passes)
        got = [' '.join(l.split()) for l in open(os.path.join(port, 'trace_%s.txt' % name))]
        same_ev = ev_model.get(name) == ev_port.get(name)
        same_tr = model == got
        print('%-11s events %5d %s  ticks %5d/%5d %s' % (
            name, len(ev_model.get(name, [])), 'identical' if same_ev else 'DIFFERENT',
            len(model), len(got), 'identical' if same_tr else 'DIFFERENT'))
        if not same_tr:
            for i, (a, b) in enumerate(zip(model, got)):
                if a != b:
                    print('   first difference at tick %d: model %s, port %s' % (i + 1, a, b))
                    break
        ok &= same_ev and same_tr
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
