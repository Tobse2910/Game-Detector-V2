# Game Detector V2 - gemeinsames Fenster fuer Installieren.bat und Deinstallieren.bat
#
# Wird von install.ps1 und uninstall.ps1 per Dot-Sourcing geladen:
#     . "$PSScriptRoot\ui.ps1"
#
# Der Update-Helfer (data\obs-plugins\game-detector\update.ps1) hat dieselbe
# Oberflaeche absichtlich als eigene Kopie: er wird vom Plugin in den Temp-Ordner
# kopiert und dort allein gestartet, waehrend das Update die Plugin-Dateien
# austauscht. Eine Datei, die noch etwas nachladen muss, wuerde genau dann brechen.
#
# Ohne WPF (Windows ohne Desktop) fallen alle Funktionen auf Konsolenausgabe zurueck,
# damit die Installation trotzdem laeuft.

$script:GdUi = [hashtable]::Synchronized(@{
    Titel    = "Game Detector"
    Status   = ""
    Detail   = ""
    Schritt  = 0
    Schritte = 4
    Fehler   = $false
    Fertig   = $false
    Beenden  = $false
})

$script:GdGrafisch = $false
$script:GdShell = $null

function Start-GdFenster {
    param(
        [Parameter(Mandatory = $true)] [string] $Titel,
        [int] $Schritte = 4
    )

    $script:GdUi.Titel = $Titel
    $script:GdUi.Schritte = $Schritte
    $script:GdUi.Status = ""

    $xaml = @'
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        Title="Game Detector" Height="248" Width="470"
        WindowStyle="None" ResizeMode="NoResize" WindowStartupLocation="CenterScreen"
        Background="#0F0F0F" Topmost="True">
  <Window.Resources>
    <!-- Eigenes Aussehen, weil der Standard-Button beim Ueberfahren in Windows-Blau
         umschlaegt und die gesetzte Farbe ueberdeckt. -->
    <Style x:Key="FlacherButton" TargetType="Button">
      <Setter Property="Foreground" Value="#F2F2F2"/>
      <Setter Property="Background" Value="#262626"/>
      <Setter Property="BorderThickness" Value="0"/>
      <Setter Property="FontFamily" Value="Segoe UI"/>
      <Setter Property="FontSize" Value="12.5"/>
      <Setter Property="Cursor" Value="Hand"/>
      <Setter Property="Template">
        <Setter.Value>
          <ControlTemplate TargetType="Button">
            <Border x:Name="Flaeche" Background="{TemplateBinding Background}" CornerRadius="4">
              <ContentPresenter HorizontalAlignment="Center" VerticalAlignment="Center"/>
            </Border>
            <ControlTemplate.Triggers>
              <Trigger Property="IsMouseOver" Value="True">
                <Setter TargetName="Flaeche" Property="Background" Value="#E07A35"/>
                <Setter Property="Foreground" Value="#0F0F0F"/>
              </Trigger>
              <Trigger Property="IsPressed" Value="True">
                <Setter TargetName="Flaeche" Property="Background" Value="#B85F24"/>
                <Setter Property="Foreground" Value="#0F0F0F"/>
              </Trigger>
            </ControlTemplate.Triggers>
          </ControlTemplate>
        </Setter.Value>
      </Setter>
    </Style>
  </Window.Resources>

  <Border BorderBrush="#262626" BorderThickness="1">
    <Grid Margin="28,24,28,24">
      <Grid.RowDefinitions>
        <RowDefinition Height="Auto"/>
        <RowDefinition Height="Auto"/>
        <RowDefinition Height="*"/>
        <RowDefinition Height="Auto"/>
      </Grid.RowDefinitions>

      <TextBlock x:Name="Titel" Grid.Row="0" Text="Game Detector"
                 Foreground="#F2F2F2" FontSize="17" FontWeight="SemiBold" FontFamily="Segoe UI"/>

      <TextBlock x:Name="Status" Grid.Row="1" Text="" Margin="0,14,0,0"
                 Foreground="#E07A35" FontSize="13" FontFamily="Segoe UI" TextWrapping="Wrap"/>

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
                Style="{StaticResource FlacherButton}"/>
      </StackPanel>
    </Grid>
  </Border>
</Window>
'@

    try {
        $runspace = [runspacefactory]::CreateRunspace()
        $runspace.ApartmentState = "STA"
        $runspace.ThreadOptions = "ReuseThread"
        $runspace.Open()
        $runspace.SessionStateProxy.SetVariable("ui", $script:GdUi)
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
            $maxBreite = 470 - 56 - 2

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

        $ps.BeginInvoke() | Out-Null
        $script:GdShell = $ps
        $script:GdGrafisch = $true

        # Erst jetzt, wo das eigene Fenster steht: das Konsolenfenster verstecken,
        # damit nicht zwei Fenster herumstehen. Die Startdateien tun dasselbe ueber
        # -WindowStyle Hidden; das hier greift auch, wenn das Skript von Hand
        # gestartet wurde. Bei fehlendem WPF bleibt die Konsole sichtbar, sonst waere
        # gar nichts mehr zu sehen.
        try {
            if (-not ("Win.GdConsole" -as [type])) {
                Add-Type -Namespace Win -Name GdConsole -MemberDefinition @'
[DllImport("kernel32.dll")] public static extern IntPtr GetConsoleWindow();
[DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
'@
            }
            $fenster = [Win.GdConsole]::GetConsoleWindow()
            if ($fenster -ne [IntPtr]::Zero) {
                [Win.GdConsole]::ShowWindow($fenster, 0) | Out-Null # 0 = SW_HIDE
            }
        } catch {}
    } catch {
        $script:GdGrafisch = $false
        Write-Host ""
        Write-Host "  $Titel" -ForegroundColor Cyan
        Write-Host ""
    }
}

function Set-GdStatus {
    param([string] $Status, [string] $Detail = "", $Schritt = $null)

    $script:GdUi.Status = $Status
    $script:GdUi.Detail = $Detail
    if ($null -ne $Schritt) { $script:GdUi.Schritt = $Schritt }

    if (-not $script:GdGrafisch) { Write-Host "  $Status" -ForegroundColor Gray }
}

function Stop-GdFenster {
    param([string] $Status, [string] $Detail = "", [switch] $Fehler, [int] $WartenSekunden = 0)

    $script:GdUi.Status = $Status
    $script:GdUi.Detail = $Detail

    if ($Fehler) {
        $script:GdUi.Fehler = $true
    } else {
        $script:GdUi.Fertig = $true
        $script:GdUi.Schritt = $script:GdUi.Schritte
    }

    if (-not $script:GdGrafisch) {
        Write-Host ""
        if ($Fehler) { Write-Host "  FEHLER: $Status" -ForegroundColor Red }
        else { Write-Host "  $Status" -ForegroundColor Green }
        if ($Detail) { Write-Host "  $Detail" -ForegroundColor Gray }
        Write-Host ""
        Read-Host "  [Enter] zum Beenden" | Out-Null
        return
    }

    if ($WartenSekunden -gt 0) {
        Start-Sleep -Seconds $WartenSekunden
        $script:GdUi.Beenden = $true
        Start-Sleep -Milliseconds 600
        return
    }

    # Fenster stehen lassen, bis es geschlossen wird.
    while ($script:GdShell -and -not $script:GdShell.InvocationStateInfo.State.Equals([System.Management.Automation.PSInvocationState]::Completed)) {
        Start-Sleep -Milliseconds 200
    }
}

# Ordnerauswahl, wenn OBS nicht automatisch gefunden wird. Read-Host gibt es im
# Fenstermodus nicht, deshalb der Windows-Dialog.
function Select-GdOrdner {
    param([string] $Beschreibung)

    if (-not $script:GdGrafisch) {
        $eingabe = Read-Host "  OBS-Ordner"
        return $eingabe.Trim('"', ' ')
    }

    try {
        Add-Type -AssemblyName System.Windows.Forms
        $dialog = New-Object System.Windows.Forms.FolderBrowserDialog
        $dialog.Description = $Beschreibung
        $dialog.ShowNewFolderButton = $false
        if ($dialog.ShowDialog() -eq [System.Windows.Forms.DialogResult]::OK) {
            return $dialog.SelectedPath
        }
    } catch {}

    return ""
}
