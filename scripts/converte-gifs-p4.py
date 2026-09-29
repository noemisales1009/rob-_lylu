# Converte conjuntos de quadros PNG (300x450, fundo transparente) em GIFs da P4:
# paleta com o indice 255 = magenta (fundo e transparencia), disposal 2, 100 ms
# (10 quadros por segundo: cada quadro de 300x450 leva ~40 ms para decodificar
# na placa). Antes, limpa os pedacos soltos: tudo que nao encosta (com folga de
# 8 px) no maior pedaco da figura e apagado.
#
# Uso: python converte-gifs-p4.py <pasta> <nome> [<nome> ...]
#   <pasta>/<nome>/frames/*.png  ->  output/lylu-telas/lylu-<nome>.gif (+ previa)
# Depois: copiar os GIFs para lylu_p4/main/gifs/ e reconfigurar (idf.py reconfigure),
# porque a lista de GIFs embutidos so e lida quando o CMake roda.
import glob, os, sys
from collections import deque
from PIL import Image, ImageFilter

BASE = sys.argv[1]
SAIDA = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "output", "lylu-telas")
FUNDO = (0x11, 0x21, 0x19)     # C_FUNDO da tela: a borda suavizada mistura com ele
MAGENTA = (255, 0, 255)
MS = 100

def maior_pedaco(mask):
    w, h = mask.size
    px = mask.load()
    visto = bytearray(w * h)
    melhor = []
    for y0 in range(h):
        for x0 in range(w):
            if not px[x0, y0] or visto[y0 * w + x0]:
                continue
            fila = deque([(x0, y0)]); visto[y0 * w + x0] = 1; comp = []
            while fila:
                x, y = fila.popleft(); comp.append((x, y))
                for nx, ny in ((x+1, y), (x-1, y), (x, y+1), (x, y-1)):
                    if 0 <= nx < w and 0 <= ny < h and px[nx, ny] and not visto[ny * w + nx]:
                        visto[ny * w + nx] = 1; fila.append((nx, ny))
            if len(comp) > len(melhor):
                melhor = comp
    m = Image.new("L", (w, h), 0)
    mp = m.load()
    for x, y in melhor:
        mp[x, y] = 255
    return m

def limpa(im):
    alfa = im.getchannel("A")
    solido = alfa.point(lambda a: 255 if a > 40 else 0)
    perto = maior_pedaco(solido).filter(ImageFilter.MaxFilter(17))   # o corpo + 8 px de folga
    novo_alfa = Image.composite(alfa, Image.new("L", im.size, 0), perto)
    apagados = sum(1 for a, b in zip(alfa.tobytes(), novo_alfa.tobytes()) if a > 40 and b <= 40)
    im.putalpha(novo_alfa)
    return im, apagados

def converte(nome):
    arqs = sorted(glob.glob(os.path.join(BASE, nome, "frames", "*.png")))
    rgbs, mascaras, total_apagado = [], [], 0
    for f in arqs:
        im, n = limpa(Image.open(f).convert("RGBA"))
        total_apagado += n
        base = Image.new("RGBA", im.size, FUNDO + (255,))
        rgbs.append(Image.alpha_composite(base, im).convert("RGB"))
        mascaras.append(im.getchannel("A").point(lambda a: 255 if a >= 128 else 0))
    w, h = rgbs[0].size
    mosaico = Image.new("RGB", (w * len(rgbs), h))
    for i, q in enumerate(rgbs):
        mosaico.paste(q, (i * w, 0), mascaras[i])
    pal_img = mosaico.quantize(colors=255, method=Image.Quantize.MEDIANCUT)
    pal = pal_img.getpalette()[:255 * 3] + list(MAGENTA)
    pal_img.putpalette(pal)
    saidas = []
    for q, m in zip(rgbs, mascaras):
        p = q.quantize(palette=pal_img, dither=Image.Dither.NONE)
        pp, mm = p.load(), m.load()
        for y in range(h):
            for x in range(w):
                if not mm[x, y]: pp[x, y] = 255
                elif pp[x, y] == 255: pp[x, y] = 254
        p.putpalette(pal)
        saidas.append(p)
    os.makedirs(SAIDA, exist_ok=True)
    gif = os.path.join(SAIDA, f"lylu-{nome}.gif")
    saidas[0].save(gif, save_all=True, append_images=saidas[1:], duration=MS, loop=0,
                   transparency=255, background=255, disposal=2, optimize=False)
    # previa: 6 quadros sobre o fundo da tela
    prev = Image.new("RGB", (w * 6, h), FUNDO)
    for i, k in enumerate(range(0, len(saidas), 6)):
        fr = saidas[k].convert("RGBA"); fr.putalpha(mascaras[k])
        prev.paste(fr, (i * w, 0), fr)
    prev.save(os.path.join(SAIDA, f"previa-{nome}.png"))
    print(f"{nome:9s} {len(arqs)} quadros {w}x{h}  apagados {total_apagado:5d} px soltos  -> {os.path.getsize(gif)//1024} KB", flush=True)

for n in sys.argv[2:]:
    converte(n)
