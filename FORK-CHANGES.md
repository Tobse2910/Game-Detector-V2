# Fork Changes - Smart Context Mode

This is a modified version of **Game Detector** by **Fábio F. Magalhães (FabioZumbi12)**
(<https://github.com/FabioZumbi12/game-detector>).

The original work is licensed under the **GNU General Public License v2.0**, and this
modified version stays under the same license. The original `LICENSE` file, the
copyright notices and the in-app "Developed by FabioZumbi12" credits are unchanged.

- **Upstream base:** `v0.2.5` + 3 commits (`a7d06ad`, 2026-02-23)
- **Modified by:** **Tobias Schlothane** - [it-kicodebyts.com](https://it-kicodebyts.com)
  (designed, specified and tested by Tobias Schlothane; implemented together with
  [Claude Code](https://claude.com/claude-code))
- **Date of change:** 2026-09-11
- **Branch:** `feature/smart-context-mode`

As required by section 2(a) of the GPL-2.0, the files changed by this fork are listed
below together with what was changed.

---

## What was added

**Smart Context Mode.** Upstream answers the question *"is a known game process
running?"*. This fork adds a second, independent question: *"which application is the
user actually looking at right now?"* - answered by polling the Windows foreground
window once per second - and only switches the stream category once that answer has
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
    and Rockstar launchers) are neutral - they neither change the category nor age a
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
- **Switch now**, which applies the pending context without waiting out the delay - the
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
| `discord.exe`, `obs64.exe`, `explorer.exe`, `WaveLink.exe`, `Spotify.exe`, `steam.exe`, `EpicGamesLauncher.exe`, `Launcher.exe`, … | - | ignored, category is kept |

Process names support `*` and `?` wildcards. Title templates support the placeholders
`{game}`, `{category}`, `{app}` and `{window}`. Browser handling deliberately uses only
the process name, the foreground state and the window title - no browser history is
read. A per-rule "window title contains" field is already in place, so an optional
local domain detection can be added later without touching the engine.

---

## Result visibility

A category change made through the platform API used to leave no visible trace, which
made a working change look broken:

- Only failures were logged. After `Changing category to: X` nothing followed, so the
  log looked like the change had been dropped. The result is now logged either way,
  with the title that went along with it.
- The dock gained a "Last set" row with a timestamp, kept until the next change. The
  status line above it resets itself after three seconds, which was too short to
  notice during a stream.
- OBS' own Stream Information window is an input form, not a live view. It fills
  itself once when it loads and then keeps showing that value, and its Done button
  writes whatever still stands in the form back to the platform, undoing the change
  that was just made. A plugin cannot refresh or read that window, `obs-frontend-api`
  exposes nothing for it, so the dock states this instead of pretending otherwise.

## Stream information panel

OBS' own Twitch "Stream Information" dock is a browser dock pointed at
`dashboard.twitch.tv/u/<name>/stream-manager/edit-stream-info`. That has three
consequences: its fields cannot be written from a plugin, it never notices a change
made through the API, and its Done button posts whatever stale value it still shows,
undoing the change that was just made. `obs-frontend-api` offers nothing for it.

`src/StreamInfoPanel.h/.cpp` replaces it, talking to Helix directly. It covers
everything that dock offers which Helix actually exposes: title with its 140 character
limit, category with search and box art, tags, stream language, content classification
labels and branded content. Missing, because Twitch has no endpoint for them: the go
live notification and the audience setting.

It reloads itself after every category change, four seconds later so Twitch has the
new value, and declines to reload while there are unsaved edits, so a half typed title
is never wiped out.

Two things the API only reveals when you try:

- `content_classification_labels` takes **at most six** entries, and
  `GET /helix/content_classification_labels` returns **seven**. `MatureGame` is derived
  from the category and answers 400 "label provided is not editable by the caller",
  which fails the whole request. It is shown but greyed out, and never sent.
- An update has to list every editable label with its state. Leaving one out keeps its
  previous value, so unchecking would silently do nothing.

The dock is wrapped in a `QScrollArea`: with this panel added it is taller than most
docking spots, and without it the lower half would be cut off rather than reachable.

## Update check and one click update

`src/UpdateChecker.h/.cpp` asks the GitHub API for the newest release of this fork and
shows a notice in the dock when it is newer than the running build. The check runs ten
seconds after startup and then at most once a day, and can be switched off in the
settings. The running version comes from `project()` in CMakeLists via
`GAME_DETECTOR_VERSION`, which is the single place a version is defined.

"Update now" runs the whole thing: download the release ZIP, hand it to an elevated
helper, close OBS, replace the plugin, start OBS again. A plugin cannot do this alone.
It lives in `C:\Program Files\obs-studio`, which needs elevation, and Windows keeps its
DLL locked for as long as OBS has it loaded. So the work is split:

1. The plugin downloads the ZIP to the temp folder. Binary data cannot go through
   `ExecuteNetworkRequest`, whose QString would mangle it, so `DownloadToFile` writes
   straight to a file. A transfer in flight can be aborted, otherwise closing OBS
   during a download would block on it.
2. `data/update.ps1` is started through `ShellExecuteExW` with the `runas` verb, which
   is what raises the UAC prompt. It is run from a copy in the temp folder, because the
   update replaces the shipped script itself.
3. The helper waits for the OBS process id it was handed, checks that the package
   really contains `game-detector.dll`, keeps a copy of the installed DLL, replaces
   plugin and locale files, and restores that copy if anything fails halfway. It shows
   its own window while doing so, because OBS is closed at that point.
4. OBS is restarted through a shortcut opened by `explorer.exe`. Both parts matter:
   going through Explorer keeps OBS from inheriting the helper's administrator rights,
   and the shortcut carries the working directory, without which OBS starts in the
   wrong folder and fails with "Failed to find locale/en-US.ini".

The helper is taken from the downloaded package rather than from the installed version
whenever it can be found there. A bug in the helper would otherwise be permanent: the
broken copy on disk would run every future update. The installed copy is the fallback,
and it accepts both an unpacked `-Source` and a `-Zip` so either side can be older.
5. The plugin closes OBS through `obs_frontend_get_main_window()` once the helper is
   waiting, which is what makes OBS save its scenes on the way out. An external
   `WM_CLOSE` does not work on an elevated OBS.

Two guards worth naming: the download URL is rejected unless it starts with this fork's
own release path, because it ends up being handled by an elevated helper, and the
update refuses to start while `obs_frontend_streaming_active()` or
`obs_frontend_recording_active()` is true, since it closes OBS.

`.github/workflows/release.yml` builds the plugin on every `v*` tag and attaches a ZIP
containing the DLL, the locale files and the installer scripts from `dist/`. That
release is what the update check looks at.

---

## Files

### Added

| File | Purpose |
|---|---|
| `src/SmartContextManager.h/.cpp` | Foreground polling, rule engine, stability timer, anti-flapping, category lock |
| `src/SmartContextRulesDialog.h/.cpp` | Editor for the rule list |
| `src/StreamInfoPanel.h/.cpp` | Title, category, tags, language, classification labels and branded content straight through Helix; replaces OBS' Twitch stream info dock |
| `src/UpdateChecker.h/.cpp` | Asks the GitHub API for the newest release, downloads it and hands it to the elevated helper |
| `data/update.ps1` | Elevated helper: waits for OBS to exit, replaces the plugin, rolls back on failure, restarts OBS |
| `.github/workflows/release.yml` | Builds on a `v*` tag and attaches the ready to use ZIP to the release |
| `data/locale/de-DE.ini` | German translation (complete, including the existing strings) |
| `FORK-CHANGES.md` | This file |

### Modified

| File | Change |
|---|---|
| `src/ConfigManager.h/.cpp` | New config keys (`smart_context_*`, `update_check_*`), their getters/setters and the built-in default rule list |
| `src/GameDetector.h/.cpp` | Added `getGameNameForExe()`, a read-only case-insensitive lookup in the configured game list. Existing detection untouched |
| `src/GameDetectorDock.h/.cpp` | New "Smart Context" group (mode toggle, category lock, delay, live status: active application / detected context / active since / switching in / current Twitch category). The pre-existing auto-update is suppressed while Smart Context Mode or the lock is on, and a merely running game no longer claims the category in that mode |
| `src/GameDetectorSettingsDialog.h/.cpp` | Button opening the rule editor, update check switch, running version |
| `src/TwitchAuthManager.h/.cpp` | Reads and writes the full channel information, searches categories, fetches classification labels |
| `src/PlatformManager.h/.cpp` | Logs the result of a category change, not just failures, and remembers the title that went with it |
| `src/NetworkCommon.h` | `DownloadToFile()` for binary downloads, with an abort flag so closing OBS does not wait on a transfer |
| `src/PluginMain.cpp` | Stop the Smart Context poller and the update check on module unload |
| `CMakeLists.txt` | Added the new source files and passes the project version to the code as `GAME_DETECTOR_VERSION` |
| `data/locale/en-US.ini` | New `SmartContext.*` and `Update.*` strings |

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
