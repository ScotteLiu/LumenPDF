import QtQuick
import QtQuick.Shapes
import Lumen
import Lumen.Backend

// Draw-your-signature sheet.
//
// A modal overlay rather than a separate window: signing is a single short act
// in the middle of reading a document, and a second window would break that.
// Strokes are captured as points and stamped as PDF vector paths, so the
// result stays sharp at any zoom instead of being a bitmap.
Item {
    id: root

    property int targetPage: 0

    signal accepted()

    anchors.fill: parent
    visible: opacity > 0
    opacity: 0
    z: 400

    // Each stroke is a list of points normalised to the pad, so the C++ side
    // never has to know the pad's pixel size.
    property var strokes: []
    property var currentStroke: []
    readonly property bool hasInk: strokes.length > 0

    function open() {
        root.strokes = [];
        root.currentStroke = [];
        root.state = "open";
        root.forceActiveFocus();
    }

    function close() { root.state = "" }

    function clear() {
        root.strokes = [];
        root.currentStroke = [];
    }

    function apply() {
        if (!hasInk)
            return;
        // Aspect of the drawn area, so the stamped signature is not stretched.
        Document.annotate.signPage(root.targetPage, root.strokes,
                                   canvasArea.height / canvasArea.width, 0);
        root.accepted();
        root.close();
    }

    states: State {
        name: "open"
        PropertyChanges { root.opacity: 1 }
    }

    transitions: Transition {
        NumberAnimation {
            property: "opacity"
            duration: Motion.normal
            easing.type: Easing.OutCubic
        }
    }

    Keys.onEscapePressed: root.close()

    // Scrim. This one *is* modal -- the document must not move while drawing.
    Rectangle {
        anchors.fill: parent
        color: Tokens.scrim

        MouseArea {
            anchors.fill: parent
            onClicked: root.close()
        }
    }

    Squircle {
        id: sheet

        anchors.centerIn: parent
        width: Math.min(parent.width - Tokens.space6 * 2, 560)
        height: header.height + canvasArea.height + footer.height + Tokens.space5 * 2

        radius: Tokens.radiusLarge
        fillColor: Tokens.surfaceElevated
        strokeColor: Tokens.separator
        strokeWidth: 1

        transformOrigin: Item.Center
        scale: root.state === "open" ? 1.0 : 0.96
        Behavior on scale {
            SpringAnimation {
                spring: Motion.spring
                damping: Motion.damping
                epsilon: Motion.epsilon
            }
        }

        // Swallow clicks so the scrim behind does not close the sheet.
        MouseArea { anchors.fill: parent }

        Column {
            id: header
            x: Tokens.space5
            y: Tokens.space5
            width: parent.width - Tokens.space5 * 2
            spacing: 2

            Text {
                text: qsTr("Sign")
                font.family: Tokens.fontFamily
                font.pixelSize: Tokens.textTitle
                font.weight: Tokens.weightSemiBold
                color: Tokens.textPrimary
            }

            Text {
                text: qsTr("Draw your signature. It will be placed on page %1.")
                        .arg(root.targetPage + 1)
                font.family: Tokens.fontFamily
                font.pixelSize: Tokens.textSmall
                color: Tokens.textSecondary
            }
        }

        // -- The pad ---------------------------------------------------------
        Squircle {
            id: canvasArea

            x: Tokens.space5
            y: header.y + header.height + Tokens.space4
            width: parent.width - Tokens.space5 * 2
            height: width * 0.42

            radius: Tokens.radiusSmall
            fillColor: Tokens.dark ? Qt.rgba(1, 1, 1, 0.04) : Qt.rgba(0, 0, 0, 0.03)
            strokeColor: Tokens.separator
            strokeWidth: 1

            // Signature baseline, the way a paper form has one.
            Rectangle {
                y: parent.height * 0.72
                x: Tokens.space5
                width: parent.width - Tokens.space5 * 2
                height: 1
                color: Tokens.separator
            }

            Text {
                anchors.centerIn: parent
                text: qsTr("Draw here")
                font.family: Tokens.fontFamily
                font.pixelSize: Tokens.textBody
                color: Tokens.textTertiary
                visible: !root.hasInk && root.currentStroke.length === 0
            }

            // Strokes are rendered as GPU shapes, not a software Canvas, so
            // drawing keeps up with the pointer.
            // PathMultiline, not a Repeater of ShapePath: Repeater delegates
            // must be Items, and a ShapePath is not one. PathMultiline is the
            // element built for exactly this -- several disconnected polylines
            // in one path, which is what a set of pen strokes is.
            Shape {
                anchors.fill: parent
                preferredRendererType: Shape.CurveRenderer

                ShapePath {
                    strokeColor: Tokens.textPrimary
                    strokeWidth: 2.4
                    fillColor: "transparent"
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin

                    PathMultiline {
                        // Normalised points scaled back up for display. The
                        // in-progress stroke is appended so the line follows
                        // the pointer before the button is released.
                        paths: {
                            const all = root.strokes.concat(
                                root.currentStroke.length >= 2 ? [root.currentStroke] : []);
                            const out = [];
                            for (const stroke of all) {
                                const line = [];
                                for (const p of stroke) {
                                    line.push(Qt.point(p.x * canvasArea.width,
                                                       p.y * canvasArea.height));
                                }
                                out.push(line);
                            }
                            return out;
                        }
                    }
                }
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.CrossCursor

                function normalised(mx, my) {
                    return Qt.point(Math.max(0, Math.min(1, mx / width)),
                                    Math.max(0, Math.min(1, my / height)));
                }

                onPressed: (mouse) => {
                    root.currentStroke = [normalised(mouse.x, mouse.y)];
                }

                onPositionChanged: (mouse) => {
                    if (!pressed)
                        return;
                    // Reassign rather than push: QML only notices a list
                    // property changing when the reference changes.
                    root.currentStroke = root.currentStroke.concat(
                        [normalised(mouse.x, mouse.y)]);
                }

                onReleased: {
                    if (root.currentStroke.length >= 2)
                        root.strokes = root.strokes.concat([root.currentStroke]);
                    root.currentStroke = [];
                }
            }
        }

        // -- Footer ----------------------------------------------------------
        Item {
            id: footer
            x: Tokens.space5
            y: canvasArea.y + canvasArea.height + Tokens.space4
            width: parent.width - Tokens.space5 * 2
            height: Tokens.controlHeightLarge

            LumenButton {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Clear")
                variant: LumenButton.Variant.Plain
                enabled: root.hasInk
                onClicked: root.clear()
            }

            Row {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: Tokens.space2

                LumenButton {
                    text: qsTr("Cancel")
                    variant: LumenButton.Variant.Secondary
                    onClicked: root.close()
                }

                LumenButton {
                    text: qsTr("Place on Page")
                    variant: LumenButton.Variant.Primary
                    enabled: root.hasInk
                    onClicked: root.apply()
                }
            }
        }
    }
}
