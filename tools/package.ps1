# Game Detector V2 - Paket schnueren und pruefen
#
# Wird von release.ps1 und release-watch.ps1 per Dot-Sourcing geladen:
#     . "$PSScriptRoot\package.ps1"
#
# Die Pruefungen hier sind aus echten Fehlern entstanden und sollen verhindern, dass
# noch einmal ein Release herausgeht, das sich nicht installieren laesst:
#
# - 1.1.0 und 1.2.0 hatten alles im Wurzelverzeichnis des Archivs, wodurch
#   Installieren.bat sein Skript nicht fand.
# - Ein Release mit einer DLL, deren Version nicht zum Tag passt, laesst den
#   Update-Hinweis bei allen Nutzern dauerhaft stehen.
# - Ein Release ohne ZIP daran gibt dem Ein-Klick-Update nichts zum Laden.

function New-GdPackage {
    param(
        [Parameter(Mandatory = $true)] [string] $Repo,
        [Parameter(Mandatory = $true)] [string] $Version,
        [Parameter(Mandatory = $true)] [string] $Zielordner,
        [scriptblock] $Melden = { param($t) Write-Host "   $t" -ForegroundColor Gray }
    )

    $dll = Join-Path $Repo "build_x64\RelWithDebInfo\game-detector.dll"
    if (-not (Test-Path $dll)) { throw "Die gebaute DLL fehlt: $dll" }

    # Die Version steckt als Zeichenkette in der DLL (GAME_DETECTOR_VERSION).
    $text = [System.Text.Encoding]::ASCII.GetString([System.IO.File]::ReadAllBytes($dll))
    if ($text -notmatch [regex]::Escape($Version)) {
        throw "Die gebaute DLL enthaelt die Version $Version nicht. Wurde nach der Versionsanhebung gebaut?"
    }
    & $Melden "DLL enthaelt $Version"

    $stage = Join-Path $Zielordner "package"
    $plugin = Join-Path $stage "plugin"

    New-Item -ItemType Directory -Force -Path (Join-Path $plugin "obs-plugins\64bit") | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $plugin "data\obs-plugins\game-detector") | Out-Null

    Copy-Item $dll (Join-Path $plugin "obs-plugins\64bit") -Force
    Copy-Item (Join-Path $Repo "data\locale") (Join-Path $plugin "data\obs-plugins\game-detector") -Recurse -Force
    Copy-Item (Join-Path $Repo "data\update.ps1") (Join-Path $plugin "data\obs-plugins\game-detector") -Force
    Copy-Item (Join-Path $Repo "dist\install.ps1"), (Join-Path $Repo "dist\uninstall.ps1"),
              (Join-Path $Repo "dist\ui.ps1") $plugin -Force
    foreach ($f in @("INSTALL.md", "README.md", "FORK-CHANGES.md")) {
        Copy-Item (Join-Path $Repo $f) $plugin -Force
    }
    # In der Wurzel nur, was angeklickt werden soll.
    Copy-Item (Join-Path $Repo "dist\Installieren.bat"), (Join-Path $Repo "dist\Deinstallieren.bat"),
              (Join-Path $Repo "dist\ANLEITUNG.txt"), (Join-Path $Repo "LICENSE") $stage -Force

    $pflicht = @(
        "Installieren.bat", "Deinstallieren.bat", "ANLEITUNG.txt", "LICENSE",
        "plugin\install.ps1", "plugin\uninstall.ps1", "plugin\ui.ps1",
        "plugin\obs-plugins\64bit\game-detector.dll",
        "plugin\data\obs-plugins\game-detector\update.ps1",
        "plugin\data\obs-plugins\game-detector\locale\de-DE.ini",
        "plugin\data\obs-plugins\game-detector\locale\en-US.ini"
    )
    foreach ($p in $pflicht) {
        if (-not (Test-Path (Join-Path $stage $p))) { throw "Im Paket fehlt: $p" }
    }
    & $Melden "$($pflicht.Count) Pflichtdateien vorhanden"

    # Die Startdateien rufen Pfade fest auf; ein Paket, in dem die Dateien nur
    # irgendwo liegen, laesst den Installer ins Leere zeigen.
    foreach ($launcher in @("Installieren.bat", "Deinstallieren.bat")) {
        $bat = Get-Content (Join-Path $stage $launcher) -Raw
        foreach ($m in [regex]::Matches($bat, "%~dp0([^`"']+)")) {
            $ziel = Join-Path $stage $m.Groups[1].Value
            if (-not (Test-Path $ziel)) {
                throw "$launcher ruft '$($m.Groups[1].Value)' auf, das im Paket fehlt."
            }
            & $Melden "$launcher -> $($m.Groups[1].Value)"
        }
    }

    $zip = Join-Path $Zielordner "Game-Detector-V2-v$Version.zip"
    if (Test-Path $zip) { Remove-Item -LiteralPath $zip -Force }
    Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $zip
    & $Melden ("ZIP fertig, " + [math]::Round((Get-Item $zip).Length / 1KB) + " KB")

    return $zip
}

# Prueft, was das Plugin beim Update-Check tatsaechlich sieht. Wirft, wenn das
# Release fuer die Nutzer nicht brauchbar waere.
function Assert-GdReleaseSichtbar {
    param(
        [Parameter(Mandatory = $true)] [string] $GhRepo,
        [Parameter(Mandatory = $true)] [string] $Tag,
        [scriptblock] $Melden = { param($t) Write-Host "   $t" -ForegroundColor Gray }
    )

    $alt = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $antwort = & gh api "repos/$GhRepo/releases/latest" 2>&1
        $code = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $alt
    }

    if ($code -ne 0) { throw "Die Release-Abfrage ist fehlgeschlagen: $(($antwort | Out-String).Trim())" }

    $daten = ($antwort | Out-String) | ConvertFrom-Json
    $zips = @($daten.assets | Where-Object { $_.name -like "*.zip" } | ForEach-Object { $_.name })

    & $Melden "neuestes Release: $($daten.tag_name)"
    & $Melden "Vorabversion:     $($daten.prerelease)"
    & $Melden "ZIP daran:        $($zips -join ', ')"

    if ($daten.tag_name -ne $Tag) { throw "GitHub liefert '$($daten.tag_name)' als neuestes Release, nicht $Tag." }
    if ($daten.prerelease) { throw "Das Release ist als Vorabversion markiert; der Update-Check ueberspringt es." }
    if (-not $zips) { throw "Am Release haengt kein ZIP; der Update-Knopf haette nichts zu laden." }
}
