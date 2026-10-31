"""Monta as ações frontais da P4 a partir das folhas criadas com ImageGen.

As poses da mão são os desenhos originais. O rosto e os ombros ficam presos
à mesma base; somente as regiões animadas são montadas sobre ela. O fluxo
óptico cria os intermediários locais, sem dissolver duas mãos sobrepostas.
Uso: python scripts/build-fala-frontal.py [--instalar]
Requer Pillow, numpy e opencv-python-headless (tmp/lylu-frontal-deps).
"""
from pathlib import Path
import argparse
import json
import shutil
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tmp/lylu-frontal-deps"))
import cv2
import numpy as np
from PIL import Image, ImageChops, ImageDraw, ImageFilter

OUT = ROOT / "output/lylu-fala-frontal"
SIZE = (300, 450)
RAW = (340, 512)
BG = (17, 33, 25)
MS = 60  # 16,7 fps; o GIF antigo tinha 10 fps e grandes saltos de pose.
STEPS = 6


def rgb(im):
    b = Image.new("RGBA", im.size, BG + (255,))
    b.alpha_composite(im)
    return np.asarray(b.convert("RGB"))


def cells(path, centers, rows, width, height):
    sheet = Image.open(path).convert("RGBA")
    return [sheet.crop((round(x - width / 2), y, round(x - width / 2) + width, y + height))
            .resize(RAW, Image.Resampling.LANCZOS) for y in rows for x in centers]


def register(im, base):
    """Alinha os desenhos pela testa/óculos, sem usar os braços como âncora."""
    a, b = (cv2.cvtColor(rgb(q), cv2.COLOR_RGB2GRAY)[:280] for q in (im, base))
    sift = cv2.SIFT_create()
    ka, da = sift.detectAndCompute(a, None)
    kb, db = sift.detectAndCompute(b, None)
    good = [m for m, n in cv2.BFMatcher().knnMatch(da, db, k=2) if m.distance < .72 * n.distance]
    if len(good) < 5:
        raise RuntimeError(f"Poucos pontos para alinhar o rosto: {len(good)}")
    src = np.float32([ka[m.queryIdx].pt for m in good])
    dst = np.float32([kb[m.trainIdx].pt for m in good])
    matrix, inliers = cv2.estimateAffinePartial2D(src, dst, method=cv2.RANSAC, ransacReprojThreshold=2)
    if matrix is None or inliers.sum() < 5:
        raise RuntimeError("Não foi possível alinhar o rosto")
    aligned = cv2.warpAffine(np.asarray(im), matrix, RAW, flags=cv2.INTER_CUBIC,
                             borderMode=cv2.BORDER_CONSTANT, borderValue=(0, 0, 0, 0))
    return Image.fromarray(aligned)


def mask_polygon(points, blur=3):
    mask = Image.new("L", RAW, 0)
    ImageDraw.Draw(mask).polygon(points, fill=255)
    return mask.filter(ImageFilter.GaussianBlur(blur))


def region(base, pose, mask):
    return Image.composite(pose, base, mask)


def fit(im):
    # A folha gerada tem posições aproximadas; a placa recebe quadros finais
    # independentes, sem atlas. Retira fragmentos do personagem vizinho.
    a = np.asarray(im).copy()
    n, labels, stats, _ = cv2.connectedComponentsWithStats((a[..., 3] >= 128).astype(np.uint8))
    if n > 1:
        largest = 1 + np.argmax(stats[1:, cv2.CC_STAT_AREA])
        keep = cv2.dilate((labels == largest).astype(np.uint8), np.ones((5, 5), np.uint8))
        a[..., 3] *= keep
    im = Image.fromarray(a)
    q = Image.new("RGBA", SIZE)
    q.alpha_composite(im.resize((240, 361), Image.Resampling.LANCZOS), (30, 35))
    return q


def smooth(poses, route):
    """Oito poses principais, seis passos entre elas, em um laço contínuo."""
    flow = cv2.DISOpticalFlow_create(cv2.DISOPTICAL_FLOW_PRESET_MEDIUM)
    yy, xx = np.mgrid[:SIZE[1], :SIZE[0]].astype(np.float32)
    frames = []
    for ia, ib in zip(route, route[1:] + route[:1]):
        a, b = poses[ia], poses[ib]
        if ia == ib:
            frames.extend([a.copy() for _ in range(STEPS)])
            continue
        ga, gb = (cv2.cvtColor(rgb(p), cv2.COLOR_RGB2GRAY) for p in (a, b))
        forward, backward = flow.calc(ga, gb, None), flow.calc(gb, ga, None)
        for step in range(STEPS):
            t = step / STEPS
            t = t * t * (3 - 2 * t)
            # Usa só o desenho mais próximo: não cria duas mãos transparentes.
            source, field, amount = (a, forward, t) if t < .5 else (b, backward, 1 - t)
            # Inverte o deslocamento no ponto de origem. Subtrair o fluxo
            # direto no destino rasgava a manga nas viagens maiores da mão.
            mapx, mapy = xx.copy(), yy.copy()
            for _ in range(6):
                sampled = cv2.remap(field, mapx, mapy, cv2.INTER_LINEAR,
                                    borderMode=cv2.BORDER_REPLICATE)
                mapx, mapy = xx - amount * sampled[..., 0], yy - amount * sampled[..., 1]
            warped = cv2.remap(np.asarray(source), mapx, mapy, cv2.INTER_LINEAR,
                               borderMode=cv2.BORDER_CONSTANT, borderValue=(0, 0, 0, 0))
            frames.append(Image.fromarray(warped))
    return frames


def verify(path, quiet):
    """Confere o arquivo codificado que vai realmente para a placa."""
    im = Image.open(path)
    assert im.size == SIZE and im.info.get("loop") == 0, path
    frames = []
    for i in range(im.n_frames):
        im.seek(i)
        assert im.info["duration"] >= MS and im.info["duration"] % 10 == 0
        q = im.convert("RGBA")
        box = q.getchannel("A").getbbox()
        assert box and box[0] >= 20 and box[1] >= 25 and box[2] <= 280 and box[3] <= 405, (path, i, box)
        frames.append(q)
    assert ImageChops.difference(frames[0].convert("RGB"), frames[-1].convert("RGB")).getbbox() is None, path
    head = frames[0].crop((0, 0, 300, 145)).convert("RGB")
    mouth = frames[0].crop((135, 191, 166, 207)).convert("RGB")
    for q in frames:
        assert ImageChops.difference(head, q.crop((0, 0, 300, 145)).convert("RGB")).getbbox() is None, path
        if quiet:
            assert ImageChops.difference(mouth, q.crop((135, 191, 166, 207)).convert("RGB")).getbbox() is None, path


def main():
    args = argparse.ArgumentParser()
    args.add_argument("--instalar", action="store_true")
    install = args.parse_args().instalar
    gestos = cells(OUT / "gestos-fundo.png", [188, 521, 854], [0, 512, 1024], 340, 512)
    base = gestos[0]
    gestos = [base] + [register(q, base) for q in gestos[1:]]
    expressoes = [register(q, base) for q in cells(OUT / "expressoes.png", [233, 641, 1046], [0, 627], 418, 627)]
    faces = [register(q, base) for q in cells(OUT / "poses.png", [238, 634, 1032], [0, 627], 418, 627)]

    # Cabeça, ombros, coração e braço de apoio ficam iguais em todos os quadros.
    mao_baixa = mask_polygon([(156, 395), (220, 355), (340, 330), (340, 512), (156, 512)])
    # Inclui toda a silhueta inferior: recortar apenas a mão deixaria um pedaço
    # da manga antiga do lado de fora e cortaria a mão que repousa no bolso.
    corpo_gesto = mask_polygon([(0, 269), (95, 269), (115, 229), (175, 229),
                               (175, 269), (340, 269), (340, 512), (0, 512)])
    boca = mask_polygon([(142, 216), (198, 216), (198, 250), (142, 250)], 2)
    olhos = mask_polygon([(107, 163), (234, 163), (234, 207), (107, 207)], 2)
    mouths = [region(base, q, boca) for q in faces[:4]]
    eyes = [base] + [region(base, q, olhos) for q in faces[4:6]]
    body_talk = [fit(region(base, q, mao_baixa)) for q in gestos[:8]]
    body_think = [fit(base)] + [fit(region(base, q, corpo_gesto)) for q in expressoes[1:3]]
    body_remind = [fit(base)] + [fit(region(base, q, corpo_gesto)) for q in expressoes[4:6]]

    # Uma base imóvel no primeiro e no último instante elimina o salto do laço.
    movements = {
        "falando": smooth(body_talk, [0, 0, 1, 2, 3, 4, 6, 7]),
        "ouvindo": [fit(base) for _ in range(96)],
        "pensando": smooth(body_think, [0, 1, 2, 2, 2, 2, 1, 0]),
        "recado": smooth(body_remind, [0, 1, 2, 2, 2, 2, 1, 0]),
    }
    # A mão repousa antes de repetir; os gestos não ficam insistentes.
    movements["falando"] += [fit(base) for _ in range(32)]
    movements["pensando"] += [fit(base) for _ in range(28)]
    movements["recado"] += [fit(base) for _ in range(36)]
    lips = [fit(q) for q in mouths]
    blinks = [fit(q) for q in eyes]
    mouth_mask, eye_mask = fit_mask(boca), fit_mask(olhos)
    # Variações pequenas de boca, com fechamento entre frases; não é lipsync fonético.
    speech = [0, 1, 3, 1, 0, 1, 2, 1, 0, 1, 3, 1, 0, 0, 1, 2]
    actions = {}
    for name, frames in movements.items():
        finished = []
        for i, q in enumerate(frames):
            # O fluxo da mão não pode arrastar cabelo, óculos ou testa.
            q = q.copy()
            q.paste(fit(base).crop((0, 0, 300, 211)), (0, 0))
            if name in ("falando", "recado"):
                mouth = speech[(i // 2) % len(speech)]
                q = Image.composite(lips[mouth], q, mouth_mask)
            # Uma piscada por laço; duração 240 ms, sem piscar a cada palavra.
            k = (i - 28) % len(frames)
            blink = {0: 1, 1: 2, 2: 2, 3: 1}.get(k, 0)
            if blink:
                q = Image.composite(blinks[blink], q, eye_mask)
            finished.append(q)
        # Entrada e retorno sempre em repouso, com boca fechada.
        finished[0] = fit(base)
        finished[-1] = fit(base)
        actions[name] = finished

    palette_source = Image.new("RGB", (SIZE[0] * 8, SIZE[1] * 4), BG)
    for row, frames in enumerate(actions.values()):
        for col in range(8):
            palette_source.paste(Image.fromarray(rgb(frames[col * (len(frames) - 1) // 7])),
                                 (col * SIZE[0], row * SIZE[1]))
    palette = palette_source.quantize(colors=255, method=Image.Quantize.MEDIANCUT)
    palette.putpalette(palette.getpalette()[:765] + [255, 0, 255])
    metadata = {}
    names = {"falando": "lylu-recados.gif", "ouvindo": "lylu-ouvindo.gif",
             "pensando": "lylu-pensando.gif", "recado": "lylu-lembrete.gif"}
    for name, frames in actions.items():
        encoded = []
        for q in frames:
            p = Image.fromarray(rgb(q)).quantize(palette=palette, dither=Image.Dither.NONE)
            # Fundo e bordas vazias usam o índice 255, como no decoder da placa.
            p.paste(255, mask=q.getchannel("A").point(lambda a: 255 if a < 128 else 0))
            encoded.append(p)
        dest = OUT / names[name]
        encoded[0].save(dest, save_all=True, append_images=encoded[1:], duration=MS,
                        transparency=255, background=255, loop=0, disposal=2, optimize=False)
        verify(dest, quiet=name in ("ouvindo", "pensando"))
        # A prévia usa o próprio GIF decodificado (não os PNGs antes da paleta).
        gif = Image.open(dest)
        preview = []
        durations = []
        for i in range(gif.n_frames):
            gif.seek(i)
            f = Image.new("RGBA", SIZE, BG + (255,))
            f.alpha_composite(gif.convert("RGBA"))
            preview.append(f.convert("RGB"))
            durations.append(gif.info["duration"])
        preview[0].save(OUT / f"previa-{name}.webp", save_all=True,
                        append_images=preview[1:], duration=durations, loop=0, quality=90)
        contact = Image.new("RGB", (SIZE[0] * 8, SIZE[1]), BG)
        for col in range(8):
            # Amostra por TEMPO: o encoder reúne os quadros repetidos da pausa.
            target = col * (sum(durations) - MS) / 7
            elapsed = 0
            index = 0
            for index, duration in enumerate(durations):
                elapsed += duration
                if elapsed > target:
                    break
            contact.paste(preview[index], (col * SIZE[0], 0))
        contact.save(OUT / f"quadros-{name}.png")
        metadata[name] = {"poses_principais": 8, "quadros_montados": len(frames),
                          "quadros_gif": gif.n_frames, "duracao_ms": sum(durations),
                          "bytes": dest.stat().st_size, "arquivo": names[name]}
        if install:
            shutil.copyfile(dest, ROOT / "lylu_p4/main/gifs" / names[name])
    (OUT / "animacao.json").write_text(json.dumps(metadata, indent=2, ensure_ascii=False), encoding="utf-8")
    print(json.dumps(metadata, indent=2, ensure_ascii=False))


def fit_mask(mask):
    out = Image.new("L", SIZE)
    out.paste(mask.resize((240, 361), Image.Resampling.LANCZOS), (30, 35))
    return out


if __name__ == "__main__":
    main()
