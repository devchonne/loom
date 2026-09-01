# loom

A minimal retro markdown scratchpad for Linux. Dark chrome, a CRT scanline glow, and optional rain/storm ambiance behind the text — built to feel like a quiet terminal, not an editor.

Markdown stays in the file as markdown. Headings, emphasis, code, and the rest are formatted in place; markers such as `#` hide when the caret leaves the line and come back when you edit it. There's no rendering step and no preview pane — what you see is the raw file, just dressed up while you're not touching it.

loom is built primarily for Hyprland on Arch, with live theme inheritance from [Omarchy](https://omarchy.org). The editor itself has no hard dependency on either — it runs as a normal Qt6 app anywhere those libraries are available — but the install instructions and desktop integration below assume that setup.

## Features

- Live markdown (format-only — the document is always raw text)
- Multiple tabs, unnamed scratch buffers
- Crash-safe session restore (`~/.local/state/loom/`)
- Omarchy theme inheritance, live
- Zen mode: hides tabs and status bar for distraction-free writing
- Ambient rain/storm weather with optional audio
- Keyboard-first, with a `Ctrl+K` cheat sheet
- Zoom with `Ctrl` + mouse wheel

## Develop

Install build dependencies once:

```bash
sudo pacman -S --needed qt6-base qt6-multimedia qt6-wayland tomlplusplus md4c cmake ninja gcc pkgconf gtest
```

Configure a debug build in `build/` and run it from the repo. This binary is only for iterating — Walker will not see it until you install (next section).

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/loom
```

Rebuild after C++ changes; restarting an old `./build/loom` will not pick them up.

```bash
cmake --build build && ./build/loom
```

Tests:

```bash
ctest --test-dir build --output-on-failure
```

## Install (Walker)

This installs a Release build into your user prefix so **Walker** (Super+Space) lists **loom** with its icon.

One-time configure:

```bash
cmake -S . -B build-release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$HOME/.local"
```

Install (and re-install after a new version):

```bash
cmake --build build-release
ctest --test-dir build-release --output-on-failure
cmake --install build-release
update-desktop-database ~/.local/share/applications
gtk-update-icon-cache -f ~/.local/share/icons/hicolor 2>/dev/null || true
```

That writes:

| Path | What |
| --- | --- |
| `~/.local/bin/loom` | the app (`~/.local/bin` is on PATH) |
| `~/.local/share/applications/loom.desktop` | Walker / gtk-launch entry |
| `~/.local/share/icons/hicolor/scalable/apps/loom.svg` | vector icon |

Search Walker for `loom`, `markdown`, or `scratchpad`. Open Walker again if an already-running session has not picked up the new entry yet.

### New version

1. Develop and test with `./build/loom`.
2. When you want Walker to launch that version, run the install block above (`cmake --build` / `cmake --install`).
3. Optional: bump `project(loom VERSION …)` in `CMakeLists.txt` and `setApplicationVersion` in `src/app/Application.cpp`.

To uninstall the Walker entry and binary:

```bash
rm -f ~/.local/bin/loom \
      ~/.local/share/applications/loom.desktop \
      ~/.local/share/icons/hicolor/scalable/apps/loom.svg
update-desktop-database ~/.local/share/applications
```

### Optional: pacman package

`makepkg -si` from the repo root builds the same Release tree and installs to `/usr` instead of `~/.local`. The PKGBUILD relocates makepkg's work directory under `/tmp/loom-pkgbuild` so it cannot clobber this repo's `src/` tree. Bump `pkgver` in `PKGBUILD` when you cut a packaged release.

## Shortcuts

`Ctrl+K` inside the app lists everything. The important ones:

| Key | Action |
| --- | --- |
| `Ctrl+S` / `Ctrl+Shift+S` | save / save as |
| `Ctrl+,` | settings |
| `Ctrl+T` | theme picker |
| `Ctrl+Shift+T` | next theme |
| `Ctrl+K` | shortcut cheat sheet |
| `Ctrl+N` / `Ctrl+W` | new / close tab |
| `Ctrl+Tab` | cycle tabs |
| `Ctrl+Z` | undo |
| `Ctrl+F` | find |
| `Ctrl+M` | toggle markdown rendering |
| `Ctrl+Shift+F` | zen mode |
| `Ctrl` + wheel | zoom |

## Files

- Config: `~/.config/loom/config.toml`
- Session: `~/.local/state/loom/session.json`
- Scratch notes: `~/.local/state/loom/scratch/`

Unnamed tabs survive reboot. Named files autosave by default (toggle in settings).

## Hyprland

Suggested rules so it sits like a floating notepad:

```
windowrule = float, class:^(loom)$
windowrule = size 60% 70%, class:^(loom)$
windowrule = opacity 0.96, class:^(loom)$
bind = SUPER, N, exec, loom
```

## Theme

Default is **omarchy (live)** — loom reads

`~/.local/state/omarchy/current/theme/colors.toml`

and recolors when you switch desktop themes.

Pin a palette from **settings** (`Ctrl+,`) or the theme picker (`Ctrl+T`). `Ctrl+Shift+T` cycles.

Hacker: phosphor, amber crt, hotline, rootkit, soviet.

Chill: tokyo night, nord mist, matcha, dusk, paper lantern.

## Typography

Body and UI chrome default to the bundled **Departure Mono** (SIL OFL), then iA Writer Mono S, JetBrains Mono, and Noto Sans Mono.

## License

MIT — see [LICENSE](LICENSE). Departure Mono is bundled separately under the SIL Open Font License (`resources/fonts/OFL.txt`).
