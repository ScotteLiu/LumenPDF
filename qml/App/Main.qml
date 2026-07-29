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
    Shortcut {
        sequence: "Ctrl+F"
        onActivated: {
            sidebar.expanded = true;
            sidebar.tab = 2;
            searchField.forceActiveFocus();
            searchField.selectAll();
        }
    }
    Shortcut { sequences: [StandardKey.FindNext]; onActivated: Document.search.next() }
    Shortcut { sequences: [StandardKey.FindPrevious]; onActivated: Document.search.previous() }
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
            iconPath: Icons.thumbnails
            tooltip: qsTr("Toggle sidebar  (Ctrl+B)")
            active: sidebar.expanded
            anchors.verticalCenter: parent.verticalCenter
            onClicked: sidebar.expanded = !sidebar.expanded
        }

        LumenIconButton {
            iconPath: Icons.open
            tooltip: qsTr("Open  (Ctrl+O)")
            anchors.verticalCenter: parent.verticalCenter
            onClicked: openDialog.open()
        }

        Item { width: Tokens.space3; height: 1 }
        LumenSeparator {
            vertical: true
            height: Tokens.space5
            anchors.verticalCenter: parent.verticalCenter
        }
        Item { width: Tokens.space3; height: 1 }

        LumenIconButton {
            iconPath: Icons.zoomOut
            tooltip: qsTr("Zoom out  (Ctrl+-)")
            enabled: Document.status === DocumentStatus.Ready
            anchors.verticalCenter: parent.verticalCenter
            onClicked: pageView.zoomBy(0.8)
        }

        // Click the percentage to snap back to 100%.
        Item {
            width: 58
            height: toolbar.height

            Text {
                anchors.centerIn: parent
                text: Math.round(pageView.zoom * 100) + "%"
                font.family: Tokens.fontFamily
                font.pixelSize: Tokens.textSmall
                font.weight: Tokens.weightMedium
                font.features: ({ "tnum": 1 })   // tabular figures: no jitter
                color: zoomHover.hovered ? Tokens.textPrimary : Tokens.textSecondary
                Behavior on color { ColorAnimation { duration: Motion.instant } }
            }

            HoverHandler { id: zoomHover }
            TapHandler { onTapped: pageView.zoom = 1.0 }
        }

        LumenIconButton {
            iconPath: Icons.zoomIn
            tooltip: qsTr("Zoom in  (Ctrl++)")
            enabled: Document.status === DocumentStatus.Ready
            anchors.verticalCenter: parent.verticalCenter
            onClicked: pageView.zoomBy(1.25)
        }

        LumenIconButton {
            iconPath: Icons.fitWidth
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

            onTextChanged: Document.search.query = text

            // Enter walks the results; Escape gets out of the way entirely.
            Keys.onReturnPressed: (event) => {
                if (event.modifiers & Qt.ShiftModifier)
                    Document.search.previous();
                else
                    Document.search.next();
                event.accepted = true;
            }
            Keys.onEscapePressed: {
                text = "";
                focus = false;
            }
        }

        LumenIconButton {
            iconPath: Tokens.dark ? Icons.sun : Icons.moon
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
        expanded: true

        // 0 = thumbnails, 1 = outline, 2 = search results
        property int tab: Platform.initialSidebarTab >= 0
                          ? Platform.initialSidebarTab : 0

        LumenSegmented {
            id: sidebarTabs
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: Tokens.space3
            options: [qsTr("Pages"), qsTr("Outline"), qsTr("Search")]
            currentIndex: sidebar.tab
            onSelected: (index) => sidebar.tab = index
        }

        // Panels are stacked and cross-faded rather than reloaded: switching
        // tabs must not throw away thumbnail renders or search results.
        Item {
            anchors.top: sidebarTabs.bottom
            anchors.topMargin: Tokens.space2
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom

            ThumbnailRail {
                anchors.fill: parent
                currentIndex: pageView.currentPage
                onPageRequested: (index) => pageView.goToPage(index)
                opacity: sidebar.tab === 0 ? 1 : 0
                visible: opacity > 0
                Behavior on opacity { NumberAnimation { duration: Motion.fast } }
            }

            OutlinePanel {
                anchors.fill: parent
                currentPage: pageView.currentPage
                onPageRequested: (index) => pageView.goToPage(index)
                opacity: sidebar.tab === 1 ? 1 : 0
                visible: opacity > 0
                Behavior on opacity { NumberAnimation { duration: Motion.fast } }
            }

            SearchPanel {
                anchors.fill: parent
                opacity: sidebar.tab === 2 ? 1 : 0
                visible: opacity > 0
                Behavior on opacity { NumberAnimation { duration: Motion.fast } }
            }
        }
    }

    // Search drives navigation: selecting a match scrolls the page into view
    // and opens the results panel so the reader can see where they are.
    Connections {
        target: Document.search

        function onNavigateTo(pageIndex) {
            pageView.goToPage(pageIndex);
        }

        // As soon as a search finds something, show it. Typing in the find
        // field and having the results stay hidden behind a tab would be
        // hostile.
        function onCountChanged() {
            if (Document.search.count > 0) {
                sidebar.expanded = true;
                sidebar.tab = 2;
            }
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

