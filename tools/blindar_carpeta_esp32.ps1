#Requires -Version 5.1
<#
.SYNOPSIS
  Impide que se creen scripts nuevos en la carpeta del ESP32 de FL Studio.

.DESCRIPTION
  Deniega por permisos de Windows la creacion de archivos y subcarpetas
  dentro de Hardware\ESP32-S3 MIDI Controller. FL solo necesita LEER esa
  carpeta, asi que sigue funcionando igual.

  Asi ningun programa (ni un agente, ni la restauracion de un respaldo)
  puede volver a dejar ahi un device_*.py que se autoasigne al puerto
  y devuelva MIDI a la placa.

  Uso:
    .\blindar_carpeta_esp32.ps1            # blindar
    .\blindar_carpeta_esp32.ps1 -Quitar    # volver a como estaba
    .\blindar_carpeta_esp32.ps1 -Estado    # solo ver como esta
#>
param(
    [switch]$Quitar,
    [switch]$Estado
)

$ErrorActionPreference = "Continue"

$carpetas = @(
    "$env:USERPROFILE\Documents\Image-Line\FL Studio\Settings\Hardware\ESP32-S3 MIDI Controller",
    "$env:USERPROFILE\OneDrive2\Documentos\Image-Line\FL Studio\Settings\Hardware\ESP32-S3 MIDI Controller"
)

$cuenta = "$env:USERDOMAIN\$env:USERNAME"

foreach ($carpeta in $carpetas) {
    if (-not (Test-Path $carpeta)) {
        Write-Host "no existe: $carpeta" -ForegroundColor DarkGray
        continue
    }

    Write-Host ""
    Write-Host "=== $carpeta" -ForegroundColor Cyan

    if ($Estado) {
        # WD = crear archivos, AD = crear carpetas
        $acl = icacls $carpeta 2>$null
        $denegado = $acl | Where-Object { $_ -match "\(DENY\)" -or $_ -match "\(N\)" }
        if ($denegado) {
            Write-Host "BLINDADA:" -ForegroundColor Green
            $denegado | ForEach-Object { Write-Host "   $_" }
        } else {
            Write-Host "sin blindaje (se pueden crear archivos)" -ForegroundColor Yellow
        }
        continue
    }

    if ($Quitar) {
        icacls $carpeta /remove:d "$cuenta" | Out-Null
        Write-Host "blindaje retirado" -ForegroundColor Yellow
    } else {
        # Solo sobre la carpeta: no hereda, para no bloquear los archivos ya existentes
        icacls $carpeta /deny "${cuenta}:(WD,AD)" | Out-Null
        Write-Host "blindada: no se pueden crear archivos ni subcarpetas" -ForegroundColor Green
    }
}

Write-Host ""
Write-Host "Recuerda: FL solo LEE esta carpeta, sigue funcionando igual."
