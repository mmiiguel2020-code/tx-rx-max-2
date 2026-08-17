#Requires -Version 5.1
<#
.SYNOPSIS
  Deja el ESP32 sin ningun script de FL Studio.

.DESCRIPTION
  Los scripts device_*.py que llevan "# name=ESP32-S3 MIDI Controller"
  se autoasignan al puerto en cuanto FL arranca. Varios de ellos
  devolvian MIDI a la placa en OnRefresh y eso satura el audio.

  Este script busca cualquier device_*.py que mencione el ESP32 en las
  carpetas Hardware de FL y lo mueve a una cuarentena con fecha.
  No borra nada de forma definitiva.

  Uso:
    .\esp32_sin_scripts.ps1              # revisa y pone en cuarentena
    .\esp32_sin_scripts.ps1 -SoloVer     # solo informa, no mueve nada
#>
param(
    [switch]$SoloVer
)

$ErrorActionPreference = "SilentlyContinue"

$carpetas = @(
    "$env:USERPROFILE\Documents\Image-Line\FL Studio\Settings\Hardware",
    "$env:USERPROFILE\OneDrive2\Documentos\Image-Line\FL Studio\Settings\Hardware",
    "$env:USERPROFILE\OneDrive\Documentos\Image-Line\FL Studio\Settings\Hardware",
    "$env:USERPROFILE\OneDrive\Documents\Image-Line\FL Studio\Settings\Hardware"
)

$cuarentena = Join-Path $env:USERPROFILE "Documents\tools_pc\scripts_esp32_en_cuarentena"

$encontrados = @()

foreach ($carpeta in $carpetas) {
    if (-not (Test-Path $carpeta)) { continue }
    $archivos = Get-ChildItem $carpeta -Recurse -Filter "device_*.py" -File
    foreach ($a in $archivos) {
        $texto = Get-Content $a.FullName -Raw -Encoding UTF8
        if ($texto -match "ESP32") { $encontrados += $a }
    }
}

if ($encontrados.Count -eq 0) {
    Write-Host "ESP32 limpio: ningun script de FL apunta a la placa." -ForegroundColor Green
    exit 0
}

Write-Host "ATENCION: hay $($encontrados.Count) script(s) del ESP32 en FL:" -ForegroundColor Yellow
foreach ($a in $encontrados) { Write-Host "   $($a.FullName)" }

if ($SoloVer) {
    Write-Host "(modo -SoloVer: no se movio nada)" -ForegroundColor Cyan
    exit 1
}

$destino = Join-Path $cuarentena (Get-Date -Format "yyyy-MM-dd_HH-mm")
New-Item -ItemType Directory -Path $destino -Force | Out-Null

foreach ($a in $encontrados) {
    # El nombre incluye la carpeta de origen para no pisar copias homonimas
    $etiqueta = (Split-Path $a.DirectoryName -Leaf) -replace '[^\w\-]', '_'
    $nombre = "$etiqueta__$($a.Name)"
    Move-Item $a.FullName (Join-Path $destino $nombre) -Force
    Write-Host "   movido: $($a.Name)" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "Listo. Cuarentena: $destino" -ForegroundColor Green
Write-Host "En FL: MIDI Settings -> Controller type = (none), Output ESP32 = OFF"
exit 0
