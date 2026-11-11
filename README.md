# Hum

Spotify and Suno in one small panel that slides out of the Omarchy bar.

- `src/`: the engine (`hum`), a small Qt program. A hidden page runs Spotify's
  Web Playback SDK (Spotify needs Widevine, so it takes a browser engine) and
  plays Suno MP3s. It talks to Spotify's Web API and Suno's site API, exposes
  MPRIS for media keys, and quits after 20 minutes with nothing playing.
- `plugin/`: the Omarchy shell plugin (`tom.hum`): the bar icon and panel.
  It talks to the engine over `$XDG_RUNTIME_DIR/hum.sock` and starts it on demand.

## Keys

| Key | Does |
| --- | --- |
| F8 | play / pause |
| F8 twice, quickly | open the panel |
| Tab, 1, 2 | Spotify / Suno |
| / | search Spotify |
| Up/Down, Enter | move, play or open |
| Left | back |
| Space, n, b | play/pause, next, previous |
| Esc | close |

## Build

    bin/build    # build/hum
