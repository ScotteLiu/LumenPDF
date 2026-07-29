import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import Lumen
import Lumen.Backend

ApplicationWindow {
    id: window

    width: 1280
    height: 840
    minimumWidth: 720
    minimumHeight: 520
    visible: true
    title: Document.title.length > 0
           ? Document.title + " — LumenPDF"
           : "LumenPDF"

    color: Tokens.canvas

    Component.onCompleted: Platform.applyBackdrop(window, Tokens.dark)

    onActiveChanged: if (active) Platform.setDarkTitleBar(window, Tokens.dark)

    // -- Actions ------------------------------------------------------------
    Shortcut { sequences: [StandardKey.Open]; onActivated: openDialog.open() }
    Shortcut { sequences: [StandardKey.Close]; onActivated: Document.close() }
    Shortcut { sequence: "Ctrl+B"; onActivated: sidebar.expanded = !sidebar.expanded }
    Shortcut { sequences: [StandardKey.ZoomIn]; onActivated: pageView.zoomBy(1.25) }
    Shortcut { sequences: [StandardKey.ZoomOut]; onActivated: pageView.zoomBy(0.8) }
    Shortcut { sequence: "Ctrl+0"; onActivated: pageView.fitWidth() }
    Shortcut { sequence: "Ctrl+F"; onActivated: searchField.forceActiveFocus() }
    Shortcut { sequence: "Ctrl+Shift+D"; onActivated: Tokens.dark = !Tokens.dark }

    FileDialog {
        id: openDialog
        title: qsTr("Open PDF")
        nameFilters: [qsTr("PDF documents (*.pdf)"), qsTr("All files (*)")]
        onAccepted: Document.open(selectedFile)
    }

    // -- Layout -------------------------------------------------------------
    LumenToolbar {
        id: toolbar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        z: 10

        LumenIconButton {
            glyph: "☰"
            tooltip: qsTr("Toggle sidebar  (Ctrl+B)")
            active: sidebar.expanded
            anchors.verticalCenter: parent.verticalCenter
            onClicked: sidebar.expanded = !sidebar.expanded
        }

        Item { width: Tokens.space2; height: 1 }

        LumenIconButton {
            glyph: "＋"
            tooltip: qsTr("Open  (Ctrl+O)")
            anchors.verticalCenter: parent.verticalCenter
            onClicked: openDialog.open()
        }

        Item { width: Tokens.space4; height: 1 }

        LumenIconButton {
            glyph: "−"
            tooltip: qsTr("Zoom out")
            enabled: Document.status === DocumentStatus.Ready
            anchors.verticalCenter: parent.verticalCenter
            onClicked: pageView.zoomBy(0.8)
        }

        Item {
            width: 64
            height: toolbar.height
            Text {
                anchors.centerIn: parent
                text: Math.round(pageView.zoom * 100) + "%"
                font.family: Tokens.fontFamily
                font.pixelSize: Tokens.textSmall
                font.weight: Tokens.weightMedium
                color: Tokens.textSecondary
            }
        }

        LumenIconButton {
            glyph: "＋"
            tooltip: qsTr("Zoom in")
            enabled: Document.status === DocumentStatus.Ready
            anchors.verticalCenter: parent.verticalCenter
            onClicked: pageView.zoomBy(1.25)
        }

        LumenIconButton {
            glyph: "⤢"
            tooltip: qsTr("Fit width  (Ctrl+0)")
            enabled: Document.status === DocumentStatus.Ready
            anchors.verticalCenter: parent.verticalCenter
            onClicked: pageView.fitWidth()
        }
    }

    // Right-aligned toolbar cluster. Kept out of the Row so it can anchor.
    Row {
        z: 11
        anchors.right: parent.right
        anchors.rightMargin: Tokens.space3
        anchors.top: parent.top
        height: Tokens.toolbarHeight
        spacing: Tokens.space2

        LumenSearchField {
            id: searchField
            anchors.verticalCenter: parent.verticalCenter
            placeholder: qsTr("Search in document")
            enabled: Document.status === DocumentStatus.Ready
        }

        LumenIconButton {
            glyph: Tokens.dark ? "☀" : "☾"
            tooltip: qsTr("Toggle theme  (Ctrl+Shift+D)")
            anchors.verticalCenter: parent.verticalCenter
            onClicked: Tokens.dark = !Tokens.dark
        }
    }

    LumenSidebar {
        id: sidebar
        anchors.top: toolbar.bottom
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        expanded: false

        ThumbnailRail {
            anchors.fill: parent
            currentIndex: pageView.currentPage
            onPageRequested: (index) => pageView.goToPage(index)
        }
    }

    PageView {
        id: pageView
        anchors.top: toolbar.bottom
        anchors.left: sidebar.right
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        visible: Document.status === DocumentStatus.Ready
    }

    EmptyState {
        anchors.fill: pageView
        visible: Document.status !== DocumentStatus.Ready
        loading: Document.status === DocumentStatus.Loading
        errorText: Document.status === DocumentStatus.Error ? Document.errorString : ""
        onOpenRequested: openDialog.open()
    }

    // Whole-window drag and drop.
    DropArea {
        anchors.fill: parent
        onDropped: (drop) => {
            if (drop.hasUrls && drop.urls.length > 0)
                Document.open(drop.urls[0]);
        }
    }
}
