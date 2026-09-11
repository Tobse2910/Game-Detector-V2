# Game Detector V2 - Update-Helfer
#
# Wird vom Plugin mit Administratorrechten gestartet (UAC-Abfrage), wartet bis OBS
# beendet ist, tauscht die Plugin-Dateien aus und startet OBS wieder.
#
# Laeuft nie von Hand: das Plugin uebergibt alle drei Argumente selbst.

param(
    [Parameter(Mandatory = $true)] [string] $Zip,
    [Parameter(Mandatory = $true)] [string] $ObsDir,
    [Parameter(Mandatory = $true)] [int]    $WaitPid
)

$ErrorActionPreference = "Stop"

function Zeile($text, $farbe = "Gray") { Write-Host $text -ForegroundColor $farbe }

function Abbruch($text) {
    Zeile ""
    Zeile "  FEHLER: $text" Red
    Zeile ""
    Zeile "  Das Plugin wurde NICHT geaendert. Deine bisherige Version laeuft weiter." Gray
    Zeile "  Du kannst OBS normal starten und das Update spaeter erneut versuchen."
    Zeile ""
    Read-Host "  [Enter] zum Beenden"
    exit 1
}

Zeile ""
Zeile "  Game Detector V2 - Update" Cyan
Zeile "  =========================" Cyan
Zeile ""

# --- Argumente pruefen, bevor irgendetwas angefasst wird --------------------
if (-not (Test-Path -LiteralPath $Zip)) { Abbruch "Die heruntergeladene Datei ist nicht mehr da: $Zip" }
if (-not (Test-Path -LiteralPath (Join-Path $ObsDir "bin\64bit\obs64.exe"))) {
    Abbruch "In $ObsDir steckt kein OBS Studio."
}

# --- Auf das Ende von OBS warten -------------------------------------------
Zeile "  Warte, bis OBS Studio geschlossen ist..." Gray

$frist = (Get-Date).AddSeconds(180)
while (Get-Process -Id $WaitPid -ErrorAction SilentlyContinue) {
    if ((Get-Date) -gt $frist) {
        Abbruch "OBS Studio laeuft nach 3 Minuten noch. Bitte OBS schliessen und das Update erneut starten."
    }
    Start-Sleep -Milliseconds 500
}

# Die DLL wird erst kurz nach dem Prozessende freigegeben.
Start-Sleep -Seconds 2
Zeile "  OBS ist geschlossen." Green
Zeile ""

# --- ZIP entpacken ----------------------------------------------------------
$temp = Join-Path $env:TEMP ("game-detector-update-" + [System.Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force -Path $temp | Out-Null

try {
    Expand-Archive -LiteralPath $Zip -DestinationPath $temp -Force
} catch {
    Abbruch "Das Update-Paket liess sich nicht entpacken: $($_.Exception.Message)"
}

# Die DLL wird gesucht statt an einer festen Stelle erwartet: im Archiv liegt sie
# unter plugin\obs-plugins\64bit\, aber dieser Helfer laeuft auch noch, wenn eine
# spaetere Version das Archiv anders aufbaut.
$gefunden = Get-ChildItem -LiteralPath $temp -Filter "game-detector.dll" -Recurse -File |
            Select-Object -First 1
if (-not $gefunden) { Abbruch "Im Update-Paket fehlt game-detector.dll." }

$neueDll = $gefunden.FullName

# Die Sprachdateien liegen relativ zur DLL: <basis>\obs-plugins\64bit\game-detector.dll
# und <basis>\data\obs-plugins\game-detector
$basis = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $neueDll))
$neueDaten = Join-Path $basis "data\obs-plugins\game-detector"

if (-not (Test-Path -LiteralPath $neueDaten)) { Abbruch "Im Update-Paket fehlen die Sprachdateien." }

Zeile "  Update-Paket geprueft." Green

# --- Sicherung der laufenden Version ---------------------------------------
$zielDll = Join-Path $ObsDir "obs-plugins\64bit\game-detector.dll"
$zielDaten = Join-Path $ObsDir "data\obs-plugins\game-detector"
$sicherung = Join-Path $temp "backup-game-detector.dll"

if (Test-Path -LiteralPath $zielDll) {
    Copy-Item -LiteralPath $zielDll -Destination $sicherung -Force
}

# --- Austauschen ------------------------------------------------------------
try {
    New-Item -ItemType Directory -Force -Path $zielDaten | Out-Null
    Copy-Item -LiteralPath $neueDll -Destination $zielDll -Force
    Copy-Item -Path (Join-Path $neueDaten "*") -Destination $zielDaten -Recurse -Force
    Zeile "  Plugin aktualisiert." Green
} catch {
    $grund = $_.Exception.Message

    # Die alte DLL zurueckholen, damit kein halb aktualisierter Zustand bleibt.
    if (Test-Path -LiteralPath $sicherung) {
        try {
            Copy-Item -LiteralPath $sicherung -Destination $zielDll -Force
            Zeile "  Die vorherige Version wurde wiederhergestellt." Yellow
        } catch {
            Zeile "  Die Wiederherstellung ist ebenfalls fehlgeschlagen." Red
            Zeile "  Installiere das Plugin bitte von Hand neu: $Zip" Yellow
        }
    }

    Abbruch "Kopieren fehlgeschlagen: $grund"
}

# --- OBS wieder starten -----------------------------------------------------
Zeile ""
$obsExe = Join-Path $ObsDir "bin\64bit\obs64.exe"

try {
    # Ueber explorer.exe, damit OBS als normaler Benutzer startet und nicht mit den
    # Administratorrechten dieses Helfers weiterlaeuft.
    Start-Process -FilePath "explorer.exe" -ArgumentList "`"$obsExe`""
    Zeile "  Fertig. OBS Studio startet wieder." Green
} catch {
    Zeile "  Fertig. Bitte OBS Studio selbst starten." Yellow
}

Zeile ""
Zeile "  Das Fenster kann geschlossen werden." Gray
Start-Sleep -Seconds 5
