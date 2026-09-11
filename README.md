![Screenshot_29](https://github.com/user-attachments/assets/bc139a51-43d5-4266-8214-ab06be8517d4)

 [English](README.md) | [Português (BR)](README.pt-BR.md) 

# Game Detector OBS Plugin
[![GitHub Release](https://img.shields.io/github/v/release/FabioZumbi12/game-detector)](https://github.com/FabioZumbi12/game-detector/releases/latest)  

Plugin to detect installed games and integrate with Twitch  
OBS Plugins Page: https://obsproject.com/forum/resources/game-detector.2260/

**🎯 Minimum OBS Version: 28.0+** | **Latest Built: OBS 31.1.1**

---

## 📘 About Game Detector OBS Plugin

GameDetector is a plugin for OBS Studio that automatically identifies games installed on your PC (Steam and Epic Games), allowing:

- Automatic game selection  
- Twitch integration
- Editing and correction of detected game names and executables  
- Automatic metadata creation  
- User-friendly interface inside OBS  

The focus is speed, accurate detection, and zero performance impact.

## 📥 Installation and Usage

Checkout the [WIKI Page](../../wiki)

## 🤝 Credits

Developed by **Fábio F. Magalhães (FabioZumbi12)**.  
Contributions and PRs are welcome!

---

## 🧠 Smart Context Mode (fork)

This branch is a modified version that adds **Smart Context Mode**: instead of only
detecting *that* a game is running, it evaluates the Windows **foreground window** to
detect which application you are actually using, and switches the Twitch category and
stream title only after that application has been in front for a configurable time
(default 5 minutes), with anti-flapping and a category lock.

See [FORK-CHANGES.md](FORK-CHANGES.md) for the full list of changes. The original
GPL-2.0 license and all upstream credits are preserved.
