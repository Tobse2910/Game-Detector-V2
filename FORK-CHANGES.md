# Fork Changes — Smart Context Mode

This is a modified version of **Game Detector** by **Fábio F. Magalhães (FabioZumbi12)**
(<https://github.com/FabioZumbi12/game-detector>).

The original work is licensed under the **GNU General Public License v2.0**, and this
modified version stays under the same license. The original `LICENSE` file, the
copyright notices and the in-app "Developed by FabioZumbi12" credits are unchanged.

- **Upstream base:** `v0.2.5` + 3 commits (`a7d06ad`, 2026-02-23)
- **Modified by:** kicodebyts (Tobias Schlothane)
- **Date of change:** 2026-09-11
- **Branch:** `feature/smart-context-mode`

As required by section 2(a) of the GPL-2.0, the files changed by this fork are listed
below together with what was changed.

---

## What was added

**Smart Context Mode.** Upstream answers the question *"is a known game process
running?"*. This fork adds a second, independent question: *"which application is the
user actually looking at right now?"* — answered by polling the Windows foreground
window once per second — and only switches the stream category once that answer has
been stable for a configurable amount of time.

Nothing from upstream was reimplemented. Twitch OAuth, the category and title API
calls, game scanning, the dock, the game list, the executable mapping and the existing
action delay / cooldown logic are all reused as they are.

### Behaviour

- The foreground executable (and optionally its window title) is matched against a
  user editable rule list. Each rule can set a Twitch category, a stream title
  template, its own delay, an ignore flag and a priority.
- A new category is **never** applied immediately. It must be the foreground
  application for the configured stability delay (30 s / 1 / 2 / 5 / 10 minutes,
  default **5 minutes**) before anything is sent to Twitch.
- **Anti-flapping:**
  - after a switch, at least 60 seconds pass before the next one,
  - ignored applications (Discord, OBS, Explorer, Wave Link, Spotify, the Steam, Epic
    and Rockstar launchers) are neutral — they neither change the category nor age a
    pending one,
  - a short alt-tab does not discard accumulated time: the progress of the application
    you left is parked for 60 seconds and restored if you come back.
- **Games have priority.** A game the user is actually looking at beats generic and
  wildcard rules, but never an explicit exact rule and never the ignore rules.
- **Category lock** blocks every automatic change, including the pre-existing
  auto-update path.

### Manual control

Automation you cannot take over by hand is not useful mid-stream, so the dock also has
a manual section:

- a category picker filled from your own rules (and freely typeable) plus **Apply**,
  which sets that category and the matching rule's title template immediately,
- **Switch now**, which applies the pending context without waiting out the delay — the
  button names the category it would switch to and is only active while something is
  actually pending,
- **Reset timer**, which discards the pending switch and starts counting from zero.

Manual actions are explicit user decisions, so they bypass the stability delay, the
switch cooldown and the category lock. They still respect the platform rate limit; if
that blocks them, the dock says so instead of failing silently. The upstream manual
controls (*Set Category*, *Set 'Just Chatting'*, the manual category dialog and the
hotkeys) are untouched and keep working.

### Rules

The rule list ships with defaults and is fully editable under
*Settings → Manage Smart Context Rules*:

| Process / Rule | Category | Notes |
|---|---|---|
| `WARDOGS.exe` | WARDOGS | own title template |
| `FiveM.exe`, `FiveM_*.exe` | Grand Theft Auto V | own title template |
| `Code.exe`, `devenv.exe`, `idea64.exe`, … | Software and Game Development | dev tools |
| `firefox.exe`, `chrome.exe`, `msedge.exe` | Just Chatting | browsers |
| `discord.exe`, `obs64.exe`, `explorer.exe`, `WaveLink.exe`, `Spotify.exe`, `steam.exe`, `EpicGamesLauncher.exe`, `Launcher.exe`, … | — | ignored, category is kept |

Process names support `*` and `?` wildcards. Title templates support the placeholders
`{game}`, `{category}`, `{app}` and `{window}`. Browser handling deliberately uses only
the process name, the foreground state and the window title — no browser history is
read. A per-rule "window title contains" field is already in place, so an optional
local domain detection can be added later without touching the engine.

---

## Files

### Added

| File | Purpose |
|---|---|
| `src/SmartContextManager.h/.cpp` | Foreground polling, rule engine, stability timer, anti-flapping, category lock |
| `src/SmartContextRulesDialog.h/.cpp` | Editor for the rule list |
| `data/locale/de-DE.ini` | German translation (complete, including the existing strings) |
| `FORK-CHANGES.md` | This file |

### Modified

| File | Change |
|---|---|
| `src/ConfigManager.h/.cpp` | New config keys (`smart_context_*`), their getters/setters and the built-in default rule list |
| `src/GameDetector.h/.cpp` | Added `getGameNameForExe()`, a read-only case-insensitive lookup in the configured game list. Existing detection untouched |
| `src/GameDetectorDock.h/.cpp` | New "Smart Context" group (mode toggle, category lock, delay, live status: active application / detected context / active since / switching in / current Twitch category). The pre-existing auto-update is suppressed while Smart Context Mode or the lock is on, and a merely running game no longer claims the category in that mode |
| `src/GameDetectorSettingsDialog.h/.cpp` | Button opening the rule editor |
| `src/PluginMain.cpp` | Stop the Smart Context poller on module unload |
| `CMakeLists.txt` | Added the two new source files |
| `data/locale/en-US.ini` | New `SmartContext.*` strings |

Untouched: Twitch and Trovo authentication, the platform API calls, the scanners,
hotkeys, the game list, OBS scenes and audio settings.

---

## Building

```
cmake --preset windows-x64 -DENABLE_INNO_SETUP=OFF
cmake --build build_x64 --config RelWithDebInfo --target GameDetector
```

Install to `%APPDATA%\obs-studio\plugins\game-detector\` with `bin\64bit\game-detector.dll`
and the contents of `data\` under `data\`.
