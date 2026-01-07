# AniEXE

**Offline Local-Anime-Launcher setup for *your* chosen Launcher.**

**AniEXE** is a windows executable (compiled from C++ for robustness), that turns your Anime folder  something a **game launcher** (Steam, Heroic, Playnite, etc.) can launch and track — without servers, libraries, scraping! **Right alongside your games and Visual Novels**

It scans the folder it lives in, sorts episodes naturally, and launches them in order using **mpv.net**. That’s it!

if you've wanted a library for your Anime with No database, No daemon. This is it.

## How it works

When you run **AniEXE.exe**:

1. Finds all video files in the same folder  
2. Filters out samples / trailers / extras  
3. Sorts episodes using *natural* sorting (01, 02, 10 — not 1, 10, 2)
4. Generates a temporary playlist
5. Launches it with mpv / mpv.net
6. Cleans it up and exits!

Your launcher:
- Tracks playtime
- Shows artwork
- Remembers “last played”
- etc.

mpv,net:
- Handles resume
- Handles playback, exactly from where you left off.
- Anime-made sub-styling (ASS rendering, subtitle timing)
- OP/ED markers so you know when the songs play and you can skip them in one click!

## Folder layout (recommended)

Anime/

└─ Anime_name/

├─ Season number (1, 2 if applicable)

├─ AniEXE.exe

├─ cover.png (for your launcher, if your launcher accepts banners as well, you can include them here!)

├─ EP01.mkv

├─ EP02.mkv

└─ EP03.mkv
           and so on.

# Each Anime Season = one folder = one launcher entry.

If your anime has multiple seasons, sort of them out by season number. if your anime has only one season, one folder is sufficient.
this is so that **your launcher** can track how much of the *season* you watched, and you can **input different entries per season! since every season has a different cover artwork!**

# Requirements:

**1. mpv.net**

**2. Game launcher**

AniEXE requires mpv.net to handle playback, resume, and ordered episodes.

 Install via **winget**:

```powershell

winget install mpv.net

```

**How to install with winget**:

**1.** Press Win + X

**2.** Select Terminal, and click.

**3.** Paste the command above and press Enter

**4.** Wait until its install and close terminal.

*You can use **any** game launcher of your choice, though i recommend Heroic Games Launcher.*

```powershell

winget install HeroicGamesLauncher.HeroicGamesLauncher

```

*You can install it the exact same way as mpv.net*


*Heroic is recommended because:*

*It supports custom executables*
*Tracks playtime and last played*
*Allows full UI theming (you can make your anime library look however you want! I even have a cool theme for it.)*

**Once you have mpv.net and a Game launcher:**

**Place AniEXE.exe in your Anime folder (per season), Folder layout is stated above!**

In *your game launcher*:

1. Click "ADD GAME" (or your launcher equivalent)
2. Enter Title, app image, .exe location (AniEXE.exe in your folder!) 

**Make sure to set a category for your Anime entries (like "Anime" category, or Genre category - "mystery", "romance" etc), so you can diffentiate your Anime Library with your Game/Visual Novel library.**

 **Enjoy your Anime library! :D**

