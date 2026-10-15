"""Dump SR.EXE's data-driven UI: dialog table at DS:3ad8 -> lists of 18-byte widgets."""
import struct, sys
sys.path.insert(0, __import__('os').path.dirname(__file__))
from sr_dis import IMG

DS = 0x3e08 * 16
TYPES = {0x08: 'window', 0x40: 'window2', 0x10: 'button', 0x80: 'label', 0x100: 'ctext',
         0x200: 'listhdr', 0x400: 'listrow', 0x800: 'input'}

def ds_word(o): return struct.unpack('<H', IMG[DS + o:DS + o + 2])[0]
def ds_str(o):
    if o == 0 or o >= 0x8800: return ''
    e = IMG.index(b'\0', DS + o)
    return IMG[DS + o:e].decode('latin1')

def dialog(idx):
    p = ds_word(0x3ad8 + 2 * idx)
    items = []
    for _ in range(64):
        w = struct.unpack('<9h', IMG[DS + p:DS + p + 18])
        flags = w[0] & 0xffff
        items.append(dict(addr=p, flags=flags, type=TYPES.get(flags & 0xffd8, hex(flags & 0xffd8)),
                          text=ds_str(w[1] & 0xffff), colour=w[2], id=w[3], rect=w[4:8], extra=w[8]))
        p += 18
        if flags & 2: break
    return items

if __name__ == '__main__':
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 80
    for i in range(n):
        p = ds_word(0x3ad8 + 2 * i)
        if not (0x1000 < p < 0x8800): print(f'-- stop at {i} (ptr {p:#x})'); break
        print(f'== dialog {i} @DS:{p:04x}')
        for it in dialog(i):
            print(f"   {it['type']:8s} f={it['flags']:#06x} col={it['colour']:3d} id={it['id']:6d} "
                  f"rect={it['rect']} x={it['extra']:5d} '{it['text']}'")
