#!/usr/bin/env python3
"""Draw the controller icons the binding prompts show (vrui::bindingIconPath), one for every way a button is
worked, into mod-template's Textures\\MyMod\\f4cf\\bindings\\.

Every icon is three parts, drawn in white for a tint to colour, each knocking a 1 px gap into the part under it:

  - the button, as the controller prints it: the trigger as a T in a box whose top slopes down, the grip as GRIP
    in a rounded box, a face button as its letter in a circle, the thumbstick as a ring and its knob with a small
    arrow on each side, grey;
  - the controller, an L or an R in a small circle, over the button's top left;
  - how it is worked, a caption over the button's bottom left: LONG, HOLD, 2X (a double press), RLSE (a release)
    or TCH (a touch). A press and a tap have none. A thumbstick push lights its direction's arrow instead.

The names are what bindingIconPath builds:

  <side>-trigger, <side>-grip, <side>-thumbstick       pressed or tapped
  x, y (left), a, b (right)                            a letter pressed or tapped
  <side>-<how>-<button>                                how: longpress, hold, double, release, touch
  <side>-thumbstick-<direction>                        pushed: up, down, left, right

<side> is left or right, and <button> trigger, grip, thumbstick or the letter. Each is 64 px tall with the button
centred 4 px below the middle, so a prompt draws every button at one scale, and as wide as it needs, in whole
multiples of 4. Uncompressed 32-bit BGRA with no mipmaps, as the framework's panel icons are.

Needs Pillow and NumPy: pip install pillow numpy
Usage: python tools/gen_binding_icons.py [--sheet sheet.png]
       --sheet also writes every icon on one picture, green on dark, to look them over.
"""
import argparse
import struct
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'mod-template' / 'data' / 'mod' / 'Textures' / 'MyMod' / 'f4cf' / 'bindings'
FONT = ROOT / 'src' / 'render' / 'fonts' / 'Roboto-Black.ttf'

SS = 8               # supersampling: every part is drawn 8 times larger and scaled down
STROKE = 2.6         # an outline's width
DIM = 0.25           # the opacity of the thumbstick's arrows that are not pushed
HEIGHT = 64          # every icon's height
CENTER = 36          # the button's middle, from the top: the controller's circle above it needs the room
CAP_RATIO = 0.711    # Roboto's capital height as a share of its font size

# how a button is worked: the caption, and the word in an icon's name
CAPTIONS = {'longpress': 'LONG', 'hold': 'HOLD', 'double': '2X', 'release': 'RLSE', 'touch': 'TCH'}
DIRECTIONS = ('up', 'down', 'left', 'right')
SIDES = {'left': ('x', 'y'), 'right': ('a', 'b')}

# where the controller's circle and the caption sit against each button: the circle's top left, the caption's
# right edge and its bottom, from the button's top left and bottom left
LAYOUT = {
    'trigger': {'circle': (-20, -12), 'caption_right': 8, 'caption_bottom': 2},
    'grip': {'circle': (-20, -18), 'caption_right': 3, 'caption_bottom': 5},
    'letter': {'circle': (-20, -16), 'caption_right': 4, 'caption_bottom': 0},
    'thumbstick': {'circle': (-8, -8), 'caption_right': 12, 'caption_bottom': -4},
}


# ---------------- drawing ----------------

class Part:
    """A part of an icon: its picture, and the area it covers, which knocks out what is drawn under it."""

    def __init__(self, img, cover):
        self.img = img
        self.cover = cover


def white(alpha):
    """A white picture with this alpha."""
    img = Image.new('RGBA', alpha.size, (255, 255, 255, 0))
    img.putalpha(alpha)
    return img


def supersampled(size, draw):
    """A mask of `size` drawn SS times larger by draw(ImageDraw, scale)."""
    big = Image.new('L', (size[0] * SS, size[1] * SS), 0)
    draw(ImageDraw.Draw(big), SS)
    return big


def outline_of(big, size):
    """The outline STROKE px wide inside a supersampled filled shape, and the shape, both at `size`."""
    eroded = big.filter(ImageFilter.MinFilter(int(STROKE * SS) * 2 + 1))
    ring = np.clip(np.array(big).astype(int) - np.array(eroded), 0, 255).astype(np.uint8)
    return Image.fromarray(ring).resize(size, Image.LANCZOS), big.resize(size, Image.LANCZOS)


def text(word, cap_height):
    """A word in Roboto Black with capitals `cap_height` px tall, cropped to its ink."""
    font = ImageFont.truetype(str(FONT), int(cap_height / CAP_RATIO * SS))
    box = font.getbbox(word)
    img = Image.new('L', (box[2] + 4 * SS, box[3] + 4 * SS), 0)
    ImageDraw.Draw(img).text((2 * SS, 2 * SS), word, font=font, fill=255)
    ys, xs = np.nonzero(np.array(img))
    img = img.crop((xs.min(), ys.min(), xs.max() + 1, ys.max() + 1))
    return img.resize((max(1, round(img.width / SS)), max(1, round(img.height / SS))), Image.LANCZOS)


def badge(ring, shape, size, word, cap_height, dx=0, dy=0):
    """An outline with a word centred in it by its ink, moved by dx, dy."""
    img = white(ring)
    t = text(word, cap_height)
    img.paste(Image.new('RGBA', t.size, (255, 255, 255, 255)), ((size[0] - t.width) // 2 + dx, (size[1] - t.height) // 2 + dy), t)
    return Part(img, np.array(shape) > 40)


def hand_circle(side):
    """The controller: L or R in a circle."""
    size = (30, 30)
    ring, shape = outline_of(supersampled(size, lambda d, s: d.ellipse((1.7 * s, 1.7 * s, 28.3 * s, 28.3 * s), fill=255)), size)
    # the L's ink sits left in its box, one pixel right centres it
    return badge(ring, shape, size, 'L' if side == 'left' else 'R', 15.5, dx=1 if side == 'left' else 0)


def trigger():
    """A T in a box whose top slopes down to the right, as a trigger's face, with rounded corners."""
    w, h = 38, 40
    big = supersampled((w, h), lambda d, s: d.polygon([(2 * s, 4 * s), ((w - 3) * s, 11 * s), ((w - 3) * s, 37 * s), (2 * s, 37 * s)], fill=255))
    big = big.filter(ImageFilter.GaussianBlur(3 * SS)).point(lambda v: 255 if v > 128 else 0)
    ring, shape = outline_of(big, (w, h))
    # low and left of the middle, under the slope and clear of the controller's circle
    return badge(ring, shape, (w, h), 'T', 20, dx=-2, dy=3)


def grip():
    """GRIP in a rounded box."""
    size = (text('GRIP', 17).width + 14, 29)
    ring, shape = outline_of(supersampled(size, lambda d, s: d.rounded_rectangle((1 * s, 1 * s, (size[0] - 1) * s, 28 * s), radius=8 * s, fill=255)), size)
    return badge(ring, shape, size, 'GRIP', 17)


def letter(name):
    """A face button: its letter in a circle."""
    size = (38, 38)
    ring, shape = outline_of(supersampled(size, lambda d, s: d.ellipse((1.5 * s, 1.5 * s, 36.5 * s, 36.5 * s), fill=255)), size)
    # Y is heavy at the top: one pixel down sets it in the middle of the circle
    return badge(ring, shape, size, name.upper(), 19, dy=1 if name == 'y' else 0)


def thumbstick(lit=None):
    """The ring and its knob, with a small grey arrow on each side; the arrow of a push, `lit`, larger and full."""
    size, c, radius, gap, arrow = (56, 56), 28, 16, 3, 5
    ring, shape = outline_of(supersampled(size, lambda d, s: d.ellipse(((c - radius) * s, (c - radius) * s, (c + radius) * s, (c + radius) * s), fill=255)), size)
    img = white(ring)
    knob = supersampled(size, lambda d, s: d.ellipse(((c - 6.5) * s, (c - 6.5) * s, (c + 6.5) * s, (c + 6.5) * s), fill=255))
    img.alpha_composite(white(knob.resize(size, Image.LANCZOS)))
    cover = np.array(shape) > 40
    for direction in DIRECTIONS:
        n = arrow * (1.5 if direction == lit else 1.0)

        def draw(d, s, direction=direction, n=n):
            m, o, length, half = c * s, (radius + gap) * s, n * s, n * 1.15 * s
            d.polygon({
                'up': [(m, m - o - length), (m - half, m - o), (m + half, m - o)],
                'down': [(m, m + o + length), (m - half, m + o), (m + half, m + o)],
                'left': [(m - o - length, m), (m - o, m - half), (m - o, m + half)],
                'right': [(m + o + length, m), (m + o, m - half), (m + o, m + half)],
            }[direction], fill=255)

        mask = supersampled(size, draw).resize(size, Image.LANCZOS)
        alpha = np.array(mask).astype(float) * (1.0 if direction == lit else DIM)
        img.alpha_composite(white(Image.fromarray(alpha.astype(np.uint8))))
        cover |= np.array(mask) > 40
    return Part(img, cover)


def caption(word, h=22, size=18.3, pad=4, stroke=2.0, radius=6, track=0.6):
    """A caption: bold capitals 13 px tall, a little apart and narrowed, in a rounded outline 22 px tall."""
    font = ImageFont.truetype(str(FONT), int(size * SS))
    th = h * SS
    t = Image.new('L', (int(font.getlength(word) + track * SS * len(word)) + 8 * SS, th), 0)
    d = ImageDraw.Draw(t)
    cap = font.getbbox('H')
    x, y = 2 * SS, (th - (cap[3] - cap[1])) // 2 - cap[1]
    for ch in word:
        d.text((x, y), ch, font=font, fill=255)
        x += font.getlength(ch) + track * SS
    cols = np.nonzero(np.array(t).max(axis=0))[0]
    t = t.crop((cols.min(), 0, cols.max() + 1, th))
    # LONG is 47 px of text, as the framework's first icons had it; every caption is narrowed alike
    t = t.resize((max(1, int(t.width * CAPTION_XSCALE)), th), Image.LANCZOS)
    w = t.width + 2 * pad * SS
    img = Image.new('L', (w, th), 0)
    d = ImageDraw.Draw(img)
    s = stroke * SS
    d.rounded_rectangle((s / 2, s / 2, w - s / 2 - 1, th - s / 2 - 1), radius=radius * SS, outline=255, width=int(s))
    img.paste(255, (pad * SS, 0), t)
    small = img.resize((round(w / SS), h), Image.LANCZOS)
    return Part(white(small), np.ones((small.height, small.width), bool))


CAPTION_XSCALE = 1.0
CAPTION_XSCALE = 47 / (caption('LONG').img.width - 2 * 4)


def compose(parts):
    """The icon: parts [(Part, x, y)] from the bottom up, placed against the button's top left at (0, 0) - which
    is the second part - each knocking a 1 px gap into what is under it, in a canvas HEIGHT px tall with the
    button's middle at CENTER, as wide as they need in multiples of 4, centred."""
    button = parts[1][0]
    top = CENTER - button.img.height // 2
    left = min(x for _, x, _ in parts)
    width = max(x + p.img.width for p, x, _ in parts) - left
    canvas_width = -(-(width + 2) // 4) * 4
    dx, dy = (canvas_width - width) // 2 - left, top
    for p, x, y in parts:
        assert 0 <= y + dy and y + dy + p.img.height <= HEIGHT, 'a part is outside the icon'
    canvas = np.zeros((HEIGHT, canvas_width, 4), np.uint8)
    for p, x, y in parts:
        x, y = x + dx, y + dy
        cover = np.zeros((HEIGHT, canvas_width), np.uint8)
        cover[y:y + p.img.height, x:x + p.img.width] = p.cover * 255
        gap = np.array(Image.fromarray(cover).filter(ImageFilter.MaxFilter(3))) > 0
        canvas[gap, 3] = 0
        img = Image.fromarray(canvas)
        img.alpha_composite(p.img, (x, y))
        canvas = np.array(img)
    return Image.fromarray(canvas)


def icon(side, button, how=None, lit=None):
    """The icon of a button on a controller, worked as `how` (a key of CAPTIONS, or None for a press), or the
    thumbstick pushed to `lit`."""
    if button == 'trigger':
        main, kind = trigger(), 'trigger'
    elif button == 'grip':
        main, kind = grip(), 'grip'
    elif button == 'thumbstick':
        main, kind = thumbstick(lit), 'thumbstick'
    else:
        main, kind = letter(button), 'letter'
    layout = LAYOUT[kind]
    parts = [(hand_circle(side), *layout['circle']), (main, 0, 0)]
    if how:
        cap = caption(CAPTIONS[how])
        parts.append((cap, layout['caption_right'] - cap.img.width, main.img.height + layout['caption_bottom'] - cap.img.height))
    return compose(parts)


def all_icons():
    """Every icon by its name, without the extension."""
    icons = {}
    for side, letters in SIDES.items():
        for button in ('trigger', 'grip', 'thumbstick') + letters:
            icons[button if len(button) == 1 else f'{side}-{button}'] = icon(side, button)
            for how in CAPTIONS:
                icons[f'{side}-{how}-{button}'] = icon(side, button, how)
        for direction in DIRECTIONS:
            icons[f'{side}-thumbstick-{direction}'] = icon(side, 'thumbstick', lit=direction)
    return icons


# ---------------- writing ----------------

def write_dds(img, path):
    """Uncompressed 32-bit BGRA with no mipmaps: a DDS header and the pixels, top row first."""
    w, h = img.size
    pixel_format = struct.pack('<II4sI4I', 32, 0x41, b'\0\0\0\0', 32, 0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000)
    header = struct.pack('<4sIIIIIII44x', b'DDS ', 124, 0x100F, h, w, w * 4, 0, 0) + pixel_format + struct.pack('<I16x', 0x1000)
    assert len(header) == 128
    bgra = np.array(img.convert('RGBA'))[:, :, [2, 1, 0, 3]]
    path.write_bytes(header + bgra.tobytes())


def write_sheet(icons, path):
    """Every icon, green on dark, a row per button of a controller."""
    rows = []
    for side, letters in SIDES.items():
        for button in ('trigger', 'grip', 'thumbstick') + letters:
            rows.append([button if len(button) == 1 else f'{side}-{button}'] + [f'{side}-{how}-{button}' for how in CAPTIONS])
        rows.append([f'{side}-thumbstick-{d}' for d in DIRECTIONS])
    cell = max(i.width for i in icons.values()) + 16
    sheet = Image.new('RGBA', (cell * 6 + 16, (HEIGHT + 12) * len(rows) + 16), (24, 30, 27, 255))
    for y, row in enumerate(rows):
        for x, name in enumerate(row):
            green = Image.new('RGBA', icons[name].size, (10, 250, 120, 255))
            green.putalpha(icons[name].getchannel('A'))
            sheet.alpha_composite(green, (8 + x * cell, 8 + y * (HEIGHT + 12)))
    sheet.save(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--sheet', type=Path, help='also write every icon on one picture')
    args = parser.parse_args()
    icons = all_icons()
    OUT.mkdir(parents=True, exist_ok=True)
    for name, img in icons.items():
        write_dds(img, OUT / f'{name}.dds')
    print(len(icons), 'icons written to', OUT)
    if args.sheet:
        write_sheet(icons, args.sheet)


if __name__ == '__main__':
    main()
