#!/usr/bin/env python3
"""Draw the controller icons the binding prompts show (vrui::bindingIconPath), one for every way a button is
worked, into mod-template's Textures\\MyMod\\f4cf\\bindings\\.

Every icon is up to three parts, drawn in white for a tint to colour, each knocking a 1 px gap into the part under it:

  - the button, as the controller prints it: the trigger as a T in a box whose top slopes down, the grip as GRIP
    in a rounded box, a face button as its letter in a circle, the thumbstick as a ring with a half-opaque line
    around it and its knob, with a half-opaque shadow to its lower right;
  - the controller, an L or an R in a small circle, over the button's top left;
  - how it is worked, a solid disk with a symbol cut out of it, over the button's bottom left: a clock for a long
    press, an arrow down onto a bar for a hold, 2x for a double press, an arrow up off a bar for a release, and
    ripples for a touch. A press and a tap have none. A thumbstick push is a smaller disk with its direction's
    arrow, over the stick's bottom right.

The names are what bindingIconPath builds:

  <side>-trigger, <side>-grip, <side>-thumbstick       pressed or tapped
  x, y (left), a, b (right)                            a letter pressed or tapped
  <side>-<how>-<button>                                how: longpress, hold, double, release, touch
  <side>-thumbstick-<direction>                        pushed: up, down, left, right

<side> is left or right, and <button> trigger, grip, thumbstick or the letter. Each is 80 px tall with the button
centred in it, so a prompt draws every button at one scale, and as wide as it needs, in whole multiples of 4.
Uncompressed 32-bit BGRA with no mipmaps, as the framework's panel icons are.

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
HALF = 0.5           # the opacity of the thumbstick's outer line
SHADOW = 0.45        # the opacity of the shadow under its knob
HEIGHT = 80          # every icon's height: the thumbstick with its circle above and a mark below needs it all
CENTER = 40          # the button's middle, from the top
CAP_RATIO = 0.711    # Roboto's capital height as a share of its font size

# how a button is worked, as an icon's name has it
HOWS = ('longpress', 'hold', 'double', 'release', 'touch')
DIRECTIONS = ('up', 'down', 'left', 'right')
SIDES = {'left': ('x', 'y'), 'right': ('a', 'b')}

# where the controller's circle and the marks sit against each button: the circle's top left, and the middle of the
# mark of how it is worked and of a push's arrow, from the button's top left
LAYOUT = {
    'trigger': {'circle': (-20, -12), 'mark': (-5, 36)},
    'grip': {'circle': (-20, -18), 'mark': (-6, 29)},
    'letter': {'circle': (-20, -16), 'mark': (-5, 33)},
    'thumbstick': {'circle': (-12, -12), 'mark': (7, 47), 'arrow': (47, 47)},
}
MARK_RADIUS, MARK_SCALE = 16.5, 1.5     # the mark of how a button is worked, and its symbol's size
ARROW_RADIUS, ARROW_SCALE = 13.4, 1.08  # a push's arrow, smaller
GLYPH_WIDTH = 2.6                       # the symbol's lines, before its scale


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


def disk(size, cx, cy, r):
    """A filled circle's alpha at `size`, as floats."""
    return np.array(supersampled(size, lambda d, s: d.ellipse(((cx - r) * s, (cy - r) * s, (cx + r) * s, (cy + r) * s), fill=255)).resize(size, Image.LANCZOS)).astype(float)


def ring(size, c, r, width):
    """A circle's outline `width` px wide inside radius r, as floats."""
    big = supersampled(size, lambda d, s: d.ellipse(((c - r) * s, (c - r) * s, (c + r) * s, (c + r) * s), fill=255))
    eroded = big.filter(ImageFilter.MinFilter(int(width * SS) * 2 + 1))
    out = Image.fromarray(np.clip(np.array(big).astype(int) - np.array(eroded), 0, 255).astype(np.uint8))
    return np.array(out.resize(size, Image.LANCZOS)).astype(float)


def thumbstick():
    """The ring and its knob, with depth: a half-opaque line around the ring as its raised rim, and a half-opaque
    shadow to the knob's lower right, a pixel apart from it, as light from the upper left."""
    size, c, radius, rim = (52, 52), 26, 20, 24.4
    alpha = np.maximum(ring(size, c, rim, 2.0) * HALF, ring(size, c, radius, STROKE))
    shadow = np.clip(disk(size, c + 2.4, c + 2.4, 6.8) - disk(size, c, c, 7.5), 0, 255) * SHADOW
    alpha = np.maximum(alpha, shadow)
    alpha = np.maximum(alpha, disk(size, c, c, 6.5))
    return Part(white(Image.fromarray(alpha.astype(np.uint8))), disk(size, c, c, rim) > 40)


def mark(how, radius, scale):
    """A solid disk with a symbol cut out of it: how a button is worked, or a push's direction. The symbol is drawn
    on a grid of about 16 units around the middle, `scale` px each."""
    size = int(np.ceil(2 * radius)) + 2
    c = size / 2
    big = Image.new('L', (size * SS, size * SS), 0)
    d = ImageDraw.Draw(big)
    d.ellipse(((c - radius) * SS, (c - radius) * SS, (c + radius) * SS, (c + radius) * SS), fill=255)
    w = GLYPH_WIDTH * scale * SS

    def at(x, y):
        return ((c + x * scale) * SS, (c + y * scale) * SS)

    def dot(x, y, r):
        px, py = at(x, y)
        d.ellipse((px - r, py - r, px + r, py + r), fill=0)

    def lines(*points):
        d.line([at(x, y) for x, y in points], fill=0, width=round(w), joint='curve')
        for x, y in (points[0], points[-1]):
            dot(x, y, w / 2)

    def arc(cx, cy, r, start, end):
        px, py = at(cx, cy)
        rr = r * scale * SS + w / 2
        d.arc((px - rr, py - rr, px + rr, py + rr), start, end, fill=0, width=round(w))
        for a in (start, end):
            dot(cx + r * np.cos(np.radians(a)), cy + r * np.sin(np.radians(a)), w / 2)

    if how == 'longpress':
        arc(0, 0, 6.6, 0, 360)
        lines((0, -3.6), (0, 0), (2.9, 1.7))
    elif how in ('hold', 'release'):
        if how == 'hold':
            lines((0, -7.5), (0, 1.5))
            lines((-4.2, -2.5), (0, 1.5), (4.2, -2.5))
        else:
            lines((0, 2.6), (0, -7))
            lines((-4.2, -2.8), (0, -7), (4.2, -2.8))
        lines((-6, 6.6), (6, 6.6))
    elif how == 'double':
        font = ImageFont.truetype(str(FONT), round(14 * scale * SS))
        d.text(at(-0.3, 4.7), '2\u00d7', font=font, fill=0, anchor='ms')
    elif how == 'touch':
        dot(0, 4, 2.6 * scale * SS)
        arc(0, 2.79, 5.6, 214.7, 325.3)
        arc(0, 2.2, 9.4, 218.1, 321.9)
    else:
        tip, tail, head = 6.5, 7, 5
        points = {'up': ((0, tail), (0, -tip), ((-head, -tip + head), (0, -tip), (head, -tip + head))),
                  'down': ((0, -tail), (0, tip), ((-head, tip - head), (0, tip), (head, tip - head))),
                  'left': ((tail, 0), (-tip, 0), ((-tip + head, -head), (-tip, 0), (-tip + head, head))),
                  'right': ((-tail, 0), (tip, 0), ((tip - head, -head), (tip, 0), (tip - head, head)))}[how]
        lines(points[0], points[1])
        lines(*points[2])

    alpha = big.resize((size, size), Image.LANCZOS)
    yy, xx = np.mgrid[0:size, 0:size]
    return Part(white(alpha), (xx - c + 0.5) ** 2 + (yy - c + 0.5) ** 2 <= radius ** 2)


def compose(parts):
    """The icon: parts [(Part, x, y)] from the bottom up, placed against the button's top left at (0, 0) - which
    is the second part - each knocking a 1 px gap into what is under it, in a canvas HEIGHT px tall with the
    button's middle at CENTER, as wide as they need in multiples of 4, centred."""
    button = parts[1][0]
    top = CENTER - round(button.img.height / 2)
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


def centred(part, x, y):
    """A part placed by its middle."""
    return part, round(x - part.img.width / 2), round(y - part.img.height / 2)


def icon(side, button, how=None, lit=None):
    """The icon of a button on a controller, worked as `how` (one of HOWS, or None for a press), or the thumbstick
    pushed to `lit`."""
    if button == 'trigger':
        main, kind = trigger(), 'trigger'
    elif button == 'grip':
        main, kind = grip(), 'grip'
    elif button == 'thumbstick':
        main, kind = thumbstick(), 'thumbstick'
    else:
        main, kind = letter(button), 'letter'
    layout = LAYOUT[kind]
    parts = [(hand_circle(side), *layout['circle']), (main, 0, 0)]
    if how:
        parts.append(centred(mark(how, MARK_RADIUS, MARK_SCALE), *layout['mark']))
    if lit:
        parts.append(centred(mark(lit, ARROW_RADIUS, ARROW_SCALE), *layout['arrow']))
    return compose(parts)


def all_icons():
    """Every icon by its name, without the extension."""
    icons = {}
    for side, letters in SIDES.items():
        for button in ('trigger', 'grip', 'thumbstick') + letters:
            icons[button if len(button) == 1 else f'{side}-{button}'] = icon(side, button)
            for how in HOWS:
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
            rows.append([button if len(button) == 1 else f'{side}-{button}'] + [f'{side}-{how}-{button}' for how in HOWS])
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
