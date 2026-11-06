import numpy as np, math
from PIL import Image, ImageDraw, ImageFilter

RINGS = [  # cx, cy, angle, rx, ry (from prime_mark.svg, 108 box)
    (54.6, 53.4, -64.0, 35.2, 8.2),
    (54.4, 54.9, 18.6, 29.3, 8.1),
    (55.3, 54.8, 78.0, 18.2, 5.8),
]


def atom_layer(size, color=(255, 255, 255), width=1.5, ss=4):
    """Atom logo on transparent RGBA, size x size."""
    S = size * ss
    k = S / 108.0
    alpha = np.zeros((S, S), np.uint8)
    for cx, cy, ang, rx, ry in RINGS:
        pad = int(rx * k + 40)
        ring = Image.new("L", (pad * 2, pad * 2), 0)
        d = ImageDraw.Draw(ring)
        d.ellipse([pad - rx * k, pad - ry * k, pad + rx * k, pad + ry * k],
                  outline=255, width=max(1, int(width * k)))
        ring = ring.rotate(-ang, resample=Image.BICUBIC)
        tmp = Image.new("L", (S, S), 0)
        tmp.paste(ring, (int(cx * k) - pad, int(cy * k) - pad))
        alpha = np.maximum(alpha, np.array(tmp))
    star = Image.new("L", (S, S), 0)
    d = ImageDraw.Draw(star)
    cx, cy = 54.5 * k, 53.8 * k
    r, t = 4.4 * k, 1.1 * k
    d.polygon([(cx, cy - r), (cx + t, cy - t), (cx + r, cy), (cx + t, cy + t),
               (cx, cy + r), (cx - t, cy + t), (cx - r, cy), (cx - t, cy - t)], fill=255)
    alpha = np.maximum(alpha, np.array(star))
    rgba = Image.new("RGBA", (S, S), color + (255,))
    rgba.putalpha(Image.fromarray(alpha))
    return rgba.resize((size, size), Image.LANCZOS)


def fbm(w, h, octaves, seed, base=4):
    rng = np.random.default_rng(seed)
    out = np.zeros((h, w), np.float32)
    amp, tot = 1.0, 0.0
    for o in range(octaves):
        n = base * 2 ** o
        g = rng.random((max(2, int(n * h / w)) + 1, n + 1)).astype(np.float32)
        im = Image.fromarray((g * 255).astype(np.uint8)).resize((w, h), Image.BICUBIC)
        out += amp * np.asarray(im, np.float32) / 255
        tot += amp
        amp *= 0.55
    return out / tot


def screen(base, add):
    return 1 - (1 - base) * (1 - add)


def cover(W=2560, H=416, seed=7):
    rng = np.random.default_rng(seed)
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    u, v = xx / W, yy / H
    top = np.array([0.012, 0.014, 0.045])
    bot = np.array([0.10, 0.05, 0.22])
    img = top[None, None] * (1 - v[..., None]) + bot[None, None] * v[..., None]
    n1 = fbm(W, H, 6, 1, 3)
    n2 = fbm(W, H, 6, 2, 3)
    n3 = fbm(W, H, 5, 3, 5)
    band = np.exp(-(((v - 0.55 - 0.25 * (u - 0.5)) / 0.28) ** 2))
    neb_v = np.clip((n1 - 0.38) * 2.6, 0, 1) * band
    neb_t = np.clip((n2 - 0.42) * 2.6, 0, 1) * np.exp(-(((u - 0.82) / 0.22) ** 2 + ((v - 0.35) / 0.6) ** 2))
    neb_p = np.clip((n3 - 0.45) * 3, 0, 1) * np.exp(-(((u - 0.15) / 0.2) ** 2 + ((v - 0.8) / 0.5) ** 2))
    img = screen(img, neb_v[..., None] * np.array([0.34, 0.16, 0.62]) * 0.85)
    img = screen(img, neb_t[..., None] * np.array([0.06, 0.38, 0.52]) * 0.8)
    img = screen(img, neb_p[..., None] * np.array([0.55, 0.14, 0.45]) * 0.6)
    dust = np.clip((fbm(W, H, 5, 9, 4) - 0.5) * 3, 0, 1) * band
    img = img * (1 - 0.45 * dust[..., None])
    cx, cy = W / 2, H * 0.32
    d = np.sqrt(((xx - cx) / (H * 0.9)) ** 2 + ((yy - cy) / (H * 0.7)) ** 2)
    halo = np.exp(-d ** 2 * 2.2)
    img = screen(img, halo[..., None] * np.array([0.30, 0.22, 0.75]) * 0.75)

    stars = np.zeros((H, W, 3), np.float32)
    N = 1500
    sx = rng.random(N) * W
    sy = rng.random(N) * H
    mag = rng.random(N) ** 4.5
    for x, y, m in zip(sx, sy, mag):
        tint = rng.choice([0, 1, 2], p=[0.6, 0.25, 0.15])
        col = [(1, 0.96, 0.9), (0.7, 0.82, 1), (1, 0.8, 0.65)][tint]
        r = 0.55 + m * 1.6
        x0, x1, y0, y1 = int(x - 4), int(x + 5), int(y - 4), int(y + 5)
        if x0 < 0 or y0 < 0 or x1 > W or y1 > H:
            continue
        gx, gy = np.meshgrid(np.arange(x0, x1), np.arange(y0, y1))
        e = np.exp(-(((gx - x) ** 2 + (gy - y) ** 2) / (2 * (r * 0.6) ** 2)))
        stars[y0:y1, x0:x1] += e[..., None] * np.array(col) * (0.35 + 0.65 * m)
    img = screen(img, np.clip(stars, 0, 1))

    sp = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    dr = ImageDraw.Draw(sp)
    for _ in range(9):
        x = rng.random() * W
        y = rng.random() * H * 0.9
        if abs(x - W / 2) < 260:
            continue
        L = 14 + rng.random() * 26
        dr.line([(x - L, y), (x + L, y)], fill=(200, 215, 255, 150), width=1)
        dr.line([(x, y - L), (x, y + L)], fill=(200, 215, 255, 150), width=1)
        dr.ellipse([x - 2.2, y - 2.2, x + 2.2, y + 2.2], fill=(255, 255, 255, 255))
    spg = np.array(sp.filter(ImageFilter.GaussianBlur(0.7))).astype(np.float32) / 255
    spb = np.array(sp.filter(ImageFilter.GaussianBlur(5))).astype(np.float32) / 255
    img = screen(img, spg[..., :3] * spg[..., 3:4])
    img = screen(img, spb[..., :3] * spb[..., 3:4] * 0.9)

    gx, gy = np.meshgrid(np.arange(W), np.arange(H))

    def planet(px, py, R, ringrx, ringry, tilt, tint):
        nonlocal img
        dx, dy = (gx - px) / R, (gy - py) / R
        r2 = dx ** 2 + dy ** 2
        z = np.sqrt(np.clip(1 - r2, 0, 1))
        lam = np.clip(-dx * 0.6 - dy * 0.5 + z * 0.45, 0, 1) ** 1.1
        bands = 0.85 + 0.15 * np.sin(dy * 14 + 1.5 * dx)
        col = np.array(tint)[None, None] * (0.05 + lam[..., None]) * bands[..., None]
        rim = np.exp(-((np.sqrt(r2) - 1) / 0.04) ** 2) * 0.2
        col = col + rim[..., None] * np.array([0.5, 0.45, 1.0])
        a = np.clip((1 - r2) * R * 0.5, 0, 1)
        img = img * (1 - a[..., None]) + col * a[..., None]
        rl = Image.new("L", (W, H), 0)
        dd = ImageDraw.Draw(rl)
        dd.ellipse([px - ringrx, py - ringry, px + ringrx, py + ringry], outline=255, width=3)
        dd.ellipse([px - ringrx * 0.88, py - ringry * 0.88, px + ringrx * 0.88, py + ringry * 0.88], outline=140, width=2)
        rl = rl.rotate(-tilt, center=(px, py), resample=Image.BICUBIC)
        ra = np.asarray(rl, np.float32) / 255
        back = ((gy < py + (gx - px) * math.tan(math.radians(-tilt)) ) & (r2 < 1))
        ra = np.where(back, 0, ra) * 0.9
        img = screen(img, ra[..., None] * np.array([0.78, 0.7, 1.0]) * 0.8)

    planet(W * 0.15, H * 0.62, 34, 90, 20, -16, (0.55, 0.35, 0.95))
    planet(W * 0.89, H * 0.27, 14, 38, 8, -10, (0.2, 0.7, 0.85))

    base = Image.fromarray((np.clip(img, 0, 1) * 255).astype(np.uint8), "RGB").convert("RGBA")
    L = 230
    logo = atom_layer(L, width=1.9)
    lx, ly = int(W / 2 - L / 2), int(H * 0.32 - L / 2)
    comp = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    comp.paste(logo, (lx, ly), logo)
    for rad, st in ((18, 0.75), (7, 0.9)):
        g = comp.filter(ImageFilter.GaussianBlur(rad))
        arr = np.array(g).astype(np.float32)
        arr[..., :3] = np.array([165, 145, 255])
        arr[..., 3] = np.clip(arr[..., 3] * 2.4 * st, 0, 255)
        base = Image.alpha_composite(base, Image.fromarray(arr.astype(np.uint8), "RGBA"))
    base = Image.alpha_composite(base, comp)
    arr = np.array(base.convert("RGB")).astype(np.float32) / 255
    vig = 1 - 0.35 * (((u - 0.5) * 1.6) ** 2)
    arr *= vig[..., None]
    return Image.fromarray((np.clip(arr, 0, 1) * 255).astype(np.uint8), "RGB")


if __name__ == "__main__":
    im = cover()
    im.save("brand/gen/cover.png")
    im.crop((680, 0, 1880, 416)).save("brand/gen/cover_preview.png")
