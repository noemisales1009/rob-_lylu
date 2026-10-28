# Ponte Lylu v3: voz comprimida (ADPCM), para responder mais rápido

Para: Hermes. Da: Noemi / Claude Code.

## Por que

Medimos uma conversa (fala de 6 s) pelo log da placa:
- a placa sobe a fala: 205 KB em 6,7 s
- relay + você: 9,2 s
- a placa baixa a resposta: 203 KB em 6,1 s

A placa transfere só ~30 KB/s, e a voz ia crua (PCM, 32 KB por segundo de fala).
Agora ela vai em **IMA ADPCM**, 4 vezes menor (8 KB por segundo de fala). Também
aumentamos a janela TCP da placa. Juntas, as duas mudanças devem cortar uns 10 s
de cada conversa.

## O que muda

- **Placa** (já gravada): manda a fala em ADPCM **só** quando o `poll` responde
  com `"audio_formatos": [..., "ima_adpcm_16000"]`. Toca a resposta em ADPCM
  quando o payload dela tiver `"formato": "ima_adpcm_16000"`. Sem isso, continua
  tudo em PCM, como hoje.
- **Edge Function**: aceita o cabeçalho `x-lylu-formato` nas chamadas binárias
  (`voz` da placa e `audio_up` do relay), guarda `formato` no payload da `voz`,
  repassa `formato` no `complete` e anuncia `audio_formatos` no `poll` e no GET
  (`"version": 3`).
- **Relay** (`lylu_voz.py`): `adpcm_codifica` / `adpcm_decodifica` / `para_pcm`.
  Batem bit a bit com o `adpcm.c` da placa (testado). Puro Python: 10 s de voz
  levam ~0,5 s.
- **Relay** (`lylu_bridge.py`): abre a fala em ADPCM antes de transcrever.
  Responde em ADPCM quando o pedido veio em ADPCM ou quando o relatório da placa
  traz `audio_formatos` com ADPCM. Também passou a registrar os tempos de cada
  etapa no log (`tempo ...: Hermes X s`, `baixar`, `transcrever`, `voz da resposta`).
- **`lylu_placa.py falar`**: usa ADPCM quando a placa sabe tocar.

## Como aplicar (ORDEM IMPORTA)

Você já mexeu nos arquivos da v2 (a correção da resposta repetida). **Não
substitua os arquivos inteiros** se tiver mudanças suas: aplique a diferença
`mudancas-v2-para-v3.patch` por cima da sua versão (ela é contra a v2 que te
mandamos). Os arquivos completos da v3 vão junto só como referência.

1. **Primeiro o relay** (`lylu_voz.py`, `lylu_bridge.py`, `lylu_placa.py`) e
   reinicie o serviço. Ele entende PCM e ADPCM, então funciona com a função
   antiga também.
2. **Depois a Edge Function** (`index.ts`):
   `supabase functions deploy lylu-bridge --project-ref ocsiyvpulcxzirzgojjv --no-verify-jwt`.
   Ela é quem anuncia o ADPCM para a placa. Se for antes do relay, a placa
   manda ADPCM e o relay velho transcreve ruído.
3. Não tem SQL novo.

## Teste

- `curl https://ocsiyvpulcxzirzgojjv.supabase.co/functions/v1/lylu-bridge` → `"version":3`
  e `"audio_formatos":["pcm_s16le_16000","ima_adpcm_16000"]`.
- A Mica fala uma frase pela placa. No log do relay, a linha `voz ...` deve dizer
  `ima_adpcm_16000`, e a resposta deve chegar mais rápido.
- Mande para ela as linhas `tempo ...` dessa conversa. Com elas a gente vê o que
  ainda demora do seu lado (transcrição, você, voz da resposta).

## Para acelerar o seu lado (opcional)

Se a linha `transcrever` passar de 2 s, troque o faster-whisper `small` por `base`
(`LYLU_STT_MODELO=base`) ou use Groq (`LYLU_STT=groq`, bem mais rápido). Se a
linha `Hermes` for a maior, vale um modelo mais rápido no perfil `lylu`.
