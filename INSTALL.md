# Game Detector mit Smart Context Mode — Installation

Ein OBS-Plugin, das erkennt, **welche Anwendung du gerade tatsächlich benutzt**, und
danach automatisch deine Twitch-Kategorie und deinen Streamtitel setzt.

Nicht „läuft ein Spiel?", sondern „welches Fenster ist im Vordergrund?" — und der
Wechsel passiert erst, wenn das eine Weile stabil so bleibt.

---

## Voraussetzungen

- **Windows** (die Vordergrunderkennung ist Windows-spezifisch)
- **OBS Studio 28 oder neuer** (getestet mit 32.2.1)
- Ein Twitch-Konto

---

## Installation — der einfache Weg

1. OBS Studio **komplett schließen**
2. ZIP entpacken
3. Doppelklick auf **`Installieren.bat`**
4. Windows-Abfrage mit **„Ja"** bestätigen

Das Skript sucht deine OBS-Installation, kopiert das Plugin hinein und sagt dir, wie es
weitergeht. Zum Entfernen gibt es **`Deinstallieren.bat`** — deine Einstellungen bleiben
dabei erhalten.

Wenn das klappt, kannst du direkt zu Schritt 2 weiter unten springen.

---

## Installation — von Hand

Falls du lieber selbst kopierst oder das Skript dein OBS nicht findet.

### 1. Plugin kopieren

OBS **schließen**. Dann den Inhalt der ZIP nach:

```
C:\Program Files\obs-studio\
```

entpacken, sodass daraus wird:

```
C:\Program Files\obs-studio\obs-plugins\64bit\game-detector.dll
C:\Program Files\obs-studio\data\obs-plugins\game-detector\locale\...
```

> **Wichtig:** Der Ordner `%APPDATA%\obs-studio\plugins\` funktioniert **nicht** —
> OBS durchsucht ihn unter Windows nicht. Es muss in den Programmordner.
> Dafür brauchst du Administratorrechte.

Falls dein OBS woanders liegt: Der Zielordner ist der, in dem auch `bin\64bit\obs64.exe` liegt.

### 2. OBS starten und Dock einblenden

Neue Docks sind in OBS standardmäßig versteckt:

**Docks → Game Detector** anhaken.

### 3. Twitch verbinden

Im Dock auf das **🖥️-Symbol** (Einstellungen).

1. Bei **Plattform-Aktion** auf **„Kategorie über API"** stellen
   *(Der andere Modus schickt nur einen Chat-Befehl an einen Bot und kann den Titel nicht setzen.)*
2. Häkchen bei **„Gemeinsame Berechtigungen"**
3. **„Twitch: Verbinden"** → im Browser autorisieren (30 Sekunden Zeit)
4. **OK**

### 4. Smart Context einschalten

Im Dock:

- ☑ **Smart Context Mode**
- **Verzögerung** wählen (30 Sekunden bis 10 Minuten, Standard 5 Minuten)

Fertig. Ab jetzt läuft es automatisch.

---

## Einrichtung

### Regeln

**🖥️ → „Smart-Context-Regeln verwalten"**

Jede Regel ordnet einem Programm eine Kategorie zu — optional mit Streamtitel, eigener
Verzögerung und Priorität.

| Spalte | Bedeutung |
|---|---|
| Prozess / Regel | Name der `.exe`. Platzhalter `*` und `?` erlaubt |
| Fenstertitel enthält | Optional: Regel greift nur, wenn der Fenstertitel das enthält |
| Kategorie | Was auf Twitch gesetzt wird |
| Titelvorlage | Optional. Leer = Titel bleibt unangetastet |
| Verzögerung | Eigene Wartezeit statt der globalen |
| Ignorieren | Dieses Programm ändert nie etwas |
| Priorität | Höher gewinnt, wenn mehrere Regeln passen |

**Titel-Platzhalter:** `{game}`, `{category}`, `{app}`, `{window}`

> **Tipp:** Programmnamen sind selten das, was man erwartet. Wardogs heißt
> `WardogsClient-Win64-Shipping.exe`, Elgato Wave Link heißt `Elgato.WaveLink.exe`.
> Starte das Programm, schau im Dock bei **„Aktive Anwendung"** nach — dort steht der
> echte Name.

### Programme ignorieren

Button **„Programme ignorieren…"** direkt im Dock.

Ignorierte Programme sind komplett neutral: Ist eins im Vordergrund, passiert gar
nichts — die Kategorie bleibt, und ein laufender Countdown wird weder fortgesetzt noch
zurückgesetzt. Du kannst also stundenlang in Discord sein, ohne dass sich etwas ändert.

**„Aktuelles Programm hinzufügen"** nimmt das Fenster, das gerade vorne war — kein
Namen-Raten nötig.

Vorbelegt sind u. a. Discord, OBS, Explorer, Spotify, Wave Link, Stream Deck sowie die
Steam-, Epic- und Rockstar-Launcher.

### Chat-Meldung (optional)

**🖥️ → Plattform-Aktion → „Wechsel im Chat ankündigen"**

Postet nach jedem Wechsel eine Nachricht in deinen Chat, z. B.:

```
Kategorie gewechselt zu {game}
```

---

## Wie es sich verhält

```
Spiel im Vordergrund
  → nach der eingestellten Zeit   → Kategorie + Titel des Spiels

Kurz zu Firefox getabbt (unter der Wartezeit)
  → keine Änderung, gesammelte Zeit bleibt erhalten

Länger in Firefox
  → Just Chatting

In Discord, OBS, Spotify (ignoriert)
  → egal wie lange: keine Änderung
```

**Anti-Flapping:**
- nach jedem Wechsel mindestens 60 Sekunden Ruhe
- ein Alt-Tab unter 60 Sekunden wirft die gesammelte Zeit nicht weg
- ignorierte Programme unterbrechen nichts

**Kategorie sperren:** Häkchen im Dock — dann findet gar keine automatische Änderung
mehr statt. Die manuellen Knöpfe funktionieren weiter.

---

## Manuelle Steuerung

Im Dock, unter der Statusanzeige:

| | |
|---|---|
| Kategorie-Auswahl + **Übernehmen** | Setzt sofort eine Kategorie samt passender Titelvorlage |
| **Jetzt wechseln: …** | Übernimmt den anstehenden Wechsel ohne zu warten |
| **Timer zurücksetzen** | Verwirft den anstehenden Wechsel |

Manuelle Klicks umgehen Wartezeit, Cooldown und Sperre — es sind ja deine bewussten
Entscheidungen.

---

## Bekannte Eigenheiten

**OBS' „Streaminformation"-Dock aktualisiert sich nicht.** Das ist OBS' eigenes
Eingabeformular und für Plugins nicht erreichbar — die OBS-API bietet keine Funktion
dafür. Es zeigt weiter alte Werte an, und **„Fertig"** würde diese alten Werte an Twitch
zurückschicken. Nutze stattdessen die Anzeige im Game-Detector-Dock, dort stehen
Kategorie und Titel live.

**Nur Windows.** Die Vordergrunderkennung nutzt Windows-Funktionen. Unter Linux und
macOS läuft das restliche Plugin, aber Smart Context erkennt nichts.

---

## Credits und Lizenz

**Ursprüngliches Plugin:** **Fábio F. Magalhães (FabioZumbi12)**
<https://github.com/FabioZumbi12/game-detector>

**Smart Context Mode:** **Tobias Schlothane** — [it-kicodebyts.com](https://it-kicodebyts.com)

Lizenz: **GNU General Public License v2.0**, wie das Original. Alle Änderungen sind in
[FORK-CHANGES.md](FORK-CHANGES.md) dokumentiert.

Wenn du dieses Plugin weitergibst, musst du den Quelltext mitliefern oder verlinken —
das verlangt die GPL.
