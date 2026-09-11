# Game Detector mit Smart Context Mode - Deinstallation
#
# Entfernt Plugin und Sprachdateien aus der OBS-Installation.
# Die eigenen Einstellungen bleiben erhalten, falls du spaeter neu installierst.

# -ObsDir ueberspringt die Suche. Gedacht fuer ungewoehnliche Installationen und
# fuer Tests gegen einen anderen Ordner als das eigene OBS.
param([string] $ObsDir)

$ErrorActionPreference = "Stop"
$quelle = Split-Path -Parent $MyInvocation.MyCommand.Path

. "$quelle\ui.ps1"

Start-GdFenster -Titel "Game Detector wird entfernt" -Schritte 2

# Nur das OBS im Zielordner blockiert; siehe install.ps1.
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
    Set-GdStatus "OBS Studio wurde nicht gefunden." "Bitte den OBS-Ordner auswaehlen." 0
    $obs = Select-GdOrdner -Beschreibung "OBS-Ordner auswaehlen (enthaelt bin\64bit\obs64.exe)"
}

if (-not $obs -or -not (Test-Path (Join-Path $obs "bin\64bit\obs64.exe"))) {
    Stop-GdFenster -Fehler -Status "Kein OBS Studio gefunden." -Detail "Es wurde nichts geaendert."
    exit 1
}

if (Test-ObsLaeuft -Ordner $obs) {
    Stop-GdFenster -Fehler -Status "OBS Studio laeuft noch." -Detail (
        "Bitte OBS komplett schliessen und die Deinstallation erneut starten. " +
        "Solange OBS laeuft, ist die Plugin-Datei gesperrt.")
    exit 1
}

Set-GdStatus "Plugin wird entfernt..." $obs 1

$dll = Join-Path $obs "obs-plugins\64bit\game-detector.dll"
$daten = Join-Path $obs "data\obs-plugins\game-detector"

$entfernt = $false
try {
    if (Test-Path $dll) { Remove-Item $dll -Force; $entfernt = $true }
    if (Test-Path $daten) { Remove-Item $daten -Recurse -Force; $entfernt = $true }
} catch {
    Stop-GdFenster -Fehler -Status "Entfernen fehlgeschlagen." -Detail (
        "$($_.Exception.Message) Laeuft diese Deinstallation wirklich als Administrator?")
    exit 1
}

if ($entfernt) {
    Stop-GdFenster -Status "Deinstallation abgeschlossen." -Detail (
        "Deine Einstellungen bleiben erhalten unter:" + [Environment]::NewLine +
        "$env:APPDATA\obs-studio\plugin_config\game-detector" + [Environment]::NewLine + [Environment]::NewLine +
        "Wenn du das Plugin spaeter neu installierst, sind deine Regeln und die " +
        "Twitch-Verbindung wieder da.")
} else {
    Stop-GdFenster -Status "Es war nichts installiert." -Detail (
        "In $obs wurde kein Game Detector gefunden.")
}
