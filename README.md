<<<<<<< HEAD
# ytplay
=======
# ytplay 🎬

```
  ██╗   ██╗████████╗██████╗ ██╗      █████╗ ██╗   ██╗
  ╚██╗ ██╔╝╚══██╔══╝██╔══██╗██║     ██╔══██╗╚██╗ ██╔╝
   ╚████╔╝    ██║   ██████╔╝██║     ███████║ ╚████╔╝
    ╚██╔╝     ██║   ██╔═══╝ ██║     ██╔══██║  ╚██╔╝
     ██║      ██║   ██║     ███████╗██║  ██║   ██║
     ╚═╝      ╚═╝   ╚═╝     ╚══════╝╚═╝  ╚═╝   ╚═╝
  Stream · Search · Download — YouTube from your terminal
```

A fast, cross-platform CLI tool to **search YouTube** and stream or download videos — all from your terminal. Written in pure C (C99) for maximum portability.

---

## Features

- 🔍 **YouTube search** — type a query, get a numbered list of results
- ▶️  **Stream** — play instantly without saving any files
- 💾 **Download** — save to temp folder (or any path), then play
- 🎵 **Audio-only** mode — extract and play just the audio
- 🎚️  **Quality presets** — `--1080`, `--720`, `--480`, `--4k`, `--worst`
- 🌐 **Subtitles** — download and embed subtitles in any language
- 🖥️  **Cross-platform** — Linux, macOS, Windows (same binary, same flags)
- 🎨 **ASCII art banner** with colour output (ANSI, auto-disabled when piped)
- 🔌 **Player agnostic** — works with mpv, vlc, ffplay, iina, mplayer, …
- ⚡ **--first / -1** — instantly play the top result, no interaction needed

---

## Requirements

| Tool      | Purpose                        | Install                                |
|-----------|--------------------------------|----------------------------------------|
| **yt-dlp**| YouTube search + stream URLs   | `pip install yt-dlp`                   |
| **mpv**   | Video player (default)         | `sudo apt install mpv` / `brew install mpv` |
| **gcc**   | To compile ytplay              | Usually pre-installed on Linux/macOS   |

> **Alternative players**: vlc, ffplay, iina (macOS), mplayer — any will work.  
> On Windows, VLC is a good choice if mpv isn't installed.

---

## Build

### Linux / macOS

```bash
git clone https://github.com/you/ytplay
cd ytplay
make
```

To install system-wide:

```bash
sudo make install
# now just type: ytplay "your search"
```

### Windows (MSVC)

```cmd
nmake -f Makefile.win
```

### Windows (MinGW/MSYS2)

```bash
gcc -O2 -o ytplay.exe ytplay.c
```

---

## Quick Start

```bash
# Search and pick from a list
ytplay "lofi hip hop"

# Play top result immediately
ytplay -1 "rick astley never gonna give you up"

# Download 1080p and keep the file
ytplay -d -k --1080 "big buck bunny"

# Audio only — great for music/podcasts
ytplay -a "beethoven moonlight sonata"

# Use VLC, show 15 results
ytplay -p vlc -n 15 "open source films"

# Stream with subtitles in English
ytplay --subs en "ted talk climate change"

# 4K if available
ytplay --4k "drone footage 4k"

# Low bandwidth mode
ytplay --worst "podcast episode"
```

---

## All Options

```
SEARCH OPTIONS
  -n, --results <N>             Number of results to show (default: 8, max: 25)
  -1, --first                   Auto-play first result (no menu)
  --no-banner                   Suppress ASCII art (good for scripts)

PLAYBACK OPTIONS
  -s, --stream                  Stream directly — no local file [default]
  -d, --download                Download to temp dir, then play
  -k, --keep                    Keep downloaded file after playback
  -a, --audio-only              Audio only (no video)
  -q, --quality <FMT>           yt-dlp format string (advanced)
      --4k                      Preset: 2160p
      --1080                    Preset: 1080p [default quality]
      --720                     Preset: 720p
      --480                     Preset: 480p
      --360                     Preset: 360p
      --worst                   Worst quality (fastest stream)
      --subs <LANG>             Embed subtitles (e.g. --subs en)

PLAYER OPTIONS
  -p, --player <PLAYER>         Player binary (mpv, vlc, ffplay, iina, …)
      --player-args <ARGS>      Extra flags passed to the player
      --ytdlp-args  <ARGS>      Extra flags passed to yt-dlp

OUTPUT OPTIONS
  -o, --output <DIR>            Download directory (default: system temp)
      --no-color                Disable ANSI colour output
      --quiet                   Minimal output (good for scripting)
  -v, --verbose                 Show full yt-dlp and player commands

MISC
  -h, --help                    Show help
      --version                 Show version
```

---

## How It Works

**Stream mode (default)**  
ytplay calls `yt-dlp` with `ytsearch<N>:query` to get video metadata, displays the results, then either:
- (mpv/iina) passes the YouTube URL directly — these players understand YouTube natively.
- (other players) uses `yt-dlp -g` to get the raw stream URL and pipes it to the player.

**Download mode** (`-d`)  
ytplay calls yt-dlp to download the video to a temp directory (or your chosen `--output` path), then opens it in the player. Use `--keep` to prevent deletion after playback.

---

## Platform Notes

| Platform | Default player | Temp directory     |
|----------|---------------|---------------------|
| Linux    | mpv           | `/tmp/ytplay/`      |
| macOS    | iina or mpv   | `$TMPDIR/ytplay/`   |
| Windows  | mpv or vlc    | `%TEMP%\ytplay\`    |

---

## Scripting Examples

```bash
# Cron / script: no banner, no colour, play first result quietly
ytplay --no-banner --no-color --quiet -1 "ambient music"

# Get the yt-dlp command that would be run
ytplay -v -1 "test video" 2>&1

# Chain with notification
ytplay -1 -d -k --1080 "documentary" && notify-send "ytplay" "Download complete!"
```

---

## License

MIT — do whatever you like.

---

## Credits

Built on the shoulders of:
- [yt-dlp](https://github.com/yt-dlp/yt-dlp) — the backbone of all YouTube interaction
- [mpv](https://mpv.io) — the best open-source video player
>>>>>>> 64bc3a2 (v1.4.0)
