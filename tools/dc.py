"""Look up Ghidra-decompiled functions by image offset (as used in sr_dis) or name.
usage: python tools/dc.py 0x3388        -> function containing image offset 0x3388
       python tools/dc.py FUN_1338_0008 -> by name"""
import re, sys, bisect

SRC = 'C:/Users/inact/SR2/out/sr_decompiled.c'

def load():
    text = open(SRC, encoding='utf-8', errors='replace').read()
    parts = re.split(r'^// ==== (\S+) @ ([0-9a-f]{4}):([0-9a-f]{4})\n', text, flags=re.M)
    funcs = []
    for i in range(1, len(parts) - 3, 4):
        name, seg, off, body = parts[i], int(parts[i + 1], 16), int(parts[i + 2], 16), parts[i + 3]
        funcs.append((seg * 16 + off - 0x10000, name, seg, off, body))
    funcs.sort()
    return funcs

def find(funcs, key):
    if key.startswith('0x') or re.fullmatch(r'[0-9a-f]+', key):
        lin = int(key, 16)
        starts = [f[0] for f in funcs]
        i = bisect.bisect_right(starts, lin) - 1
        return funcs[i] if i >= 0 else None
    for f in funcs:
        if f[1] == key: return f
    return None

if __name__ == '__main__':
    funcs = load()
    f = find(funcs, sys.argv[1])
    if f:
        print(f'// {f[1]}  image 0x{f[0]:05x}  ({f[2]:04x}:{f[3]:04x})')
        print(f[4])
