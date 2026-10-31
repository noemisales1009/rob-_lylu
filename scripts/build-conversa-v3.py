"""Monta a animação do app juntando o rosto (estudo-inicial.png) com a mão
no queixo (poses.png, quadros 7 a 18).

Uso: python scripts/build-conversa-v3.py
Requer Pillow, numpy e opencv-python-headless (podem ficar em tmp/animation-packages).

Atlas: quadros 0–23 são o rosto, alinhados pelo ombro; 24–68 são o gesto de
pensar, ampliados para a cabeça ter o mesmo tamanho da do rosto, com os
intermediários feitos por fluxo óptico. A ordem dos quadros fica em animacao.js.
"""
from pathlib import Path
import json
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tmp/animation-packages"))
import cv2
import numpy as np
from PIL import Image

FONTE = ROOT / "output/lylu-conversa-v2"
OUT = ROOT / "output/lylu-conversa-v3"
PUBLIC = ROOT / "lylu_app/public"
LADO = 240
FUNDO = (17, 33, 25)  # #112119, o fundo do app
COLUNAS = 9
PASSOS = 4

# Onde o centro do ombro cai no quadro final, e a largura do moletom ali.
OMBRO_X, OMBRO_Y, OMBRO_LARGURA = 120, 173, 161

# O rosto das poses vai para cima do rosto do estudo: centro dos óculos medido
# à mão no primeiro quadro de cada folha, depois acertado por matchTemplate na
# região óculos–boca (melhor correlação 0,79 com escala 1,34).
OLHO_ROSTO = (167, 108)
OLHO_POSES = (125, 78.5)
ESCALA_POSES = 1.34


def ombro(rgba):
    """Topo do ombro e centro/largura do moletom logo abaixo dele."""
    c = rgba.astype(int)
    r, g, b, a = c[..., 0], c[..., 1], c[..., 2], c[..., 3]
    moletom = (a > 128) & (g > r - 15) & (g > b + 15) & (r < 140)
    topo = int(np.argmax(moletom.sum(1) > 120))
    xs = np.nonzero(moletom[topo + 10:topo + 30].any(0))[0]
    return (xs.min() + xs.max()) / 2, topo, xs.max() - xs.min()


def transforma(img, escala, de, para, borda):
    m = np.float32([[escala, 0, para[0] - de[0] * escala],
                    [0, escala, para[1] - de[1] * escala]])
    return cv2.warpAffine(img, m, (LADO, LADO), flags=cv2.INTER_AREA if escala < 1 else cv2.INTER_CUBIC,
                          borderMode=cv2.BORDER_CONSTANT, borderValue=borda)


def rosto():
    folha = np.asarray(Image.open(FONTE / "estudo-inicial.png").convert("RGBA"))
    quadros, ref = [], None
    for i in range(24):
        x, y = i % 6, i // 6
        cel = folha[y * 256:(y + 1) * 256, x * 256:(x + 1) * 256]
        cx, oy, larg = ombro(cel)
        escala = OMBRO_LARGURA / larg
        if ref is None:
            # posição do olho do primeiro quadro, já no quadro final
            ref = ((OLHO_ROSTO[0] - cx) * escala + OMBRO_X, (OLHO_ROSTO[1] - oy) * escala + OMBRO_Y)
        q = transforma(cel, escala, (cx, oy), (OMBRO_X, OMBRO_Y), (0, 0, 0, 0))
        fundo = Image.new("RGBA", (LADO, LADO), FUNDO + (255,))
        fundo.alpha_composite(Image.fromarray(q))
        quadros.append(np.asarray(fundo.convert("RGB")))
    return quadros, ref


def pensando(olho):
    folha = np.asarray(Image.open(FONTE / "poses.png").convert("RGB"))
    cel = folha.shape[0] // 6
    poses = []
    for i in range(6, 18):  # sobe a mão (7–12), encosta no queixo, desce (13–18)
        x, y = i % 6, i // 6
        p = folha[y * cel:(y + 1) * cel, x * cel:(x + 1) * cel]
        poses.append(transforma(np.ascontiguousarray(p), ESCALA_POSES, OLHO_POSES, olho, FUNDO))
    fluxo = cv2.DISOpticalFlow_create(cv2.DISOPTICAL_FLOW_PRESET_MEDIUM)
    yy, xx = np.mgrid[:LADO, :LADO].astype(np.float32)
    quadros = []
    # Mesmo método da v2: o desenho mais próximo no tempo, deformado pelo
    # fluxo, sem misturar duas mãos.
    for a, b in zip(poses, poses[1:]):
        ga, gb = (cv2.cvtColor(p, cv2.COLOR_RGB2GRAY) for p in (a, b))
        ida, volta = fluxo.calc(ga, gb, None), fluxo.calc(gb, ga, None)
        quadros.append(a)
        for passo in range(1, PASSOS):
            t = passo / PASSOS
            if t < 0.5:
                q = cv2.remap(a, xx - t * ida[..., 0], yy - t * ida[..., 1], cv2.INTER_LINEAR,
                              borderMode=cv2.BORDER_REPLICATE)
            else:
                q = cv2.remap(b, xx - (1 - t) * volta[..., 0], yy - (1 - t) * volta[..., 1],
                              cv2.INTER_LINEAR, borderMode=cv2.BORDER_REPLICATE)
            quadros.append(q)
    quadros.append(poses[-1])
    return quadros


def main():
    faces, olho = rosto()
    gesto = pensando(olho)
    quadros = faces + gesto
    linhas = -(-len(quadros) // COLUNAS)
    atlas = Image.new("RGB", (LADO * COLUNAS, LADO * linhas), FUNDO)
    for i, q in enumerate(quadros):
        atlas.paste(Image.fromarray(q), ((i % COLUNAS) * LADO, (i // COLUNAS) * LADO))
    OUT.mkdir(exist_ok=True)
    atlas.save(PUBLIC / "lylu-conversa-v3.webp", quality=90, method=6)
    Image.fromarray(faces[0]).save(PUBLIC / "lylu-conversa-poster.webp", quality=95)
    # Conferência: rosto e gesto lado a lado e sobrepostos.
    sobre = Image.blend(Image.fromarray(faces[0]), Image.fromarray(gesto[0]), 0.5)
    conf = Image.new("RGB", (LADO * 4, LADO), FUNDO)
    for i, q in enumerate([Image.fromarray(faces[0]), Image.fromarray(gesto[0]),
                           Image.fromarray(gesto[24]), sobre]):
        conf.paste(q, (i * LADO, 0))
    conf.save(OUT / "verificacao.png")
    meta = {"columns": COLUNAS, "frames": len(quadros), "frameSize": LADO,
            "rosto": [0, 23], "pensando": [24, len(quadros) - 1], "olho": [round(v, 1) for v in olho]}
    (OUT / "animacao.json").write_text(json.dumps(meta, indent=2), encoding="utf-8")
    print(json.dumps(meta))
    print(f"Atlas: {(PUBLIC / 'lylu-conversa-v3.webp').stat().st_size // 1024} KiB")


if __name__ == "__main__":
    main()
