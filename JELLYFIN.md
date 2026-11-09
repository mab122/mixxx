# Jellyfin in Mixxx (native)

> [!WARNING]
> **Unofficial fork with vibecoded (AI-generated) Jellyfin support.**
> The Jellyfin integration and the related library changes in this fork were
> written by an AI agent (Claude, Anthropic) at the request of mab122. They have
> not been reviewed or endorsed by the Mixxx project. Please do not report
> problems of this fork to Mixxx.

This fork (branch `feature/jellyfin`, based on Mixxx `main` / 2.7-alpha)
adds a **Jellyfin** entry to the library sidebar, next to iTunes, Rekordbox and Serato.
It needs no external tools: no Python, no FUSE, no daemon.

Tested against Jellyfin **12.4.0** (`jellyfin/jellyfin:latest`, Nov 2026). Works with 10.9+.

## Usage

1. Click **Jellyfin** in the sidebar. A login dialog asks for the server address
   (e.g. `http://192.168.1.10:8096` or `https://jellyfin.example.com`), user name and password.
2. The library syncs in the background (title shows `Jellyfin (syncing 1000/5321)`).
   The sidebar shows **Playlists**, **Artists** (→ albums), **Albums** and **Genres**.
3. Downloads never block the UI:
   - a track that stays selected for 0.5 s is downloaded in the background
     (scrolling through the library downloads nothing),
   - loading to a deck (double-click, drag & drop, controller, context menu) or the
     preview button downloads immediately and loads the track as soon as it is there —
     unless another track was loaded into that deck (or it was ejected / started
     playing) in the meantime,
   - progress is shown in the sidebar: `Jellyfin (↓ 2 · 45%)`.
   Analysis, cues, loops and hot cues work like for any local file.
4. Right-click a playlist / artist / album / genre: **Download for Offline Use**,
   **Download and Analyze**, **Remove Downloaded Files**, **Add to Auto DJ**,
   **Import as Playlist / Crate**.
5. Right-click **Jellyfin**: **Sync with Server**, download/analyze the whole library,
   **Clear Download Cache**, log in / log out.

Preferences → **Jellyfin**: account, cache folder, cache usage, clear cache.
Nothing is ever deleted automatically.

## How it works

| Piece | File | Job |
| --- | --- | --- |
| `JellyfinClient` | `src/library/jellyfin/jellyfinclient.*` | REST API: `/System/Info/Public`, `/Users/AuthenticateByName`, `/Items` (paged, 1000/page), `/Playlists/{id}/Items`, `/Audio/{id}/stream?static=true`, `/Sessions/Logout` |
| `JellyfinDAO` | `jellyfindao.*` | `jellyfin_library`, `jellyfin_playlists`, `jellyfin_playlist_tracks` shadow tables in `mixxxdb.sqlite`. They persist, so the library is browsable offline |
| `JellyfinCache` | `jellyfincache.*` | Background download queue (2 parallel; user requests first, bulk downloads last; only the 3 most recent prefetches are kept). Files land in `<system cache dir>/jellyfin/<itemId>.<ext>` (shown in Preferences). Manual cleanup only, files loaded in a deck are kept |
| Track models | `jellyfintrackmodel.*` | `isTrackAvailable()` = file cached; `getTrack()` never blocks; `requestTrack()` downloads in the background |
| Core hook | `src/library/trackmodel.h`, `widget/wtracktableview.cpp`, `widget/wtrackmenu.cpp`, `library/tabledelegates/previewbuttondelegate.cpp` | New `TrackModel::isTrackAvailable()` / `requestTrack()` (defaults keep the old behaviour); the views use them for prefetch-on-selection and deferred loading |
| Core hook | `src/library/remotetrackprovider.*`, `util/dnd.cpp`, `library/library.cpp`, `mixer/basetrackplayer.cpp` | `RemoteTrackProvider`: drop targets accept paths of not-yet-downloaded tracks and load them after the download. `DeferredTrackLoad`: per-deck token, the latest load request wins; every load/eject into a deck cancels pending deferred loads |
| `JellyfinFeature` | `jellyfinfeature.*` | Sidebar entry, menus, sync on startup / login |
| Login dialog | `dlgjellyfinlogin.*` | Server address + credentials |
| Preferences page | `src/preferences/dialog/dlgprefjellyfin.*` | Account + cache settings |

The cache path of a track never changes, so its Mixxx library entry (BPM, key, beatgrid,
cues, waveform) survives deleting and re-downloading the file.

Security: the password is never stored. Only the access token issued by the server is kept
in `mixxx.cfg` (`[Jellyfin]` group). Revoke it in the Jellyfin dashboard → Devices, or with
*Log out*. Authentication uses the modern `Authorization: MediaBrowser ... Token=` header
(legacy `X-Emby-*` headers are disabled by default since Jellyfin 10.11).

## Build

```bash
# Arch: sudo pacman -S microsoft-gsl   (or pass -isystem to a GSL checkout, see below)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
ninja -C build mixxx
./build/mixxx
```

Without `microsoft-gsl` installed: `git clone --depth 1 -b v4.2.0 https://github.com/microsoft/GSL build/_gsl`
and add `-DCMAKE_CXX_FLAGS="-isystem $PWD/build/_gsl/include"`.

## Tests

```bash
ninja -C build mixxx-test
QT_QPA_PLATFORM=offscreen ./build/mixxx-test --gtest_filter='JellyfinTest.*'

# end-to-end against a real server
MIXXX_JELLYFIN_TEST_URL=http://127.0.0.1:8096 MIXXX_JELLYFIN_TEST_USER=dj \
MIXXX_JELLYFIN_TEST_PASSWORD=... QT_QPA_PLATFORM=offscreen \
./build/mixxx-test --gtest_filter='JellyfinTest.*'
```

## Not done yet

- Adding not-yet-downloaded tracks to Auto DJ from the track table skips them
  (use the sidebar context menu, which downloads first).
- Writing BPM/key back to Jellyfin (Jellyfin has no API for custom audio tags).
- Cover art for tracks that are not cached yet.
- Quick Connect login.
