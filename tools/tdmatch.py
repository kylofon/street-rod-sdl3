"""Match Street Rod functions against the named functions of an earlier port (TD1, TD2, TD3).

Street Rod looks like the TD1/TD2 engine; both are Microsoft C programs, so at least the C runtime,
and any shared assembly library, should match. Every function is disassembled with its immediates and displacements masked; each
TD3 function is scored against each TD2 function by the share of its 6-instruction windows that also
occur in the TD2 function. The best TD2 match above the threshold becomes a candidate name (to be
confirmed in the specs).

usage: tdmatch.py REPO EXE INDEX TAG [SR index base, default port/sr]
       e.g. tdmatch.py ../TestDrive2 work/TD2EGA_unp.exe port/td2ega_functions.json td2
       REPO/port/symbols.csv names the functions. Writes <base>_<TAG>_matches.csv.
"""
import csv, json, os, re, struct, sys
from collections import defaultdict
from capstone import Cs, CS_ARCH_X86, CS_MODE_16

N = 6
THRESHOLD = 0.5
td2_dir, td2_exe, td2_index, TAG = sys.argv[1:5]
base = sys.argv[5] if len(sys.argv) > 5 else 'port/sr'
md = Cs(CS_ARCH_X86, CS_MODE_16)
NUM = re.compile(r'0x[0-9a-f]+|\b\d+\b')


def image(path):
    d = open(path, 'rb').read()
    return d[struct.unpack_from('<H', d, 8)[0] * 16:]


def grams(code):
    ops = [mn + ' ' + NUM.sub('#', op) for _, _, mn, op in md.disasm_lite(code, 0)]
    return ops, {tuple(ops[k:k + N]) for k in range(len(ops) - N + 1)}


td2_img = image(os.path.join(td2_dir, td2_exe))
names = {}
for r in csv.DictReader(open(os.path.join(td2_dir, 'port/symbols.csv'))):
    if r['kind'] == 'func':
        names[r['address']] = r['name']
td2 = []
index = defaultdict(set)            # gram -> TD2 function ids
idx = json.load(open(os.path.join(td2_dir, td2_index)))
for f in (idx['functions'] if isinstance(idx, dict) else idx):
    if 'image' in f:
        s, size = int(f['image'], 16), f['size']
    else:                                       # TD1's index: one code segment, start/end offsets
        s = int(f['start'], 16)
        size = int(f['end'], 16) - s
        f['start'] = '0x' + f['start']          # as TD1's symbols.csv writes addresses
    ops, g = grams(td2_img[s:s + size])
    if len(ops) < N + 2:
        continue
    fid = len(td2)
    td2.append((f['start'], names.get(f['start'], ''), len(ops), g))
    for x in g:
        index[x].add(fid)

j = json.load(open(base + '_functions.json'))
td3_img = image(j['exe'])
rows = []
for f in j['functions']:
    s = int(f['image'], 16)
    ops, g = grams(td3_img[s:s + f['size']])
    if len(ops) < N + 2 or not g:
        continue
    votes = defaultdict(int)
    for x in g:
        for fid in index.get(x, ()):
            votes[fid] += 1
    if not votes:
        continue
    fid, v = max(votes.items(), key=lambda kv: kv[1] / max(len(g), len(td2[kv[0]][3])))
    score = v / max(len(g), len(td2[fid][3]))
    if score >= THRESHOLD:
        rows.append((f['start'], f['image'], td2[fid][1], td2[fid][0], '%.2f' % score, len(ops), td2[fid][2]))

with open(base + '_%s_matches.csv' % TAG, 'w', newline='') as o:
    w = csv.writer(o)
    w.writerow(['sr_start', 'sr_image', TAG + '_name', TAG + '_address', 'score', 'sr_insns', TAG + '_insns'])
    w.writerows(rows)
print('%d of %d SR functions matched a %s function (score >= %.2f), %d of them named'
      % (len(rows), len(j['functions']), TAG, THRESHOLD, sum(1 for r in rows if r[2])))
for r in rows:
    print('  %s  %-28s %s  %s' % (r[0], r[2] or '-', r[3], r[4]))
