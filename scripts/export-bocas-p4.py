"""Extrai as quatro bocas do GIF final; mesmas coordenadas e paleta da P4.

A boca fica em uma camada pequena e acompanha o PCM que está tocando.
Não exige decodificar ou reiniciar o GIF a cada sílaba.
"""
from pathlib import Path
from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'output/lylu-fala-frontal'
RECT = (134, 188, 167, 207)
W, H = RECT[2] - RECT[0], RECT[3] - RECT[1]
gif = Image.open(ROOT / 'lylu_p4/main/gifs/lylu-recados.gif')
frames = []
for stamp in (0, 120, 720, 240):
    elapsed = 0
    for i in range(gif.n_frames):
        gif.seek(i)
        q = gif.convert('RGBA')
        elapsed += gif.info['duration']
        if elapsed > stamp:
            break
    mouth = q.crop(RECT)
    assert mouth.getchannel('A').getextrema() == (255, 255), 'A boca deve cobrir a anterior inteira'
    frames.append(mouth)
for a in range(4):
    for b in range(a + 1, 4):
        assert ImageChops.difference(frames[a].convert('RGB'), frames[b].convert('RGB')).getbbox(), 'Bocas repetidas'
preview = Image.new('RGB', (W * 4 * 4, H * 4))
lines = ['// Gerado por scripts/export-bocas-p4.py. Pixels BGRA, sem RAM de decodificação.',
         '#pragma once', '#include "lvgl.h"', f'#define BOCA_X {RECT[0]}', f'#define BOCA_Y {RECT[1]}']
for i, im in enumerate(frames):
    im.save(OUT / f'boca-{i}.png')
    preview.paste(im.resize((W * 4, H * 4)), (i * W * 4, 0))
    data = im.tobytes('raw', 'BGRA')
    lines.append(f'static const uint8_t boca_pixels_{i}[] = {{')
    lines.extend('    ' + ','.join(str(v) for v in data[n:n + 32]) + ',' for n in range(0, len(data), 32))
    lines.append('};')
lines.append('static const lv_image_dsc_t BOCA_QUADROS[4] = {')
for i in range(4):
    lines.append('    { .header = { .magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_ARGB8888, '
                 f'.w = {W}, .h = {H}, .stride = {W * 4}'
                 f' }}, .data_size = {W * H * 4}, .data = boca_pixels_{i} }},')
lines.append('};')
(ROOT / 'lylu_p4/main/boca_frames.h').write_text('\n'.join(lines) + '\n', encoding='utf-8')
preview.save(OUT / 'bocas-pcm.png')
print(f'4 bocas distintas, {W}x{H}, {W * H * 16} bytes, mesma posição em todos os estados')
