"""Export every image in LIB1/2/3 to PNG, plus a contact sheet per library."""
import os, sys
from PIL import Image, ImageDraw
sys.path.insert(0, os.path.dirname(__file__))
from srlib import Lib, planes_to_indices, EGA_PALETTE

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'out', 'images')
PAL = sum(EGA_PALETTE, ())
BASE_ID = {'LIB1': 0, 'LIB2': 1000, 'LIB3': 2000}

def to_image(lib, i):
    e = lib.entries[i]
    im = Image.new('P', (e['w'], e['h']))
    im.putpalette(PAL)
    im.putdata(planes_to_indices(lib.decode(i), e['w'], e['h']))
    return im

for name, base in BASE_ID.items():
    lib = Lib(os.path.join(ROOT, name))
    d = os.path.join(OUT, name)
    os.makedirs(d, exist_ok=True)
    imgs = []
    for i, e in enumerate(lib.entries):
        im = to_image(lib, i)
        im.save(os.path.join(d, f'{base + i:04d}_{e["w"]}x{e["h"]}_f{e["flags"]:03x}.png'))
        imgs.append((base + i, im))
    # contact sheet: shelf-pack at 2x
    W, pad, x, y, rowh, places = 1400, 6, 6, 6, 0, []
    for gid, im in imgs:
        w, h = im.width * 2, im.height * 2 + 12
        if x + w > W:
            x, y, rowh = 6, y + rowh + pad, 0
        places.append((gid, im, x, y)); x += w + pad; rowh = max(rowh, h)
    sheet = Image.new('RGB', (W, y + rowh + 6), (40, 40, 48))
    dr = ImageDraw.Draw(sheet)
    for gid, im, px, py in places:
        dr.text((px, py), str(gid), fill=(255, 255, 0))
        sheet.paste(im.convert('RGB').resize((im.width * 2, im.height * 2), Image.NEAREST), (px, py + 12))
    sheet.save(os.path.join(OUT, f'{name}_sheet.png'))
    print(name, len(imgs), 'images')
