import QtQuick
import QtQuick.Dialogs
import Lumen
import Lumen.Backend

// Print.
//
// Built here rather than using the system print dialog for the same reason the
// rest of the interface is: a native dialog in the middle of this application
// would be the only thing in it that looks like something else. It also keeps
// "print to a PDF file" as a first-class option instead of a driver that may or
// may not be installed.
Item {
    id: root

    readonly property var printer: Document.printer

    // Zero-based internally, one-based in the field, because nobody counts
    // pages from zero.
    property int firstPage: 0
    property int lastPage: -1
    property int copies: 1
    property bool greyscale: false
    property bool fitToPage: true
    property bool wholeDocument: true

    anchors.fill: parent
    visible: opacity > 0
    opacity: 0
    z: 445

    function open() {
        root.wholeDocument = true;
        root.firstPage = 0;
        root.lastPage = -1;
        root.copies = 1;
        printer.refreshPrinters();
        root.state = "open";
        root.forceActiveFocus();
    }

    function close() {
        if (printer.busy)
            return;
        root.state = "";
    }

    states: State {
        name: "open"
        PropertyChanges { root.opacity: 1 }
    }

    transitions: Transition {
        NumberAnimation {
            property: "opacity"; duration: Motion.fast; easing.type: Easing.OutCubic
        }
    }

    Keys.onEscapePressed: root.close()

    Connections {
        target: root.printer
        function onFinished(pages) {
            root.state = "";
            root.toastRequested(qsTr("Printed %n page(s)", "", pages));
        }
        function onFailed(reason) {
            root.errorRequested(reason);
        }
    }

    signal toastRequested(string message)
    signal errorRequested(string message)

    FileDialog {
        id: pdfDialog
        title: qsTr("Print to PDF")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "pdf"
        nameFilters: [qsTr("PDF documents (*.pdf)")]
        onAccepted: {
            root.printer.printToFile(selectedFile, root.effectiveFirst,
                                     root.effectiveLast, root.greyscale,
                                     root.fitToPage);
        }
    }

    readonly property int effectiveFirst: wholeDocument ? -1 : firstPage
    readonly property int effectiveLast: wholeDocument ? -1 : lastPage

    Rectangle {
        anchors.fill: parent
        color: Tokens.scrim
        MouseArea { anchors.fill: parent; onClicked: root.close() }
    }

    Squircle {
        anchors.centerIn: parent
        width: Math.min(parent.width - Tokens.space6 * 2, 460)
        height: content.implicitHeight + Tokens.space5 * 2

        radius: Tokens.radiusLarge
        fillColor: Tokens.surfaceElevated
        strokeColor: Tokens.separator
        strokeWidth: 1

        transformOrigin: Item.Center
        scale: root.state === "open" ? 1.0 : 0.96
        Behavior on scale {
            SpringAnimation {
                spring: Motion.spring; damping: Motion.damping; epsilon: Motion.epsilon
            }
        }

        MouseArea { anchors.fill: parent }

        Column {
            id: content
            x: Tokens.space5
            y: Tokens.space5
            width: parent.width - Tokens.space5 * 2
            spacing: Tokens.space4

            Text {
                text: qsTr("Print")
                font.family: Tokens.fontFamily
                font.pixelSize: Tokens.textTitle
                font.weight: Tokens.weightSemiBold
                color: Tokens.textPrimary
            }

            // -- Printer --------------------------------------------------
            Column {
                width: parent.width
                spacing: Tokens.space2
                visible: root.printer.printers.length > 0

                Text {
                    text: qsTr("Printer")
                    font.family: Tokens.fontFamily
                    font.pixelSize: Tokens.textSmall
                    font.weight: Tokens.weightMedium
                    color: Tokens.textSecondary
                }

                Flow {
                    width: parent.width
                    spacing: Tokens.space2

                    Repeater {
                        model: root.printer.printers

                        delegate: Squircle {
                            required property var modelData
                            readonly property bool current: modelData === root.printer.printer

                            width: Math.min(name.implicitWidth + Tokens.space4 * 2,
                                            content.width)
                            height: Tokens.controlHeight
                            radius: Tokens.radiusSmall
                            fillColor: current ? Tokens.accentSubtle
                                     : hover.hovered ? Tokens.hoverOverlay
                                     : "transparent"
                            strokeColor: current ? Tokens.accent : Tokens.separator
                            strokeWidth: 1

                            Behavior on fillColor { ColorAnimation { duration: Motion.instant } }

                            Text {
                                id: name
                                anchors.centerIn: parent
                                width: Math.min(implicitWidth, parent.width - Tokens.space4 * 2)
                                elide: Text.ElideMiddle
                                text: modelData
                                font.family: Tokens.fontFamily
                                font.pixelSize: Tokens.textSmall
                                color: current ? Tokens.accent : Tokens.textPrimary
                            }

                            HoverHandler { id: hover }
                            TapHandler { onTapped: root.printer.printer = modelData }
                        }
                    }
                }
            }

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                visible: root.printer.printers.length === 0
                text: qsTr("No printer is installed. You can still print to a PDF file.")
                font.family: Tokens.fontFamily
                font.pixelSize: Tokens.textBody
                color: Tokens.textSecondary
            }

            // -- Range ----------------------------------------------------
            Column {
                width: parent.width
                spacing: Tokens.space2

                Text {
                    text: qsTr("Pages")
                    font.family: Tokens.fontFamily
                    font.pixelSize: Tokens.textSmall
                    font.weight: Tokens.weightMedium
                    color: Tokens.textSecondary
                }

                LumenSegmented {
                    width: parent.width
                    options: [qsTr("All %1").arg(root.printer.pageCount), qsTr("Range")]
                    currentIndex: root.wholeDocument ? 0 : 1
                    onSelected: (index) => {
                        root.wholeDocument = index === 0;
                        if (!root.wholeDocument && root.lastPage < 0)
                            root.lastPage = root.printer.pageCount - 1;
                    }
                }

                Row {
                    spacing: Tokens.space2
                    visible: !root.wholeDocument

                    NumberField {
                        id: fromField
                        minimum: 1
                        maximum: Math.max(1, root.printer.pageCount)
                        value: root.firstPage + 1
                        onValueEdited: (v) => {
                            root.firstPage = v - 1;
                            if (root.lastPage < root.firstPage)
                                root.lastPage = root.firstPage;
                        }
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("to")
                        font.family: Tokens.fontFamily
                        font.pixelSize: Tokens.textBody
                        color: Tokens.textSecondary
                    }

                    NumberField {
                        minimum: fromField.value
                        maximum: Math.max(1, root.printer.pageCount)
                        value: (root.lastPage < 0 ? root.printer.pageCount - 1
                                                  : root.lastPage) + 1
                        onValueEdited: (v) => root.lastPage = v - 1
                    }
                }
            }

            // -- Options --------------------------------------------------
            Row {
                spacing: Tokens.space4

                Column {
                    spacing: Tokens.space2

                    Text {
                        text: qsTr("Copies")
                        font.family: Tokens.fontFamily
                        font.pixelSize: Tokens.textSmall
                        font.weight: Tokens.weightMedium
                        color: Tokens.textSecondary
                    }

                    NumberField {
                        minimum: 1
                        maximum: 99
                        value: root.copies
                        onValueEdited: (v) => root.copies = v
                    }
                }

                Column {
                    spacing: Tokens.space2

                    Text {
                        text: qsTr("Colour")
                        font.family: Tokens.fontFamily
                        font.pixelSize: Tokens.textSmall
                        font.weight: Tokens.weightMedium
                        color: Tokens.textSecondary
                    }

                    LumenSegmented {
                        options: [qsTr("Colour"), qsTr("Greyscale")]
                        currentIndex: root.greyscale ? 1 : 0
                        onSelected: (index) => root.greyscale = index === 1
                    }
                }
            }

            Row {
                spacing: Tokens.space2

                Column {
                    spacing: Tokens.space2

                    Text {
                        text: qsTr("Scale")
                        font.family: Tokens.fontFamily
                        font.pixelSize: Tokens.textSmall
                        font.weight: Tokens.weightMedium
                        color: Tokens.textSecondary
                    }

                    LumenSegmented {
                        options: [qsTr("Fit to page"), qsTr("Actual size")]
                        currentIndex: root.fitToPage ? 0 : 1
                        onSelected: (index) => root.fitToPage = index === 0
                    }
                }
            }

            // -- Progress -------------------------------------------------
            Column {
                width: parent.width
                spacing: Tokens.space2
                visible: root.printer.busy

                Text {
                    text: qsTr("Printing… %1%").arg(Math.round(root.printer.progress * 100))
                    font.family: Tokens.fontFamily
                    font.pixelSize: Tokens.textSmall
                    color: Tokens.textSecondary
                }

                Rectangle {
                    width: parent.width
                    height: 3
                    radius: 1.5
                    color: Tokens.hoverOverlay

                    Rectangle {
                        width: parent.width * root.printer.progress
                        height: parent.height
                        radius: parent.radius
                        color: Tokens.accent
                        Behavior on width { NumberAnimation { duration: Motion.fast } }
                    }
                }
            }

            Item { width: 1; height: Tokens.space1 }

            Row {
                anchors.right: parent.right
                spacing: Tokens.space2

                LumenButton {
                    text: qsTr("Cancel")
                    variant: LumenButton.Variant.Secondary
                    enabled: !root.printer.busy
                    onClicked: root.close()
                }

                LumenButton {
                    text: qsTr("Save as PDF…")
                    variant: LumenButton.Variant.Secondary
                    enabled: !root.printer.busy
                    onClicked: pdfDialog.open()
                }

                LumenButton {
                    text: qsTr("Print")
                    variant: LumenButton.Variant.Primary
                    enabled: !root.printer.busy && root.printer.printers.length > 0
                    onClicked: root.printer.print(root.effectiveFirst, root.effectiveLast,
                                                  root.copies, root.greyscale,
                                                  root.fitToPage)
                }
            }
        }
    }
}
