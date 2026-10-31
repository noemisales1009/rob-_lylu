// Fotos do Lylu Foco sem mostrar a janela, com dados à parte, sem som e sem
// notificação do Windows (o main desliga os dois no modo teste):
//   LYLU_TESTE=<pasta> LYLU_TESTE_PARADO=120 npx electron .
const fs = require("node:fs");
const path = require("node:path");
const { app } = require("electron");

module.exports = (janela, pasta) => {
  const wc = janela.webContents;
  const espera = (ms) => new Promise((r) => setTimeout(r, ms));
  const js = (s) => wc.executeJavaScript(s, true);
  const recarrega = () => new Promise((r) => { wc.once("did-finish-load", r); wc.reload(); });
  const foto = async (nome) => {
    await espera(900);
    const img = await wc.capturePage();
    fs.writeFileSync(path.join(pasta, `${nome}.png`), img.toPNG());
    const b = janela.getBounds();
    console.log(`${nome}: janela ${b.width}x${b.height} em ${b.x},${b.y}`);
  };
  wc.on("console-message", (e, nivel, msg) => {
    const n = e.level ?? nivel;
    if (n === "error" || n === "warning" || n >= 2) console.log("console:", e.message ?? msg);
  });

  wc.once("did-finish-load", async () => {
    try {
      await js("localStorage.clear(); localStorage.setItem('lylu_foco_recentes', JSON.stringify(['Relatório do projeto', 'Estudar SQL', 'Responder e-mails']))");
      await recarrega();
      await espera(1500);
      console.log("silencio:", JSON.stringify({ notificacao: await js("Notification.permission"), mudo: wc.isAudioMuted() }));
      console.log("vozes (s):", JSON.stringify(await js(`Promise.all(['sons/pausa_5min.wav', 'sons/pausa_20min.wav', 'sons/pausa_fim.wav']
        .map((s) => new Promise((r) => { const a = new Audio(s); a.onloadedmetadata = () => r(+a.duration.toFixed(2)); a.onerror = () => r('erro'); })))`)));
      await foto("1-escolher");
      await js("document.getElementById('tarefa').value = 'Relatório do projeto'; document.getElementById('f-tarefa').requestSubmit()");
      await espera(800);
      await foto("2-foco-comecando");
      await espera(4000);
      await foto("3-foco");
      await js("document.getElementById('b-principal').click()");
      await foto("4-foco-pausado");
      await js("document.getElementById('b-ajustes').click()");
      await foto("5-ajustes");
      await js("document.getElementById('b-pronto').click()");
      await js(`localStorage.setItem('lylu_foco_sessao', JSON.stringify({ passo: 'pausa', tarefa: 'Relatório do projeto',
        bloco: 1, pausaLonga: false, relogio: { total: 300, resto: 212, fim: 0 } }))`);
      await recarrega();
      await espera(1200);
      await js("document.querySelectorAll('#cuidados li')[0].click()");
      await foto("6-pausa-tela-cheia");
      // concluir a tarefa na pausa: a pausa continua
      await js("document.getElementById('b-concluir').click()");
      await foto("6b-pausa-concluida");
      console.log("depois de concluir na pausa:", JSON.stringify(await js(`({
        link: !document.getElementById('b-concluir').hidden,
        tarefa: document.getElementById('tarefa-atual').textContent,
        fala: document.getElementById('fala').textContent })`)));
      // toque rápido no "Segure 3 s": não sai, e a Lylu explica que é para segurar
      await js("const b = document.getElementById('b-segura'); b.dispatchEvent(new PointerEvent('pointerdown')); b.dispatchEvent(new PointerEvent('pointerup'))");
      console.log("toque rapido:", JSON.stringify(await js(`({ fala: document.getElementById('fala').textContent,
        aindaNaPausa: document.body.classList.contains('cheia') })`)));
      // segurar 3 s: sai da pausa e, como concluiu, vai escolher a próxima
      await js("document.getElementById('b-segura').dispatchEvent(new PointerEvent('pointerdown'))");
      await espera(3400);
      await foto("7-depois-de-segurar");
      const conta = () => js(`({ recentes: document.querySelectorAll('#recentes li').length,
        feitas: document.querySelectorAll('#feitas li').length, passo: !document.getElementById('t-escolher').hidden,
        rodape: document.getElementById('blocos').textContent })`);
      console.log("antes de apagar:", JSON.stringify(await conta()));
      await js("document.querySelector('#recentes li .apaga').click()");
      await js("document.querySelector('#feitas li .apaga').click()");
      console.log("depois de apagar:", JSON.stringify(await conta()));
      await foto("7b-depois-de-apagar");
      // os últimos 10 s do foco: o aviso antes de cobrir a tela
      await js(`localStorage.setItem('lylu_foco_sessao', JSON.stringify({ passo: 'foco', tarefa: 'Relatório do projeto',
        bloco: 1, pausaLonga: false, relogio: { total: 1500, resto: 9, fim: Date.now() + 9000 } }))`);
      await recarrega();
      await espera(1500);
      await foto("8-aviso-10s");
      await espera(9000);
      await foto("9-pausa-sozinha");
      // a pausa acaba com ela longe (rode com LYLU_TESTE_PARADO=120): o foco espera por ela
      await js(`localStorage.setItem('lylu_foco_sessao', JSON.stringify({ passo: 'pausa', tarefa: 'Relatório do projeto',
        bloco: 1, pausaLonga: false, relogio: { total: 300, resto: 2, fim: Date.now() + 2000 } }))`);
      await recarrega();
      await espera(3500);
      console.log("pausa acabou com ela longe:", JSON.stringify(await js(`({ fala: document.getElementById('fala').textContent,
        botao: document.getElementById('b-principal').textContent, cheia: document.body.classList.contains('cheia') })`)));
      await foto("10-esperando-voltar");
      // o tempo acabou com o PC dormindo ou o programa fechado
      await js(`localStorage.setItem('lylu_foco_sessao', JSON.stringify({ passo: 'foco', tarefa: 'Relatório do projeto',
        bloco: 1, pausaLonga: false, relogio: { total: 1500, resto: 0, fim: Date.now() - 300000 } }))`);
      await recarrega();
      await espera(1500);
      console.log("acabou com ela fora:", JSON.stringify(await js(`({ fala: document.getElementById('fala').textContent,
        escolher: !document.getElementById('t-escolher').hidden, cheia: document.body.classList.contains('cheia') })`)));
      await foto("11-acabou-longe");
    } catch (e) {
      console.error("FALHOU", e);
    }
    app.quit();
  });
};
