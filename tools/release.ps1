# Game Detector V2 - ein Release von Anfang bis Ende
#
#   tools\release.ps1 -Version 1.3.3
#   tools\release.ps1 -Version 1.3.3 -Notes "Was neu ist..."
#   tools\release.ps1 -Version 1.3.3 -DryRun      (alles ausser push und Release)
#
# Macht die Schritte, die sonst von Hand anfallen: Version setzen, bauen, Paket
# schnueren, pruefen, committen, taggen, Release mit dem ZIP veroeffentlichen.
#
# Gedacht ist das eigentlich der Workflow in .github/workflows/release.yml, der bei
# jedem Tag laeuft. Solange GitHub Actions fuer dieses Konto gesperrt ist (Abrechnung),
# bricht der nach wenigen Sekunden ab, ohne einen Schritt auszufuehren. Dieses Skript
# ist der Weg daran vorbei und bleibt danach fuer Zwischenstaende nuetzlich.
#
# Bricht bei jedem Fehler ab, bevor etwas veroeffentlicht wird. Ein halbes Release,
# etwa ein Tag ohne Datei daran, waere schlimmer als keins: der Update-Hinweis im
# Plugin zeigt dann auf ein Release, das niemand installieren kann.

param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^\d+\.\d+\.\d+$')]
    [string] $Version,

    [string] $Notes,
    [string] $NotesFile,
    [switch] $DryRun,
    [switch] $SkipBuild
)

$ErrorActionPreference = "Stop"

$repo = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$tag = "v$Version"
$remote = "tobse"
$ghRepo = "Tobse2910/Game-Detector-V2"

. "$PSScriptRoot\package.ps1"

function Schritt($text) { Write-Host ""; Write-Host "== $text" -ForegroundColor Cyan }
function Info($text) { Write-Host "   $text" -ForegroundColor Gray }
function Gut($text) { Write-Host "   $text" -ForegroundColor Green }
function Abbruch($text) { Write-Host ""; Write-Host "ABBRUCH: $text" -ForegroundColor Red; exit 1 }

# git und gh schreiben auch im Normalfall auf stderr, etwa "release not found" bei
# einer Pruefung, deren erwartete Antwort genau das ist. Bei ErrorActionPreference
# "Stop" wuerde PowerShell daran abbrechen, deshalb laufen externe Aufrufe hier durch
# und werden ueber ihren Rueckgabewert bewertet.
function Extern {
    param([string] $Datei, [string[]] $Argumente)

    $alt = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $ausgabe = & $Datei @Argumente 2>&1
        return [pscustomobject]@{
            Code    = $LASTEXITCODE
            Ausgabe = ($ausgabe | Out-String).Trim()
            Zeilen  = @($ausgabe | ForEach-Object { "$_" })
        }
    } finally {
        $ErrorActionPreference = $alt
    }
}

# --- Werkzeuge ---------------------------------------------------------------
Schritt "Werkzeuge pruefen"

# CMake liegt bei Visual Studio und ist nicht im PATH.
$cmake = Get-Command cmake.exe -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty Source
if (-not $cmake) {
    foreach ($kandidat in @(
        "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
        "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
        "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
        "C:\Program Files\CMake\bin\cmake.exe")) {
        if (Test-Path $kandidat) { $cmake = $kandidat; break }
    }
}
if (-not $cmake -and -not $SkipBuild) { Abbruch "cmake.exe nicht gefunden. Mit -SkipBuild laesst sich ein vorhandener Build verwenden." }
if ($cmake) { Info "cmake: $cmake" }

if (-not (Get-Command gh.exe -ErrorAction SilentlyContinue)) { Abbruch "Die GitHub-CLI (gh) fehlt." }
$auth = Extern gh @("auth", "status")
if ($auth.Code -ne 0) { Abbruch "gh ist nicht angemeldet. Erst 'gh auth login' ausfuehren." }
Info "gh angemeldet"

# --- Ausgangslage -----------------------------------------------------------
Schritt "Ausgangslage pruefen"

Push-Location $repo
try {
    $offen = (Extern git @("status", "--porcelain")).Zeilen | Where-Object { $_ }
    if ($offen -and -not $DryRun) {
        Info "Nicht committete Aenderungen:"
        $offen | ForEach-Object { Info "  $_" }
        Info "Sie werden mit der Versionsanhebung zusammen committet."
    }

    $vorhanden = (Extern git @("tag", "-l", $tag)).Zeilen | Where-Object { $_ }
    if ($vorhanden) { Abbruch "Tag $tag existiert lokal schon. Erst 'git tag -d $tag' und ggf. auf dem Remote loeschen." }

    $vorhandenesRelease = Extern gh @("release", "view", $tag, "--repo", $ghRepo)
    if ($vorhandenesRelease.Code -eq 0) { Abbruch "Release $tag gibt es auf GitHub schon." }

    Gut "Version $Version ist frei"

    # --- Version setzen ------------------------------------------------------
    Schritt "Version auf $Version setzen"

    # Geprueft wird, ob die Stelle ueberhaupt existiert, nicht ob sich der Text
    # aendert: steht die Zielversion schon drin, etwa nach einem abgebrochenen Lauf,
    # ist das in Ordnung und kein fehlender Eintrag.
    $cmakeListe = Join-Path $repo "CMakeLists.txt"
    $inhalt = Get-Content $cmakeListe -Raw
    $muster = 'project\(GameDetector VERSION \d+\.\d+\.\d+\)'
    if ($inhalt -notmatch $muster) { Abbruch "In CMakeLists.txt wurde die project()-Zeile nicht gefunden." }
    Set-Content -LiteralPath $cmakeListe -NoNewline -Value (
        [regex]::Replace($inhalt, $muster, "project(GameDetector VERSION $Version)"))
    Info "CMakeLists.txt"

    $buildspec = Join-Path $repo "buildspec.json"
    $inhalt = Get-Content $buildspec -Raw
    # Nur die Version des Plugins selbst, nicht die der Abhaengigkeiten weiter oben.
    $muster = '("displayName": "OBSGameDetector",\s*\r?\n\s*"version": ")\d+\.\d+\.\d+(")'
    if ($inhalt -notmatch $muster) { Abbruch "In buildspec.json wurde die Plugin-Version nicht gefunden." }
    Set-Content -LiteralPath $buildspec -NoNewline -Value (
        [regex]::Replace($inhalt, $muster, "`${1}$Version`${2}"))
    Info "buildspec.json"

    # --- Bauen ---------------------------------------------------------------
    if ($SkipBuild) {
        Schritt "Bauen uebersprungen (-SkipBuild)"
    } else {
        Schritt "Bauen"
        $build = Extern $cmake @("--build", (Join-Path $repo "build_x64"), "--config", "RelWithDebInfo", "--target", "GameDetector")
        $build.Zeilen | Where-Object { $_ -match 'error|fehler|\.dll$' } | ForEach-Object { Info $_.Trim() }
        if ($build.Code -ne 0) {
            $build.Zeilen | Select-Object -Last 15 | ForEach-Object { Info $_.Trim() }
            Abbruch "Der Build ist fehlgeschlagen."
        }
    }

    # --- Paket ---------------------------------------------------------------
    Schritt "Paket schnueren"

    $arbeit = Join-Path ([System.IO.Path]::GetTempPath()) ("gd-release-" + $Version + "-" + (Get-Date -Format "HHmmss"))
    New-Item -ItemType Directory -Force -Path $arbeit | Out-Null

    try {
        $dateien = New-GdPackage -Repo $repo -Version $Version -Zielordner $arbeit -Melden ${function:Info}
    } catch {
        Abbruch $_.Exception.Message
    }

    $zip = $dateien[0]

    # --- Release-Text --------------------------------------------------------
    Schritt "Release-Text"

    if ($NotesFile) {
        if (-not (Test-Path $NotesFile)) { Abbruch "Die Datei mit dem Release-Text fehlt: $NotesFile" }
        $text = Get-Content $NotesFile -Raw
        Info "aus $NotesFile"
    } elseif ($Notes) {
        $text = $Notes
        Info "aus -Notes"
    } else {
        # Aus den Commits seit dem letzten Tag, damit nie ein leeres Release entsteht.
        $beschreibung = Extern git @("describe", "--tags", "--abbrev=0")
        $letzter = if ($beschreibung.Code -eq 0) { $beschreibung.Ausgabe } else { "" }
        if ($letzter) {
            $commits = (Extern git @("log", "$letzter..HEAD", "--pretty=format:- %s")).Zeilen | Where-Object { $_ }
            Info "aus den Commits seit $letzter"
        } else {
            $commits = (Extern git @("log", "-10", "--pretty=format:- %s")).Zeilen | Where-Object { $_ }
            Info "aus den letzten Commits"
        }
        if (-not $commits) { $commits = @("- Kleinere Korrekturen") }

        $text = @"
### Installation

1. ZIP herunterladen und entpacken
2. OBS Studio komplett schliessen
3. **``Installieren.bat``** doppelklicken und die Windows-Abfrage mit Ja bestaetigen

Details stehen in ``ANLEITUNG.txt`` im ZIP.

### Aenderungen

$($commits -join "`n")

Fork von [FabioZumbi12/game-detector](https://github.com/FabioZumbi12/game-detector), GPL-2.0.
"@
    }

    $textDatei = Join-Path $arbeit "notes.md"
    Set-Content -LiteralPath $textDatei -Value $text -Encoding UTF8

    # --- Veroeffentlichen ----------------------------------------------------
    if ($DryRun) {
        Schritt "Probelauf, es wird nichts veroeffentlicht"
        Info "Version waere: $Version"
        Info "ZIP liegt in:  $zip"
        Info "Release-Text:  $textDatei"
        Info ""
        Info "Die Versionsanhebung in CMakeLists.txt und buildspec.json steht im"
        Info "Arbeitsverzeichnis. Zum Verwerfen:"
        Info "  git checkout CMakeLists.txt buildspec.json"
        Gut "Probelauf ohne Fehler"
        return
    }

    Schritt "Committen und taggen"

    $add = Extern git @("add", "-A")
    if ($add.Code -ne 0) { Abbruch "git add ist fehlgeschlagen: $($add.Ausgabe)" }

    $nochOffen = (Extern git @("status", "--porcelain")).Zeilen | Where-Object { $_ }
    if (-not $nochOffen) {
        Info "Keine Aenderungen zu committen, der Tag kommt auf den aktuellen Stand."
    } else {
        $commit = Extern git @("commit", "-q", "-m", "Release $Version")
        if ($commit.Code -ne 0) { Abbruch "git commit ist fehlgeschlagen: $($commit.Ausgabe)" }
        Info "committet"
    }

    $tagSetzen = Extern git @("tag", "-a", $tag, "-m", "Game Detector V2 $Version")
    if ($tagSetzen.Code -ne 0) { Abbruch "Der Tag liess sich nicht setzen: $($tagSetzen.Ausgabe)" }
    Info "Tag $tag gesetzt"

    Schritt "Zu GitHub schieben"

    $pushMain = Extern git @("push", $remote, "HEAD:main")
    if ($pushMain.Code -ne 0) { Abbruch "git push ist fehlgeschlagen, nichts ist veroeffentlicht: $($pushMain.Ausgabe)" }
    Info "main aktualisiert"

    $pushTag = Extern git @("push", $remote, $tag)
    if ($pushTag.Code -ne 0) { Abbruch "Der Tag liess sich nicht pushen: $($pushTag.Ausgabe)" }
    Info "Tag gepusht"

    Schritt "Release veroeffentlichen"

    $anlegen = Extern gh (@("release", "create", $tag) + $dateien + @("--repo", $ghRepo,
                           "--title", "Game Detector V2 $Version", "--notes-file", $textDatei))
    if ($anlegen.Code -ne 0) {
        Info $anlegen.Ausgabe
        Abbruch ("Das Release liess sich nicht anlegen. Tag und Commit sind schon auf GitHub. " +
                 "Nachholen mit: gh release create $tag `"$zip`" --repo $ghRepo --title `"Game Detector V2 $Version`" --notes-file `"$textDatei`"")
    }

    # --- Gegenpruefen --------------------------------------------------------
    Schritt "Gegenpruefen, was das Plugin sieht"

    # Genau der Aufruf, den UpdateChecker macht. Ohne ZIP daran bringt das Release
    # niemandem etwas: der Ein-Klick-Knopf im Dock haette nichts zu laden.
    try {
        Assert-GdReleaseSichtbar -GhRepo $ghRepo -Tag $tag -Melden ${function:Info}
    } catch {
        Abbruch $_.Exception.Message
    }

    Write-Host ""
    Gut "Release $Version ist draussen und wird vom Update-Check gefunden."
    Info "https://github.com/$ghRepo/releases/tag/$tag"
    Write-Host ""
    Info "Wer 1.2.0 oder neuer installiert hat, sieht den Hinweis beim naechsten"
    Info "OBS-Start, spaetestens nach 24 Stunden."
} finally {
    Pop-Location
}
