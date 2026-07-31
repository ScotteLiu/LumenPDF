import QtQuick
import Lumen

// A small numeric entry.
//
// Clamping happens on commit rather than on every keystroke: clamping as you
// type means deleting a digit to retype it snaps the value to the minimum and
// eats the rest of what you were about to enter.
Squircle {
    id: root

    property int minimum: 0
    property int maximum: 9999
    property int value: 0

    signal valueEdited(int value)

    width: 76
    height: Tokens.controlHeight
    radius: Tokens.radiusSmall
    fillColor: Tokens.surface
    strokeColor: input.activeFocus ? Tokens.accent : Tokens.separator
    strokeWidth: 1

    Behavior on strokeColor { ColorAnimation { duration: Motion.instant } }

    function commit() {
        const parsed = parseInt(input.text, 10);
        const clamped = isNaN(parsed)
                        ? root.value
                        : Math.max(root.minimum, Math.min(root.maximum, parsed));
        input.text = String(clamped);
        if (clamped !== root.value)
            root.valueEdited(clamped);
    }

    TextInput {
        id: input

        anchors.fill: parent
        anchors.leftMargin: Tokens.space3
        anchors.rightMargin: Tokens.space3 + stepper.width

        verticalAlignment: TextInput.AlignVCenter
        text: String(root.value)
        color: Tokens.textPrimary
        font.family: Tokens.fontFamily
        font.pixelSize: Tokens.textBody
        font.features: ({ "tnum": 1 })
        selectByMouse: true
        selectionColor: Tokens.accentSubtle
        selectedTextColor: Tokens.textPrimary
        validator: IntValidator { bottom: root.minimum; top: root.maximum }

        onEditingFinished: root.commit()
        Keys.onUpPressed: root.valueEdited(Math.min(root.maximum, root.value + 1))
        Keys.onDownPressed: root.valueEdited(Math.max(root.minimum, root.value - 1))
    }

    Column {
        id: stepper
        anchors.right: parent.right
        anchors.rightMargin: Tokens.space1
        anchors.verticalCenter: parent.verticalCenter
        width: 16
        spacing: 1

        Repeater {
            model: [1, -1]

            delegate: Rectangle {
                required property int modelData

                width: 16
                height: 12
                radius: 3
                color: hover.hovered ? Tokens.hoverOverlay : "transparent"

                LumenIcon {
                    anchors.centerIn: parent
                    // The chevron points the way the value will move.
                    path: modelData > 0 ? Icons.chevronUp : Icons.chevronDown
                    size: 10
                    color: Tokens.textSecondary
                }

                HoverHandler { id: hover }
                TapHandler {
                    onTapped: {
                        const next = Math.max(root.minimum,
                                              Math.min(root.maximum, root.value + modelData));
                        if (next !== root.value)
                            root.valueEdited(next);
                    }
                }
            }
        }
    }
}
