import sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter
sys.path.insert(0, "brand/gen")
from art import atom_layer, fbm, screen

ART = "tdesktop/Telegram/Resources/art/"


def squircle_mask(S, margin, radius):
    m = Image.new("L", (S * 2, S * 2), 0)
    d = ImageDraw.Draw(m)
    d.rounded_rectangle([margin * 2, margin * 2, (S - margin) * 2 - 1, (S - margin) * 2 - 1],
                        radius=radius * 2, fill=255)
    return m.resize((S, S), Image.LANCZOS)


def icon(size, margin_frac, thick):
    S = 1024
    margin = int(S * margin_frac)
    inner = S - 2 * margin
    yy, xx = np.mgrid[0:S, 0:S].astype(np.float32) / S
    top = np.array([0.03, 0.03, 0.12])
    bot = np.array([0.20, 0.09, 0.42])
    img = top * (1 - yy[..., None]) + bot * yy[..., None]
    n = fbm(S, S, 6, 4, 3)
    neb = np.clip((n - 0.4) * 2.4, 0, 1) * np.exp(-(((yy - 0.75) / 0.35) ** 2 + ((xx - 0.3) / 0.6) ** 2))
    img = screen(img, neb[..., None] * np.array([0.40, 0.18, 0.70]) * 0.8)
    neb2 = np.clip((fbm(S, S, 5, 8, 3) - 0.42) * 2.6, 0, 1) * np.exp(-(((yy - 0.25) / 0.4) ** 2 + ((xx - 0.85) / 0.4) ** 2))
    img = screen(img, neb2[..., None] * np.array([0.06, 0.40, 0.55]) * 0.7)
    d = np.sqrt((xx - 0.5) ** 2 + (yy - 0.5) ** 2)
    img = screen(img, np.exp(-(d / 0.3) ** 2)[..., None] * np.array([0.28, 0.2, 0.7]) * 0.8)
    rng = np.random.default_rng(3)
    if size >= 64:
        for _ in range(110):
            x, y = rng.random(2) * S
            r = 1.5 + rng.random() ** 3 * 4
            gx, gy = np.meshgrid(np.arange(int(x) - 8, int(x) + 9), np.arange(int(y) - 8, int(y) + 9))
            if gx.min() < 0 or gy.min() < 0 or gx.max() >= S or gy.max() >= S:
                continue
            e = np.exp(-((gx - x) ** 2 + (gy - y) ** 2) / (2 * r * r * 0.4))
            img[gy, gx] = screen(img[gy, gx], e[..., None] * np.array([1, 0.97, 0.92]) * (0.3 + rng.random() * 0.6))
    base = Image.fromarray((np.clip(img, 0, 1) * 255).astype(np.uint8), "RGB").convert("RGBA")
    L = int(inner * 0.78)
    logo = atom_layer(L, width=thick, ss=2)
    comp = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    comp.paste(logo, ((S - L) // 2, (S - L) // 2), logo)
    for rad, st in ((L * 0.07, 0.8), (L * 0.025, 0.9)):
        g = comp.filter(ImageFilter.GaussianBlur(rad))
        arr = np.array(g).astype(np.float32)
        arr[..., :3] = np.array([170, 150, 255])
        arr[..., 3] = np.clip(arr[..., 3] * 2.2 * st, 0, 255)
        base = Image.alpha_composite(base, Image.fromarray(arr.astype(np.uint8), "RGBA"))
    base = Image.alpha_composite(base, comp)
    mask = squircle_mask(S, margin, int(inner * 0.225))
    out = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    out.paste(base, (0, 0), mask)
    # subtle rim
    rim = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    ImageDraw.Draw(rim).rounded_rectangle([margin, margin, S - margin - 1, S - margin - 1],
                                          radius=int(inner * 0.225), outline=(190, 170, 255, 90), width=max(2, S // 256))
    out = Image.alpha_composite(out, rim)
    return out.resize((size, size), Image.LANCZOS)


def thick_for(size):
    return 2.0 if size >= 128 else (3.0 if size >= 48 else 4.2)


def main():
    sizes = [16, 32, 48, 64, 128, 256, 512]
    for s in sizes:
        icon(s, 0.04, thick_for(s)).save(ART + "icon%d.png" % s)
        icon(s * 2, 0.04, thick_for(s * 2)).save(ART + "icon%d@2x.png" % s)
    icon(256, 0.08, 2.0).save(ART + "logo_256.png")
    icon(256, 0.0, 2.0).save(ART + "logo_256_no_margin.png")
    ico_imgs = {s: icon(s, 0.03, thick_for(s)) for s in (16, 24, 32, 48, 64, 128, 256)}
    ico_imgs[256].save(ART + "icon256.ico", format="ICO",
                       sizes=[(s, s) for s in ico_imgs], append_images=[ico_imgs[s] for s in ico_imgs if s != 256])
    icon(512, 0.04, 2.0).save("brand/prime_icon_512.png")


if __name__ == "__main__":
    main()
