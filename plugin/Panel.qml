import QtQuick
import QtQuick.Controls
import Quickshell
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

  readonly property color fg: Commons.Color.popups.text
  readonly property color dim: Qt.darker(fg, 1.6)
  readonly property color accent: Commons.Color.accent
  readonly property string font: Style.font.family

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
  property bool loading: false
  property string message: ""
  property int cursor: 0
  property bool cursorActive: false

  readonly property bool spotifyReadyToBrowse: !!(spotify && spotify.loggedIn)
  readonly property bool sunoReadyToBrowse: !!(suno && suno.loggedIn)

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
        var section = spLists[m].section
        if (section && section !== (m > 0 ? spLists[m - 1].section : undefined)) out.push({ kind: "header", label: section })
        out.push({ kind: "list", item: spLists[m] })
      }
      return out
    }
    if (!suno.loggedIn) {
      out.push({ kind: "action", id: "suno.login", label: suno.loggingIn ? "Finish signing in, in the Suno window" : "Sign in to Suno", glyph: gMusic })
      return out
    }
    if (suOpen) {
      out.push({ kind: "back", label: suOpen.name })
      for (var n = 0; n < suTracks.length; n++) out.push({ kind: "track", source: "suno", index: n, item: suTracks[n] })
      if (!suTracks.length && !loading) out.push({ kind: "note", label: "No songs here" })
      return out
    }
    for (var p = 0; p < suLists.length; p++) out.push({ kind: "list", item: suLists[p] })
    return out
  }

  function selectable(row) { return row && row.kind !== "header" && row.kind !== "note" }

  function playingThis(row) {
    if (!now || row.kind !== "track") return false
    return now.id === (row.source === "suno" ? row.item.id : row.item.uri)
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
    else suOpen = null
    cursor = 0
    list.positionViewAtBeginning()
  }

  function playRow(row) {
    message = ""
    if (row.source === "suno") {
      svc.call("suno.play", { list: suOpen.id, index: row.index }, report)
    } else if (row.source === "search") {
      svc.call("spotify.play", { uris: spResults.tracks.map(function(t) { return t.uri }), index: row.index }, report)
    } else if (spOpen && spOpen.uri) {
      svc.call("spotify.play", { context: spOpen.uri, uri: row.item.uri }, report)
    } else {
      // Liked Songs and Recently Played have no Spotify context to play from.
      svc.call("spotify.play", { uris: spTracks.map(function(t) { return t.uri }), index: row.index }, report)
    }
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
  onSpotifyReadyToBrowseChanged: if (spotifyReadyToBrowse) loadLists(); else { spLists = []; spOpen = null }
  onSunoReadyToBrowseChanged: if (sunoReadyToBrowse) loadLists(); else { suLists = []; suOpen = null }
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
      onTabRequested: function(direction) { root.setTab(root.tab === "spotify" ? "suno" : "spotify") }
      onTextKey: function(t) {
        if (t === " ") root.transport("toggle")
        else if (t === "n") root.transport("next")
        else if (t === "b") root.transport("previous")
        else if (t === "/" && root.tab === "spotify" && root.spotifyReadyToBrowse) searchField.forceActiveFocus()
        else if (t === "1") root.setTab("spotify")
        else if (t === "2") root.setTab("suno")
      }

      // ---- header: the two sources ----
      Row {
        id: tabs
        anchors.top: parent.top
        anchors.left: parent.left
        spacing: Style.space(4)
        Repeater {
          model: [{ id: "spotify", label: "Spotify" }, { id: "suno", label: "Suno" }]
          Item {
            required property var modelData
            width: tabLabel.implicitWidth + Style.space(20)
            height: Style.space(28)
            Text {
              id: tabLabel
              anchors.centerIn: parent
              textFormat: Text.PlainText
              text: modelData.label
              color: root.tab === modelData.id ? root.fg : root.dim
              font.family: root.font
              font.pixelSize: Style.font.title
              font.bold: root.tab === modelData.id
            }
            Rectangle {
              anchors.bottom: parent.bottom
              anchors.horizontalCenter: parent.horizontalCenter
              width: parent.width - Style.space(12)
              height: Math.max(2, Style.space(2))
              color: root.accent
              visible: root.tab === modelData.id
            }
            MouseArea {
              anchors.fill: parent
              cursorShape: Qt.PointingHandCursor
              onClicked: root.setTab(modelData.id)
            }
          }
        }
      }

      Text {
        anchors.verticalCenter: tabs.verticalCenter
        anchors.right: parent.right
        textFormat: Text.PlainText
        text: root.loading ? "Loading…" : ""
        color: root.dim
        font.family: root.font
        font.pixelSize: Style.font.caption
      }

      // ---- everything between the header and the player ----
      Column {
        id: topBlock
        anchors.top: tabs.bottom
        anchors.topMargin: Style.space(10)
        anchors.left: parent.left
        anchors.right: parent.right
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

        // One-time Spotify setup: the Client ID of Tom's developer app.
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

        // Search, Spotify only.
        TextField {
          id: searchField
          visible: root.tab === "spotify" && root.spotifyReadyToBrowse
          width: parent.width
          placeholderText: root.gSearch + "  Search Spotify  ( / )"
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

        Text {
          readonly property string sourceError: root.tab === "spotify" ? (root.spotify ? root.spotify.error : "") : (root.suno ? root.suno.error : "")
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
        anchors.topMargin: Style.space(6)
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: player.top
        anchors.bottomMargin: Style.space(8)
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
          readonly property bool hasArt: (row.kind === "track" || row.kind === "list") && !!row.item.image && String(row.item.image).indexOf("https://") === 0
          width: ListView.view.width - Style.space(8)
          height: row.kind === "header" ? Style.space(26)
                : row.kind === "note" ? noteText.implicitHeight + Style.space(10)
                : Style.space(row.kind === "track" ? 38 : 34)

          Rectangle {
            anchors.fill: parent
            radius: Style.cornerRadius
            visible: root.selectable(rowItem.row)
            color: rowItem.hot ? Style.hoverFill : "transparent"
          }

          Text {
            visible: rowItem.row.kind === "header"
            anchors.bottom: parent.bottom
            anchors.bottomMargin: Style.space(4)
            x: Style.space(8)
            textFormat: Text.PlainText
            text: rowItem.row.kind === "header" ? rowItem.row.label : ""
            color: root.dim
            font.family: root.font
            font.pixelSize: Style.font.caption
            font.capitalization: Font.AllUppercase
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

          Item {
            visible: root.selectable(rowItem.row)
            anchors.fill: parent
            anchors.leftMargin: Style.space(8)
            anchors.rightMargin: Style.space(8)

            // Cover, or a glyph standing in for one.
            Item {
              id: lead
              width: rowItem.row.kind === "back" ? Style.space(18) : Style.space(28)
              height: width
              anchors.verticalCenter: parent.verticalCenter
              Image {
                anchors.fill: parent
                visible: rowItem.hasArt && status === Image.Ready
                source: rowItem.hasArt ? rowItem.row.item.image : ""
                sourceSize.width: Style.space(56)
                sourceSize.height: Style.space(56)
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
              }
              Text {
                anchors.centerIn: parent
                visible: !rowItem.hasArt
                textFormat: Text.PlainText
                text: rowItem.row.kind === "back" ? root.gBack
                    : rowItem.row.kind === "action" ? rowItem.row.glyph
                    : rowItem.row.kind === "list" && (rowItem.row.item.id === "liked") ? root.gHeart
                    : rowItem.row.kind === "list" ? root.gList : root.gMusic
                color: rowItem.current ? root.accent : root.dim
                font.family: root.font
                font.pixelSize: Style.font.heading
              }
            }

            Column {
              anchors.left: lead.right
              anchors.leftMargin: Style.space(10)
              anchors.right: trailing.left
              anchors.rightMargin: Style.space(8)
              anchors.verticalCenter: parent.verticalCenter
              Text {
                width: parent.width
                elide: Text.ElideRight
                textFormat: Text.PlainText
                text: rowItem.row.kind === "track" || rowItem.row.kind === "list" ? rowItem.row.item[rowItem.row.kind === "track" ? "title" : "name"] : rowItem.row.label
                color: rowItem.current ? root.accent : root.fg
                font.family: root.font
                font.pixelSize: Style.font.body
                font.bold: rowItem.row.kind === "back"
              }
              Text {
                width: parent.width
                visible: text !== ""
                elide: Text.ElideRight
                textFormat: Text.PlainText
                text: rowItem.row.kind === "track" ? (rowItem.row.item.artist || "")
                    : rowItem.row.kind === "list" && rowItem.row.item.owner && !rowItem.row.item.own ? "by " + rowItem.row.item.owner
                    : ""
                color: root.dim
                font.family: root.font
                font.pixelSize: Style.font.caption
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
              font.pixelSize: Style.font.caption
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

      // ---- now playing ----
      Item {
        id: player
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: root.now ? Style.space(84) : 0
        visible: root.now !== null
        clip: true

        PanelSeparator {
          anchors.top: parent.top
          anchors.left: parent.left
          anchors.right: parent.right
          foreground: root.fg
        }

        Image {
          id: art
          readonly property bool ok: !!root.now && !!root.now.image && String(root.now.image).indexOf("https://") === 0
          x: 0
          y: Style.space(14)
          width: Style.space(48)
          height: width
          source: ok ? root.now.image : ""
          sourceSize.width: Style.space(96)
          sourceSize.height: Style.space(96)
          fillMode: Image.PreserveAspectCrop
          asynchronous: true
          visible: ok && status === Image.Ready
        }
        Text {
          anchors.centerIn: art
          visible: !art.visible
          textFormat: Text.PlainText
          text: root.gMusic
          color: root.dim
          font.family: root.font
          font.pixelSize: Style.font.display
        }

        Column {
          anchors.left: art.right
          anchors.leftMargin: Style.space(10)
          anchors.right: controls.left
          anchors.rightMargin: Style.space(8)
          y: Style.space(16)
          Text {
            width: parent.width
            elide: Text.ElideRight
            textFormat: Text.PlainText
            text: root.now ? root.now.title : ""
            color: root.fg
            font.family: root.font
            font.pixelSize: Style.font.body
            font.bold: true
          }
          Text {
            width: parent.width
            elide: Text.ElideRight
            textFormat: Text.PlainText
            text: root.now ? (root.now.artist || "") + (root.now.source === "suno" ? "  ·  Suno" : "") : ""
            color: root.dim
            font.family: root.font
            font.pixelSize: Style.font.caption
          }
        }

        Row {
          id: controls
          anchors.right: parent.right
          y: Style.space(18)
          spacing: Style.space(4)
          Repeater {
            model: [
              { cmd: "previous", g: root.gPrev, on: !!root.now && root.now.canPrev },
              { cmd: "toggle", g: root.now && root.now.playing ? root.gPause : root.gPlay, on: true },
              { cmd: "next", g: root.gNext, on: !!root.now && root.now.canNext },
            ]
            Item {
              required property var modelData
              width: Style.space(30)
              height: Style.space(30)
              Rectangle {
                anchors.fill: parent
                radius: width / 2
                color: ctlMouse.containsMouse ? Style.hoverFill : "transparent"
              }
              Text {
                anchors.centerIn: parent
                textFormat: Text.PlainText
                text: modelData.g
                color: modelData.on ? root.fg : root.dim
                font.family: root.font
                font.pixelSize: modelData.cmd === "toggle" ? Style.font.display : Style.font.heading
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

        // Progress, click to seek.
        Item {
          id: progress
          anchors.left: parent.left
          anchors.right: parent.right
          y: Style.space(70)
          height: Style.space(12)
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
            height: Math.max(2, Style.space(3))
            radius: height / 2
            color: Qt.rgba(root.fg.r, root.fg.g, root.fg.b, 0.15)
            Rectangle {
              width: root.now && root.now.duration > 0 ? parent.width * Math.min(1, root.position / root.now.duration) : 0
              height: parent.height
              radius: parent.radius
              color: root.accent
            }
            MouseArea {
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
