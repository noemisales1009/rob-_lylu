import { defineConfig, loadEnv } from "vite";
import basicSsl from "@vitejs/plugin-basic-ssl";

// No `npm run dev` o app fala com "/ponte" e o próprio Vite repassa para a
// lylu-bridge, pondo a chave no caminho. Assim a chave não vai para o navegador
// e a função não precisa de CORS. A chave fica em .env.local (fora do código).
export default defineConfig(({ mode, command }) => {
  const env = loadEnv(mode, process.cwd(), "LYLU_");
  const ponte = env.LYLU_PONTE_URL ? new URL(env.LYLU_PONTE_URL) : null;
  if (!ponte || !env.LYLU_TOKEN) {
    console.warn("\n  Falta LYLU_PONTE_URL ou LYLU_TOKEN em .env.local: o Enviar não vai funcionar.\n");
  }
  // A chave do app só vale depois que o Hermes instala o pacote do app (o GET da
  // ponte passa a ter "app": true). Até lá vai a da placa; confere a cada minuto.
  let chave = env.LYLU_TOKEN;
  let qual = "";
  const escolheChave = async () => {
    if (!ponte || !env.LYLU_TOKEN_APP) return;
    try {
      const app = (await (await fetch(ponte.href)).json()).app === true;
      chave = app ? env.LYLU_TOKEN_APP : env.LYLU_TOKEN;
      const nome = app ? "do app" : "da placa";
      if (nome !== qual) console.log(`\n  Ponte: usando a chave ${nome}.\n`);
      qual = nome;
    } catch {
      // sem rede agora: fica com a que estava
    }
  };
  if (mode === "development") {
    escolheChave();
    setInterval(escolheChave, 60_000).unref();
  }
  return {
    // No build (APK) o app fala direto com a ponte, com a chave do app (role "app":
    // só conversa com o Hermes, revogável). No dev ela nunca vai para o navegador.
    define: command === "build" ? {
      "import.meta.env.VITE_PONTE_URL": JSON.stringify(env.LYLU_PONTE_URL ?? ""),
      "import.meta.env.VITE_LYLU_TOKEN_APP": JSON.stringify(env.LYLU_TOKEN_APP ?? ""),
    } : {},
    // O celular só libera o microfone em https; o certificado é feito na hora
    // (o navegador avisa uma vez que não o conhece).
    plugins: [basicSsl()],
    server: {
      host: true, // abre também no celular, na mesma rede Wi-Fi
      proxy: ponte && {
        "/ponte": {
          target: ponte.origin,
          changeOrigin: true,
          rewrite: () => ponte.pathname,
          configure: (proxy) => proxy.on("proxyReq", (req) => req.setHeader("x-lylu-token", chave)),
        },
      },
    },
  };
});
