"""Ajusta o projeto Android que o Capacitor gera (cap add android) para a Lylu:
microfone, cores, ícone e tela de abertura com o rosto dela.

Uso: python prepara_android.py <pasta android>   (o gerar-apk.ps1 já chama)
Pode rodar de novo à vontade: só troca o que ainda não está como deve.
"""
from pathlib import Path
import sys

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]  # rob-_lylu
FUNDO = (17, 33, 25)  # #112119, o fundo do app e da tela da placa
ANDROID = Path(sys.argv[1])
MAIN = ANDROID / "app/src/main"
RES = MAIN / "res"


def troca(arquivo, velho, novo):
    s = arquivo.read_text(encoding="utf-8")
    if novo in s:
        return
    assert s.count(velho) == 1, f"{arquivo.name}: não achei {velho!r}"
    arquivo.write_text(s.replace(velho, novo), encoding="utf-8")


def manifesto():
    internet = '<uses-permission android:name="android.permission.INTERNET" />'
    troca(MAIN / "AndroidManifest.xml", internet,
          internet + '\n    <uses-permission android:name="android.permission.RECORD_AUDIO" />'
          '\n    <uses-permission android:name="android.permission.MODIFY_AUDIO_SETTINGS" />')


def cores():
    (RES / "values/ic_launcher_background.xml").write_text(
        '<?xml version="1.0" encoding="utf-8"?>\n<resources>\n'
        '    <color name="ic_launcher_background">#112119</color>\n'
        '    <color name="lylu_fundo">#112119</color>\n</resources>\n', encoding="utf-8")
    estilos = RES / "values/styles.xml"
    # Atrás das barras do sistema e na abertura: o verde do app, não branco.
    troca(estilos, '<item name="android:background">@null</item>',
          '<item name="android:background">@null</item>\n'
          '        <item name="android:windowBackground">@color/lylu_fundo</item>')
    troca(estilos, '<item name="android:background">@drawable/splash</item>',
          '<item name="android:background">@drawable/splash</item>\n'
          '        <item name="windowSplashScreenBackground">@color/lylu_fundo</item>')


def lylu():
    """Primeiro quadro do rosto (fundo transparente) e o recorte da cabeça."""
    folha = Image.open(ROOT / "output/lylu-conversa-v2/estudo-inicial.png").convert("RGBA")
    corpo = folha.crop((0, 0, 256, 256))
    return corpo, corpo.crop((55, 5, 245, 195)), corpo.crop((55, 5, 245, 256))


def icones(cabeca, busto):
    for pasta in RES.glob("mipmap-*dpi"):
        for nome in ("ic_launcher.png", "ic_launcher_round.png", "ic_launcher_foreground.png"):
            alvo = pasta / nome
            lado = Image.open(alvo).size[0]
            if nome == "ic_launcher_foreground.png":
                # Ícone adaptável: o launcher corta as bordas; o rosto fica nos 2/3 do
                # meio e o moletom desce até a borda (sem corte reto no meio do ícone).
                img = Image.new("RGBA", (lado, lado), (0, 0, 0, 0))
                miolo = round(lado * 0.66)
                alto = round(miolo * busto.height / busto.width)
                img.paste(busto.resize((miolo, alto), Image.LANCZOS), ((lado - miolo) // 2, (lado - miolo) // 2))
            else:
                img = Image.new("RGBA", (lado, lado), FUNDO + (255,))
                img.alpha_composite(cabeca.resize((lado, lado), Image.LANCZOS))
                if nome == "ic_launcher_round.png":
                    mascara = Image.new("L", (lado, lado), 0)
                    ImageDraw.Draw(mascara).ellipse((0, 0, lado - 1, lado - 1), fill=255)
                    img.putalpha(mascara)
            img.save(alvo)


def abertura(corpo):
    for alvo in RES.glob("drawable*/splash.png"):
        w, h = Image.open(alvo).size
        img = Image.new("RGBA", (w, h), FUNDO + (255,))
        lado = round(min(w, h) * 0.45)
        img.alpha_composite(corpo.resize((lado, lado), Image.LANCZOS), ((w - lado) // 2, (h - lado) // 2))
        img.convert("RGB").save(alvo)


manifesto()
cores()
corpo, cabeca, busto = lylu()
icones(cabeca, busto)
abertura(corpo)
print("projeto Android ajustado para a Lylu")
