"""Tiny helper: disassemble the unpacked SR.EXE image at seg:off or linear image offset."""
import struct, sys
from capstone import Cs, CS_ARCH_X86, CS_MODE_16
EXE = 'C:/Users/inact/SR2/out/SR_unpacked.exe'
raw = open(EXE, 'rb').read()
H = struct.unpack('<14H', raw[:28])
IMG = raw[H[4] * 16:]
RELOCS = set()
for i in range(H[3]):
    o, s = struct.unpack('<HH', raw[H[12] + 4 * i:H[12] + 4 * i + 4])
    RELOCS.add(s * 16 + o)
md = Cs(CS_ARCH_X86, CS_MODE_16)

def dis(seg, off, n=60):
    base = seg * 16
    out = []
    for ins in md.disasm(IMG[base + off:base + off + n * 8], off):
        if len(out) >= n: break
        rel = any((base + a) in RELOCS for a in range(ins.address, ins.address + ins.size))
        out.append(f'{seg:04x}:{ins.address:04x}  {ins.mnemonic:6s} {ins.op_str}{"   ;RELOC" if rel else ""}')
    return '\n'.join(out)

if __name__ == '__main__':
    a = sys.argv[1]
    n = int(sys.argv[2]) if len(sys.argv) > 2 else 60
    s, o = (int(x, 16) for x in a.split(':'))
    print(dis(s, o, n))

def far_calls_to(target_linear):
    """Find 'lcall seg:off' (9A) instructions whose target is the given linear image address."""
    import re
    hits = []
    for m in re.finditer(rb'\x9a(..)(..)', IMG[:0x3e080], re.S):
        off = int.from_bytes(m.group(1), 'little'); seg = int.from_bytes(m.group(2), 'little')
        if seg * 16 + off == target_linear and (m.start() + 3) in RELOCS:
            hits.append(m.start())
    return hits

def context(addr, before=40, after=0):
    seg = (addr - before) // 16
    return dis(seg, addr - seg * 16 - ((addr - before) - seg * 16) + ((addr - before) - seg * 16), 0) if False else \
        '\n'.join(f'{ins.address:05x}  {ins.mnemonic:6s} {ins.op_str}' for ins in md.disasm(IMG[addr - before:addr + 5 + after], addr - before))

def near_calls_to(target_linear, window=0x10000):
    """Find 'call rel16' (E8) instructions in the 64K around target that land on it."""
    import struct
    hits = []
    lo = max(0, target_linear - window); hi = min(0x3e080, target_linear + window)
    for a in range(lo, hi - 3):
        if IMG[a] == 0xE8:
            rel, = struct.unpack('<h', IMG[a + 1:a + 3])
            if a + 3 + rel == target_linear:
                hits.append(a)
    return hits

def calls_to(target_linear):
    return sorted(far_calls_to(target_linear) + near_calls_to(target_linear))
