"""Street Rod 2 LIB file reader (format reverse-engineered from SR.EXE)."""
import struct

COUNTS = [1, 1, 2, 3, 4, 2, 3, 5, 6, 6, 4, 7, 7, 5, 8, 8]
VALUES = [0x00, 0xff, 0x00, 0x00, 0x00, 0xff, 0xff, 0x00, 0xff, 0x00, 0xff, 0xff, 0x00, 0xff, 0xff, 0x00]

def rle_decode(src, table):
    lut = [0] * 256
    for k, b in enumerate(table):
        lut[b] = k + 1
    out = bytearray()
    i = 0
    while i < len(src):
        b = src[i]; i += 1
        if b in (0x00, 0xff):
            c = src[i]; i += 1
            if c == 0:
                out.append(table[0] if b == 0 else table[1])
            else:
                out += bytes([b]) * (c + 1)
        elif lut[b]:
            k = lut[b] - 1
            out += bytes([VALUES[k]]) * COUNTS[k]
        else:
            out.append(b)
    return bytes(out)

class Lib:
    def __init__(self, path):
        d = open(path, 'rb').read()
        n, = struct.unpack('<H', d[:2])
        base = 2 + 28 * n
        self.entries = []
        for i in range(n):
            r = d[2 + 28 * i:30 + 28 * i]
            w, h, flags = struct.unpack('<3H', r[:6])
            table = r[6:22]
            off, size = struct.unpack('<IH', r[22:28])
            blob = d[base + off:base + off + size + 1]
            self.entries.append(dict(w=w, h=h, flags=flags, table=table, raw=blob))

    def decode(self, i):
        e = self.entries[i]
        raw = e['raw']
        assert raw[0] == 0xBC, 'missing start sentinel'
        return rle_decode(raw[1:len(raw) - 1], e['table'])

EGA_PALETTE = [(0,0,0),(0,0,170),(0,170,0),(0,170,170),(170,0,0),(170,0,170),(170,85,0),(170,170,170),
               (85,85,85),(85,85,255),(85,255,85),(85,255,255),(255,85,85),(255,85,255),(255,255,85),(255,255,255)]

def planes_to_indices(data, w, h, layout='plane'):
    """Return a list of 4-bit pixel indices (row-major)."""
    bpr = (w + 7) // 8
    px = [0] * (w * h)
    nplanes = len(data) // (bpr * h)          # 4 normally; 1 for single-plane (flag 0x2xx) images
    for p in range(nplanes):
        for y in range(h):
            if layout == 'plane':
                row = data[p * bpr * h + y * bpr:][:bpr]
            else:  # row-interleaved: each row holds its 4 planes back to back
                row = data[(y * 4 + p) * bpr:][:bpr]
            for x in range(w):
                if row[x >> 3] & (0x80 >> (x & 7)):
                    px[y * w + x] |= 1 << p
    if nplanes == 1:
        px = [15 if v else 0 for v in px]
    return px
