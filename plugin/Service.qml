import QtQuick
import Quickshell
import Quickshell.Io

// The panel's line to the hum engine: JSON lines over a unix socket.
//
// The engine runs only while there's music. Asking it anything starts it
// when it isn't running; merely opening the shell never does.
Item {
  id: svc

  readonly property string socketPath: (Quickshell.env("XDG_RUNTIME_DIR") || "/tmp") + "/hum.sock"
  // The engine's latest state push, or null while it isn't running.
  property var st: null
  readonly property bool up: sock.connected
  property bool starting: false
  property string startError: ""

  property int _seq: 0
  property var _pending: ({})
  property var _queue: []
  property int _tries: 0
  property bool _launched: false

  Socket {
    id: sock
    path: svc.socketPath
    parser: SplitParser {
      onRead: function(line) { svc._handle(line) }
    }
    onConnectedChanged: {
      if (connected) {
        retry.stop()
        svc.starting = false
        svc.startError = ""
        var queued = svc._queue
        svc._queue = []
        for (var i = 0; i < queued.length; i++) sock.write(queued[i])
        if (queued.length) sock.flush()
      } else {
        svc.st = null
        var pending = svc._pending
        svc._pending = {}
        for (var id in pending) pending[id]({ ok: false, error: "The music engine stopped" })
      }
    }
  }

  // Connecting to a socket nobody listens on just fails quietly, so retry on
  // a timer: the second miss starts the engine, and Chromium takes a moment.
  Timer {
    id: retry
    interval: 350
    repeat: true
    onTriggered: {
      if (sock.connected) { stop(); return }
      svc._tries++
      if (svc._tries === 2 && !svc._launched) {
        svc._launched = true
        Quickshell.execDetached(["hum"])
      }
      if (svc._tries > 45) {
        stop()
        svc.starting = false
        svc.startError = "The music engine didn't start. Is hum installed?"
        var queued = svc._queue
        svc._queue = []
        return
      }
      sock.connected = false
      sock.connected = true
    }
  }

  // Connect if the engine is already running, without starting it.
  function probe() {
    if (!sock.connected) sock.connected = true
  }

  function ensure() {
    if (sock.connected || retry.running) return
    _tries = 0
    _launched = false
    starting = true
    startError = ""
    sock.connected = true
    retry.start()
  }

  function call(cmd, args, cb) {
    var id = ++_seq
    if (cb) _pending[id] = cb
    var line = JSON.stringify({ id: id, cmd: cmd, args: args || {} }) + "\n"
    if (sock.connected) {
      sock.write(line)
      sock.flush()
    } else {
      _queue.push(line)
      ensure()
    }
  }

  function _handle(line) {
    var msg
    try { msg = JSON.parse(line) } catch (e) { return }
    if (msg.event === "state") { st = msg.data; return }
    var cb = _pending[msg.id]
    if (cb) {
      delete _pending[msg.id]
      cb(msg)
    }
  }

  Component.onCompleted: probe()
}
