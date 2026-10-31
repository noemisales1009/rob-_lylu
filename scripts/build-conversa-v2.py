"""Monta a animação do app a partir das 36 poses geradas com ImageGen.

Uso: python scripts/build-conversa-v2.py
Requer Pillow, numpy e opencv-python-headless. No ambiente local, as duas
últimas dependências podem ficar em tmp/animation-packages.
"""
from pathlib import Path
import json
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tmp/animation-packages"))
import cv2
import numpy as np
from PIL import Image

OUT = ROOT / "output/lylu-conversa-v2"
PUBLIC = ROOT / "lylu_app/public"
SIDE = 208
STEPS = 4
MS = 35


def main():
    source = Image.open(OUT / "poses.png").convert("RGB")
    assert source.width == source.height and source.width % 6 == 0
    cell = source.width // 6
    poses = [np.asarray(source.crop((x * cell, y * cell, (x + 1) * cell,
             (y + 1) * cell)).resize((SIDE, SIDE), Image.Resampling.LANCZOS))
             for y in range(6) for x in range(6)]
    flow = cv2.DISOpticalFlow_create(cv2.DISOPTICAL_FLOW_PRESET_MEDIUM)
    yy, xx = np.mgrid[:SIDE, :SIDE].astype(np.float32)
    frames = []
    # A movimentação vem do fluxo entre os desenhos: não é só dissolver duas
    # mãos paradas. Ambos os sentidos ajudam a preencher regiões descobertas.
    for index, a in enumerate(poses):
        b = poses[(index + 1) % len(poses)]
        ga = cv2.cvtColor(a, cv2.COLOR_RGB2GRAY)
        gb = cv2.cvtColor(b, cv2.COLOR_RGB2GRAY)
        forward = flow.calc(ga, gb, None)
        backward = flow.calc(gb, ga, None)
        frames.append(Image.fromarray(a))
        for step in range(1, STEPS):
            t = step / STEPS
            wa = cv2.remap(a, xx - t * forward[..., 0], yy - t * forward[..., 1],
                           cv2.INTER_LINEAR, borderMode=cv2.BORDER_REPLICATE)
            wb = cv2.remap(b, xx - (1 - t) * backward[..., 0],
                           yy - (1 - t) * backward[..., 1], cv2.INTER_LINEAR,
                           borderMode=cv2.BORDER_REPLICATE)
            # Não misturar as duas silhuetas: nos dedos que se abrem isso
            # produz uma mão fantasma. Usa o desenho mais próximo no tempo.
            frames.append(Image.fromarray(wa if t < 0.5 else wb))
    atlas = Image.new("RGB", (SIDE * 12, SIDE * 12))
    for index, frame in enumerate(frames):
        atlas.paste(frame, ((index % 12) * SIDE, (index // 12) * SIDE))
    PUBLIC.mkdir(exist_ok=True)
    atlas.save(PUBLIC / "lylu-conversa-v2.webp", quality=90, method=6)
    frames[0].save(PUBLIC / "lylu-conversa-poster.webp", quality=95)
    frames[0].save(OUT / "previa.webp", save_all=True, append_images=frames[1:],
                   duration=MS, loop=0, quality=88, method=4)
    metadata = {"columns": 12, "frames": len(frames), "frameSize": SIDE,
                "frameMs": MS, "sourcePoses": len(poses), "interpolation": "bidirectional DIS optical flow"}
    (OUT / "animacao.json").write_text(json.dumps(metadata, indent=2), encoding="utf-8")
    print(json.dumps(metadata))
    print(f"Atlas: {(PUBLIC / 'lylu-conversa-v2.webp').stat().st_size // 1024} KiB")


if __name__ == "__main__":
    main()
