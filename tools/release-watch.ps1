# Game Detector V2 - fehlende Releases nachziehen
#
#   tools\release-watch.ps1              einmal pruefen und nachziehen
#   tools\release-watch.ps1 -DryRun      nur zeigen, was zu tun waere
#   tools\release-watch.ps1 -Einrichten  als Windows-Aufgabe registrieren
#   tools\release-watch.ps1 -Entfernen   Aufgabe wieder loeschen
#
# Das ist der Ersatz fuer .github/workflows/release.yml, solange GitHub Actions fuer
# dieses Konto gesperrt ist. Die Sperre kommt nicht von Actions selbst: das Repo ist
# oeffentlich, und fuer oeffentliche Repos sind Standard-Runner kostenlos und
# unbegrenzt. Es ist eine andere offene Rechnung im Konto, die alle Jobs blockiert.
#
# Ein eigener Runner waere die naheliegende Alternative und ebenfalls kostenlos, ist
# an einem oeffentlichen Repo aber gefaehrlich: ueber einen Pull Request kann ein
# Fremder Code auf dem Rechner ausfuehren lassen, auf dem der Runner laeuft. Deshalb
# dieser Weg, der ausschliesslich eigenen Code ausfuehrt.
#
# Ablauf: Tags holen, jeden Tag ohne Release bauen und veroeffentlichen. Wer also
# einen Tag pusht, bekommt sein Release, sobald dieser Rechner laeuft.

param(
    [switch] $DryRun,
    [switch] $Einrichten,
    [switch] $Entfernen,
    [int] $IntervallMinuten = 15
)

$ErrorActionPreference = "Stop"

$repo = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$remote = "tobse"
$ghRepo = "Tobse2910/Game-Detector-V2"
$aufgabe = "GameDetectorV2-ReleaseWatch"

. "$PSScriptRoot\package.ps1"

# Als geplante Aufgabe laeuft das Skript versteckt. Ohne Protokoll waere nicht zu
# sehen, ob es ueberhaupt lief, geschweige denn warum es abgebrochen hat.
$logDatei = Join-Path $env:LOCALAPPDATA "game-detector-release-watch.log"

function Protokoll($zeile) {
    try {
        $stempel = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
        Add-Content -LiteralPath $logDatei -Value "$stempel  $zeile" -Encoding UTF8
    } catch {}
}

function Schritt($text) { Write-Host ""; Write-Host "== $text" -ForegroundColor Cyan; Protokoll "== $text" }
function Info($text) { Write-Host "   $text" -ForegroundColor Gray; Protokoll "   $text" }
function Gut($text) { Write-Host "   $text" -ForegroundColor Green; Protokoll "   $text" }
function Warnung($text) { Write-Host "   $text" -ForegroundColor Yellow; Protokoll "   ACHTUNG: $text" }

# Das Protokoll nicht unbegrenzt wachsen lassen.
try {
    if ((Test-Path $logDatei) -and (Get-Item $logDatei).Length -gt 512KB) {
        $behalten = Get-Content $logDatei -Tail 400
        Set-Content -LiteralPath $logDatei -Value $behalten -Encoding UTF8
    }
} catch {}

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

# --- Windows-Aufgabe --------------------------------------------------------
if ($Einrichten) {
    Schritt "Als Windows-Aufgabe registrieren"

    # schtasks.exe und nicht Register-ScheduledTask: letzteres antwortet auf diesem
    # System mit "Zugriff verweigert", auch fuer eine Aufgabe im eigenen
    # Benutzerkontext. schtasks kommt ohne Administratorrechte aus.
    $skript = Join-Path $PSScriptRoot "release-watch.ps1"
    $befehl = "powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File \`"$skript\`""

    $anlegen = Extern schtasks.exe @("/create", "/tn", $aufgabe, "/tr", $befehl,
                                     "/sc", "MINUTE", "/mo", "$IntervallMinuten", "/f")
    if ($anlegen.Code -ne 0) { Warnung "Die Aufgabe liess sich nicht anlegen: $($anlegen.Ausgabe)"; exit 1 }

    Gut "Aufgabe '$aufgabe' eingerichtet, Pruefung alle $IntervallMinuten Minuten"
    Info "Protokoll: $logDatei"
    Info "Sofort ausfuehren: schtasks /run /tn $aufgabe"
    Info "Entfernen:         tools\release-watch.ps1 -Entfernen"
    exit 0
}

if ($Entfernen) {
    Schritt "Windows-Aufgabe entfernen"
    $abfrage = Extern schtasks.exe @("/query", "/tn", $aufgabe)
    if ($abfrage.Code -ne 0) { Info "Es war keine Aufgabe eingerichtet."; exit 0 }

    $loeschen = Extern schtasks.exe @("/delete", "/tn", $aufgabe, "/f")
    if ($loeschen.Code -ne 0) { Warnung "Die Aufgabe liess sich nicht loeschen: $($loeschen.Ausgabe)"; exit 1 }
    Gut "Aufgabe entfernt"
    exit 0
}

# --- Pruefen ----------------------------------------------------------------
Push-Location $repo
try {
    Schritt "Tags und Releases vergleichen"

    $hol = Extern git @("fetch", $remote, "--tags", "--quiet")
    if ($hol.Code -ne 0) { Warnung "git fetch fehlgeschlagen: $($hol.Ausgabe)"; exit 1 }

    # Nur Tags dieses Forks. Vom Upstream haengen 28 Tags v0.0.1 bis v0.2.5 mit im
    # Repo, fuer die es hier nie ein Release gab und auch keins geben soll.
    $tags = (Extern git @("tag", "-l", "v*", "--sort=-v:refname")).Zeilen |
            Where-Object { $_ -match '^v(\d+)\.\d+\.\d+$' -and [int]$Matches[1] -ge 1 }
    if (-not $tags) { Info "Keine Versions-Tags dieses Forks vorhanden."; exit 0 }

    $releaseAbfrage = Extern gh @("release", "list", "--repo", $ghRepo, "--limit", "100", "--json", "tagName")
    if ($releaseAbfrage.Code -ne 0) { Warnung "Die Release-Liste kam nicht: $($releaseAbfrage.Ausgabe)"; exit 1 }

    $vorhandene = @()
    if ($releaseAbfrage.Ausgabe) {
        $vorhandene = @(($releaseAbfrage.Ausgabe | ConvertFrom-Json) | ForEach-Object { $_.tagName })
    }

    Info "Tags:     $($tags.Count)"
    Info "Releases: $($vorhandene.Count)"

    $fehlend = @($tags | Where-Object { $vorhandene -notcontains $_ })

    if (-not $fehlend) {
        Gut "Jeder Tag hat sein Release, nichts zu tun."
        exit 0
    }

    Warnung "Ohne Release: $($fehlend -join ', ')"

    if ($DryRun) {
        Info "Probelauf, es wird nichts gebaut oder veroeffentlicht."
        exit 0
    }

    # --- Nachziehen ---------------------------------------------------------
    # Nur der neueste fehlende Tag: aeltere Versionen nachtraeglich zu bauen hiesse,
    # alten Code aus dem Arbeitsverzeichnis zu paketieren, und das waere falsch.
    $tag = $fehlend[0]
    $version = $tag.Substring(1)

    Schritt "Release $version nachziehen"

    # Der Tag muss vom Arbeitsstand aus erreichbar sein, sonst wuerde Code aus einem
    # anderen Zweig paketiert.
    $vorfahre = Extern git @("merge-base", "--is-ancestor", $tag, "HEAD")
    if ($vorfahre.Code -ne 0) {
        Warnung "$tag liegt nicht im aktuellen Zweig."
        Info "Es wird nichts gebaut, sonst landete fremder Code im Release."
        Info "Erst auschecken ('git checkout $tag'), dann dieses Skript erneut aufrufen."
        exit 1
    }
    Info "$tag liegt im aktuellen Zweig"

    $imCode = Select-String -Path (Join-Path $repo "CMakeLists.txt") -Pattern 'project\(GameDetector VERSION (\d+\.\d+\.\d+)\)' |
              ForEach-Object { $_.Matches[0].Groups[1].Value } | Select-Object -First 1
    if ($imCode -ne $version) {
        Warnung "In CMakeLists.txt steht Version $imCode, der Tag sagt $version."
        Info "Es wird nichts veroeffentlicht: der Update-Hinweis wuerde sonst nie verschwinden."
        exit 1
    }
    Info "Version im Code passt zum Tag"

    $cmake = Get-Command cmake.exe -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty Source
    if (-not $cmake) {
        foreach ($kandidat in @(
            "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
            "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
            "C:\Program Files\CMake\bin\cmake.exe")) {
            if (Test-Path $kandidat) { $cmake = $kandidat; break }
        }
    }
    if (-not $cmake) { Warnung "cmake.exe nicht gefunden."; exit 1 }

    $build = Extern $cmake @("--build", (Join-Path $repo "build_x64"), "--config", "RelWithDebInfo", "--target", "GameDetector")
    if ($build.Code -ne 0) {
        Warnung "Der Build ist fehlgeschlagen."
        $build.Zeilen | Select-Object -Last 10 | ForEach-Object { Info $_.Trim() }
        exit 1
    }
    Info "gebaut"

    $arbeit = Join-Path ([System.IO.Path]::GetTempPath()) ("gd-watch-" + $version + "-" + (Get-Date -Format "HHmmss"))
    New-Item -ItemType Directory -Force -Path $arbeit | Out-Null

    try {
        $dateien = New-GdPackage -Repo $repo -Version $version -Zielordner $arbeit -Melden ${function:Info}
    } catch {
        Warnung "Das Paket wurde abgelehnt: $($_.Exception.Message)"
        exit 1
    }

    $commits = (Extern git @("log", "-8", "--pretty=format:- %s")).Zeilen | Where-Object { $_ }
    $notiz = @"
### Installation

1. ZIP herunterladen und entpacken
2. OBS Studio komplett schliessen
3. **``Installieren.bat``** doppelklicken und die Windows-Abfrage mit Ja bestaetigen

Details stehen in ``ANLEITUNG.txt`` im ZIP.

### Aenderungen

$($commits -join "`n")

Fork von [FabioZumbi12/game-detector](https://github.com/FabioZumbi12/game-detector), GPL-2.0.
"@
    $notizDatei = Join-Path $arbeit "notes.md"
    Set-Content -LiteralPath $notizDatei -Value $notiz -Encoding UTF8

    $anlegen = Extern gh (@("release", "create", $tag) + $dateien + @("--repo", $ghRepo,
                           "--title", "Game Detector V2 $version", "--notes-file", $notizDatei))
    if ($anlegen.Code -ne 0) {
        Warnung "Das Release liess sich nicht anlegen: $($anlegen.Ausgabe)"
        exit 1
    }

    try {
        Assert-GdReleaseSichtbar -GhRepo $ghRepo -Tag $tag -Melden ${function:Info}
    } catch {
        Warnung "Das Release ist da, aber nicht brauchbar: $($_.Exception.Message)"
        exit 1
    }

    Gut "Release $version veroeffentlicht und geprueft"
    Info "https://github.com/$ghRepo/releases/tag/$tag"
} finally {
    Pop-Location
}
