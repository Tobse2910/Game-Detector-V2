![Game Detector - Smart Context Mode](img/smart-context-mode.png)

# Game Detector V2 (Smart Context Mode)

[![Release](https://img.shields.io/github/v/release/Tobse2910/Game-Detector-V2)](https://github.com/Tobse2910/Game-Detector-V2/releases/latest)
[![Lizenz](https://img.shields.io/badge/Lizenz-GPL--2.0-blue)](LICENSE)
[![OBS](https://img.shields.io/badge/OBS-28%2B-green)](https://obsproject.com/)

Ein OBS-Plugin, das erkennt, **welche Anwendung du gerade tatsächlich benutzt**, und
danach automatisch deine Twitch-Kategorie und deinen Streamtitel setzt.

Nicht "läuft irgendwo ein Spiel?", sondern "welches Fenster ist im Vordergrund?", und
der Wechsel passiert erst, wenn das eine Weile stabil so bleibt.

## ⬇️ Herunterladen

**[Aktuelle Version herunterladen](https://github.com/Tobse2910/Game-Detector-V2/releases/latest)**

1. ZIP herunterladen und entpacken
2. OBS Studio komplett schließen
3. **`Installieren.bat`** doppelklicken und die Windows-Abfrage mit Ja bestätigen

Nach dem Entpacken liegen genau vier Dinge da: die beiden Startdateien, die Anleitung
und die Lizenz. Anzuklicken ist nur `Installieren.bat`.

Nur Windows, OBS 28 oder neuer (getestet auf 32.2.1). Die Vordergrunderkennung nutzt
Windows-Schnittstellen.

## ✨ Was es kann

**Smart Context Mode.** Wertet das Vordergrundfenster aus und wechselt Kategorie und
Titel erst, wenn eine Anwendung die eingestellte Zeit lang vorne war (Standard 5
Minuten). Mit Anti-Flapping: 60 Sekunden Ruhe nach jedem Wechsel, 60 Sekunden
Alt-Tab-Toleranz, und ignorierte Programme sind vollständig neutral.

**Eigene Regeln.** Programm (mit `*` und `?` als Platzhalter), optional ein Fenstertitel,
Kategorie, Titelvorlage, eigene Verzögerung, Ignorieren-Schalter und Priorität. Dazu ein
eigener Editor für ignorierte Programme mit "aktuelles Programm hinzufügen".

**Streaminformation im Dock.** Titel, Kategorie mit Suche und Titelbild, Tags,
Stream-Sprache, Inhaltskennzeichnung und gesponserte Inhalte, alles direkt über die
Twitch-API. Das OBS-Fenster "Streaminformation" brauchst du dafür nicht mehr; es ist
eine Twitch-Webseite, die sich nach einem Wechsel nicht aktualisiert und deren
Fertig-Knopf den alten Wert zurückschreibt.

**Update mit einem Klick.** Ist eine neuere Version da, steht oben im Dock ein Hinweis.
Ein Klick lädt sie, fragt einmal nach Administratorrechten, beendet OBS, tauscht das
Plugin aus und startet OBS wieder. Geht beim Austauschen etwas schief, wird die
vorherige Version wiederhergestellt. Während Stream oder Aufnahme wird das Update
verweigert.

**Sichtbares Ergebnis.** Die Zeilen "Aktuelle Twitch Kategorie" und "Letzter Wechsel"
lesen live von Twitch, und jedes Ergebnis steht im OBS-Log.

Manuelle Übersteuerung ist überall möglich: Kategorie setzen, jetzt wechseln, Timer
zurücksetzen, Kategorie sperren.

## 📄 Mehr

- **[Installation und Einrichtung](INSTALL.md)** Schritt für Schritt
- **[Alle Änderungen gegenüber dem Original](FORK-CHANGES.md)**

---

## 👤 Credits und Lizenz

**Ursprüngliches Plugin** von **Fábio F. Magalhães (FabioZumbi12)**
<https://github.com/FabioZumbi12/game-detector>
Plugin-Seite: <https://obsproject.com/forum/resources/game-detector.2260/>

Darin enthalten und unverändert übernommen: Twitch- und Trovo-Anmeldung, die
Plattform-Aufrufe, die Spielesuche in Steam-, Epic-, GOG- und Ubisoft-Bibliotheken, die
Spieleliste und das Dock.

**Smart Context Mode und alles Weitere** von **Tobias Schlothane**
[it-kicodebyts.com](https://it-kicodebyts.com)
Konzipiert, spezifiziert und getestet von Tobias Schlothane, umgesetzt gemeinsam mit
[Claude Code](https://claude.com/claude-code) (Anthropic).

Lizenziert unter der **GNU General Public License v2.0**, wie das Original. Die GPL gibt
dir das Recht, den kompletten Quelltext zu bekommen, ihn anzusehen, zu verändern und
weiterzugeben. Wenn du dieses Plugin weitergibst, musst du den Quelltext mitgeben.

Englische Fassung des Originals: [README.md des Upstream](https://github.com/FabioZumbi12/game-detector#readme)
Portugiesisch: [README.pt-BR.md](README.pt-BR.md)
