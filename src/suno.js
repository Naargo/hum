// Runs inside a hidden suno.com song page. Suno only plays its audio on its
// own site, so Hum lets Suno's own player do it: this presses the song's Play
// button, watches Suno's <audio> element, and reports back to the engine with
// "HUM " console messages, the same way the Spotify page does.
(() => {
  if (window.hum) return;
  const emit = (o) => console.log("HUM " + JSON.stringify(o));

  // Suno keeps a short silent clip in a second <audio>; the song is the other.
  const song = () => [...document.querySelectorAll("audio")].find((a) => {
    const src = a.currentSrc || a.src;
    return src && !/\/sil-/.test(src);
  });

  const report = (a) => emit({
    t: "sunoState", paused: a.paused, position: Math.round(a.currentTime * 1000),
    duration: isFinite(a.duration) ? Math.round(a.duration * 1000) : 0,
  });

  // Suno's player can swap its <audio> element; keep listening to the current one.
  const watched = new WeakSet();
  let lastTick = 0;
  setInterval(() => {
    const a = song();
    if (!a || watched.has(a)) return;
    watched.add(a);
    for (const e of ["play", "pause", "seeked", "durationchange"]) a.addEventListener(e, () => report(a));
    a.addEventListener("timeupdate", () => {
      const now = Date.now();
      if (now - lastTick > 1000) { lastTick = now; report(a); }
    });
    a.addEventListener("ended", () => emit({ t: "sunoEnded" }));
    report(a);
  }, 400);

  // Start the song with the page's own Play button (the song's, not the
  // playbar's). Press again every few seconds until sound starts, in case the
  // first press landed before Suno's player was ready.
  const started = Date.now();
  let lastPress = 0;
  const kick = setInterval(() => {
    const a = song();
    if (a && !a.paused) { clearInterval(kick); return; }
    const button = document.querySelector('button[aria-label="Play"]');
    if (button && Date.now() - lastPress > 3000) { lastPress = Date.now(); button.click(); }
    if (Date.now() - started > 25000) {
      clearInterval(kick);
      emit({ t: "error", source: "suno", message: "Suno's player didn't start" });
    }
  }, 300);

  // Pause and resume through Suno's playbar so its own state stays in step.
  const playbar = () => [...document.querySelectorAll('button[aria-label^="Playbar: "]')]
    .find((b) => /^Playbar: (Play|Pause) button$/.test(b.getAttribute("aria-label")));

  window.hum = {
    pause() {
      const a = song();
      if (!a || a.paused) return;
      const b = playbar();
      if (b) b.click(); else a.pause();
    },
    resume() {
      const a = song();
      if (!a || !a.paused) return;
      const b = playbar();
      if (b) b.click(); else a.play().catch(() => {});
    },
    seek(ms) {
      const a = song();
      if (a) a.currentTime = ms / 1000;
    },
  };
})();
