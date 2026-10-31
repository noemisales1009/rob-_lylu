// A Lylu animada é a mesma do app do celular: copia de lylu_app o que o Lylu Foco usa.
import { copyFileSync, mkdirSync } from "node:fs";

const app = new URL("../lylu_app/", import.meta.url);
const destino = new URL("./app/compartilhado/", import.meta.url);
mkdirSync(destino, { recursive: true });
for (const [de, para] of [
  ["src/animacao.js", "animacao.js"],
  ["public/lylu-conversa-v3.webp", "lylu-conversa-v3.webp"],
  ["public/lylu-rosto.png", "lylu-rosto.png"],
]) {
  copyFileSync(new URL(de, app), new URL(para, destino));
}
// O ícone redondo (janela, barra de tarefas e bandeja).
copyFileSync(new URL("./icone/lylu.ico", import.meta.url), new URL("lylu.ico", destino));
