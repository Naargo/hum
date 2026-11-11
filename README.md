# Hum

Running a music player at 1.2 GB of RAM is absurd. It plays songs. It is not
rendering a moon landing.

Hum is Spotify and Suno in one small panel that slides out of the
[Omarchy](https://omarchy.org) bar. Idle, it uses about 140 MB. Playing, about
170 MB. After 20 minutes with nothing playing, it quits and uses nothing.

- `src/`: the engine (`hum`), a small Qt program. A hidden page runs Spotify's
  Web Playback SDK (Spotify needs Widevine, so it takes a browser engine) and
  plays Suno MP3s. It talks to Spotify's Web API and Suno's site API, exposes
  MPRIS so your media keys work, and quits after 20 minutes with nothing playing.
- `plugin/`: the Omarchy shell plugin (`tom.hum`): the bar icon and panel.
  It talks to the engine over `$XDG_RUNTIME_DIR/hum.sock` and starts it when needed.

Spotify playback needs Spotify Premium (Spotify's rule, not mine).

## Install

You need Qt 6 with WebEngine, and a Widevine CDM. Brave, Chrome or Chromium
already gives you one.

    sudo pacman -S --needed qt6-base qt6-webengine
    git clone https://github.com/Naargo/hum.git ~/hum
    cd ~/hum && bin/build
    ln -s ~/hum/build/hum ~/.local/bin/hum
    ln -s ~/hum/plugin ~/.config/omarchy/plugins/hum

Then add `{ "id": "tom.hum" }` to the bar's widgets in
`~/.config/omarchy/shell.json`.

## Connect Spotify

Spotify only lets each person play through their own developer app, so:

1. Go to [developer.spotify.com/dashboard](https://developer.spotify.com/dashboard)
   and create an app.
2. Set the redirect URI to `http://127.0.0.1:8898/callback` and tick **Web API**
   and **Web Playback SDK**.
3. Copy the Client ID, open Hum's panel, paste it in, and connect.

## Suno

Sign in from the Suno tab. This part is new and not well tested yet.

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

F8 needs a Hyprland binding that sends `hum.mediaKey` to the shell. Any MPRIS
media key works for plain play/pause.

## License

MIT
