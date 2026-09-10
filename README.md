# loom

A minimal retro markdown scratchpad for Linux. Dark chrome, a CRT scanline glow, and optional rain/storm ambiance behind the text — built to feel like a quiet terminal, not an editor.

Markdown stays in the file as markdown. Headings, emphasis, code, and the rest are formatted in place; markers such as `#` hide when the caret leaves the line and come back when you edit it. There's no rendering step and no preview pane — what you see is the raw file, just dressed up while you're not touching it.

loom is built primarily for Hyprland on Arch, with live theme inheritance from [Omarchy](https://omarchy.org). The editor itself has no hard dependency on either — it runs as a normal Q6 app anywhere those libraries are available — but the install instructions and desktop integration below assume that setup.

## Features1

- Live markdown (format-only — the document is always raw text)
- Multiple tabs, unnamed scratch buffers
- Crash-safe session restore (`~/.local/state/loom/`)
- Omarchy theme inheritance, live
- Zen mode: hides tabs and status bar for distraction-free writing
- Ambient rain/storm weather with optional audio
- Keyboard-first, with a searchable `Ctrl+K` cheat sheet (global search + per-section tabs)
- Zoom with `Ctrl` + mouse wheel
- Pipe tables that stay aligned as you type, with `Tab`/`Shift+Tab` cell navigation (`Ctrl+Shift+\` or `/table align` to reflow manually)
- Checkboxes (`[]` / `[x]`, nest with `[[]]`, `[[[]]]`, ... like `*`/`**` indent) render as a clickable square with a checkmark — click to toggle
- Table of contents generation (`/toc`) and a jump-to-heading outline overlay (`Ctrl+Shift+O`)
- PDF export with selectable templates, behind `Ctrl+Shift+P` (no UI chrome)
- Internet radio: a local station library, searchable public directory, and an icon-only mini player in the footer
- In-document anchor links: click (or `Ctrl+Enter`) to follow, `Alt+Left` to jump back. `Ctrl+click` also opens external URLs.
- Optional vaults: point loom at a folder and get a file tree, `[[wikilinks]]`, backlinks and a note switcher. Off by default, and invisible when off.

<img width="1708" height="1362" alt="image" src="https://github.com/user-attachments/assets/a5a10553-2376-4c03-8d26-e949bb714722" />
<img width="1707" height="1362" alt="image" src="https://github.com/user-attachments/assets/8278103b-4d27-42b6-b2a8-4e81a81d023a" />



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

### Releases (GitHub)

Every push builds and runs the test suite (`.github/workflows/ci.yml`). Versioning on `main` is automated with [release-please](https://github.com/googleapis/release-please) (`.github/workflows/release-please.yml`):

1. Write commits against `main` (directly or via PR) using [Conventional Commits](https://www.conventionalcommits.org/) — `feat:` and `fix:` prefixes drive the version bump, `feat!:`/`BREAKING CHANGE:` bump major.
2. release-please keeps a standing `chore: release X.Y.Z` pull request up to date with a generated `CHANGELOG.md` entry and version bumps in `CMakeLists.txt`, `src/app/Application.cpp`, and `PKGBUILD`.
3. Merging that PR tags the commit (`vX.Y.Z`) and publishes a GitHub Release with the changelog notes.

Config lives in `release-please-config.json` and `.release-please-manifest.json` at the repo root.

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

`Ctrl+K` inside the app lists everything. Start typing to search every shortcut *and* slash command at once — matching happens across keys, descriptions and section names, and each tab shows how many hits it holds. `Tab` / `Shift+Tab` cycles the sections (`all`, `files`, `tabs`, `edit`, `tables`, `navigate`, `view`, `radio`, `vault`, `slash`), `Up`/`Down` walks the list, and `Esc` clears the query before it closes the overlay.

The important ones:

| Key | Action |
| --- | --- |
| `Ctrl+S` / `Ctrl+Shift+S` | save / save as |
| `Ctrl+Shift+P` | export pdf (template picker) |
| `Ctrl+,` | settings |
| `Ctrl+T` | theme picker |
| `Ctrl+Shift+T` | next theme |
| `Ctrl+K` | searchable shortcut cheat sheet |
| `Ctrl+N` / `Ctrl+W` | new / close tab |
| `Ctrl+Tab` | cycle tabs |
| `Ctrl+Z` | undo |
| `Ctrl+F` | find |
| `Ctrl+M` | toggle markdown rendering |
| `Ctrl+Shift+F` | zen mode |
| `Ctrl+Shift+O` | outline overlay (jump to heading) |
| `Ctrl+E` | vault tree (only with a vault configured) |
| `Ctrl+Shift+E` | open a note in the vault |
| `Ctrl+Shift+B` | backlinks to this note |
| `Ctrl+Shift+V` | switch vault |
| `Ctrl+Alt+R` | radio library |
| `Ctrl+Alt+P` | radio play / pause |
| `Ctrl+Alt+S` | radio stop |
| `Ctrl+Shift+\` | align the table under the caret |
| `Tab` / `Shift+Tab` (in a table) | move to the next / previous cell |
| `Enter` (in a table) | insert a row (on a blank last row, leave the table) |
| `Ctrl+Shift+Down` / `Up` | insert / delete table row |
| `Ctrl+Shift+Right` / `Left` | insert / delete table column |
| `Ctrl+Enter` | follow a heading link, or insert a line break inside a table cell |
| click a heading link | jump to that heading |
| `Alt+Left` | jump back after following a link |
| `Ctrl` + wheel | zoom |

Slash commands (type `/` at the start of a line): `/toc [depth]` inserts or refreshes a table of contents, `/toc list` and `/outline` open the outline overlay, `/table [NxM|align]` inserts a table skeleton or aligns the current one, `/table row|col` and `/table delrow|delcol` edit the table under the caret, `/pdf [template]` exports a PDF, `/radio [play|pause|stop|add]` drives the radio player (`/radio mini 1|0` hides the footer control, `/radio vol 0-100` sets volume), `/vault [path]` opens a vault or the note switcher (`/vault tree 1|0`, `/vault new`, `/vault links`, `/vault switch`, `/vault reveal`, `/vault off`).

### Vaults

A vault is one folder, and nothing more. Point loom at it in **settings** (`Ctrl+,`) — tick `enable vault`, pick a directory — and notes inside that folder gain a file tree, `[[wikilinks]]`, backlinks and a name-based note switcher. Notes outside it behave exactly as they always have.

The whole feature is off by default, and off is genuinely free: no index, no file watcher, and no sidebar widget is ever built. There is deliberately no visual hint that vaults exist until you turn them on.

It is a place, not a mode. loom never asks "am I in vault mode?", only "is this file inside the vault?" — so a scratch buffer, a stray `/tmp/notes.md` and a vault note can sit in three tabs at once and each does the right thing. Switching vaults does not disturb your open tabs.

`Ctrl+E` shows and hides the tree, and hidden means gone — no collapsed strip, no splitter handle, nothing. It stays hidden in zen mode and comes back when you leave. The vault folder is the single top-level row, so everything you create visibly hangs off one named parent, and names are shown without their extension (`monday`, not `monday.md`). In the tree, `Enter` opens (or expands), `Alt+N` and `Alt+D` create a note or folder, `Alt+R` renames, `Delete` removes, and dragging moves things. Renaming or moving a file that is open in a tab retargets the tab instead of leaving it pointing at a path that no longer exists. Right-click for the same actions plus **show in file manager**, which opens the folder — or the containing folder with the file selected — in your desktop file manager.

New notes are markdown: type `meeting` and you get `meeting.md`. If you want something else, say so explicitly (`scratch.txt`) and that is what you get. Because extensions are hidden, renaming keeps the one the file already had — typing `tuesday` over `monday` gives you `tuesday.md`, never a suffixless file the vault would stop treating as a note.

| Link | Means |
| --- | --- |
| `[[note]]` | the note named "note", found anywhere in the vault |
| `[[note\|label]]` | same target, different link text |
| `[[note#heading]]` | straight to that heading |

Click a `[[link]]` (no modifier needed, like a heading link) or press `Ctrl+Enter` on it. If the note does not exist yet, following the link creates it, seeded with an H1 — but only ever inside a vault, so a plain scratchpad never grows files behind your back. `Alt+Left` jumps back, across files as well as within one. When two notes share a name, a sibling in the same folder wins, then the shallowest path.

Links only ever open files loom can edit as text (`.md`, `.markdown`, `.txt`); a link pointing at an image or any other asset is handed to the desktop instead of being decoded into a buffer. Targets that try to climb out of the vault with `..` or an absolute path are refused rather than followed.

Turning the vault off does not break documents you wrote with it on: a `[[link]]` still resolves, just relative to the file it sits in rather than by name across a tree. Same syntax, same click, narrower reach — which also makes wikilinks quietly useful with no vault at all.

Backlinks (`Ctrl+Shift+B`) are computed when you ask for them rather than kept warm in the background, since they are the only vault operation that needs to read file contents. They use the editor's own markdown parser, so a link inside a code fence is not counted.

One syntax note: `[[` is also loom's nested-checkbox marker. A checkbox wins at the start of a line, so `[[]] task` and `[[x]] done` stay checkboxes; `[[note]]` has content between the brackets and stays a link. The only casualty is a note named exactly `x`, which cannot be addressed at the very start of a line.

Vault metadata — which vaults you have opened, tree width — lives in `~/.local/state/loom/vaults.json`. Nothing is ever written *into* the vault folder: no dotfolder, no index, no sidecar files. It stays a plain directory of plain markdown that any other tool can read.

### PDF export

`Ctrl+Shift+P` (or `/pdf`) opens a template picker and writes the current tab to a PDF. There is no button for it — markdown stays the point of the app, and export is meant to stay out of the way until you want it. `/pdf <template>` skips the picker. loom remembers the last template you used.

| Template | Look |
| --- | --- |
| `paper` | white A4, print-first margins, page footer (default) |
| `terminal` | dark page in the live loom theme, accent headings |
| `manuscript` | 12pt, double spaced, 25mm margins, for edits |
| `compact` | 9pt, tight margins, no footer, for reference sheets |
| `plain` | no styling at all, raw markdown html |

Checkboxes export as `[ ]` / `[x]` with no bullet in front, `*`/`**`/`***` nesting becomes real nested lists, `<!-- toc -->` markers are dropped while the generated list is kept, and images resolve the same way the editor resolves them (absolute, then next to the file, then `~/.local/state/loom/media/`), scaled down to the text column when they are wider than the page.

<img width="1709" height="1364" alt="image" src="https://github.com/user-attachments/assets/02b6e0b6-f8d2-4bd6-85f7-6cf0629b7fd9" />


### Radio

`Ctrl+Alt+R` (or `/radio`) opens the station library. Stations are yours and local — nothing syncs anywhere. `enter` plays, `alt+f` favorites, `alt+d` removes, `alt+a` opens the manual add form. `tab` cycles `all` / `favorites` / `recent` / `browse`, and the genre dropdown narrows the list further.

The `browse` tab searches [Radio Browser](https://www.radio-browser.info), which ships pre-configured — no key, no account, nothing to set up. It picks a mirror by DNS SRV lookup so one server going down does not break discovery. Results are throwaway until you press `enter` to save one; stations the directory could not reach recently are dimmed and sorted last. Genres are always picked from the directory's own tag vocabulary rather than typed, so filtering stays consistent for manual and directory-added stations alike.

Once something is playing, a play/pause and stop pair sits in the footer next to the theme name. It only appears once you have saved at least one station, clicking the space around the icons reopens the dialog on the playing station, and stop tears the stream down rather than pausing it. The `show mini player` checkbox in the dialog (or `/radio mini 0`) hides it for good. It is not visible in zen mode, where `Ctrl+Alt+P` / `Ctrl+Alt+S` and the slash commands take over.

`.pls` and `.m3u` playlists are fetched and unwrapped once to find the real stream. Dropped streams reconnect with a short backoff before giving up. HLS (`.m3u8`) is not supported yet.

## Files

- Config: `~/.config/loom/config.toml`
- Session: `~/.local/state/loom/session.json`
- Scratch notes: `~/.local/state/loom/scratch/`
- Radio stations: `~/.local/state/loom/stations.json`
- Known vaults: `~/.local/state/loom/vaults.json` (only written once you enable a vault)

Unnamed tabs survive reboot. Named files autosave by default (toggle in settings).

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
