"""Dump events of an AdLib Visual Composer .MUS file."""
import sys
d = open(sys.argv[1], 'rb').read()
limit = int(sys.argv[2]) if len(sys.argv) > 2 else 120
data = d[70:70 + int.from_bytes(d[0x2a:0x2e], 'little')]
pos, tick, status, n = 0, 0, 0, 0
names = {0x80: 'off', 0x90: 'on', 0xA0: 'vol', 0xB0: 'ctl', 0xC0: 'prog', 0xD0: 'press', 0xE0: 'bend'}
while pos < len(data) and n < limit:
    while data[pos] == 0xF8:
        tick += 240; pos += 1
    tick += data[pos]; pos += 1
    b = data[pos]
    if b < 0x80:
        b = status
    else:
        pos += 1
        if b < 0xF0: status = b
    hi = b & 0xF0
    if hi in (0x80, 0x90, 0xB0, 0xE0):
        args = list(data[pos:pos + 2]); pos += 2
    elif hi in (0xA0, 0xC0, 0xD0):
        args = list(data[pos:pos + 1]); pos += 1
    elif b == 0xF0:
        e = data.index(0xF7, pos); args = list(data[pos:e]); pos = e + 1
    else:
        args = []
    print(f'{tick:6d}  ch{b & 15:2d} {names.get(hi, hex(b)):5s} {args}')
    n += 1
    if b == 0xFC: break
