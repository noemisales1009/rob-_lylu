# Compila (e opcionalmente grava) a Lylu na ESP32-P4.
# O ESP-IDF não aceita o caminho "Robô Lylu" (espaço + acento), então o
# código é espelhado em C:\esp\lylu_p4 e compilado de lá.
#
#   .\compilar.ps1            -> só compila
#   .\compilar.ps1 gravar     -> compila e grava na placa (COM13)
param([string]$acao = "", [string]$porta = "COM13")

$origem = $PSScriptRoot
$destino = "C:\esp\lylu_p4"

robocopy $origem $destino /MIR /XD build managed_components /XF sdkconfig sdkconfig.old dependencies.lock /NFL /NDL /NJH /NJS /NP | Out-Null
. C:\esp\esp-idf-v5.5.4\export.ps1 *> $null
Set-Location $destino

# O sdkconfig nasce do sdkconfig.defaults, mas o ESP-IDF só olha os defaults
# quando o sdkconfig ainda não existe. Então, se os defaults mudaram, o antigo é
# jogado fora para as opções novas valerem.
# A comparação é pelo CONTEÚDO, não pela data: o robocopy mexe na data sem o
# arquivo ter mudado, e aí toda compilação virava uma recompilação inteira.
$config = "$destino\sdkconfig"
$padroes = "$destino\sdkconfig.defaults"
$marca = "$destino\build\sdkconfig.defaults.usado"
if (Test-Path $padroes) {
    $somaAgora = (Get-FileHash $padroes -Algorithm SHA256).Hash
    $somaAntes = if (Test-Path $marca) { (Get-Content $marca -Raw).Trim() } else { "" }
    if ((Test-Path $config) -and ($somaAntes -ne $somaAgora)) {
        Write-Host "sdkconfig.defaults mudou: refazendo o sdkconfig"
        Remove-Item $config
    }
}
if (-not (Test-Path $config)) { idf.py set-target esp32p4; if ($LASTEXITCODE) { exit $LASTEXITCODE } }
if (Test-Path $padroes) {
    New-Item -ItemType Directory -Force "$destino\build" | Out-Null
    (Get-FileHash $padroes -Algorithm SHA256).Hash | Out-File $marca -Encoding utf8
}
if ($acao -eq "gravar") { idf.py -p $porta flash } else { idf.py build }
exit $LASTEXITCODE
