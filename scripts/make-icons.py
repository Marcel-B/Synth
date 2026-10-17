#!/usr/bin/env python3
"""Draws the app icons of every plugin (icons/<Target>.png, 1024 px) and their small copies for the website
(website/icons/<Target>.png, 128 px). Needs Pillow; the PNGs are committed, so the build does not run this.

    python3 scripts/make-icons.py

The look is the website's favicon: a dark panel with its top right corner cut off, the sign in one of Night City's
colours with the other two shifted to the sides like a misaligned screen. Instruments draw it in yellow, effects in
cyan, the MIDI effect in red. A new plugin gets an entry in ICONS and its PNGs, else CMake stops at its ICON_BIG.
"""

import math
import random
import sys
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parent.parent

SIZE = 1024
SS = 3  # drawn three times as large and scaled down, since Pillow's shapes have no antialiasing
S = SIZE * SS

BACKGROUND = (10, 11, 16)
PANEL = (20, 21, 28)
BORDER = (42, 45, 58)
YELLOW = (252, 238, 10)
CYAN = (0, 240, 255)
RED = (255, 0, 60)

# The rounded square takes 824 of 1024 pixels, as macOS's own icons do, leaving room for the shadow.
BODY = 824
RADIUS = 185
GLYPH = 250        # half the width the sign may take, in 1024 px
STROKE = 46        # its line width
SHIFT = 13         # how far red and cyan stand beside it


def px(v):
    return int(round(v * SS))


def to_screen(x, y):
    """Unit coordinates (-1..1, y up) to pixels of the large canvas."""
    return (S / 2 + x * GLYPH * SS, S / 2 - y * GLYPH * SS)


class Pen:
    def __init__(self, image):
        self.image = image
        self.draw = ImageDraw.Draw(image)

    def line(self, points, width=STROKE):
        pts = [to_screen(x, y) for x, y in points]
        w = px(width)
        self.draw.line(pts, fill=255, width=w, joint="curve")
        r = w / 2
        for x, y in (pts[0], pts[-1]):
            self.draw.ellipse((x - r, y - r, x + r, y + r), fill=255)

    def curve(self, f, x0=-1.0, x1=1.0, steps=400, width=STROKE):
        self.line([(x0 + (x1 - x0) * i / steps, f(x0 + (x1 - x0) * i / steps)) for i in range(steps + 1)], width)

    def dot(self, x, y, r):
        cx, cy = to_screen(x, y)
        rr = r * GLYPH * SS
        self.draw.ellipse((cx - rr, cy - rr, cx + rr, cy + rr), fill=255)

    def ring(self, x, y, r, width=STROKE):
        cx, cy = to_screen(x, y)
        rr = r * GLYPH * SS
        self.draw.ellipse((cx - rr, cy - rr, cx + rr, cy + rr), outline=255, width=px(width))

    def box(self, x0, y0, x1, y1, fill=True, width=STROKE, radius=0.0):
        a, b = to_screen(x0, y1)
        c, d = to_screen(x1, y0)
        self.draw.rounded_rectangle((a, b, c, d), radius=radius * GLYPH * SS, fill=255 if fill else None,
                                    outline=None if fill else 255, width=0 if fill else px(width))

    def polygon(self, points):
        self.draw.polygon([to_screen(x, y) for x, y in points], fill=255)


# --- the signs -------------------------------------------------------------------------------------------------------

def saw(t):
    return 2 * (t - math.floor(t + 0.5))


def analog(p):
    # Two periods of a sawtooth, the oscillator every subtractive synth starts from.
    p.line([(-1, -0.6), (0, 0.6), (0, -0.6), (1, 0.6)])


def fm(p):
    p.curve(lambda x: 0.62 * math.sin(math.pi * 2 * x + 0.9 * math.sin(math.pi * 2 * x)))


def synth(p):
    # Both engines: a saw cycle, then a sine.
    p.line([(-1, -0.55), (0, 0.55), (0, 0)])
    p.curve(lambda x: -0.55 * math.sin(math.pi * 2 * x), 0, 1, 200)


def dx(p):
    # An operator algorithm: three modulators over three carriers summed on a bus, feedback on the last.
    s = 0.16
    for cx in (-0.6, 0.0, 0.6):
        p.box(cx - s, 0.38, cx + s, 0.38 + 2 * s, fill=False, width=26, radius=0.04)
        p.box(cx - s, -0.42, cx + s, -0.42 + 2 * s, radius=0.04)
        p.line([(cx, 0.38), (cx, -0.1)], 26)
        p.line([(cx, -0.42), (cx, -0.75)], 26)
    p.line([(-0.6, -0.75), (0.6, -0.75)], 26)
    p.line([(0.6 + s, 0.54), (0.92, 0.54), (0.92, 0.92), (0.6, 0.92), (0.6, 0.7)], 22)


def wavetable(p):
    # Three frames of a table one behind the other, a sine turning into a brighter wave.
    for i, k in enumerate((0.0, 0.5, 1.0)):
        dx_, dy = -0.3 + 0.3 * i, -0.5 + 0.5 * i

        def f(x, dx_=dx_, dy=dy, k=k):
            t = (x - dx_ + 0.65) / 1.3 * 2 * math.pi
            return dy + 0.3 * (math.sin(t) + k * (0.5 * math.sin(2 * t) + 0.33 * math.sin(3 * t)))
        p.curve(f, dx_ - 0.65, dx_ + 0.65, 300, 30)


def granular(p):
    # A cloud of grains, thickest in the middle.
    rnd = random.Random(7)
    for _ in range(34):
        a = rnd.uniform(0, 2 * math.pi)
        r = math.sqrt(rnd.random())
        x, y = 0.9 * r * math.cos(a), 0.62 * r * math.sin(a)
        p.dot(x, y, 0.05 + 0.1 * (1 - r) * rnd.random())


def physical(p):
    # A tuning fork ringing: a struck resonator, what the plugin models.
    p.line([(-0.32, 0.85), (-0.32, -0.1)], 40)
    p.line([(0.32, 0.85), (0.32, -0.1)], 40)
    p.curve(lambda x: -0.1 - math.sqrt(max(0.0, 0.32 ** 2 - x * x)), -0.32, 0.32, 60, 40)
    p.line([(0, -0.42), (0, -0.92)], 44)
    for side in (-1, 1):
        for r in (0.62, 0.86):
            p.line([(side * (r - 0.06 * math.cos(a)), 0.38 + r * 0.5 * math.sin(a))
                    for a in [(-0.7 + 1.4 * i / 30) for i in range(31)]], 26)


def additiv(p):
    # Partials falling off as 1/k.
    n = 9
    for k in range(1, n + 1):
        x = -0.92 + (k - 1) * 1.84 / (n - 1)
        p.line([(x, -0.7), (x, -0.7 + 1.5 / k)], 40)


def pd(p):
    # A CZ resonance wave: a fast cosine under a falling window, cycle by cycle.
    def f(x):
        ph = ((x + 1) / 2 * 2) % 1
        return 0.72 * (1 - ph) * math.cos(2 * math.pi * 2 * ph)
    p.curve(f, steps=800, width=40)


def sampler(p):
    # A recorded wave with the loop's two markers.
    rnd = random.Random(3)
    n = 13
    for i in range(n):
        x = -0.92 + i * 1.84 / (n - 1)
        h = (math.exp(-1.6 * (x + 0.92)) * 0.7 + 0.12) * (0.5 + 0.5 * rnd.random())
        p.line([(x, -h), (x, h)], 30)
    for x, d in ((-0.5, 1), (0.62, -1)):
        p.line([(x, -0.95), (x, 0.95)], 20)
        p.polygon([(x, 0.95), (x + d * 0.2, 0.95), (x, 0.72)])


def bass(p):
    # A sawtooth through a resonant filter: every drop rings.
    def f(x):
        ph = (x + 1) % 1
        return 0.3 * (1 - 2 * ph) + 0.5 * math.exp(-4.5 * ph) * math.sin(2 * math.pi * 2 * ph)
    p.curve(f, steps=800, width=40)


def neuro(p):
    # A Reese mangled by drive: two detuned saws folded and torn.
    rnd = random.Random(11)
    pts = []
    for i in range(17):
        x = -1 + i * 2 / 16
        y = 0.5 * math.sin(4.2 * x) * math.cos(1.3 * x + 0.5) + rnd.uniform(-0.4, 0.4)
        pts.append((x, max(-0.85, min(0.85, y))))
    p.line(pts, 36)


def groovebox(p):
    # Steps of four tracks, set ones filled.
    grid = ["X..X", ".X.X", "XXXX", "..X."]
    s = 0.38
    for r, row in enumerate(grid):
        for c, ch in enumerate(row):
            x0 = -0.85 + c * 0.45
            y1 = 0.85 - r * 0.45
            p.box(x0, y1 - s, x0 + s, y1, fill=ch == "X", width=18, radius=0.05)


def robovox(p):
    # A robot's head: square eyes, a mouth of bars that speak.
    p.box(-0.82, -0.82, 0.82, 0.72, fill=False, width=38, radius=0.16)
    p.line([(0, 0.72), (0, 0.95)], 30)
    p.dot(0, 0.98, 0.09)
    p.box(-0.5, 0.12, -0.18, 0.4)
    p.box(0.18, 0.12, 0.5, 0.4)
    for i, h in enumerate([0.08, 0.18, 0.26, 0.14, 0.22, 0.1]):
        x = -0.45 + i * 0.18
        p.line([(x, -0.38 - h), (x, -0.38 + h)], 26)


def vocoder(p):
    # The bands of a voice, mirrored like a channel vocoder's meters.
    levels = [0.25, 0.5, 0.8, 0.62, 0.9, 0.55, 0.35, 0.6, 0.3, 0.18]
    for i, h in enumerate(levels):
        x = -0.9 + i * 1.8 / (len(levels) - 1)
        p.line([(x, -0.8 * h), (x, 0.8 * h)], 34)


def wobble(p):
    # A drawn LFO: a bent rise, a step down, a bent fall.
    def seg(x0, y0, x1, y1, bend, n=80):
        return [(x0 + (x1 - x0) * i / n, y0 + (y1 - y0) * (i / n) ** bend) for i in range(n + 1)]
    pts = seg(-1, -0.6, -0.35, 0.65, 0.35) + [(-0.35, -0.6)] + seg(-0.35, -0.6, 0.3, 0.65, 3.0) \
        + seg(0.3, 0.65, 1.0, -0.6, 0.3)
    p.line(pts)


def tape(p):
    # Two reels and the tape between them.
    for x in (-0.48, 0.48):
        p.ring(x, 0.12, 0.38, 32)
        p.dot(x, 0.12, 0.08)
        for k in range(3):
            a = math.pi / 2 + k * 2 * math.pi / 3
            p.line([(x + 0.12 * math.cos(a), 0.12 + 0.12 * math.sin(a)),
                    (x + 0.27 * math.cos(a), 0.12 + 0.27 * math.sin(a))], 22)
    p.line([(-0.86, 0.12), (-0.86, -0.62), (0.86, -0.62), (0.86, 0.12)], 26)


def freeze(p):
    # A snowflake.
    for k in range(6):
        a = math.pi / 2 + k * math.pi / 3
        c, s = math.cos(a), math.sin(a)
        p.line([(0, 0), (0.9 * c, 0.9 * s)], 36)
        for d in (0.42, 0.66):
            for side in (-1, 1):
                b = a + side * math.pi / 4
                p.line([(d * c, d * s), (d * c + 0.24 * math.cos(b), d * s + 0.24 * math.sin(b))], 28)


def arp(p):
    # Notes climbing a piano roll.
    for i in range(4):
        x0 = -0.95 + i * 0.5
        y = -0.68 + i * 0.45
        p.box(x0, y - 0.15, x0 + 0.4, y + 0.15, radius=0.05)


def delay(p):
    # A stab and its echoes, each quieter.
    for i in range(5):
        x = -0.85 + i * 0.42
        h = 0.85 * 0.68 ** i
        p.line([(x, -h), (x, h)], 52 - i * 6)


def chorus(p):
    # Three voices, a little apart.
    for i, ph in enumerate((-0.5, 0.0, 0.5)):
        p.curve(lambda x, ph=ph, i=i: 0.38 * math.sin(math.pi * 2 * x + ph) + (i - 1) * 0.3, steps=300, width=28)


def glue(p):
    # A compressor's curve: one to one up to the knee, flatter above, with the axes.
    p.line([(-0.85, 0.85), (-0.85, -0.85), (0.85, -0.85)], 22)
    pts = [(-0.85, -0.85), (0.0, 0.0)]
    pts += [(0.0 + 0.85 * i / 20, 0.85 * i / 20 * 0.35) for i in range(1, 21)]
    p.line(pts, 40)


def shimmer(p):
    # Sparks rising an octave at a time.
    def star(x, y, r):
        w = r * 0.22
        p.polygon([(x, y + r), (x + w, y + w), (x + r, y), (x + w, y - w), (x, y - r), (x - w, y - w), (x - r, y),
                   (x - w, y + w)])
    star(-0.3, -0.25, 0.68)
    star(0.48, 0.42, 0.42)
    star(0.62, -0.55, 0.24)
    star(-0.62, 0.68, 0.2)


def filt(p):
    # A low pass with its resonance peak.
    def f(x):
        fc = 0.25
        k = 4.0
        w = math.exp(3 * (x - fc))
        mag = 1 / math.sqrt((1 - w * w) ** 2 + (w / k) ** 2)
        return max(-0.85, min(0.85, -0.25 + 0.42 * math.log10(mag) * 2.2))
    p.curve(f, steps=500)


def stutter(p):
    # One slice played again and again.
    for i in range(4):
        x0 = -0.95 + i * 0.5
        p.curve(lambda x, x0=x0: 0.6 * math.sin((x - x0) / 0.4 * 2 * math.pi) * (1 - (x - x0) / 0.4) ** 0.5,
                x0, x0 + 0.38, 100, 30)
    p.line([(-1, -0.8), (1, -0.8)], 18)


def multiband(p):
    # Three bands meeting at two crossovers.
    for c, h in ((-0.62, 0.5), (0.0, 0.85), (0.62, 0.35)):
        p.curve(lambda x, c=c, h=h: -0.6 + h * math.exp(-((x - c) / 0.2) ** 2), c - 0.42, c + 0.42, 150, 34)
    p.line([(-1, -0.75), (1, -0.75)], 18)


def zone(p):
    # A flat line turned down in one place.
    p.curve(lambda x: 0.35 - 0.9 * math.exp(-((x - 0.05) / 0.28) ** 2), steps=400)
    p.polygon([(0.05, -0.85), (-0.12, -0.62), (0.22, -0.62)])


def distortion(p):
    # A sine clipped flat.
    p.curve(lambda x: max(-0.45, min(0.45, 0.9 * math.sin(math.pi * 2 * x))), steps=500)


def chrome_glitch(p):
    # A wave torn into horizontal slices that no longer line up.
    p.curve(lambda x: 0.6 * math.sin(math.pi * 2 * x), steps=400, width=52)
    cuts = [0, 340, 430, 560, 610, 700, SIZE]
    offsets = [0, 70, -55, 110, -30, 0]
    img = p.image
    for (a, b), off in zip(zip(cuts, cuts[1:]), offsets):
        strip = img.crop((0, px(a), S, px(b)))
        img.paste(0, (0, px(a), S, px(b)))
        img.paste(strip, (px(off), px(a)))


def scope(p):
    # An oscilloscope's graticule and a trace.
    for v in (-0.5, 0.0, 0.5):
        p.line([(-0.92, v), (0.92, v)], 8)
        p.line([(v * 1.7, -0.92), (v * 1.7, 0.92)], 8)
    p.box(-0.95, -0.95, 0.95, 0.95, fill=False, width=24, radius=0.08)
    p.curve(lambda x: 0.55 * math.sin(math.pi * 2 * x) * math.cos(math.pi * x * 0.5), -0.88, 0.88, 300, 32)


def expander(p):
    # A module of two oscillators and a multimode filter: a pulse over the notch between lowpass and highpass.
    p.line([(-0.95, 0.35), (-0.55, 0.35), (-0.55, 0.85), (-0.05, 0.85), (-0.05, 0.35), (0.45, 0.35), (0.45, 0.85),
            (0.95, 0.85)], 34)

    def f(x):
        w = math.exp(3.2 * x)
        k = 1.6
        mag = abs(1 - w * w) / math.sqrt((1 - w * w) ** 2 + (k * w) ** 2)
        return max(-0.85, -0.2 + 0.45 * math.log10(max(mag, 1e-3)))
    p.curve(f, steps=90)


def lfo(p):
    # One cycle of a sine above the controller's steps it turns into.
    def f(x):
        return 0.42 * math.sin(math.pi * x / 0.9)
    p.curve(lambda x: f(x) + 0.28, -0.9, 0.9, 300, 30)
    pts = []
    for i in range(9):
        x0 = -0.9 + i * 0.2
        y = f(x0 + 0.1) - 0.42
        pts += [(x0, y), (x0 + 0.2, y)]
    p.line(pts, 16)

INSTRUMENT, EFFECT, MIDI = YELLOW, CYAN, RED

ICONS = {
    "TonwerkAnalog": (analog, INSTRUMENT),
    "TonwerkFM": (fm, INSTRUMENT),
    "TonwerkSynth": (synth, INSTRUMENT),
    "TonwerkDX": (dx, INSTRUMENT),
    "TonwerkWavetable": (wavetable, INSTRUMENT),
    "TonwerkGranular": (granular, INSTRUMENT),
    "TonwerkPhysical": (physical, INSTRUMENT),
    "TonwerkAdditiv": (additiv, INSTRUMENT),
    "TonwerkPD": (pd, INSTRUMENT),
    "TonwerkSampler": (sampler, INSTRUMENT),
    "TonwerkBass": (bass, INSTRUMENT),
    "TonwerkNeuro": (neuro, INSTRUMENT),
    "TonwerkGroovebox": (groovebox, INSTRUMENT),
    "ChromeGlitch": (chrome_glitch, YELLOW),
    "TonwerkDistortion": (distortion, EFFECT),
    "TonwerkRobovox": (robovox, EFFECT),
    "TonwerkVocoder": (vocoder, EFFECT),
    "TonwerkWobble": (wobble, EFFECT),
    "TonwerkTape": (tape, EFFECT),
    "TonwerkFreeze": (freeze, EFFECT),
    "TonwerkDelay": (delay, EFFECT),
    "TonwerkChorus": (chorus, EFFECT),
    "TonwerkGlue": (glue, EFFECT),
    "TonwerkShimmer": (shimmer, EFFECT),
    "TonwerkFilter": (filt, EFFECT),
    "TonwerkStutter": (stutter, EFFECT),
    "TonwerkMultiband": (multiband, EFFECT),
    "TonwerkZone": (zone, EFFECT),
    "TonwerkScope": (scope, EFFECT),
    "TonwerkExpander": (expander, INSTRUMENT),
    "TonwerkArp": (arp, MIDI),
    "TonwerkLFO": (lfo, MIDI),
}


# --- the frame -------------------------------------------------------------------------------------------------------

def body_mask():
    m = Image.new("L", (S, S), 0)
    o = px((SIZE - BODY) / 2)
    ImageDraw.Draw(m).rounded_rectangle((o, o, o + px(BODY), o + px(BODY)), radius=px(RADIUS), fill=255)
    return m


def panel_points(inset):
    o = px((SIZE - BODY) / 2 + inset)
    e = px((SIZE + BODY) / 2 - inset)
    cut = px(130)
    return [(o, o), (e - cut, o), (e, o + cut), (e, e), (o, e)]


def shifted(mask, dx):
    return ImageChops.offset(mask, dx, 0)


def icon(glyph, colour):
    others = [c for c in (YELLOW, CYAN, RED) if c != colour]
    left, right = (RED, CYAN) if colour == YELLOW else (others[0], others[1])

    canvas = Image.new("RGBA", (S, S), (0, 0, 0, 0))

    # The shadow under the square, as the Dock shows icons.
    shadow = Image.new("L", (S, S), 0)
    shadow.paste(body_mask(), (0, px(14)))
    shadow = shadow.filter(ImageFilter.GaussianBlur(px(20))).point(lambda v: v * 0.55)
    canvas.paste(Image.new("RGBA", (S, S), (0, 0, 0, 255)), (0, 0), shadow)

    face = Image.new("RGBA", (S, S), BACKGROUND + (255,))
    d = ImageDraw.Draw(face)
    d.polygon(panel_points(52), fill=PANEL, outline=BORDER, width=px(6))
    # A short bar in the sign's colour on the panel's lower edge, like the editors' section headers.
    o = px((SIZE - BODY) / 2 + 52)
    e = px((SIZE + BODY) / 2 - 52)
    d.rectangle((o + px(60), e - px(8), o + px(250), e + px(2)), fill=colour)

    sign = Image.new("L", (S, S), 0)
    glyph(Pen(sign))

    glow = sign.filter(ImageFilter.GaussianBlur(px(26))).point(lambda v: v * 0.55)
    face.paste(Image.new("RGBA", (S, S), colour + (255,)), (0, 0), glow)
    face.paste(Image.new("RGBA", (S, S), left + (255,)), (0, 0), shifted(sign, -px(SHIFT)))
    face.paste(Image.new("RGBA", (S, S), right + (255,)), (0, 0), shifted(sign, px(SHIFT)))
    face.paste(Image.new("RGBA", (S, S), colour + (255,)), (0, 0), sign)

    canvas.paste(face, (0, 0), body_mask())
    return canvas.resize((SIZE, SIZE), Image.LANCZOS)


def main():
    names = sys.argv[1:] or list(ICONS)
    (ROOT / "icons").mkdir(exist_ok=True)
    (ROOT / "website" / "icons").mkdir(exist_ok=True)
    for name in names:
        glyph, colour = ICONS[name]
        img = icon(glyph, colour)
        img.save(ROOT / "icons" / f"{name}.png", optimize=True)
        img.resize((128, 128), Image.LANCZOS).save(ROOT / "website" / "icons" / f"{name}.png", optimize=True)
        print(name)


if __name__ == "__main__":
    main()
