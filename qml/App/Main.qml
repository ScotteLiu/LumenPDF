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
           ? (Document.modified ? "• " : "") + Document.title + " — LumenPDF"
           : "LumenPDF"

    color: Tokens.canvas

    Component.onCompleted: {
        if (Platform.initialDark >= 0)
            Tokens.dark = Platform.initialDark === 1;
        Platform.applyBackdrop(window, Tokens.dark);
    }

    // The native title bar and window backdrop are not QML, so they have to be
    // told when the theme changes -- otherwise toggling leaves a dark title bar
    // sitting on top of a light window.
    Connections {
        target: Tokens
        function onDarkChanged() {
            Platform.applyBackdrop(window, Tokens.dark);
            Platform.setDarkTitleBar(window, Tokens.dark);
        }
    }

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

    Shortcut {
        sequences: [StandardKey.Copy]
        onActivated: Document.selection.copyToClipboard()
    }
    Shortcut {
        sequences: [StandardKey.SelectAll]
        enabled: !searchField.activeFocus
        onActivated: Document.selection.selectAll()
    }
    Shortcut {
        sequence: "Esc"
        onActivated: Document.selection.clear()
    }

    Shortcut {
        sequences: [StandardKey.Undo]
        onActivated: Document.pages.undo()
    }
    Shortcut {
        sequences: [StandardKey.Redo]
        onActivated: Document.pages.redo()
    }
    Shortcut { sequence: "Ctrl+Shift+D"; onActivated: Tokens.dark = !Tokens.dark }

    Shortcut {
        sequences: [StandardKey.Save]
        enabled: Document.modified
        onActivated: Document.save()
    }
    Shortcut {
        sequences: [StandardKey.SaveAs]
        enabled: Document.status === DocumentStatus.Ready
        onActivated: saveDialog.open()
    }

    FileDialog {
        id: openDialog
        title: qsTr("Open PDF")
        nameFilters: [qsTr("PDF documents (*.pdf)"), qsTr("All files (*)")]
        onAccepted: Document.open(selectedFile)
    }

    FileDialog {
        id: mergeDialog
        title: qsTr("Append PDF")
        nameFilters: [qsTr("PDF documents (*.pdf)")]
        onAccepted: Document.pages.mergeFrom(selectedFile)
    }

    // Which page an extract applies to is decided when the dialog opens, not
    // when it closes -- the user could scroll while the dialog is up.
    property int extractPage: -1

    FileDialog {
        id: extractDialog
        title: qsTr("Export Page As")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "pdf"
        nameFilters: [qsTr("PDF documents (*.pdf)")]
        onAccepted: {
            if (window.extractPage >= 0)
                Document.pages.extractTo(selectedFile, window.extractPage, window.extractPage);
        }
    }

    Connections {
        target: Document.pages
        function onFailed(reason) { toast.show(reason, true) }
        function onExtracted(filePath, pageCount) {
            toast.show(qsTr("Exported %n page(s)", "", pageCount));
        }
        function onMerged(filePath, pageCount) {
            toast.show(qsTr("Added %n page(s)", "", pageCount));
        }
    }

    FileDialog {
        id: saveDialog
        title: qsTr("Save PDF As")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "pdf"
        nameFilters: [qsTr("PDF documents (*.pdf)")]
        onAccepted: Document.saveAs(selectedFile)
    }

    // Transient confirmation. Deliberately not a dialog: saving succeeded, so
    // there is nothing to decide and nothing to dismiss.
    Connections {
        target: Document
        function onSaved(filePath) { toast.show(qsTr("Saved")) }
        function onSaveFailed(reason) { toast.show(reason, true) }
    }

    Connections {
        target: Document.selection
        function onCopied(characters) {
            toast.show(qsTr("Copied %n character(s)", "", characters));
        }
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

        LumenIconButton {
            iconPath: Icons.save
            tooltip: Document.modified ? qsTr("Save  (Ctrl+S)")
                                       : qsTr("No unsaved changes")
            enabled: Document.modified
            active: Document.modified
            anchors.verticalCenter: parent.verticalCenter
            onClicked: Document.save()
        }

        LumenIconButton {
            iconPath: Icons.merge
            tooltip: qsTr("Append another PDF…")
            enabled: Document.status === DocumentStatus.Ready
            anchors.verticalCenter: parent.verticalCenter
            onClicked: mergeDialog.open()
        }

        Item { width: Tokens.space3; height: 1 }
        LumenSeparator {
            vertical: true
            height: Tokens.space5
            anchors.verticalCenter: parent.verticalCenter
        }
        Item { width: Tokens.space3; height: 1 }

        LumenIconButton {
            iconPath: Icons.undo
            tooltip: Document.pages.canUndo ? Document.pages.undoLabel
                                            : qsTr("Nothing to undo")
            enabled: Document.pages.canUndo
            anchors.verticalCenter: parent.verticalCenter
            onClicked: Document.pages.undo()
        }

        LumenIconButton {
            iconPath: Icons.redo
            tooltip: Document.pages.canRedo ? Document.pages.redoLabel
                                            : qsTr("Nothing to redo")
            enabled: Document.pages.canRedo
            anchors.verticalCenter: parent.verticalCenter
            onClicked: Document.pages.redo()
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

        Item { width: Tokens.space3; height: 1 }
        LumenSeparator {
            vertical: true
            height: Tokens.space5
            anchors.verticalCenter: parent.verticalCenter
        }
        Item { width: Tokens.space3; height: 1 }

        LumenIconButton {
            id: moreButton
            iconPath: Icons.more
            tooltip: qsTr("More actions")
            active: moreMenu.opened
            enabled: Document.status === DocumentStatus.Ready
            anchors.verticalCenter: parent.verticalCenter
            onClicked: moreMenu.opened ? moreMenu.close() : moreMenu.open()
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
                onExportRequested: (index) => {
                    window.extractPage = index;
                    extractDialog.open();
                }
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

    // Everything that is real but not frequent enough to earn toolbar space.
    LumenMenu {
        id: moreMenu
        anchorItem: moreButton
        preferredWidth: 268

        items: [
            {
                label: qsTr("Export Pages as Images…"),
                icon: Icons.image,
                enabled: !Document.exporter.busy,
                action: () => imagesDialog.open()
            },
            {
                label: qsTr("Export Text…"),
                icon: Icons.text,
                action: () => textDialog.open()
            },
            { separator: true },
            {
                label: qsTr("Redact Selection…"),
                icon: Icons.redact,
                enabled: Document.redact.canRedact,
                action: () => redactConfirm.open()
            },
            {
                label: qsTr("Add Signature…"),
                icon: Icons.signature,
                action: () => {
                    signaturePad.targetPage = pageView.currentPage;
                    signaturePad.open();
                }
            },
            { separator: true },
            {
                label: qsTr("Save a Copy…"),
                icon: Icons.save,
                shortcut: "Ctrl+Shift+S",
                action: () => saveDialog.open()
            }
        ]
    }

    FolderDialog {
        id: imagesDialog
        title: qsTr("Choose a folder for the exported images")
        onAccepted: Document.exporter.exportImages(selectedFolder, "png", -1, -1)
    }

    FileDialog {
        id: textDialog
        title: qsTr("Export Text")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "txt"
        nameFilters: [qsTr("Text files (*.txt)")]
        onAccepted: Document.exporter.exportText(selectedFile)
    }

    Connections {
        target: Document.exporter
        function onFailed(reason) { toast.show(reason, true) }
        function onFinished(directory, fileCount) {
            toast.show(qsTr("Exported %n file(s)", "", fileCount));
        }
    }

    // Export progress. A thin line under the toolbar rather than a modal
    // dialog: the document stays readable while it runs.
    Rectangle {
        anchors.top: toolbar.bottom
        anchors.left: parent.left
        width: parent.width * Document.exporter.progress
        height: 2
        z: 30
        color: Tokens.accent
        opacity: Document.exporter.busy ? 1 : 0

        Behavior on width { NumberAnimation { duration: Motion.fast } }
        Behavior on opacity { NumberAnimation { duration: Motion.normal } }
    }

    LumenConfirm {
        id: redactConfirm
        title: qsTr("Redact the selected text?")
        body: qsTr("The text will be permanently destroyed — this cannot be undone, "
                 + "even before saving.\n\n"
                 + "Each affected page is flattened to an image, so those pages will "
                 + "no longer be searchable or selectable. This is what guarantees "
                 + "nothing is recoverable from underneath the black box.")
        confirmText: qsTr("Redact")
        destructive: true
        onConfirmed: Document.redact.redactSelection()
    }

    SignaturePad {
        id: signaturePad
        onAccepted: toast.show(qsTr("Signature placed"))
    }

    Connections {
        target: Document.redact
        function onFailed(reason) { toast.show(reason, true) }
        function onFlattenedPages(pageCount) {
            // Redaction flattens the page to an image. Not saying so would let
            // the user discover it later by finding that search stopped working
            // -- and a security tool that surprises you is not trustworthy.
            toast.show(qsTr("Redacted — %n page(s) flattened to an image", "",
                            pageCount));
        }
    }

    LumenToast {
        id: toast
        anchors.left: pageView.left
        anchors.right: pageView.right
        anchors.bottom: parent.bottom
        height: 120
        z: 200
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

