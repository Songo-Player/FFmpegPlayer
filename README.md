## FFmpegPlayer
An audio streaming GDExtension for godot, powered by ffmpeg

### Setup and Build
- This repo uses git submodules so clone with this `git clone --recurse-submodules git@github.com:keymaster777/FFmpegPlayer.git`
- Run the appropriate mbedTLS build script (`build_mbedtls_*.sh`), then the matching ffmpeg build script, or check their contents and config/build by yourself. mbedTLS must be built first since FFmpeg links against it for https support
- Run scons for your environment/target
- copy the resulting bin to your app and ensure you add a proper .gdextension file, check godot gdextension documention when in doubt

### Web Radio / Network Streams
`play()` accepts http(s) URLs as well as local paths, including Icecast/Shoutcast streams and HLS (`.m3u8`) playlists. Opening happens on the decode thread, so `play()` returns immediately.
- Listen for the `error(message)` signal to catch unreachable or broken streams, and `stream_title_changed(title)` for Icecast/Shoutcast "now playing" metadata (also available via `get_stream_title()`)
- `is_live()` is true for streams without a known duration; live streams are not seekable, and `resume()` after `pause()` reconnects to the live broadcast
- `stream_buffer_length` (default 1s) is used instead of `buffer_length` for live streams to absorb network jitter. `network_timeout` and `user_agent` are also configurable
- Plain `.m3u`/`.pls` playlist files (lists of stream URLs) are not parsed; read the URLs out of them in your game code and pass one to `play()`
- On Android, enable the `INTERNET` permission in your export preset. TLS certificates are not verified

### Background Player
`bgplayer/` holds `songo_bgplayer`, a small standalone player built on the same FFmpeg/mbedTLS libraries. Songo hands it the play queue when it exits with music, so playback carries on outside the app on Linux handhelds. It plays through ALSA's `default` device, so it doesn't need Godot or a display.
- Build it on (or in a chroot of) the target architecture with `./build_bgplayer.sh arm64` (or `x86_64`), after the matching mbedTLS and FFmpeg builds. It needs the ALSA headers (`libasound2-dev`); libasound itself is linked dynamically, libstdc++/libgcc statically. Output goes to `bin/songo_bgplayer.linux.<arch>`
- Run it as `songo_bgplayer <playlist.m3u> [--blend SECONDS]`. `--blend` crossfades between songs like Songo's playback blend
- The playlist is a normal m3u (local paths or http(s) URLs). Songo's playback state rides along as comment lines other m3u readers ignore:
  - `#SONGO-START-INDEX:<n>` and `#SONGO-START-POSITION:<seconds>` for where to resume
  - `#SONGO-REPEAT-ONE:1` to repeat the current song
  - `#SONGO-VOLUME:<0-1>` for the music volume
  - `#SONGO-EQ:<dB>,<dB>,...` for the 10 EQ band gains, only present when Songo's EQ is on
- Controls are read straight from `/dev/input` without grabbing, so other apps still see every press: hold Select for 1s to quit, Select+Right/Left for next/previous (or restart within the first 3s), Select+Up/Down to change the volume. SIGTERM also quits
- The queue loops like Songo's. Songs that fail to open are skipped, and it quits if nothing in the playlist plays
- The EQ is a port of Godot's `AudioEffectEQ10` (`bgplayer/godot_eq.h`), so it sounds the same as Songo with its EQ on

### AI Usage Disclaimer
- This extension was written with some AI assistance, namely around resolving compilation errors in packaging