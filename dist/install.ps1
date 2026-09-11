# Game Detector mit Smart Context Mode - Installationsskript
#
# Sucht die OBS-Installation, kopiert das Plugin hinein und meldet das Ergebnis.
# Wird von "Installieren.bat" mit Administratorrechten gestartet.

# -ObsDir ueberspringt die Suche. Gedacht fuer ungewoehnliche Installationen und
# fuer Tests gegen einen anderen Ordner als das eigene OBS.
param([string] $ObsDir)

$ErrorActionPreference = "Stop"
$quelle = Split-Path -Parent $MyInvocation.MyCommand.Path

. "$quelle\ui.ps1"

Start-GdFenster -Titel "Game Detector wird installiert" -Schritte 3

# Nur das OBS im Zielordner blockiert. Wer mehrere Installationen hat, soll nicht
# aufgehalten werden, weil irgendeine andere laeuft. Laesst sich der Pfad eines
# Prozesses nicht lesen, gilt er vorsichtshalber als das gesuchte.
function Test-ObsLaeuft {
    param([string] $Ordner)

    $prozesse = @(Get-Process obs64 -ErrorAction SilentlyContinue)
    if ($prozesse.Count -eq 0) { return $false }
    if (-not $Ordner) { return $true }

    foreach ($p in $prozesse) {
        $pfad = $null

        # Process.Path ist bei einem elevated OBS aus einer normalen Shell nicht
        # lesbar; WMI liefert ihn trotzdem.
        try { $pfad = $p.Path } catch {}
        if (-not $pfad) {
            try {
                $pfad = (Get-CimInstance Win32_Process -Filter "ProcessId=$($p.Id)" -ErrorAction Stop).ExecutablePath
            } catch {}
        }

        # Unbekannter Pfad: vorsichtshalber als das gesuchte OBS behandeln, statt in
        # eine gesperrte Datei zu schreiben.
        if (-not $pfad) { return $true }
        if ($pfad.StartsWith($Ordner, [StringComparison]::OrdinalIgnoreCase)) { return $true }
    }

    return $false
}

# --- OBS finden --------------------------------------------------------------
Set-GdStatus "OBS Studio wird gesucht..." "" 0

$obs = $null
if ($ObsDir -and (Test-Path (Join-Path $ObsDir "bin\64bit\obs64.exe"))) { $obs = $ObsDir }

foreach ($schluessel in @("HKLM:\SOFTWARE\OBS Studio", "HKLM:\SOFTWARE\WOW6432Node\OBS Studio")) {
    if ($obs) { break }
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
    Set-GdStatus "OBS Studio wurde nicht gefunden." (
        "Bitte den OBS-Ordner auswaehlen. Das ist der Ordner, in dem bin\64bit\obs64.exe liegt, " +
        "meist C:\Program Files\obs-studio") 0

    $eingabe = Select-GdOrdner -Beschreibung "OBS-Ordner auswaehlen (enthaelt bin\64bit\obs64.exe)"

    if ($eingabe -and (Test-Path "$eingabe\bin\64bit\obs64.exe")) {
        $obs = $eingabe
    } else {
        Stop-GdFenster -Fehler -Status "Dort liegt kein OBS Studio." -Detail (
            "Im gewaehlten Ordner fehlt bin\64bit\obs64.exe. Es wurde nichts geaendert.")
        exit 1
    }
}

Set-GdStatus "OBS gefunden." $obs 1

# Erst jetzt, weil dafuer bekannt sein muss, welches OBS ueberhaupt gemeint ist.
if (Test-ObsLaeuft -Ordner $obs) {
    Stop-GdFenster -Fehler -Status "OBS Studio laeuft noch." -Detail (
        "Bitte OBS komplett schliessen und die Installation erneut starten. " +
        "Solange OBS laeuft, ist die Plugin-Datei gesperrt und kann nicht ersetzt werden.")
    exit 1
}

# --- Kopieren ----------------------------------------------------------------
Set-GdStatus "Plugin wird kopiert..." "" 1

try {
    $pluginZiel = Join-Path $obs "obs-plugins\64bit"
    $datenZiel = Join-Path $obs "data\obs-plugins\game-detector"

    New-Item -ItemType Directory -Force -Path $pluginZiel | Out-Null
    New-Item -ItemType Directory -Force -Path $datenZiel | Out-Null

    Copy-Item "$quelle\obs-plugins\64bit\game-detector.dll" $pluginZiel -Force
    Set-GdStatus "Sprachdateien werden kopiert..." "" 2

    # Unterordner einzeln, weil "Copy-Item quelle\* ziel -Recurse" einen bereits
    # vorhandenen Unterordner im Ziel verschachtelt statt seinen Inhalt zu ersetzen.
    foreach ($eintrag in Get-ChildItem -LiteralPath "$quelle\data\obs-plugins\game-detector") {
        $ziel = Join-Path $datenZiel $eintrag.Name
        if ($eintrag.PSIsContainer) {
            New-Item -ItemType Directory -Force -Path $ziel | Out-Null
            Copy-Item -Path (Join-Path $eintrag.FullName "*") -Destination $ziel -Recurse -Force
        } else {
            Copy-Item -LiteralPath $eintrag.FullName -Destination $ziel -Force
        }
    }
} catch {
    Stop-GdFenster -Fehler -Status "Kopieren fehlgeschlagen." -Detail (
        "$($_.Exception.Message) Laeuft diese Installation wirklich als Administrator?")
    exit 1
}

# --- Fertig ------------------------------------------------------------------
Stop-GdFenster -Status "Installation abgeschlossen." -Detail (
    "So geht es weiter:" + [Environment]::NewLine +
    "1. OBS Studio starten" + [Environment]::NewLine +
    "2. Menue Docks, Haken bei Game Detector" + [Environment]::NewLine +
    "3. Im Dock auf das Bildschirm-Symbol: Plattform-Aktion auf Kategorie ueber API, " +
    "Haken bei Gemeinsame Berechtigungen, dann Twitch verbinden" + [Environment]::NewLine +
    "4. Im Dock Haken bei Smart Context Mode" + [Environment]::NewLine + [Environment]::NewLine +
    "Details stehen in ANLEITUNG.txt.")
