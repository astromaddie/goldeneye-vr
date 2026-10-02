#!/usr/bin/env python3
"""Numbers for the unattended stereo check (verification.md "Standard unattended stereo check") from a
dump directory written by dump_frame / dump_stereo_frame (vr-eye0.bmp, vr-eye1.bmp, vr-state.txt).
It does not replace looking at the images; it answers the questions that have a number:

  - how much of each eye is non-black, and how dark the outer border is (overscan, black bars);
  - the sky: is the top of the image the sky colour or black;
  - parallax direction: block-match the right eye against the left. Nearer content must shift LEFT
    in the right eye by more than far content (the lower, nearer rows more negative than the top);
  - the left/right eyes differ at all (a mono duplicate would match exactly);
  - the image is not upside down: the sky/ceiling sits above the floor (brightness/colour layout is
    the caller's call; this prints the top and bottom band colours).

Needs numpy and pillow (pip install numpy pillow); nothing in the build uses it.

  tools/steam_frame/analyze_dump.py scratchpad/dumps/yaw0 [more dirs]
"""
import sys
from pathlib import Path

import numpy as np
from PIL import Image


def load(path):
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.float32)


def luma(a):
    return a @ np.array([0.299, 0.587, 0.114], dtype=np.float32)


def band(a, y0, y1):
    return a[int(a.shape[0] * y0):int(a.shape[0] * y1)]


def disparity(l, r, y0, y1, block=32, search=48):
    """Median horizontal shift (pixels, + = right) of the right eye's blocks relative to the left's."""
    h, w = l.shape
    shifts = []
    for y in range(int(h * y0), int(h * y1) - block, block):
        for x in range(search + 8, w - block - search - 8, block):
            ref = l[y:y + block, x:x + block]
            if ref.std() < 6.0:
                continue
            best, best_s = None, 0
            for s in range(-search, search + 1, 2):
                cand = r[y:y + block, x + s:x + s + block]
                sad = float(np.abs(ref - cand).mean())
                if best is None or sad < best:
                    best, best_s = sad, s
            if best is not None and best < ref.std() * 0.8:
                shifts.append(best_s)
    return (float(np.median(shifts)), len(shifts)) if shifts else (None, 0)


def report(d):
    d = Path(d)
    e0, e1 = load(d / "vr-eye0.bmp"), load(d / "vr-eye1.bmp")
    h, w, _ = e0.shape
    print(f"== {d}  ({w}x{h})")
    state = d / "vr-state.txt"
    if state.exists():
        print("   " + state.read_text().strip().replace("\n", "\n   "))
    for name, e in (("eye0", e0), ("eye1", e1)):
        lum = luma(e)
        nonblack = float((lum > 6).mean()) * 100
        edge = int(min(h, w) * 0.02)
        border = np.concatenate([lum[:edge].ravel(), lum[-edge:].ravel(), lum[:, :edge].ravel(), lum[:, -edge:].ravel()])
        top = band(e, 0.0, 0.12).reshape(-1, 3).mean(axis=0)
        mid = band(e, 0.45, 0.55).reshape(-1, 3).mean(axis=0)
        bot = band(e, 0.88, 1.0).reshape(-1, 3).mean(axis=0)
        print(f"   {name}: non-black {nonblack:5.1f}%  border mean luma {border.mean():5.1f} (black {float((border < 6).mean())*100:4.1f}%)"
              f"  top rgb {np.round(top).astype(int)}  mid {np.round(mid).astype(int)}  bottom {np.round(bot).astype(int)}")
    l, r = luma(e0), luma(e1)
    diff = float(np.abs(l - r).mean())
    print(f"   eyes differ: mean abs luma difference {diff:.2f} ({'identical: mono?' if diff < 0.05 else 'differ'})")
    rows = {}
    for label, (y0, y1) in (("top third (far)", (0.05, 0.38)), ("middle", (0.38, 0.66)), ("bottom third (near)", (0.66, 0.95))):
        s, n = disparity(l, r, y0, y1)
        rows[label] = s
        print(f"   parallax {label:20s}: median shift of the right eye {s if s is not None else 'n/a'} px ({n} blocks)")
    far, near = rows["top third (far)"], rows["bottom third (near)"]
    if far is not None and near is not None:
        verdict = "OK: nearer content shifts further left in the right eye" if near < far else \
            "WRONG or inconclusive: near content does not shift left of far content"
        print(f"   parallax direction: near {near} vs far {far} -> {verdict}")


if __name__ == "__main__":
    for arg in sys.argv[1:]:
        report(arg)
