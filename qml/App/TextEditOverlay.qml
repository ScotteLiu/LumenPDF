import QtQuick
import Lumen
import Lumen.Backend

// In-place editor for one text run.
//
// The editable unit is a whole text object, because that is what PDFium can
// replace -- it cannot split one. How much text that turns out to be depends
// entirely on how the producing application grouped its glyphs, so the editor
// shows the exact run it is about to replace rather than letting the user
// assume they are changing one word.
Item {
    id: root

    // Page-local geometry of the run, in PDF points, and the scale to pixels.
    property rect runBounds: Qt.rect(0, 0, 0, 0)
    property real pointScale: 1.0
    property string runText: ""
    property real runFontSize: 12
    property bool longRun: false

    signal committed(string text)
    signal cancelled()

    visible: opacity > 0
    opacity: 0
    z: 60

    function open() {
        field.text = root.runText;
        root.state = "open";
        field.forceActiveFocus();
        field.selectAll();
    }

    function close() { root.state = "" }

    states: State {
        name: "open"
        PropertyChanges { root.opacity: 1 }
    }

    transitions: Transition {
        NumberAnimation {
            property: "opacity"
            duration: Motion.fast
            easing.type: Easing.OutCubic
        }
    }

    // Sits exactly over the run it replaces, so the user can see what is being
    // changed and roughly how it will fit.
    x: runBounds.x * pointScale
    y: runBounds.y * pointScale
    width: Math.max(120, runBounds.width * pointScale)
    height: Math.max(Tokens.controlHeight, runBounds.height * pointScale)

    Squircle {
        anchors.fill: parent
        anchors.margins: -3
        radius: Tokens.radiusSmall
        fillColor: Tokens.surfaceElevated
        strokeColor: Tokens.accent
        strokeWidth: 2
    }

    TextInput {
        id: field

        anchors.fill: parent
        anchors.leftMargin: 3
        anchors.rightMargin: 3
        verticalAlignment: TextInput.AlignVCenter
        clip: true

        // Matches the run's own size so the edit reads as being on the page,
        // not in a dialog about the page.
        font.family: Tokens.fontFamily
        font.pixelSize: Math.max(9, root.runFontSize * root.pointScale)
        color: Tokens.textPrimary
        selectionColor: Tokens.accentSubtle
        selectedTextColor: Tokens.textPrimary
        selectByMouse: true

        Keys.onReturnPressed: root.committed(field.text)
        Keys.onEnterPressed: root.committed(field.text)
        Keys.onEscapePressed: root.cancelled()
    }

    // Warning and instructions, floated below so they never cover the page
    // content being edited.
    Squircle {
        y: parent.height + Tokens.space2
        width: hint.implicitWidth + Tokens.space4 * 2
        height: hint.implicitHeight + Tokens.space3
        radius: Tokens.radiusSmall
        fillColor: Tokens.surfaceElevated
        strokeColor: root.longRun ? Qt.rgba(1, 0.72, 0.3, 0.6) : Tokens.separator
        strokeWidth: 1

        Text {
            id: hint
            anchors.centerIn: parent
            font.family: Tokens.fontFamily
            font.pixelSize: Tokens.textCaption
            color: Tokens.textSecondary
            text: root.longRun
                  // Said plainly, because it is true and the user will
                  // otherwise discover it by ruining a paragraph.
                  ? qsTr("Enter to apply · Esc to cancel — this is a long run; spacing may shift")
                  : qsTr("Enter to apply · Esc to cancel")
        }
    }
}
