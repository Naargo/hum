import QtQuick
import QtQuick.Controls
import Quickshell
import Quickshell.Widgets
import qs.Commons
import qs.Commons as Commons
import qs.Ui

// Hum: a music icon in the bar whose panel plays Spotify and Suno.
//
// The panel is only a remote. The sound comes from the hum engine (see
// ~/Work/hum/src), which runs while there's music and quits when there's
// none, so the shell itself stays as light as it was.
Panel {
  id: root
  moduleName: "hum"
  ipcTarget: "hum"
  manageIpc: false

  // Spotify's dark look: near-black panel, white text, grey for the quiet
  // bits, green for what's playing.
  readonly property color fg: "#ffffff"
  readonly property color dim: "#b3b3b3"
  readonly property color accent: "#1ed760"
  readonly property color base: "#121212"
  readonly property color hover: "#2a2a2a"
  readonly property color chip: "#2a2a2a"
  readonly property string font: "Noto Sans"

  function glyph(cp) { return String.fromCodePoint(cp) }
  readonly property string gMusic: glyph(0xF075A)
  readonly property string gPlay: glyph(0xF040A)
  readonly property string gPause: glyph(0xF03E4)
  readonly property string gNext: glyph(0xF04AD)
  readonly property string gPrev: glyph(0xF04AE)
  readonly property string gBack: glyph(0xF004D)
  readonly property string gList: glyph(0xF0CB8)
  readonly property string gHeart: glyph(0xF02D1)
  readonly property string gSearch: glyph(0xF0349)

  Service { id: svc }

  readonly property var st: svc.st
  readonly property var now: st && st.now ? st.now : null
  readonly property var spotify: st ? st.spotify : null
  readonly property var suno: st ? st.suno : null

  // ---- what the panel is showing ------------------------------------------

  property string tab: "spotify"
  property string query: ""
  property var spLists: []
  property var spOpen: null
  property var spTracks: []
  property bool spRestricted: false
  property var spResults: null
  property var suLists: []
  property var suOpen: null
  property var suTracks: []
  property var recentTracks: []
  // Sub-tabs under each source. Liked (and Suno's My songs, and Spotify's
  // Recent) are views of their own, not tiles among the playlists.
  property string spSub: "lists"
  property string suSub: "lists"
  readonly property string sub: tab === "suno" ? suSub : spSub
  readonly property var subTabs: tab === "suno"
    ? [{ id: "lists", label: "Playlists" }, { id: "liked", label: "Liked", heart: true }, { id: "mine", label: "My songs" }]
    : [{ id: "lists", label: "Playlists" }, { id: "liked", label: "Liked Songs", heart: true }, { id: "recent", label: "Recent" }]
  property var subTracks: ({})  // "spotify:liked", "suno:mine", ... -> tracks
  property bool recentNeedsReconnect: false
  property bool loading: false
  property string message: ""
  property int cursor: 0
  property bool cursorActive: false

  readonly property bool spotifyReadyToBrowse: !!(spotify && spotify.loggedIn)
  readonly property bool sunoReadyToBrowse: !!(suno && suno.loggedIn)
  readonly property bool spotifyLoggingIn: !!(spotify && spotify.loggingIn)

  // Rows of the list area, as plain objects. "kind" decides how one looks
  // and what Enter does with it.
  readonly property var rows: {
    var out = []
    if (!st || !st.spotify || !st.suno) return out
    // Read straight from st: the spotify/suno shorthands can lag a beat
    // behind it while a new state lands.
    var spotify = st.spotify, suno = st.suno
    if (tab === "spotify") {
      if (!spotify.configured) return out
      if (!spotify.loggedIn) {
        out.push({ kind: "action", id: "spotify.login", label: spotify.loggingIn ? "Waiting for your browser… (click to open it again)" : "Connect Spotify", glyph: gMusic })
        return out
      }
      if (query !== "") {
        if (!spResults) return out
        if (spResults.tracks.length) out.push({ kind: "header", label: "Songs" })
        for (var i = 0; i < spResults.tracks.length; i++)
          out.push({ kind: "track", source: "search", index: i, item: spResults.tracks[i] })
        if (spResults.playlists.length) out.push({ kind: "header", label: "Playlists" })
        for (var j = 0; j < spResults.playlists.length; j++)
          out.push({ kind: "list", item: spResults.playlists[j] })
        if (!spResults.tracks.length && !spResults.playlists.length) out.push({ kind: "note", label: "Nothing found" })
        return out
      }
      if (spSub === "recent") {
        if (recentNeedsReconnect) {
          out.push({ kind: "action", id: "spotify.login", label: spotify.loggingIn ? "Waiting for your browser… (click to open it again)" : "Reconnect Spotify to see what you played recently", glyph: gMusic })
          return out
        }
        for (var r = 0; r < recentTracks.length; r++) out.push({ kind: "track", source: "recent", index: r, item: recentTracks[r] })
        if (!recentTracks.length && !loading) out.push({ kind: "note", label: "Nothing played yet" })
        return out
      }
      if (spSub === "liked") return subRows("spotify:liked", "No liked songs yet")
      if (spOpen) {
        out.push({ kind: "back", label: spOpen.name })
        if (spRestricted) {
          out.push({ kind: "note", label: "Spotify only lists the songs of playlists you made. This one still plays." })
          out.push({ kind: "action", id: "spotify.playContext", label: "Play " + spOpen.name, glyph: gPlay })
        }
        for (var k = 0; k < spTracks.length; k++) out.push({ kind: "track", source: "spotify", index: k, item: spTracks[k] })
        return out
      }
      for (var m = 0; m < spLists.length; m++) {
        if (spLists[m].id === "liked") continue  // it has its own sub-tab
        var section = spLists[m].section
        if (section && section !== (m > 0 ? spLists[m - 1].section : undefined)) out.push({ kind: "header", label: section })
        out.push({ kind: "list", item: spLists[m] })
      }
      return out
    }
    if (!suno.loggedIn) {
      out.push({ kind: "action", id: "suno.importBrave", label: "Use my Suno sign-in from Brave", glyph: gMusic })
      out.push({ kind: "action", id: "suno.login", label: suno.loggingIn ? "Finish signing in, in the Suno window" : "Sign in to Suno here", glyph: gMusic })
      return out
    }
    if (suSub !== "lists") return subRows("suno:" + suSub, "No songs here")
    if (suOpen) {
      out.push({ kind: "back", label: suOpen.name })
      for (var n = 0; n < suTracks.length; n++) out.push({ kind: "track", source: "suno", index: n, item: suTracks[n] })
      if (!suTracks.length && !loading) out.push({ kind: "note", label: "No songs here" })
      return out
    }
    for (var p = 0; p < suLists.length; p++) {
      if (suLists[p].id === "liked" || suLists[p].id === "mine") continue  // sub-tabs
      out.push({ kind: "list", item: suLists[p] })
    }
    return out
  }

  // The songs of a sub-tab (Liked, My songs) as rows.
  function subRows(key, empty) {
    var out = [], tracks = subTracks[key] || []
    for (var i = 0; i < tracks.length; i++)
      out.push({ kind: "track", source: "sub", key: key, suno: key.indexOf("suno:") === 0, index: i, item: tracks[i] })
    if (!tracks.length && !loading) out.push({ kind: "note", label: empty })
    return out
  }

  function selectable(row) { return row && row.kind !== "header" && row.kind !== "note" }

  function playingThis(row) {
    if (!now || row.kind !== "track") return false
    return now.id === (row.source === "suno" || row.suno ? row.item.id : row.item.uri)
  }

  // ---- talking to the engine ----------------------------------------------

  function report(reply) {
    if (reply && !reply.ok) message = reply.error || "Something went wrong"
  }

  function loadLists() {
    if (tab === "spotify" && spotifyReadyToBrowse && !spLists.length) {
      loading = true
      svc.call("spotify.playlists", {}, function(r) { loading = false; if (r.ok) spLists = r.data; else report(r) })
    }
    // Liked and My songs load once; Recent reloads every time, so it's current.
    var ready = tab === "suno" ? sunoReadyToBrowse : spotifyReadyToBrowse
    var key = tab + ":" + sub
    if (ready && (sub === "liked" || sub === "mine") && !subTracks[key]) {
      loading = true
      svc.call(tab === "suno" ? "suno.tracks" : "spotify.tracks", { id: sub }, function(r) {
        loading = false
        if (!r.ok) { report(r); return }
        var next = Object.assign({}, subTracks)
        next[key] = r.data.tracks
        subTracks = next
      })
    }
    if (tab === "spotify" && spSub === "recent" && spotifyReadyToBrowse) {
      loading = true
      svc.call("spotify.tracks", { id: "recent" }, function(r) {
        loading = false
        if (r.ok) { recentTracks = r.data.tracks; recentNeedsReconnect = false; return }
        if ((r.error || "").indexOf("Reconnect") === 0) recentNeedsReconnect = true
        else report(r)
      })
    }
    if (tab === "suno" && sunoReadyToBrowse && !suLists.length) {
      loading = true
      svc.call("suno.playlists", {}, function(r) { loading = false; if (r.ok) suLists = r.data; else report(r) })
    }
  }

  function openList(item) {
    message = ""
    cursor = 0
    if (tab === "spotify") {
      query = ""
      searchField.text = ""
      spOpen = item
      spTracks = []
      spRestricted = false
      loading = true
      svc.call("spotify.tracks", { id: item.id }, function(r) {
        loading = false
        if (!r.ok) { report(r); return }
        spTracks = r.data.tracks
        spRestricted = r.data.restricted
      })
    } else {
      suOpen = item
      suTracks = []
      loading = true
      svc.call("suno.tracks", { id: item.id }, function(r) {
        loading = false
        if (r.ok) suTracks = r.data.tracks; else report(r)
      })
    }
    list.positionViewAtBeginning()
  }

  function back() {
    if (tab === "spotify" && query !== "") { searchField.text = ""; return }
    if (tab === "spotify") spOpen = null
    else if (tab === "suno") suOpen = null
    cursor = 0
    list.positionViewAtBeginning()
  }

  function playRow(row) {
    message = ""
    if (row.source === "suno") {
      svc.call("suno.play", { list: suOpen.id, index: row.index }, report)
    } else if (row.source === "sub" && row.suno) {
      // The engine only plays a Suno list it has loaded (it forgets them
      // when it restarts), so load it first.
      var id = row.key.slice(5), index = row.index
      svc.call("suno.tracks", { id: id }, function(r) {
        if (r.ok) svc.call("suno.play", { list: id, index: index }, report); else report(r)
      })
    } else if (row.source === "sub") {
      svc.call("spotify.play", { uris: subTracks[row.key].map(function(t) { return t.uri }), index: row.index }, report)
    } else if (row.source === "recent") {
      svc.call("spotify.play", { uris: recentTracks.map(function(t) { return t.uri }), index: row.index }, report)
    } else if (row.source === "search") {
      svc.call("spotify.play", { uris: spResults.tracks.map(function(t) { return t.uri }), index: row.index }, report)
    } else if (spOpen && spOpen.uri) {
      svc.call("spotify.play", { context: spOpen.uri, uri: row.item.uri }, report)
    } else {
      // Liked Songs has no Spotify context to play from.
      svc.call("spotify.play", { uris: spTracks.map(function(t) { return t.uri }), index: row.index }, report)
    }
  }

  function playOpen() {
    message = ""
    if (tab === "suno" && suOpen) svc.call("suno.play", { list: suOpen.id, index: 0 }, report)
    else if (spOpen && spOpen.uri) svc.call("spotify.play", { context: spOpen.uri }, report)
    else if (spTracks.length) svc.call("spotify.play", { uris: spTracks.map(function(t) { return t.uri }), index: 0 }, report)
  }

  // The green button on a tile: pause/resume it if it's what's playing,
  // otherwise play it from the top without opening it.
  function playTile(item, live) {
    message = ""
    if (live) { transport("toggle"); return }
    if (tab === "suno") {
      // The engine only plays a Suno list it has loaded, so load it first.
      svc.call("suno.tracks", { id: item.id }, function(r) {
        if (r.ok) svc.call("suno.play", { list: item.id, index: 0 }, report); else report(r)
      })
      return
    }
    if (item.uri) { svc.call("spotify.play", { context: item.uri }, report); return }
    // Liked Songs has no context; fetch its songs and play them as a list.
    svc.call("spotify.tracks", { id: item.id }, function(r) {
      if (!r.ok) { report(r); return }
      var uris = r.data.tracks.map(function(t) { return t.uri })
      if (uris.length) svc.call("spotify.play", { uris: uris, index: 0 }, report)
    })
  }

  function activateRow(row) {
    if (!selectable(row)) return
    if (row.kind === "back") back()
    else if (row.kind === "list") openList(row.item)
    else if (row.kind === "track") playRow(row)
    else if (row.kind === "action") {
      message = ""
      if (row.id === "spotify.playContext") svc.call("spotify.play", { context: spOpen.uri }, report)
      else svc.call(row.id, {}, report)
    }
  }

  function moveCursor(dy) {
    if (!cursorActive) { cursorActive = true; cursor = firstSelectable(0, 1); return }
    var i = cursor + dy
    while (i >= 0 && i < rows.length && !selectable(rows[i])) i += dy
    if (i >= 0 && i < rows.length) cursor = i
    list.positionViewAtIndex(cursor, ListView.Contain)
  }

  function firstSelectable(from, dir) {
    for (var i = from; i >= 0 && i < rows.length; i += dir) if (selectable(rows[i])) return i
    return 0
  }

  function setSub(name) {
    if (query !== "") searchField.text = ""
    if (tab === "suno") { if (suSub === name) return; suSub = name }
    else { if (spSub === name) return; spSub = name }
    message = ""
    cursor = 0
    list.positionViewAtBeginning()
    loadLists()
  }

  function cycleSub(direction) {
    var ids = subTabs.map(function(t) { return t.id })
    var i = ids.indexOf(sub) + (direction < 0 ? -1 : 1)
    setSub(ids[(i + ids.length) % ids.length])
  }

  function setTab(name) {
    if (tab === name) return
    tab = name
    message = ""
    cursor = 0
    loadLists()
  }

  function transport(cmd) { svc.call(cmd, {}, report) }

  Timer {
    id: searchDebounce
    interval: 350
    onTriggered: {
      var q = root.query
      if (q === "") { root.spResults = null; return }
      root.loading = true
      svc.call("spotify.search", { q: q }, function(r) {
        if (q !== root.query) return
        root.loading = false
        if (r.ok) { root.spResults = r.data; root.cursor = 0 } else root.report(r)
      })
    }
  }

  // Sign-in finishing (or the engine coming up) makes the lists loadable.
  onSpotifyReadyToBrowseChanged: if (spotifyReadyToBrowse) { recentNeedsReconnect = false; loadLists() } else { spLists = []; spOpen = null; recentTracks = []; subTracks = ({}) }
  // Reconnecting while already signed in doesn't flip spotifyReadyToBrowse,
  // so refresh Recent when the browser round trip ends.
  onSpotifyLoggingInChanged: if (!spotifyLoggingIn && tab === "spotify" && spSub === "recent") { recentNeedsReconnect = false; loadLists() }
  onSunoReadyToBrowseChanged: if (sunoReadyToBrowse) loadLists(); else { suLists = []; suOpen = null; subTracks = ({}) }
  onOpenedChanged: if (opened) {
    svc.ensure()
    message = ""
    cursorActive = false
    loadLists()
    Qt.callLater(function() { keyCatcher.forceActiveFocus() })
  }

  // ---- position, ticking while visible -------------------------------------

  property double clock: Date.now()
  Timer {
    interval: 500
    repeat: true
    running: root.opened && root.now !== null && root.now.playing
    onTriggered: root.clock = Date.now()
  }
  readonly property real position: {
    if (!now) return 0
    var p = now.position + (now.playing ? clock - now.at : 0)
    return now.duration > 0 ? Math.min(p, now.duration) : p
  }
  function mmss(ms) {
    var s = Math.max(0, Math.floor(ms / 1000))
    return Math.floor(s / 60) + ":" + ("0" + (s % 60)).slice(-2)
  }

  // ---- keys -----------------------------------------------------------------

  // F8 (the play/pause key): one tap plays or pauses whatever is playing; a
  // second tap within 400ms undoes that and slides the panel out instead.
  property double lastMediaKey: 0
  function mediaKey() {
    var t = Date.now()
    Commons.IpcRegistry.call("media", "playPause", [])
    if (t - lastMediaKey < 400) {
      lastMediaKey = 0
      root.togglePanel()
    } else {
      lastMediaKey = t
    }
  }

  // F8 held: a quick tap plays or pauses on release; holding it for
  // 400ms slides the panel out instead, and letting go then does nothing.
  property bool mediaKeyHeld: false
  Timer {
    id: holdTimer
    interval: 400
    onTriggered: { root.mediaKeyHeld = true; root.togglePanel() }
  }
  function mediaKeyDown() {
    mediaKeyHeld = false
    holdTimer.restart()
  }
  function mediaKeyUp() {
    if (!holdTimer.running) return  // the hold already opened the panel
    holdTimer.stop()
    Commons.IpcRegistry.call("media", "playPause", [])
  }

  function togglePanel() {
    if (root.opened) root.close()
    else root.open()
  }

  ShellIpc {
    target: "hum"
    function open(): void { root.open() }
    function close(): void { root.close() }
    function toggle(): void { root.togglePanel() }
    function mediaKey(): void { root.mediaKey() }
    function mediaKeyDown(): void { root.mediaKeyDown() }
    function mediaKeyUp(): void { root.mediaKeyUp() }
  }

  // ---- bar icon ---------------------------------------------------------------

  implicitWidth: button.implicitWidth
  implicitHeight: button.implicitHeight

  BarIconButton {
    id: button
    anchors.fill: parent
    bar: root.bar
    text: root.gMusic
    tooltipText: root.now ? (root.now.title + (root.now.artist ? " · " + root.now.artist : "")) : "Hum"
    onPressed: function(b) {
      if (b === Qt.MiddleButton) root.transport("toggle")
      else root.togglePanel()
    }
  }

  // ---- the panel ----------------------------------------------------------------

  KeyboardPanel {
    id: panel
    anchorItem: button
    owner: root
    bar: root.bar
    open: root.opened
    focusTarget: keyCatcher
    contentWidth: panel.fittedContentWidth(Style.space(380))
    contentHeight: panel.fittedContentHeight(Style.space(560), Style.space(560))

    PanelKeyCatcher {
      id: keyCatcher
      anchors.fill: parent
      blocked: searchField.activeFocus || idField.activeFocus
      onMoveRequested: function(dx, dy) {
        if (dy !== 0) root.moveCursor(dy)
        else if (dx < 0) root.back()
        else if (dx > 0 && root.cursorActive && root.rows[root.cursor] && root.rows[root.cursor].kind === "list") root.activateRow(root.rows[root.cursor])
      }
      onActivateRequested: if (root.cursorActive) root.activateRow(root.rows[root.cursor])
      onCloseRequested: root.close()
      onTabRequested: function(direction) { root.cycleSub(direction) }
      onTextKey: function(t) {
        if (t === " ") root.transport("toggle")
        else if (t === "n") root.transport("next")
        else if (t === "b") root.transport("previous")
        else if (t === "/" && root.tab === "spotify" && root.spotifyReadyToBrowse) searchField.forceActiveFocus()
        else if (t === "1") root.setTab("spotify")
        else if (t === "2") root.setTab("suno")
      }

      // Pure black behind everything, like Spotify's app frame.
      Rectangle {
        z: -2
        anchors.fill: parent
        anchors.margins: -panel.padding
        color: "#000000"
      }

      // ---- search: a big pill on the black, Spotify tab only ----
      TextField {
        id: searchField
        visible: root.tab === "spotify" && root.spotifyReadyToBrowse
        // Under the source pills and sub-tabs, so switching tabs never moves
        // them; only what's below the search changes.
        anchors.top: subRow.bottom
        anchors.topMargin: visible ? Style.space(10) : 0
        anchors.left: card.left
        anchors.right: card.right
        anchors.leftMargin: Style.space(12)
        anchors.rightMargin: Style.space(12)
        height: visible ? Style.space(40) : 0
        foreground: root.fg
        accent: root.accent
        leftPadding: Style.space(42)
        rightPadding: Style.space(16)
        placeholderText: "What do you want to play?"
        placeholderTextColor: root.dim
        font.family: root.font
        font.pixelSize: Style.font.body
        background: Rectangle {
          radius: height / 2
          color: searchField.activeFocus || searchField.hovered ? "#2a2a2a" : "#1f1f1f"
          border.width: searchField.activeFocus ? 2 : 0
          border.color: root.fg
          Text {
            anchors.left: parent.left
            anchors.leftMargin: Style.space(16)
            anchors.verticalCenter: parent.verticalCenter
            textFormat: Text.PlainText
            text: root.gSearch
            color: searchField.activeFocus ? root.fg : root.dim
            font.family: root.font
            font.pixelSize: Style.font.title
          }
        }
        onTextChanged: { root.query = text.trim(); searchDebounce.restart() }
        Keys.onPressed: function(event) {
          if (event.key === Qt.Key_Escape) {
            if (text !== "") text = ""
            else keyCatcher.forceActiveFocus()
            event.accepted = true
          } else if (event.key === Qt.Key_Down || event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            keyCatcher.forceActiveFocus()
            root.cursorActive = false
            root.moveCursor(1)
            event.accepted = true
          }
        }
      }

      // ---- the browse card: rounded near-black on the black ----
      Rectangle {
        id: card
        z: -1
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: player.visible ? player.top : parent.bottom
        anchors.bottomMargin: player.visible ? Style.space(4) : 0
        radius: Style.space(8)
        color: root.base
      }

      // ---- the sources, as pills ----
      Row {
        id: tabs
        anchors.top: card.top
        anchors.topMargin: Style.space(12)
        anchors.left: card.left
        anchors.leftMargin: Style.space(12)
        spacing: Style.space(8)
        Repeater {
          model: [{ id: "spotify", label: "Spotify" }, { id: "suno", label: "Suno" }]
          Item {
            required property var modelData
            readonly property bool on: root.tab === modelData.id
            width: tabLabel.implicitWidth + Style.space(26)
            height: Style.space(32)
            Rectangle {
              anchors.fill: parent
              radius: height / 2
              color: parent.on ? root.fg : (pillMouse.containsMouse ? "#3a3a3a" : root.chip)
            }
            Text {
              id: tabLabel
              anchors.centerIn: parent
              textFormat: Text.PlainText
              text: modelData.label
              color: parent.on ? "#000000" : root.fg
              font.family: root.font
              font.pixelSize: Style.font.body
            }
            MouseArea {
              id: pillMouse
              anchors.fill: parent
              hoverEnabled: true
              cursorShape: Qt.PointingHandCursor
              onClicked: root.setTab(modelData.id)
            }
          }
        }
      }

      Text {
        anchors.verticalCenter: tabs.verticalCenter
        anchors.right: card.right
        anchors.rightMargin: Style.space(14)
        textFormat: Text.PlainText
        text: root.loading ? "Loading…" : ""
        color: root.dim
        font.family: root.font
        font.pixelSize: Style.font.caption
      }

      // ---- sub-tabs: smaller, quieter chips under the source pills ----
      Row {
        id: subRow
        readonly property bool show: root.tab === "suno" ? root.sunoReadyToBrowse : root.spotifyReadyToBrowse
        visible: show
        height: show ? Style.space(28) : 0
        anchors.top: tabs.bottom
        anchors.topMargin: show ? Style.space(10) : 0
        anchors.left: card.left
        anchors.leftMargin: Style.space(12)
        spacing: Style.space(6)
        Repeater {
          model: root.subTabs
          Item {
            required property var modelData
            readonly property bool on: root.sub === modelData.id
            width: subLabel.implicitWidth + Style.space(22)
            height: Style.space(28)
            Rectangle {
              anchors.fill: parent
              radius: height / 2
              color: parent.on ? "#3a3a3a" : (subMouse.containsMouse ? root.chip : "transparent")
            }
            Text {
              id: subLabel
              anchors.centerIn: parent
              textFormat: Text.PlainText
              text: (modelData.heart ? root.gHeart + "  " : "") + modelData.label
              color: parent.on ? root.fg : root.dim
              font.family: root.font
              font.pixelSize: Style.font.bodySmall
              font.bold: parent.on
            }
            MouseArea {
              id: subMouse
              anchors.fill: parent
              hoverEnabled: true
              cursorShape: Qt.PointingHandCursor
              onClicked: root.setSub(modelData.id)
            }
          }
        }
      }

      // ---- setup and messages, under the pills ----
      Column {
        id: topBlock
        anchors.top: searchField.visible ? searchField.bottom : subRow.bottom
        anchors.topMargin: Style.space(10)
        anchors.left: card.left
        anchors.right: card.right
        anchors.leftMargin: Style.space(12)
        anchors.rightMargin: Style.space(12)
        spacing: Style.space(8)

        // Engine not up yet.
        Text {
          visible: !root.st
          width: parent.width
          wrapMode: Text.Wrap
          textFormat: Text.PlainText
          text: svc.startError !== "" ? svc.startError : "Starting the music engine…"
          color: svc.startError !== "" ? Commons.Color.urgent : root.dim
          font.family: root.font
          font.pixelSize: Style.font.body
        }

        // One-time Spotify setup: the Client ID of your developer app.
        Column {
          visible: root.tab === "spotify" && !!root.spotify && !root.spotify.configured
          width: parent.width
          spacing: Style.space(8)
          Text {
            width: parent.width
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
            text: "Paste the Client ID of your Spotify developer app (developer.spotify.com/dashboard). Its redirect URI must be http://127.0.0.1:8898/callback."
            color: root.dim
            font.family: root.font
            font.pixelSize: Style.font.bodySmall
          }
          Row {
            spacing: Style.space(6)
            width: parent.width
            TextField {
              id: idField
              width: parent.width - saveButton.width - Style.space(6)
              placeholderText: "Client ID"
              onAccepted: saveButton.save()
              Keys.onEscapePressed: keyCatcher.forceActiveFocus()
            }
            Button {
              id: saveButton
              text: "Save"
              bordered: true
              function save() {
                root.message = ""
                svc.call("spotify.setClientId", { id: idField.text }, function(r) {
                  if (!r.ok) { root.report(r); return }
                  idField.text = ""
                  keyCatcher.forceActiveFocus()
                })
              }
              onClicked: save()
            }
          }
        }

        Text {
          readonly property string sourceError: root.tab !== "suno" ? (root.spotify ? root.spotify.error : "") : (root.suno ? root.suno.error : "")
          visible: text !== ""
          width: parent.width
          wrapMode: Text.Wrap
          textFormat: Text.PlainText
          text: root.message !== "" ? root.message : sourceError
          color: Commons.Color.urgent
          font.family: root.font
          font.pixelSize: Style.font.bodySmall
        }
      }

      // ---- the list ----
      ListView {
        id: list
        anchors.top: topBlock.bottom
        anchors.topMargin: Style.space(4)
        anchors.left: card.left
        anchors.right: card.right
        anchors.leftMargin: Style.space(8)
        anchors.rightMargin: Style.space(4)
        anchors.bottom: card.bottom
        anchors.bottomMargin: Style.space(6)
        clip: true
        model: root.rows
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        delegate: Item {
          id: rowItem
          required property var modelData
          required property int index
          readonly property var row: modelData
          readonly property bool hot: root.cursorActive && root.cursor === index
          readonly property bool current: root.playingThis(row)
          readonly property bool tile: row.kind === "list"
          readonly property bool liked: tile && row.item.id === "liked"
          // The playlist or album that's playing right now.
          readonly property bool live: tile && !!root.now && !!row.item.uri && root.now.context === row.item.uri
          readonly property bool hasArt: (row.kind === "track" || tile) && !!row.item.image && String(row.item.image).indexOf("https://") === 0
          width: ListView.view.width - Style.space(8)
          height: row.kind === "header" ? Style.space(44)
                : row.kind === "note" ? noteText.implicitHeight + Style.space(10)
                : row.kind === "back" ? Style.space(60)
                : tile ? Style.space(56)
                : Style.space(row.kind === "track" ? 54 : 44)

          // Tiles are grey cards; everything else lights up on hover.
          Rectangle {
            anchors.fill: parent
            anchors.topMargin: rowItem.tile ? Style.space(4) : 0
            anchors.bottomMargin: rowItem.tile ? Style.space(4) : 0
            radius: Style.space(4)
            visible: root.selectable(rowItem.row)
            color: rowItem.tile ? (rowItem.hot ? Qt.rgba(1, 1, 1, 0.16) : Qt.rgba(1, 1, 1, 0.08))
                 : rowItem.hot ? Qt.rgba(1, 1, 1, 0.10) : "transparent"
          }

          // Section title, big and bold.
          Text {
            visible: rowItem.row.kind === "header"
            anchors.bottom: parent.bottom
            anchors.bottomMargin: Style.space(6)
            x: Style.space(4)
            textFormat: Text.PlainText
            text: rowItem.row.kind === "header" ? rowItem.row.label : ""
            color: root.fg
            font.family: root.font
            font.pixelSize: Style.font.display
            font.bold: true
          }

          Text {
            id: noteText
            visible: rowItem.row.kind === "note"
            x: Style.space(8)
            width: parent.width - Style.space(16)
            anchors.verticalCenter: parent.verticalCenter
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
            text: rowItem.row.kind === "note" ? rowItem.row.label : ""
            color: root.dim
            font.family: root.font
            font.pixelSize: Style.font.bodySmall
          }

          // ---- a playlist or album tile: cover flush left, bold name ----
          Item {
            visible: rowItem.tile
            anchors.fill: parent
            anchors.topMargin: Style.space(4)
            anchors.bottomMargin: Style.space(4)

            ClippingRectangle {
              id: tileCover
              anchors.left: parent.left
              anchors.top: parent.top
              anchors.bottom: parent.bottom
              width: height
              radius: Style.space(4)
              color: "#282828"
              Rectangle {
                anchors.fill: parent
                visible: rowItem.liked
                gradient: Gradient {
                  GradientStop { position: 0; color: "#e0284f" }
                  GradientStop { position: 1; color: "#5c0a1c" }
                }
              }
              Image {
                anchors.fill: parent
                visible: rowItem.hasArt && status === Image.Ready
                source: rowItem.hasArt ? rowItem.row.item.image : ""
                sourceSize.width: Style.space(96)
                sourceSize.height: Style.space(96)
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
              }
              Text {
                anchors.centerIn: parent
                visible: !rowItem.hasArt
                textFormat: Text.PlainText
                text: rowItem.liked ? root.gHeart : root.gList
                color: rowItem.liked ? "#1a0007" : root.dim
                font.family: root.font
                font.pixelSize: Style.font.display
              }
            }

            Text {
              anchors.left: tileCover.right
              anchors.leftMargin: Style.space(12)
              anchors.right: tilePlay.visible ? tilePlay.left : parent.right
              anchors.rightMargin: Style.space(10)
              anchors.verticalCenter: parent.verticalCenter
              elide: Text.ElideRight
              textFormat: Text.PlainText
              text: rowItem.tile ? rowItem.row.item.name : ""
              color: root.fg
              font.family: root.font
              font.pixelSize: Style.font.body
              font.bold: true
            }

            // Green play button: shows on hover, stays on what's playing.
            Rectangle {
              id: tilePlay
              z: 2
              visible: rowItem.hot || rowItem.live
              anchors.right: parent.right
              anchors.rightMargin: Style.space(8)
              anchors.verticalCenter: parent.verticalCenter
              width: Style.space(34)
              height: width
              radius: width / 2
              color: tilePlayMouse.containsMouse ? "#3be477" : root.accent
              Text {
                anchors.centerIn: parent
                textFormat: Text.PlainText
                text: rowItem.live && root.now.playing ? root.gPause : root.gPlay
                color: "#000000"
                font.family: root.font
                font.pixelSize: Style.font.title
              }
              MouseArea {
                id: tilePlayMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.playTile(rowItem.row.item, rowItem.live)
              }
            }
          }

          // ---- tracks, the back row and actions ----
          Item {
            visible: root.selectable(rowItem.row) && !rowItem.tile
            anchors.fill: parent
            anchors.leftMargin: Style.space(8)
            anchors.rightMargin: Style.space(8)

            Item {
              id: lead
              width: rowItem.row.kind === "back" ? Style.space(20) : rowItem.row.kind === "action" ? Style.space(28) : Style.space(40)
              height: width
              anchors.verticalCenter: parent.verticalCenter
              ClippingRectangle {
                anchors.fill: parent
                radius: Style.space(4)
                color: "#282828"
                visible: rowItem.row.kind === "track"
                Image {
                  anchors.fill: parent
                  visible: rowItem.hasArt && status === Image.Ready
                  source: rowItem.hasArt ? rowItem.row.item.image : ""
                  sourceSize.width: Style.space(80)
                  sourceSize.height: Style.space(80)
                  fillMode: Image.PreserveAspectCrop
                  asynchronous: true
                }
              }
              Text {
                anchors.centerIn: parent
                visible: !rowItem.hasArt
                textFormat: Text.PlainText
                text: rowItem.row.kind === "back" ? root.gBack
                    : rowItem.row.kind === "action" ? rowItem.row.glyph
                    : root.gMusic
                color: rowItem.row.kind === "back" ? root.fg : rowItem.current ? root.accent : root.dim
                font.family: root.font
                font.pixelSize: rowItem.row.kind === "back" ? Style.font.title : Style.font.heading
              }
            }

            Column {
              anchors.left: lead.right
              anchors.leftMargin: Style.space(12)
              anchors.right: rowItem.row.kind === "back" ? playBig.left : trailing.left
              anchors.rightMargin: Style.space(8)
              anchors.verticalCenter: parent.verticalCenter
              spacing: Style.space(2)
              Text {
                width: parent.width
                elide: Text.ElideRight
                textFormat: Text.PlainText
                text: rowItem.row.kind === "track" ? rowItem.row.item.title : (rowItem.row.label || "")
                color: rowItem.current ? root.accent : root.fg
                font.family: root.font
                font.pixelSize: rowItem.row.kind === "back" ? Style.font.display : Style.font.body
                font.bold: rowItem.row.kind === "back"
              }
              Text {
                width: parent.width
                visible: text !== ""
                elide: Text.ElideRight
                textFormat: Text.PlainText
                text: rowItem.row.kind === "track" ? (rowItem.row.item.artist || "") : ""
                color: root.dim
                font.family: root.font
                font.pixelSize: Style.font.bodySmall
              }
            }

            // Big green play on an open playlist or album.
            Rectangle {
              id: playBig
              z: 2
              visible: rowItem.row.kind === "back"
              anchors.right: parent.right
              anchors.verticalCenter: parent.verticalCenter
              width: visible ? Style.space(46) : 0
              height: width
              radius: width / 2
              color: playBigMouse.containsMouse ? "#3be477" : root.accent
              Text {
                anchors.centerIn: parent
                textFormat: Text.PlainText
                text: root.gPlay
                color: "#000000"
                font.family: root.font
                font.pixelSize: Style.font.display
              }
              MouseArea {
                id: playBigMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.playOpen()
              }
            }

            Text {
              id: trailing
              anchors.right: parent.right
              anchors.verticalCenter: parent.verticalCenter
              textFormat: Text.PlainText
              text: rowItem.row.kind === "track" && rowItem.row.item.duration > 0 ? root.mmss(rowItem.row.item.duration) : ""
              color: root.dim
              font.family: root.font
              font.pixelSize: Style.font.bodySmall
            }
          }

          MouseArea {
            anchors.fill: parent
            enabled: root.selectable(rowItem.row)
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onEntered: { root.cursorActive = true; root.cursor = rowItem.index }
            onClicked: root.activateRow(rowItem.row)
          }
        }
      }

      // ---- now playing, on the black ----
      Item {
        id: player
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: root.now ? Style.space(132) : 0
        visible: root.now !== null
        clip: true

        // Song on the left: rounded cover, title, artist.
        ClippingRectangle {
          id: art
          readonly property bool ok: !!root.now && !!root.now.image && String(root.now.image).indexOf("https://") === 0
          x: Style.space(4)
          y: Style.space(10)
          width: Style.space(56)
          height: width
          radius: Style.space(4)
          color: "#282828"
          Image {
            anchors.fill: parent
            source: art.ok ? root.now.image : ""
            sourceSize.width: Style.space(112)
            sourceSize.height: Style.space(112)
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            visible: art.ok && status === Image.Ready
          }
          Text {
            anchors.centerIn: parent
            visible: !art.ok
            textFormat: Text.PlainText
            text: root.gMusic
            color: root.dim
            font.family: root.font
            font.pixelSize: Style.font.display
          }
        }

        Column {
          anchors.left: art.right
          anchors.leftMargin: Style.space(12)
          anchors.right: parent.right
          anchors.rightMargin: Style.space(4)
          anchors.verticalCenter: art.verticalCenter
          spacing: Style.space(2)
          Text {
            width: parent.width
            elide: Text.ElideRight
            textFormat: Text.PlainText
            text: root.now ? root.now.title : ""
            color: root.fg
            font.family: root.font
            font.pixelSize: Style.font.body
          }
          Text {
            width: parent.width
            elide: Text.ElideRight
            textFormat: Text.PlainText
            text: root.now ? (root.now.artist || "") + (root.now.source === "suno" ? "  ·  Suno" : "") : ""
            color: root.dim
            font.family: root.font
            font.pixelSize: Style.font.bodySmall
          }
        }

        // Controls in the middle: grey skips around a white disc.
        Row {
          id: controls
          anchors.horizontalCenter: parent.horizontalCenter
          anchors.top: art.bottom
          anchors.topMargin: Style.space(6)
          spacing: Style.space(18)
          Repeater {
            model: [
              { cmd: "previous", g: root.gPrev, on: !!root.now && root.now.canPrev },
              { cmd: "toggle", g: root.now && root.now.playing ? root.gPause : root.gPlay, on: true },
              { cmd: "next", g: root.gNext, on: !!root.now && root.now.canNext },
            ]
            Item {
              required property var modelData
              readonly property bool big: modelData.cmd === "toggle"
              width: Style.space(36)
              height: Style.space(36)
              Rectangle {
                anchors.centerIn: parent
                width: parent.big ? (ctlMouse.containsMouse ? Style.space(36) : Style.space(34)) : 0
                height: width
                radius: width / 2
                color: root.fg
              }
              Text {
                anchors.centerIn: parent
                textFormat: Text.PlainText
                text: modelData.g
                color: parent.big ? "#000000" : !modelData.on ? "#535353" : ctlMouse.containsMouse ? root.fg : root.dim
                font.family: root.font
                font.pixelSize: parent.big ? Style.font.title : Style.font.display
              }
              MouseArea {
                id: ctlMouse
                anchors.fill: parent
                hoverEnabled: true
                enabled: modelData.on
                cursorShape: Qt.PointingHandCursor
                onClicked: root.transport(modelData.cmd)
              }
            }
          }
        }

        // Progress under the controls, times either side. Click to seek.
        Item {
          id: progress
          anchors.left: parent.left
          anchors.right: parent.right
          anchors.top: controls.bottom
          anchors.topMargin: Style.space(4)
          height: Style.space(16)
          Text {
            id: posText
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            textFormat: Text.PlainText
            text: root.mmss(root.position)
            color: root.dim
            font.family: root.font
            font.pixelSize: Style.font.caption
          }
          Text {
            id: durText
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            textFormat: Text.PlainText
            text: root.now && root.now.duration > 0 ? root.mmss(root.now.duration) : ""
            color: root.dim
            font.family: root.font
            font.pixelSize: Style.font.caption
          }
          Rectangle {
            id: bar
            anchors.left: posText.right
            anchors.leftMargin: Style.space(8)
            anchors.right: durText.left
            anchors.rightMargin: Style.space(8)
            anchors.verticalCenter: parent.verticalCenter
            height: Math.max(2, Style.space(4))
            radius: height / 2
            color: "#4d4d4d"
            Rectangle {
              id: fill
              width: root.now && root.now.duration > 0 ? parent.width * Math.min(1, root.position / root.now.duration) : 0
              height: parent.height
              radius: parent.radius
              color: seekMouse.containsMouse ? root.accent : root.fg
            }
            // The knob only shows while hovering, as in Spotify.
            Rectangle {
              visible: seekMouse.containsMouse
              width: Style.space(12)
              height: width
              radius: width / 2
              color: root.fg
              anchors.verticalCenter: parent.verticalCenter
              x: fill.width - width / 2
            }
            MouseArea {
              id: seekMouse
              hoverEnabled: true
              anchors.fill: parent
              anchors.margins: -Style.space(6)
              cursorShape: Qt.PointingHandCursor
              onClicked: function(mouse) {
                if (!root.now || root.now.duration <= 0) return
                var ratio = Math.max(0, Math.min(1, (mouse.x - Style.space(6)) / bar.width))
                svc.call("seek", { ms: Math.round(ratio * root.now.duration) }, root.report)
              }
            }
          }
        }
      }
    }
  }
}
