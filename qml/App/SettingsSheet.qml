import QtQuick
import Lumen
import Lumen.Backend

// Preferences.
//
// Small on purpose. Every option here is one somebody would otherwise have to
// set on every launch; anything that can have a good default has one instead of
// a switch.
Item {
    id: root

    anchors.fill: parent
    visible: opacity > 0
    opacity: 0
    z: 450

    // Locale tags, paired with the name each language calls itself. Showing
    // "日本語" rather than "Japanese" is the difference between a list you scan
    // and a list you have to translate in your head first.
    //
    // Only fully translated languages are listed. Offering one that is half
    // done produces a window in two languages and no way to tell why.
    readonly property var languages: [
        { tag: "",      name: qsTr("Same as system") },
        { tag: "en",    name: "English" },
        { tag: "zh_TW", name: "繁體中文" },
        { tag: "zh_CN", name: "简体中文" },
        { tag: "ja",    name: "日本語" }
    ]

    signal toastRequested(string message)

    function open() {
        root.state = "open";
        root.forceActiveFocus();
    }
    function close() { root.state = "" }

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

    Rectangle {
        anchors.fill: parent
        color: Tokens.scrim
        MouseArea { anchors.fill: parent; onClicked: root.close() }
    }

    Squircle {
        anchors.centerIn: parent
        width: Math.min(parent.width - Tokens.space6 * 2, 480)
        height: Math.min(parent.height - Tokens.space6 * 2,
                         content.implicitHeight + Tokens.space5 * 2)

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

        Flickable {
            anchors.fill: parent
            anchors.margins: Tokens.space5
            contentHeight: content.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            Column {
                id: content
                width: parent.width
                spacing: Tokens.space5

                Text {
                    text: qsTr("Settings")
                    font.family: Tokens.fontFamily
                    font.pixelSize: Tokens.textTitle
                    font.weight: Tokens.weightSemiBold
                    color: Tokens.textPrimary
                }

                // -- Appearance ------------------------------------------
                Column {
                    width: parent.width
                    spacing: Tokens.space2

                    Text {
                        text: qsTr("Appearance")
                        font.family: Tokens.fontFamily
                        font.pixelSize: Tokens.textSmall
                        font.weight: Tokens.weightMedium
                        color: Tokens.textSecondary
                    }

                    LumenSegmented {
                        width: parent.width
                        options: [qsTr("Dark"), qsTr("Light")]
                        currentIndex: Tokens.dark ? 0 : 1
                        onSelected: (index) => {
                            Tokens.dark = index === 0;
                            Prefs.theme = index === 0 ? "dark" : "light";
                        }
                    }
                }

                // -- Language --------------------------------------------
                Column {
                    width: parent.width
                    spacing: Tokens.space2

                    Text {
                        text: qsTr("Language")
                        font.family: Tokens.fontFamily
                        font.pixelSize: Tokens.textSmall
                        font.weight: Tokens.weightMedium
                        color: Tokens.textSecondary
                    }

                    Flow {
                        width: parent.width
                        spacing: Tokens.space2

                        Repeater {
                            model: root.languages

                            delegate: Squircle {
                                required property var modelData
                                readonly property bool current: modelData.tag === Prefs.language

                                width: label.implicitWidth + Tokens.space4 * 2
                                height: Tokens.controlHeight
                                radius: Tokens.radiusSmall
                                fillColor: current ? Tokens.accentSubtle
                                         : hover.hovered ? Tokens.hoverOverlay
                                         : "transparent"
                                strokeColor: current ? Tokens.accent : Tokens.separator
                                strokeWidth: 1

                                Behavior on fillColor { ColorAnimation { duration: Motion.instant } }

                                Text {
                                    id: label
                                    anchors.centerIn: parent
                                    text: modelData.name
                                    font.family: Tokens.fontFamily
                                    font.pixelSize: Tokens.textSmall
                                    color: current ? Tokens.accent : Tokens.textPrimary
                                }

                                HoverHandler { id: hover }
                                TapHandler {
                                    onTapped: {
                                        if (modelData.tag === Prefs.language)
                                            return;
                                        Prefs.language = modelData.tag;
                                        // Qt can retranslate a running QML tree,
                                        // but only strings inside qsTr bindings
                                        // re-evaluate; anything captured into a
                                        // JS array stays as it was. Saying so is
                                        // better than a half-translated window.
                                        root.toastRequested(
                                            qsTr("The language changes when LumenPDF restarts."));
                                    }
                                }
                            }
                        }
                    }
                }

                // -- Reading ---------------------------------------------
                Column {
                    width: parent.width
                    spacing: Tokens.space3

                    Text {
                        text: qsTr("Reading")
                        font.family: Tokens.fontFamily
                        font.pixelSize: Tokens.textSmall
                        font.weight: Tokens.weightMedium
                        color: Tokens.textSecondary
                    }

                    ToggleRow {
                        width: parent.width
                        label: qsTr("Reopen documents where I left off")
                        checked: Prefs.restorePosition
                        onToggled: Prefs.restorePosition = !Prefs.restorePosition
                    }

                    ToggleRow {
                        width: parent.width
                        label: qsTr("Check for updates")
                        detail: qsTr("Asks GitHub for the latest version. Nothing "
                                   + "is downloaded or installed without you.")
                        checked: Prefs.checkForUpdates
                        onToggled: Prefs.checkForUpdates = !Prefs.checkForUpdates
                    }
                }

                // -- Recent ----------------------------------------------
                Column {
                    width: parent.width
                    spacing: Tokens.space2
                    visible: Prefs.recentFiles.length > 0

                    Text {
                        text: qsTr("Recent files")
                        font.family: Tokens.fontFamily
                        font.pixelSize: Tokens.textSmall
                        font.weight: Tokens.weightMedium
                        color: Tokens.textSecondary
                    }

                    Row {
                        spacing: Tokens.space3

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("%n file(s) remembered", "", Prefs.recentFiles.length)
                            font.family: Tokens.fontFamily
                            font.pixelSize: Tokens.textBody
                            color: Tokens.textSecondary
                        }

                        LumenButton {
                            text: qsTr("Forget them")
                            variant: LumenButton.Variant.Secondary
                            onClicked: {
                                Prefs.clearRecent();
                                root.toastRequested(qsTr("Recent files cleared"));
                            }
                        }
                    }
                }

                // -- Version ---------------------------------------------
                Column {
                    width: parent.width
                    spacing: Tokens.space1

                    Text {
                        text: qsTr("LumenPDF %1").arg(Updates.currentVersion)
                        font.family: Tokens.fontFamily
                        font.pixelSize: Tokens.textSmall
                        color: Tokens.textTertiary
                    }

                    Text {
                        visible: Updates.updateAvailable
                        text: qsTr("Version %1 is available").arg(Updates.latestVersion)
                        font.family: Tokens.fontFamily
                        font.pixelSize: Tokens.textSmall
                        color: Tokens.accent

                        TapHandler { onTapped: Updates.openReleasePage() }
                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                    }
                }

                Item { width: 1; height: Tokens.space1 }

                Row {
                    anchors.right: parent.right
                    spacing: Tokens.space2

                    LumenButton {
                        text: qsTr("Reset everything")
                        variant: LumenButton.Variant.Secondary
                        onClicked: resetConfirm.open()
                    }

                    LumenButton {
                        text: qsTr("Done")
                        variant: LumenButton.Variant.Primary
                        onClicked: root.close()
                    }
                }
            }
        }
    }

    LumenConfirm {
        id: resetConfirm
        title: qsTr("Reset all settings?")
        body: qsTr("Theme, language, window size and the list of recent files "
                 + "go back to their defaults. Your documents are not touched.")
        confirmText: qsTr("Reset")
        destructive: true
        onConfirmed: {
            Prefs.resetAll();
            root.toastRequested(qsTr("Settings reset"));
        }
    }
}
