// O que a página pode pedir para a janela. Nada além disto chega no Node.
const { contextBridge, ipcRenderer } = require("electron");

contextBridge.exposeInMainWorld("lylu", {
  tamanho: (w, h) => ipcRenderer.send("tamanho", w, h),
  telaCheia: (sim) => ipcRenderer.send("tela-cheia", sim),
  progresso: (fracao, modo) => ipcRenderer.send("progresso", fracao, modo),
  esconder: () => ipcRenderer.send("esconder"),
  fechar: () => ipcRenderer.send("fechar"),
  paradoHa: () => ipcRenderer.invoke("parado-ha"),
  abreComWindows: () => ipcRenderer.invoke("abre-com-windows"),
  defineAbreComWindows: (sim) => ipcRenderer.send("abre-com-windows", sim),
});
