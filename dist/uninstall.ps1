# Game Detector mit Smart Context Mode - Deinstallation
#
# Entfernt Plugin und Sprachdateien aus der OBS-Installation.
# Die eigenen Einstellungen bleiben erhalten, falls du spaeter neu installierst.

$ErrorActionPreference = "Stop"

function Zeile($text, $farbe = "Gray") { Write-Host $text -ForegroundColor $farbe }

Zeile ""
Zeile "  Game Detector - Smart Context Mode deinstallieren" Cyan
Zeile "  ================================================" Cyan
Zeile ""

if (Get-Process obs64 -ErrorAction SilentlyContinue) {
    Zeile "  FEHLER: OBS Studio laeuft noch." Red
    Zeile "  Bitte OBS schliessen und erneut starten."
    Zeile ""
    Read-Host "  [Enter] zum Beenden"
    exit 1
}

$obs = $null
foreach ($schluessel in @("HKLM:\SOFTWARE\OBS Studio", "HKLM:\SOFTWARE\WOW6432Node\OBS Studio")) {
    try {
        $pfad = (Get-ItemProperty -Path $schluessel -ErrorAction Stop).'(default)'
        if ($pfad -and (Test-Path "$pfad\bin\64bit\obs64.exe")) { $obs = $pfad; break }
    } catch {}
}
if (-not $obs) {
    foreach ($pfad in @("C:\Program Files\obs-studio", "C:\Program Files (x86)\obs-studio")) {
        if (Test-Path "$pfad\bin\64bit\obs64.exe") { $obs = $pfad; break }
    }
}
if (-not $obs) {
    $obs = (Read-Host "  OBS-Ordner").Trim('"', ' ')
}

$dll = Join-Path $obs "obs-plugins\64bit\game-detector.dll"
$daten = Join-Path $obs "data\obs-plugins\game-detector"

$entfernt = $false
if (Test-Path $dll) { Remove-Item $dll -Force; Zeile "  Plugin entfernt." Green; $entfernt = $true }
if (Test-Path $daten) { Remove-Item $daten -Recurse -Force; Zeile "  Sprachdateien entfernt." Green; $entfernt = $true }

Zeile ""
if ($entfernt) {
    Zeile "  Deinstallation abgeschlossen." Green
    Zeile ""
    Zeile "  Deine Einstellungen bleiben erhalten unter:" Gray
    Zeile "  $env:APPDATA\obs-studio\plugin_config\game-detector\" Gray
} else {
    Zeile "  Es war nichts installiert." Yellow
}
Zeile ""
Read-Host "  [Enter] zum Beenden"
