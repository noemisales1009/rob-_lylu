// Monta o programa "Lylu Foco" e põe o atalho na Área de Trabalho.
//   npm run gerar
// O programa fica em C:\Users\windows\lylu-pc (fora do repositório, sem acento no
// caminho); aqui fica só o código.
import { execFileSync } from "node:child_process";
import { packager } from "@electron/packager";
import { createRequire } from "node:module";

const require = createRequire(import.meta.url);
const DESTINO = "C:\\Users\\windows\\lylu-pc";

execFileSync(process.execPath, ["copia.mjs"], { stdio: "inherit" });

const [pasta] = await packager({
  dir: ".",
  name: "Lylu Foco",
  executableName: "Lylu Foco",
  platform: "win32",
  arch: "x64",
  electronVersion: require("electron/package.json").version,
  out: DESTINO,
  overwrite: true,
  asar: true,
  icon: "icone/lylu.ico",
  // Só o app/ e o package.json: nada de ferramentas, ícones de origem ou teste.
  ignore: [/^\/node_modules/, /^\/icone/, /^\/copia\.mjs$/, /^\/gerar\.mjs$/, /^\/app\/teste\.js$/],
  win32metadata: { CompanyName: "Noemi", FileDescription: "Lylu Foco", ProductName: "Lylu Foco" },
});

const exe = `${pasta}\\Lylu Foco.exe`;
const atalho = `
$s = (New-Object -ComObject WScript.Shell).CreateShortcut([Environment]::GetFolderPath("Desktop") + "\\Lylu Foco.lnk")
$s.TargetPath = "${exe}"
$s.WorkingDirectory = "${pasta}"
$s.IconLocation = "${exe},0"
$s.Description = "O Pomodoro da Lylu"
$s.Save()`;
execFileSync("powershell.exe", ["-NoProfile", "-Command", atalho], { stdio: "inherit" });
console.log(`Pronto: ${exe}\nAtalho "Lylu Foco" na Área de Trabalho.`);
