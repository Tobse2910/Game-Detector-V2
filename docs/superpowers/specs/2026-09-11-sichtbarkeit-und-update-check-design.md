# Sichtbarkeit von Kategoriewechseln und Update-Check

Datum: 2026-09-11
Zielversion: v1.1.0

## Ausgangslage

Der Smart Context Mode setzt Kategorie und Titel auf Twitch korrekt. Nachgewiesen am
11.09.2026 per direktem Helix-GET auf `channels?broadcaster_id=`: die Live-Werte
entsprachen genau dem letzten Wechsel im OBS-Log.

Trotzdem entsteht beim Benutzer der Eindruck, es passiere nichts. Drei Ursachen:

1. **Kein Erfolgs-Logging.** `performPATCHSync` loggt nur Fehlschläge (401, 429,
   sonstige HTTP-Fehler). Nach `Changing category to: X` steht im Log nichts mehr, was
   wie ein Abbruch aussieht, obwohl der PATCH durchgelaufen ist.
2. **OBS' Streaminformation-Dock aktualisiert nicht.** Es ist ein Eingabeformular, das
   sich nur beim Laden füllt. Nach einem API-Wechsel zeigt es weiter den alten Wert.
3. **Kein Feedback im eigenen Dock**, ob der letzte Wechsel geklappt hat.

Punkt 2 ist im Plugin nicht behebbar. `obs-frontend-api.h` bietet ausschließlich
`obs_frontend_add_dock` / `add_dock_by_id` / `add_custom_qdock` / `remove_dock`, also
nur das Registrieren eigener Docks. Es gibt keine Funktion, um OBS' Streaminformation
zu aktualisieren oder dessen Felder zu lesen. Diese Grenze wird dokumentiert und im
Dock als Hinweis angezeigt, statt sie zu umgehen zu versuchen.

## Umfang

### Teil 1: Sichtbarkeit

**Erfolgs-Logging.** Der `forwardSignal`-Lambda in `PlatformManager::PlatformManager()`
ist die einzige Stelle, an der beide Plattformen ihr Ergebnis melden. Dort wird bei
Erfolg `LOG_INFO` mit Kategorie und Titel geschrieben, bei Fehlschlag `LOG_WARNING` mit
Grund. Der gesetzte Titel ist dort nicht im Signal enthalten und wird deshalb in
`updateCategory()` in einem Member `lastSetTitle` gemerkt.

**Statuszeile im Dock.** Neue Zeile "Zuletzt gesetzt:" in der bestehenden `statusForm`
des Smart-Context-Bereichs, gefüllt aus `onCategoryUpdateFinished`:
`22:48:52 OK Software and Game Development` bzw. bei Fehlschlag rot mit Grund.
Kein neues Signal nötig, der Slot existiert.

**Hinweis auf das OBS-Formular.** Statisches Label unter der Statusform: OBS'
Streaminformation zeigt veraltete Werte und "Fertig" schreibt den dort stehenden alten
Wert auf Twitch zurück. Das ist die häufigste Ursache für scheinbar zurückgesprungene
Kategorien.

### Teil 2: Update-Check

Neue Klasse `UpdateChecker` (Singleton, Muster wie `PlatformManager`):

- GET auf `https://api.github.com/repos/Tobse2910/Game-Detector-V2/releases/latest`
  über das vorhandene `ExecuteNetworkRequest` aus `NetworkCommon.h` und `RunTaskSafe`.
  Kein zweiter HTTP-Stack.
- Vergleicht `tag_name` (Format `vX.Y.Z`) mit der eigenen Version. Vergleich
  numerisch pro Segment, nicht als Zeichenkette, damit 1.10.0 > 1.9.0 gilt.
- Eigene Version: `project(GameDetector VERSION ...)` in CMakeLists ist die einzige
  Quelle der Wahrheit und wird per `target_compile_definitions` als
  `GAME_DETECTOR_VERSION` in den Code gegeben. Bisher war die Version im Code
  überhaupt nicht verfügbar.
- Signal `updateAvailable(version, htmlUrl)`. Das Dock zeigt daraufhin eine Zeile mit
  Button, der die Release-Seite im Browser öffnet. Kein Download, kein Selbst-Update:
  das Plugin liegt in `C:\Program Files\obs-studio` und bräuchte dafür Adminrechte.
- Läuft 10 Sekunden nach OBS-Start, danach höchstens einmal in 24 Stunden. Der
  Zeitpunkt des letzten Checks steht in der Config, damit ein OBS-Neustart nicht
  jedes Mal eine Abfrage auslöst.
- Abschaltbar über eine Checkbox im Einstellungsdialog.

Neue Config-Schlüssel:
- `update_check_enabled` (bool, Default true)
- `update_check_last` (Unix-Zeitstempel des letzten Checks)

### Teil 3: Release-Automatik

GitHub-Actions-Workflow, der bei jedem Tag `v*` baut und ein ZIP mit DLL, Locale-Daten
und den vorhandenen Installer-Skripten aus `dist/` an das Release hängt. Ab dann genügt
ein Tag, kein lokales Bauen und kein manuelles Hochladen mehr. Voraussetzung für den
Update-Check, denn der fragt `releases/latest` ab, und aktuell existiert kein einziges
Release.

## Nicht im Umfang

Das Flapping zwischen Firefox und VS Code (acht Wechsel in 1,5 Stunden am 11.09.2026,
mit entsprechendem Chat-Spam) bleibt unberührt. `smart_context_delay` steht auf 120
Sekunden und ist über die Einstellungen veränderbar.

Ein automatischer Download mit UAC-Prompt wurde bewusst gegen den reinen Hinweis
abgewogen und verworfen: mehr Code, mehr Fehlerquellen, und OBS müsste zum Austausch
der DLL ohnehin geschlossen sein.

## Test

- Build muss durchlaufen (`cmake --build build_x64 --config RelWithDebInfo`).
- Versionsvergleich: Einheitentest nicht vorhanden im Projekt, daher manuell geprüfte
  Fälle im Code dokumentiert (gleich, neuer, älter, unterschiedliche Segmentzahl,
  Tag mit und ohne führendes v).
- Nach Installation von v1.1.0 muss das Log bei einem Wechsel eine Erfolgszeile zeigen
  und die Dock-Zeile "Zuletzt gesetzt" gefüllt sein.
