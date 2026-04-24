# ============================================
# ESP32 Thermometer - Build & Flash Script
# LILYGO T-Display S3 + DS18B20
# ============================================

$ARDUINO_CLI = "C:\Program Files\Arduino CLI\arduino-cli.exe"
$FQBN = "esp32:esp32:lilygo_t_display_s3"
$PORT = "COM4"
$SKETCH = "$PSScriptRoot\ESP32_Thermometer"
$TFT_LIB = "$env:USERPROFILE\Documents\Arduino\libraries\TFT_eSPI"

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host " ESP32 Thermometer - Build & Flash" -ForegroundColor Cyan
Write-Host " Board: LilyGo T-Display S3" -ForegroundColor Cyan
Write-Host " Port:  $PORT" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# ---- Schritt 1: Libraries installieren ----
Write-Host "[1/5] Libraries pruefen..." -ForegroundColor Yellow

$libs = @("TFT_eSPI", "OneWire", "DallasTemperature")
foreach ($lib in $libs) {
    $installed = & $ARDUINO_CLI lib list --format json 2>$null | ConvertFrom-Json
    $found = $installed.installed_libraries | Where-Object { $_.library.name -eq $lib }
    if (-not $found) {
        Write-Host "  Installiere $lib..." -ForegroundColor Gray
        & $ARDUINO_CLI lib install $lib
    } else {
        Write-Host "  $lib v$($found.library.version) OK" -ForegroundColor Green
    }
}

# ---- Schritt 2: TFT_eSPI fuer T-Display S3 konfigurieren ----
Write-Host ""
Write-Host "[2/5] TFT_eSPI konfigurieren..." -ForegroundColor Yellow

$userSetupSelect = "$TFT_LIB\User_Setup_Select.h"

if (Test-Path $userSetupSelect) {
    $content = Get-Content $userSetupSelect -Raw
    
    # Pruefen ob Setup206 bereits aktiv ist
    if ($content -match '^\s*#include\s+<User_Setups/Setup206_LilyGo_T_Display_S3\.h>' ) {
        Write-Host "  Setup206 bereits aktiv - OK" -ForegroundColor Green
    } else {
        Write-Host "  Aktiviere Setup206_LilyGo_T_Display_S3.h..." -ForegroundColor Gray
        
        # Backup erstellen
        $backupFile = "$TFT_LIB\User_Setup_Select.h.bak"
        if (-not (Test-Path $backupFile)) {
            Copy-Item $userSetupSelect $backupFile
            Write-Host "  Backup: User_Setup_Select.h.bak" -ForegroundColor Gray
        }
        
        # Standard User_Setup.h deaktivieren (auskommentieren)
        $content = $content -replace '(?m)^(\s*)(#include\s+<User_Setup\.h>)', '$1//$2'
        
        # Setup206 aktivieren (Kommentar entfernen)
        $content = $content -replace '(?m)^(\s*)//\s*(#include\s+<User_Setups/Setup206_LilyGo_T_Display_S3\.h>)', '$1$2'
        
        Set-Content $userSetupSelect -Value $content -NoNewline
        Write-Host "  Setup206 aktiviert!" -ForegroundColor Green
    }
} else {
    Write-Host "  FEHLER: TFT_eSPI nicht gefunden unter $TFT_LIB" -ForegroundColor Red
    Write-Host "  Bitte 'arduino-cli lib install TFT_eSPI' ausfuehren" -ForegroundColor Red
    exit 1
}

# ---- Schritt 3: Kompilieren ----
Write-Host ""
Write-Host "[3/5] Kompilieren..." -ForegroundColor Yellow
Write-Host "  FQBN: $FQBN" -ForegroundColor Gray
Write-Host "  Sketch: $SKETCH" -ForegroundColor Gray
Write-Host ""

& $ARDUINO_CLI compile --fqbn $FQBN $SKETCH 2>&1

if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "FEHLER beim Kompilieren!" -ForegroundColor Red
    Write-Host "Pruefe den Code und die Libraries." -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "  Kompilierung erfolgreich!" -ForegroundColor Green

# ---- Schritt 4: Upload ----
Write-Host ""
Write-Host "[4/5] Upload auf $PORT..." -ForegroundColor Yellow
Write-Host "  Falls noetig: Boot-Button gedrueckt halten und RST druecken" -ForegroundColor Gray
Write-Host ""

& $ARDUINO_CLI upload -p $PORT --fqbn $FQBN --board-options "UploadMode=default" $SKETCH 2>&1

if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "FEHLER beim Upload!" -ForegroundColor Red
    Write-Host "Pruefe:" -ForegroundColor Red
    Write-Host "  - Ist das Board an $PORT angeschlossen?" -ForegroundColor Red
    Write-Host "  - Treiber installiert?" -ForegroundColor Red
    Write-Host "  - Boot-Button gedrueckt halten + RST zum Flashen" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "  Upload erfolgreich!" -ForegroundColor Green

# ---- Schritt 5: Serial Monitor ----
Write-Host ""
Write-Host "[5/5] Starte Serial Monitor..." -ForegroundColor Yellow
Write-Host "  Druecke Ctrl+C zum Beenden" -ForegroundColor Gray
Write-Host ""

& $ARDUINO_CLI monitor -p $PORT --config baudrate=115200
