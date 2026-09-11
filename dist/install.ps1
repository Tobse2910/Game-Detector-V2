# Game Detector mit Smart Context Mode - Installationsskript
#
# Sucht die OBS-Installation, kopiert das Plugin hinein und meldet das Ergebnis.
# Wird von "Installieren.bat" mit Administratorrechten gestartet.

$ErrorActionPreference = "Stop"
$quelle = Split-Path -Parent $MyInvocation.MyCommand.Path

function Zeile($text, $farbe = "Gray") { Write-Host $text -ForegroundColor $farbe }

Zeile ""
Zeile "  Game Detector - Smart Context Mode" Cyan
Zeile "  ==================================" Cyan
Zeile ""

# --- Laeuft OBS noch? --------------------------------------------------------
if (Get-Process obs64 -ErrorAction SilentlyContinue) {
    Zeile "  FEHLER: OBS Studio laeuft noch." Red
    Zeile ""
    Zeile "  Bitte OBS komplett schliessen und diese Installation erneut starten."
    Zeile "  Solange OBS laeuft, ist die Plugin-Datei gesperrt."
    Zeile ""
    Read-Host "  [Enter] zum Beenden"
    exit 1
}

# --- OBS finden --------------------------------------------------------------
$obs = $null
foreach ($schluessel in @("HKLM:\SOFTWARE\OBS Studio", "HKLM:\SOFTWARE\WOW6432Node\OBS Studio")) {
    try {
        $pfad = (Get-ItemProperty -Path $schluessel -ErrorAction Stop).'(default)'
        if ($pfad -and (Test-Path "$pfad\bin\64bit\obs64.exe")) { $obs = $pfad; break }
    } catch {}
}
if (-not $obs) {
    foreach ($pfad in @("C:\Program Files\obs-studio", "C:\Program Files (x86)\obs-studio",
                        "D:\Program Files\obs-studio", "E:\Program Files\obs-studio")) {
        if (Test-Path "$pfad\bin\64bit\obs64.exe") { $obs = $pfad; break }
    }
}

if (-not $obs) {
    Zeile "  OBS Studio wurde nicht automatisch gefunden." Yellow
    Zeile ""
    Zeile "  Bitte den OBS-Ordner angeben. Das ist der Ordner, in dem"
    Zeile "  bin\64bit\obs64.exe liegt - meist:"
    Zeile "  C:\Program Files\obs-studio"
    Zeile ""
    $eingabe = (Read-Host "  OBS-Ordner").Trim('"', ' ')
    if ($eingabe -and (Test-Path "$eingabe\bin\64bit\obs64.exe")) {
        $obs = $eingabe
    } else {
        Zeile ""
        Zeile "  FEHLER: Dort liegt kein OBS Studio." Red
        Zeile ""
        Read-Host "  [Enter] zum Beenden"
        exit 1
    }
}

Zeile "  OBS gefunden: $obs" Green
Zeile ""

# --- Kopieren ----------------------------------------------------------------
try {
    $pluginZiel = Join-Path $obs "obs-plugins\64bit"
    $datenZiel = Join-Path $obs "data\obs-plugins\game-detector"

    New-Item -ItemType Directory -Force -Path $pluginZiel | Out-Null
    New-Item -ItemType Directory -Force -Path $datenZiel | Out-Null

    Copy-Item "$quelle\obs-plugins\64bit\game-detector.dll" $pluginZiel -Force
    Zeile "  [1/2] Plugin kopiert." Green

    Copy-Item "$quelle\data\obs-plugins\game-detector\*" $datenZiel -Recurse -Force
    Zeile "  [2/2] Sprachdateien kopiert." Green
} catch {
    Zeile ""
    Zeile "  FEHLER beim Kopieren: $($_.Exception.Message)" Red
    Zeile ""
    Zeile "  Laeuft diese Installation wirklich als Administrator?"
    Zeile ""
    Read-Host "  [Enter] zum Beenden"
    exit 1
}

Zeile ""
Zeile "  Installation abgeschlossen." Green
Zeile ""
Zeile "  So geht es weiter:" Cyan
Zeile ""
Zeile "   1. OBS Studio starten"
Zeile "   2. Menue 'Docks' -> 'Game Detector' anhaken"
Zeile "   3. Im Dock auf das Bildschirm-Symbol (Einstellungen):"
Zeile "        - Plattform-Aktion auf 'Kategorie ueber API' stellen"
Zeile "        - Haken bei 'Gemeinsame Berechtigungen'"
Zeile "        - 'Twitch: Verbinden' und im Browser autorisieren"
Zeile "   4. Im Dock 'Smart Context Mode' anhaken"
Zeile ""
Zeile "  Details stehen in INSTALL.md." Gray
Zeile ""
Read-Host "  [Enter] zum Beenden"
