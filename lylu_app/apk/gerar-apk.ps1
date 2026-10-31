# Gera o APK do app da Lylu.
# O Android não aceita bem o caminho "Robô Lylu" (acento), então o embrulho
# Capacitor (projeto com android\) fica em C:\Users\windows\lylu-apk\app.
# Aqui fica só o que se edita; lá, só o que é gerado.
# Java 21 e o Android SDK são os mesmos do APK do Estudo em Caixas
# (C:\Users\windows\AndroidFerramentas, licenças já aceitas pela Noemi).
#
#   .\gerar-apk.ps1     -> Área de Trabalho\Lylu.apk (e rob-_lylu\output\lylu.apk)
# Sem ErrorActionPreference = Stop: o Gradle escreve avisos no stderr e o
# PowerShell 5.1 trataria isso como erro. Cada passo confere o $LASTEXITCODE.
$app = Split-Path $PSScriptRoot -Parent       # lylu_app
$raiz = Split-Path $app -Parent               # rob-_lylu
$embrulho = "C:\Users\windows\lylu-apk\app"
$ferramentas = "C:\Users\windows\AndroidFerramentas"
$env:JAVA_HOME = "$ferramentas\jdk"
$env:ANDROID_HOME = "$ferramentas\sdk"

# 1. O site. O vite.config.js põe a chave do app (role "app") só no build.
Set-Location $app
npx vite build
if ($LASTEXITCODE) { exit $LASTEXITCODE }

# 2. O embrulho recebe o dist/, sem o que o app não usa mais.
New-Item -ItemType Directory -Force $embrulho | Out-Null
Copy-Item "$PSScriptRoot\package.json", "$PSScriptRoot\capacitor.config.json" $embrulho -Force
Set-Location $embrulho
if (-not (Test-Path node_modules)) { npm install; if ($LASTEXITCODE) { exit $LASTEXITCODE } }
robocopy "$app\dist" "$embrulho\dist" /MIR /XF lylu.gif lylu-conversa-v2.webp /NFL /NDL /NJH /NJS /NP | Out-Null
if (-not (Test-Path android)) { npx cap add android; if ($LASTEXITCODE) { exit $LASTEXITCODE } }
npx cap sync android
if ($LASTEXITCODE) { exit $LASTEXITCODE }
python -I "$PSScriptRoot\prepara_android.py" android
if ($LASTEXITCODE) { exit $LASTEXITCODE }

# 3. Gradle. O APK de depuração já vem assinado e instala direto no celular.
Set-Location "$embrulho\android"
.\gradlew.bat assembleDebug --no-daemon
if ($LASTEXITCODE) { exit $LASTEXITCODE }
New-Item -ItemType Directory -Force "$raiz\output" | Out-Null
Copy-Item "app\build\outputs\apk\debug\app-debug.apk" "$raiz\output\lylu.apk" -Force -ErrorAction Stop
# Uma cópia na Área de Trabalho, que é onde a Noemi procura.
$mesa = [Environment]::GetFolderPath("Desktop")
Copy-Item "$raiz\output\lylu.apk" "$mesa\Lylu.apk" -Force -ErrorAction Stop
Write-Host "APK: $mesa\Lylu.apk"
