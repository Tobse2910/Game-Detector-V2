' ============================================================
' release-watch-hidden.vbs  —  unsichtbarer Starter fuer release-watch.ps1
' Startet PowerShell im Fenster-Modus 0 (komplett versteckt, KEIN Aufblitzen).
' Wird von der Windows-Aufgabe 'GameDetectorV2-ReleaseWatch' aufgerufen.
'
' Warum dieser Umweg: Ruft die Aufgabe powershell.exe direkt auf, blitzt alle
' 15 Minuten ein schwarzes Fenster auf. '-WindowStyle Hidden' hilft dagegen
' nicht, weil Windows das Konsolenfenster zuerst erzeugt und PowerShell es
' erst danach versteckt. Nur der Start ueber wscript mit Fenster-Modus 0
' verhindert das Fenster von Anfang an.
' ============================================================

Dim skript
skript = "C:\Users\tobia\code\game-detector\tools\release-watch.ps1"

CreateObject("WScript.Shell").Run _
  "powershell.exe -NoProfile -ExecutionPolicy Bypass -File """ & skript & """", _
  0, False
