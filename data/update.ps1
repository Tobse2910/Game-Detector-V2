# Game Detector V2 - Update-Helfer
#
# Wird vom Plugin mit Administratorrechten gestartet (UAC-Abfrage), wartet bis OBS
# beendet ist, tauscht die Plugin-Dateien aus und startet OBS wieder.
#
# Laeuft nie von Hand: das Plugin uebergibt die Argumente selbst.
#
# -Source ist ein bereits entpacktes Update-Verzeichnis, -Zip ein Archiv, das dieses
# Skript selbst entpackt. Das Plugin bevorzugt -Source, damit immer der Helfer AUS DEM
# UPDATE laeuft und nicht der der installierten Version. Beides wird unterstuetzt,
# weil eine aeltere Plugin-Version nur -Zip kennt.

param(
    [string] $Zip,
    [string] $Source,
    [Parameter(Mandatory = $true)] [string] $ObsDir,
    [Parameter(Mandatory = $true)] [int]    $WaitPid
)

$ErrorActionPreference = "Stop"

# --- Oberflaeche -------------------------------------------------------------
# Ein eigenes Fenster, weil OBS waehrend des Updates geschlossen ist. Laeuft in
# einem eigenen Runspace, damit es nicht einfriert, solange hier kopiert wird.
$ui = [hashtable]::Synchronized(@{
    Titel    = "Game Detector wird aktualisiert"
    Status   = "Vorbereiten..."
    Detail   = ""
    Schritt  = 0
    Schritte = 4
    Fehler   = $false
    Fertig   = $false
    Beenden  = $false
})

$uiThread = $null
$uiRunspace = $null

function Start-Oberflaeche {
    $xaml = @'
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        Title="Game Detector" Height="232" Width="460"
        WindowStyle="None" ResizeMode="NoResize" WindowStartupLocation="CenterScreen"
        Background="#0F0F0F" Topmost="True">
  <Border BorderBrush="#262626" BorderThickness="1">
    <Grid Margin="28,24,28,24">
      <Grid.RowDefinitions>
        <RowDefinition Height="Auto"/>
        <RowDefinition Height="Auto"/>
        <RowDefinition Height="*"/>
        <RowDefinition Height="Auto"/>
      </Grid.RowDefinitions>

      <TextBlock x:Name="Titel" Grid.Row="0" Text="Game Detector wird aktualisiert"
                 Foreground="#F2F2F2" FontSize="17" FontWeight="SemiBold"
                 FontFamily="Segoe UI"/>

      <TextBlock x:Name="Status" Grid.Row="1" Text="Vorbereiten..." Margin="0,14,0,0"
                 Foreground="#E07A35" FontSize="13" FontFamily="Segoe UI"
                 TextWrapping="Wrap"/>

      <TextBlock x:Name="Detail" Grid.Row="2" Text="" Margin="0,10,0,0"
                 Foreground="#8A8A8A" FontSize="11.5" FontFamily="Segoe UI"
                 TextWrapping="Wrap" VerticalAlignment="Top"/>

      <StackPanel Grid.Row="3">
        <Border Background="#1E1E1E" Height="4" CornerRadius="2">
          <Border x:Name="Balken" Background="#E07A35" Height="4" CornerRadius="2"
                  HorizontalAlignment="Left" Width="0"/>
        </Border>
        <Button x:Name="Schliessen" Content="Schliessen" Margin="0,18,0,0" Width="112"
                Height="30" HorizontalAlignment="Right" Visibility="Collapsed"
                Foreground="#F2F2F2" Background="#2A2A2A" BorderThickness="0"
                FontFamily="Segoe UI" FontSize="12.5" Cursor="Hand"/>
      </StackPanel>
    </Grid>
  </Border>
</Window>
'@

    $runspace = [runspacefactory]::CreateRunspace()
    $runspace.ApartmentState = "STA"
    $runspace.ThreadOptions = "ReuseThread"
    $runspace.Open()
    $runspace.SessionStateProxy.SetVariable("ui", $ui)
    $runspace.SessionStateProxy.SetVariable("xaml", $xaml)

    $ps = [powershell]::Create()
    $ps.Runspace = $runspace
    $ps.AddScript({
        Add-Type -AssemblyName PresentationFramework, PresentationCore, WindowsBase

        $reader = New-Object System.Xml.XmlNodeReader ([xml]$xaml)
        $window = [Windows.Markup.XamlReader]::Load($reader)

        $titel = $window.FindName("Titel")
        $status = $window.FindName("Status")
        $detail = $window.FindName("Detail")
        $balken = $window.FindName("Balken")
        $schliessen = $window.FindName("Schliessen")

        $schliessen.Add_Click({ $window.Close() })

        # Volle Breite des Balkens, abzueglich der Raender des Fensters.
        $maxBreite = 460 - 56 - 2

        $timer = New-Object System.Windows.Threading.DispatcherTimer
        $timer.Interval = [TimeSpan]::FromMilliseconds(120)
        $timer.Add_Tick({
            $titel.Text = $ui.Titel
            $status.Text = $ui.Status
            $detail.Text = $ui.Detail

            if ($ui.Fehler) { $status.Foreground = "#E05555"; $balken.Background = "#E05555" }

            $anteil = if ($ui.Schritte -gt 0) { [Math]::Min(1.0, $ui.Schritt / $ui.Schritte) } else { 0 }
            $balken.Width = [Math]::Round($maxBreite * $anteil)

            if ($ui.Fehler -or $ui.Fertig) { $schliessen.Visibility = "Visible" }
            if ($ui.Beenden) { $timer.Stop(); $window.Close() }
        })
        $timer.Start()

        $window.ShowDialog() | Out-Null
    }) | Out-Null

    return @{ Runspace = $runspace; Handle = $ps.BeginInvoke(); Shell = $ps }
}

# Ohne WPF (etwa auf Windows-Installationen ohne Desktop) faellt alles auf die
# Konsolenausgabe zurueck, damit das Update trotzdem laeuft.
$grafisch = $true
try {
    $oberflaeche = Start-Oberflaeche
} catch {
    $grafisch = $false
}

function Melde($status, $detail = "", $schritt = $null) {
    $ui.Status = $status
    $ui.Detail = $detail
    if ($null -ne $schritt) { $ui.Schritt = $schritt }
    if (-not $grafisch) { Write-Host "  $status" -ForegroundColor Gray }
}

function Abbruch($text) {
    $ui.Titel = "Update fehlgeschlagen"
    $ui.Fehler = $true
    $ui.Status = $text
    $ui.Detail = "Das Plugin wurde nicht geaendert, deine bisherige Version laeuft weiter. Du kannst OBS normal starten und das Update spaeter erneut versuchen."

    if (-not $grafisch) {
        Write-Host ""
        Write-Host "  FEHLER: $text" -ForegroundColor Red
        Write-Host "  Das Plugin wurde NICHT geaendert." -ForegroundColor Gray
        Read-Host "  [Enter] zum Beenden"
        exit 1
    }

    # Das Fenster bleibt stehen, bis es geschlossen wird.
    while ($oberflaeche.Runspace.RunspaceAvailability -ne "Available") { Start-Sleep -Milliseconds 200 }
    exit 1
}

# --- Argumente pruefen, bevor irgendetwas angefasst wird --------------------
if (-not $Zip -and -not $Source) { Abbruch "Dem Update-Helfer wurde keine Quelle uebergeben." }
if ($Source -and -not (Test-Path -LiteralPath $Source)) { Abbruch "Das entpackte Update ist nicht mehr da." }
if (-not $Source -and -not (Test-Path -LiteralPath $Zip)) { Abbruch "Die heruntergeladene Datei ist nicht mehr da." }
if (-not (Test-Path -LiteralPath (Join-Path $ObsDir "bin\64bit\obs64.exe"))) {
    Abbruch "In $ObsDir steckt kein OBS Studio."
}

# --- Auf das Ende von OBS warten -------------------------------------------
Melde "Warte, bis OBS Studio geschlossen ist..." "OBS speichert gerade seine Szenen und Einstellungen." 0

$frist = (Get-Date).AddSeconds(180)
while (Get-Process -Id $WaitPid -ErrorAction SilentlyContinue) {
    if ((Get-Date) -gt $frist) {
        Abbruch "OBS Studio laeuft nach 3 Minuten noch. Bitte OBS schliessen und das Update erneut starten."
    }
    Start-Sleep -Milliseconds 300
}

# Die DLL wird erst kurz nach dem Prozessende freigegeben.
Start-Sleep -Seconds 2
Melde "OBS ist geschlossen." "" 1

# --- Quelle bereitstellen ---------------------------------------------------
$temp = Join-Path $env:TEMP ("game-detector-update-" + [System.Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force -Path $temp | Out-Null

if ($Source) {
    $inhalt = $Source
} else {
    Melde "Update wird entpackt..." "" 1
    try {
        Expand-Archive -LiteralPath $Zip -DestinationPath $temp -Force
    } catch {
        Abbruch "Das Update-Paket liess sich nicht entpacken: $($_.Exception.Message)"
    }
    $inhalt = $temp
}

# Die DLL wird gesucht statt an einer festen Stelle erwartet: im Archiv liegt sie
# unter plugin\obs-plugins\64bit\, aber dieser Helfer laeuft auch noch, wenn eine
# spaetere Version das Archiv anders aufbaut.
$gefunden = Get-ChildItem -LiteralPath $inhalt -Filter "game-detector.dll" -Recurse -File |
            Select-Object -First 1
if (-not $gefunden) { Abbruch "Im Update-Paket fehlt game-detector.dll." }

$neueDll = $gefunden.FullName

# Die Sprachdateien liegen relativ zur DLL: <basis>\obs-plugins\64bit\game-detector.dll
# und <basis>\data\obs-plugins\game-detector
$basis = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $neueDll))
$neueDaten = Join-Path $basis "data\obs-plugins\game-detector"

if (-not (Test-Path -LiteralPath $neueDaten)) { Abbruch "Im Update-Paket fehlen die Sprachdateien." }

Melde "Update-Paket geprueft." "" 2

# --- Sicherung der laufenden Version ---------------------------------------
$zielDll = Join-Path $ObsDir "obs-plugins\64bit\game-detector.dll"
$zielDaten = Join-Path $ObsDir "data\obs-plugins\game-detector"
$sicherung = Join-Path $temp "backup-game-detector.dll"

if (Test-Path -LiteralPath $zielDll) {
    Copy-Item -LiteralPath $zielDll -Destination $sicherung -Force
}

# --- Austauschen ------------------------------------------------------------
Melde "Plugin wird ausgetauscht..." "" 2

try {
    New-Item -ItemType Directory -Force -Path $zielDaten | Out-Null
    Copy-Item -LiteralPath $neueDll -Destination $zielDll -Force

    # Unterordner einzeln, weil "Copy-Item quelle\* ziel -Recurse" einen bereits
    # vorhandenen Unterordner im Ziel verschachtelt statt seinen Inhalt zu ersetzen.
    foreach ($eintrag in Get-ChildItem -LiteralPath $neueDaten) {
        $ziel = Join-Path $zielDaten $eintrag.Name
        if ($eintrag.PSIsContainer) {
            New-Item -ItemType Directory -Force -Path $ziel | Out-Null
            Copy-Item -Path (Join-Path $eintrag.FullName "*") -Destination $ziel -Recurse -Force
        } else {
            Copy-Item -LiteralPath $eintrag.FullName -Destination $ziel -Force
        }
    }

    Melde "Plugin aktualisiert." "" 3
} catch {
    $grund = $_.Exception.Message

    # Die alte DLL zurueckholen, damit kein halb aktualisierter Zustand bleibt.
    if (Test-Path -LiteralPath $sicherung) {
        try {
            Copy-Item -LiteralPath $sicherung -Destination $zielDll -Force
        } catch {
            Abbruch "Kopieren fehlgeschlagen und die Wiederherstellung ebenfalls: $grund"
        }
    }

    Abbruch "Kopieren fehlgeschlagen: $grund"
}

# --- OBS wieder starten -----------------------------------------------------
Melde "OBS Studio wird gestartet..." "" 4

$obsExe = Join-Path $ObsDir "bin\64bit\obs64.exe"
$arbeitsverzeichnis = Join-Path $ObsDir "bin\64bit"
$gestartet = $false

try {
    # OBS muss aus bin\64bit heraus starten, sonst findet es seine eigenen
    # Sprachdateien nicht ("Failed to find locale/en-US.ini"). Gleichzeitig darf es
    # nicht die Administratorrechte dieses Helfers erben. Eine Verknuepfung traegt
    # das Arbeitsverzeichnis in sich und wird von explorer.exe als normaler
    # Benutzer geoeffnet, das loest beides zugleich.
    $lnk = Join-Path $env:TEMP "game-detector-obs-start.lnk"
    $shell = New-Object -ComObject WScript.Shell
    $verknuepfung = $shell.CreateShortcut($lnk)
    $verknuepfung.TargetPath = $obsExe
    $verknuepfung.WorkingDirectory = $arbeitsverzeichnis
    $verknuepfung.Save()

    Start-Process -FilePath "explorer.exe" -ArgumentList "`"$lnk`""
    $gestartet = $true
} catch {
    $gestartet = $false
}

$ui.Titel = "Update abgeschlossen"
$ui.Fertig = $true

if ($gestartet) {
    Melde "Fertig. OBS Studio startet wieder." "Dieses Fenster schliesst sich von allein." 4
} else {
    Melde "Fertig. Bitte OBS Studio selbst starten." "Der automatische Start hat nicht funktioniert, das Update selbst ist eingespielt." 4
}

if (-not $grafisch) {
    Write-Host ""
    Start-Sleep -Seconds 5
    exit 0
}

# Kurz stehen lassen, dann von allein zu.
Start-Sleep -Seconds 4
$ui.Beenden = $true
Start-Sleep -Milliseconds 600
