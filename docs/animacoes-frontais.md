# A Lylu de frente — 31/10/2026

Pedido da Mica: frente para quem conversa, movimento natural, algumas mãos,
atenção enquanto ela fala, pensamento enquanto espera e um lembrete divertido.
Oito poses principais por ação, com intermediários pequenos entre os gestos.

Os GIFs finais têm **300 × 450**. A personagem ocupa uma área fixa, com margem
para coque, cotovelos e mãos. O firmware não recorta atlas em tempo de execução:
os quadros já estão separados dentro de cada GIF. O rosto permanece na mesma
posição; as transições das mãos não deformam testa, óculos ou cabelo.

| Ação | Quando | Arquivo |
|---|---|---|
| Escuta | Gravando a voz; resposta terminada; erro de conversa | `lylu-ouvindo.gif` |
| Pensamento | Enviando ou aguardando o Hermes | `lylu-pensando.gif` |
| Fala | Enquanto o áudio da resposta toca | `lylu-recados.gif` |
| Lembrete | Fala na tela Recados ou chat com `gesto: "recado"` / `"lembrete"` | `lylu-lembrete.gif` |

Fora da Conversa, a Lylu continua aparecendo só enquanto fala, nas telas em que
já havia espaço para ela. Os GIFs antigos por tela continuam removidos.
Uma única animação fica aberta na RAM; a anterior é fechada antes da troca.

Os intermediários têm 60 ms (cerca de 16,7 fps). O encoder reúne imagens
idênticas de repouso num quadro mais longo, preservando a duração. Por isso o
número de quadros físicos do GIF não é o número de poses principais.
A boca usa uma camada pequena de quatro aberturas, extraídas do próprio GIF
por `scripts/export-bocas-p4.py`. A energia do PCM de saída, em janelas de
50 ms, escolhe a abertura; nas pausas ela fecha. O microfone não interfere.
É acompanhamento de volume, não sincronização fonética. A voz tem prioridade
sobre os estados de envio/espera da Conversa.

## Fontes e montagem

- `output/lylu-fala-frontal/poses.png`: bocas e piscadas, ferramenta ImageGen integrada.
- `output/lylu-fala-frontal/gestos-fundo.png`: gesto baixo da mão, com transparência real.
- `output/lylu-fala-frontal/expressoes.png`: mão no queixo e dedo de lembrete.
- `output/lylu-fala-frontal/prompts.json`: prompts completos usados nas três edições.
- `output/lylu-fala-frontal/antes.gif`: cópia da animação anterior.
- `output/lylu-fala-frontal/previa.html`: prévia das quatro ações já decodificadas dos GIFs finais.
- `output/lylu-fala-frontal/animacao.json`: tamanhos, durações e contagem de quadros.

Montagem: `python scripts/build-fala-frontal.py --instalar`.
Requer Pillow, numpy e OpenCV. As dependências locais ficam em
`tmp/lylu-frontal-deps`. A montagem alinha os desenhos pelo rosto, fixa as
regiões imóveis e usa fluxo óptico somente para os intermediários dos gestos.
Não dissolve duas mãos sobrepostas.

Compilação: `./lylu_p4/compilar.ps1`.
