"""Unpack a Microsoft EXEPACK-compressed DOS MZ executable."""
import struct, sys

def unpack(data):
    h = list(struct.unpack('<14H', data[:28]))
    hdr_size = h[4] * 16
    file_len = (h[2] - 1) * 512 + h[1] if h[1] else h[2] * 512
    image = data[hdr_size:file_len]
    ep = h[11] * 16                                   # EXEPACK block offset in image
    ip, cs, _mem, ep_size, sp, ss, dest_len = struct.unpack('<7H', image[ep:ep + 14])
    sig_off = 16 if image[ep + 14:ep + 16] == b'RB' else 18
    assert image[ep + sig_off - 2:ep + sig_off] == b'RB', 'not EXEPACK'

    buf = bytearray(max(dest_len * 16, ep))
    buf[:ep] = image[:ep]
    src, dst = ep, dest_len * 16
    while buf[src - 1] == 0xFF:                       # trailing padding
        src -= 1
    while True:
        cmd = buf[src - 1]; src -= 1
        length = buf[src - 1] << 8 | buf[src - 2]; src -= 2
        op = cmd & 0xFE
        if op == 0xB0:
            fill = buf[src - 1]; src -= 1
            buf[dst - length:dst] = bytes([fill]) * length
            dst -= length
        elif op == 0xB2:
            buf[dst - length:dst] = buf[src - length:src]
            dst -= length; src -= length
        else:
            raise ValueError(f'bad command {cmd:#x} at {src:#x}')
        if cmd & 1:
            break

    # packed relocation table follows the "Packed file is corrupt" message
    msg = image.find(b'Packed file is corrupt', ep)
    p = msg + len('Packed file is corrupt')
    relocs = []
    for seg in range(16):
        n, = struct.unpack('<H', image[p:p + 2]); p += 2
        for _ in range(n):
            off, = struct.unpack('<H', image[p:p + 2]); p += 2
            relocs.append((off, seg * 0x1000))

    out_img = bytes(buf)
    rel_bytes = b''.join(struct.pack('<HH', o, s) for o, s in relocs)
    hsize = (28 + len(rel_bytes) + 15) // 16 * 16
    total = hsize + len(out_img)
    nh = [0x5A4D, total % 512, (total + 511) // 512, len(relocs), hsize // 16,
          h[5], h[6], ss, sp, 0, ip, cs, 28, 0]
    head = struct.pack('<14H', *nh) + rel_bytes
    head += b'\0' * (hsize - len(head))
    return head + out_img, dict(ip=ip, cs=cs, ss=ss, sp=sp, relocs=len(relocs), image=len(out_img))

if __name__ == '__main__':
    out, info = unpack(open(sys.argv[1], 'rb').read())
    open(sys.argv[2], 'wb').write(out)
    print(info)
