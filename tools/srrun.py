"""Run srport headless with a scripted input list and turn the snapshots into PNGs + a contact sheet.

usage: srrun.py OUT_DIR SECONDS "SR_KEYS script" [game switches ...]
       [--exe srport/build-check/srport.exe] [--game Game] [--ms 500] [--fresh-game]
  OUT_DIR       snapshots (snapNNNN.bmp/.png), _sheet.png (thumbnails, 6 per row, numbered order)
  SECONDS       run time, then the process is killed (the game has no exit on its own)
  script        the SR_KEYS list (srport/src/host.h): "1:39,4:c160.120,6:e048p,20:e048r" ...
  --fresh-game  run on a copy of the game folder in OUT_DIR/Game (saves and hall_dat stay out of Game/)
Developer aid for the port; the game is never shown on screen (SDL dummy drivers).
"""
import glob, os, shutil, struct, subprocess, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import srlib  # noqa: E402  (png writer)


def bmp_rgb(path):
    d = open(path, 'rb').read()
    off = struct.unpack_from('<I', d, 10)[0]
    w, h = struct.unpack_from('<ii', d, 18)
    bpp = struct.unpack_from('<H', d, 28)[0] // 8
    stride = (w * bpp + 3) // 4 * 4
    rgb = bytearray()
    for y in range(abs(h)):
        row = d[off + (abs(h) - 1 - y if h > 0 else y) * stride:][:stride]
        for x in range(w):
            rgb += bytes((row[x * bpp + 2], row[x * bpp + 1], row[x * bpp]))
    return w, abs(h), rgb


def main():
    args = sys.argv[1:]
    opts = {'--exe': 'srport/build-check/srport.exe', '--game': 'Game', '--ms': '500'}
    fresh = '--fresh-game' in args
    if fresh:
        args.remove('--fresh-game')
    for k in list(opts):
        if k in args:
            i = args.index(k)
            opts[k] = args[i + 1]
            del args[i:i + 2]
    out, seconds, script, switches = args[0], float(args[1]), args[2], args[3:]
    os.makedirs(out, exist_ok=True)
    for f in glob.glob(os.path.join(out, 'snap*.*')):
        os.remove(f)
    game = opts['--game']
    if fresh:
        game = os.path.join(out, 'Game')
        if os.path.isdir(game):
            shutil.rmtree(game)
        shutil.copytree(opts['--game'], game)
    env = dict(os.environ, SDL_VIDEO_DRIVER='dummy', SDL_AUDIO_DRIVER='dummy', SR_SNAPSHOT_DIR=out,
               SR_SNAPSHOT_MS=opts['--ms'], SR_KEYS=script)
    try:
        r = subprocess.run([os.path.abspath(opts['--exe']), '--game-dir', game] + switches, env=env, timeout=seconds,
                           capture_output=True, text=True)
        print('exit code', r.returncode)
        sys.stdout.write(r.stdout + r.stderr)
    except subprocess.TimeoutExpired as e:
        print('stopped after %g s' % seconds)
        for s in (e.stdout, e.stderr):
            if s:
                sys.stdout.write(s if isinstance(s, str) else s.decode('latin-1'))
    snaps = sorted(glob.glob(os.path.join(out, 'snap*.bmp')))
    thumbs = []
    for f in snaps:
        w, h, rgb = bmp_rgb(f)
        srlib.png(f[:-4] + '.png', w, h, rgb)
        thumbs.append((w, h, rgb))
    if not thumbs:
        print('no snapshots')
        return
    cols, tw, th = 6, 160, 100
    rows = (len(thumbs) + cols - 1) // cols
    W, H = cols * (tw + 2), rows * (th + 2)
    sheet = bytearray([30] * W * H * 3)
    for i, (w, h, rgb) in enumerate(thumbs):
        ox, oy = (i % cols) * (tw + 2), (i // cols) * (th + 2)
        for y in range(th):
            for x in range(tw):
                s = ((y * h // th) * w + x * w // tw) * 3
                p = ((oy + y) * W + ox + x) * 3
                sheet[p:p + 3] = rgb[s:s + 3]
    srlib.png(os.path.join(out, '_sheet.png'), W, H, sheet)
    print('%d snapshots, sheet %s' % (len(thumbs), os.path.join(out, '_sheet.png')))


if __name__ == '__main__':
    main()
