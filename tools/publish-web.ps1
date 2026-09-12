# Game Detector V2 - die Download-Seite auf den Webspace laden
#
#   tools\publish-web.ps1 -Zugangsdaten   einmalig Host, Benutzer, Passwort ablegen
#   tools\publish-web.ps1                 web\index.html hochladen und pruefen
#   tools\publish-web.ps1 -DryRun         nur zeigen, was passieren wuerde
#
# Die Seite selbst muss nur hoch, wenn sie sich aendert. Der Download-Knopf zeigt auf
#   github.com/Tobse2910/Game-Detector-V2/releases/latest/download/Game-Detector-V2-latest.zip
# und damit immer auf das neueste Release, ohne dass hier etwas nachgezogen wird.
#
# Die Zugangsdaten liegen in tools\web-credentials.local.xml, per DPAPI verschluesselt:
# lesbar nur fuer diesen Benutzer auf diesem Rechner. Der Name endet auf .local.xml,
# was .gitignore ausschliesst, denn dieses Repo ist oeffentlich.

param(
    [switch] $Zugangsdaten,
    [switch] $DryRun,

    # Legt zusaetzlich das Release-ZIP dort ab, wo der Downloads-Bereich der
    # Portfolio-Seite es erwartet. Diese Seite ist eine React-App ohne verfuegbaren
    # Quellcode, ihre Links stehen also fest: /Downloads/GameDetector-SmartContext.zip
    # Ohne dieses Nachziehen bliebe dort die Version stehen, die beim Anlegen des
    # Bereichs aktuell war, und deren Nutzer bekaemen keine Update-Hinweise.
    [string] $ReleaseZip
)

$ErrorActionPreference = "Stop"

$repo = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$datei = Join-Path $PSScriptRoot "web-credentials.local.xml"
$quelle = Join-Path $repo "web\index.html"

function Schritt($text) { Write-Host ""; Write-Host "== $text" -ForegroundColor Cyan }
function Info($text) { Write-Host "   $text" -ForegroundColor Gray }
function Gut($text) { Write-Host "   $text" -ForegroundColor Green }
function Abbruch($text) { Write-Host ""; Write-Host "ABBRUCH: $text" -ForegroundColor Red; exit 1 }

# --- Zugangsdaten ablegen ---------------------------------------------------
if ($Zugangsdaten) {
    Schritt "Zugangsdaten ablegen"
    Info "Sie werden mit DPAPI verschluesselt und sind nur fuer dich auf diesem"
    Info "Rechner lesbar. Ein Kopieren der Datei auf einen anderen PC nuetzt nichts."
    Write-Host ""

    $host_ = Read-Host "FTP-Host        (z.B. 637725.test-my-website.de)"
    $benutzer = Read-Host "Benutzer"
    $geheim = Read-Host "Passwort" -AsSecureString
    $pfad = Read-Host "Zielpfad        (z.B. /htdocs/IT-Kicodebyts/game-detector)"
    $adresse = Read-Host "Oeffentliche URL (z.B. https://it-kicodebyts.com/game-detector/)"

    @{
        Host     = $host_
        Benutzer = $benutzer
        Passwort = ($geheim | ConvertFrom-SecureString)
        Pfad     = $pfad
        Adresse  = $adresse
    } | Export-Clixml -LiteralPath $datei

    Gut "Abgelegt in $datei"
    Info "Diese Datei ist durch .gitignore ausgeschlossen und darf nie committet werden."
    exit 0
}

# --- Laden ------------------------------------------------------------------
if (-not (Test-Path $datei)) {
    Abbruch ("Keine Zugangsdaten vorhanden. Einmalig anlegen mit:" + [Environment]::NewLine +
             "  tools\publish-web.ps1 -Zugangsdaten")
}
if (-not (Test-Path $quelle)) { Abbruch "Die Seite fehlt: $quelle" }
if ($ReleaseZip -and -not (Test-Path $ReleaseZip)) { Abbruch "Das Release-ZIP fehlt: $ReleaseZip" }

$d = Import-Clixml -LiteralPath $datei
$klartext = [System.Net.NetworkCredential]::new(
    "", ($d.Passwort | ConvertTo-SecureString)).Password

Schritt "Hochladen"
Info "Datei : $quelle ($([math]::Round((Get-Item $quelle).Length / 1KB)) KB)"
Info "Ziel  : ftp://$($d.Host)$($d.Pfad)/index.html"
Info "URL   : $($d.Adresse)"

if ($DryRun) {
    Gut "Probelauf, es wird nichts hochgeladen."
    exit 0
}

# System.Net statt curl: so steht das Passwort in keiner Kommandozeile und damit in
# keiner Prozessliste.
function Send-Datei {
    param([string] $Von, [string] $Nach, [string] $Benutzer, [string] $Geheim)

    $anfrage = [System.Net.FtpWebRequest]::Create($Nach)
    $anfrage.Credentials = New-Object System.Net.NetworkCredential($Benutzer, $Geheim)
    $anfrage.Method = [System.Net.WebRequestMethods+Ftp]::UploadFile
    $anfrage.UseBinary = $true
    $anfrage.UsePassive = $true
    $anfrage.EnableSsl = $true
    $anfrage.Timeout = 300000

    $inhalt = [System.IO.File]::ReadAllBytes($Von)
    $anfrage.ContentLength = $inhalt.Length

    $strom = $anfrage.GetRequestStream()
    $strom.Write($inhalt, 0, $inhalt.Length)
    $strom.Close()

    $antwort = $anfrage.GetResponse()
    $beschreibung = $antwort.StatusDescription.Trim()
    $antwort.Close()

    return @{ Meldung = $beschreibung; Bytes = $inhalt.Length }
}

try {
    $ergebnis = Send-Datei -Von $quelle -Nach "ftp://$($d.Host)$($d.Pfad)/index.html" `
                           -Benutzer $d.Benutzer -Geheim $klartext
    Info "Seite: $($ergebnis.Meldung) ($($ergebnis.Bytes) Bytes)"

    if ($ReleaseZip) {
        # Fester Dateiname, weil die React-App der Portfolio-Seite ihn so verlinkt.
        $zipZiel = "ftp://$($d.Host)/htdocs/portfolio/Downloads/GameDetector-SmartContext.zip"
        $ergebnis = Send-Datei -Von $ReleaseZip -Nach $zipZiel -Benutzer $d.Benutzer -Geheim $klartext
        Info "Release-ZIP: $($ergebnis.Meldung) ($($ergebnis.Bytes) Bytes)"
    }
} catch {
    Abbruch "Das Hochladen ist fehlgeschlagen: $($_.Exception.Message)"
} finally {
    $klartext = $null
    [System.GC]::Collect()
}

# --- Gegenpruefen -----------------------------------------------------------
# Hochgeladen heisst nicht ausgeliefert: eine Rewrite-Regel oder ein falscher Pfad
# koennen dazu fuehren, dass unter der Adresse etwas anderes steht.
Schritt "Gegenpruefen, was im Netz ankommt"

try {
    $seite = Invoke-WebRequest -Uri $d.Adresse -UseBasicParsing -TimeoutSec 30
} catch {
    Abbruch "Die Seite ist nicht erreichbar: $($_.Exception.Message)"
}

$erwartet = Get-Content $quelle -Raw

Info "HTTP $($seite.StatusCode), $($seite.Content.Length) Zeichen"

if ($seite.Content.Length -ne $erwartet.Length) {
    Info "Erwartet waren $($erwartet.Length) Zeichen."
    Abbruch "Unter der Adresse liegt nicht die hochgeladene Seite."
}

if ($seite.Content -notmatch 'Game-Detector-V2-latest\.zip') {
    Abbruch "Auf der ausgelieferten Seite fehlt der Download-Link."
}

Gut "Die Seite ist online und enthaelt den Download-Link."
Info $d.Adresse

if ($ReleaseZip) {
    # Hochgeladen heisst auch hier nicht ausgeliefert: genau die Adresse pruefen,
    # die der Downloads-Bereich der Portfolio-Seite verlinkt.
    $zipAdresse = "https://kicodebyts.com/Downloads/GameDetector-SmartContext.zip"
    try {
        $kopf = Invoke-WebRequest -Uri $zipAdresse -Method Head -UseBasicParsing -TimeoutSec 60
        $geliefert = [int]$kopf.Headers['Content-Length']
        $erwarteteGroesse = (Get-Item $ReleaseZip).Length
        Info "Downloads-Bereich: $geliefert Bytes (erwartet $erwarteteGroesse)"
        if ($geliefert -ne $erwarteteGroesse) { Abbruch "Im Downloads-Bereich liegt nicht das hochgeladene ZIP." }
        Gut "Der Downloads-Bereich der Portfolio-Seite liefert die aktuelle Version."
    } catch {
        Abbruch "Das ZIP im Downloads-Bereich ist nicht erreichbar: $($_.Exception.Message)"
    }
}
