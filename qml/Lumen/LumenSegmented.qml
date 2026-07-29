import QtQuick
import Lumen

// Segmented control.
//
// The selection is a single squircle that *slides* between segments rather
// than appearing under the new one. That continuity is the whole trick: the
// eye tracks one object moving instead of two objects blinking, which is why
// it reads as physical.
Item {
    id: root

    property var options: []          // list of { label } or plain strings
    property int currentIndex: 0

    signal selected(int index)

    implicitHeight: Tokens.controlHeight
    implicitWidth: 200

    Squircle {
        anchors.fill: parent
        radius: Tokens.radiusSmall + 2
        fillColor: Tokens.hoverOverlay
    }

    readonly property real _segmentWidth:
        options.length > 0 ? (width - 4) / options.length : width

    // The moving selection.
    Squircle {
        id: indicator
        y: 2
        height: parent.height - 4
        width: root._segmentWidth
        x: 2 + root.currentIndex * root._segmentWidth
        radius: Tokens.radiusSmall
        fillColor: Tokens.surfaceElevated
        strokeColor: Tokens.separator
        strokeWidth: 1
        visible: root.options.length > 0

        Behavior on x {
            SpringAnimation {
                spring: Motion.spring
                damping: Motion.damping
                epsilon: Motion.epsilon
            }
        }
    }

    Row {
        anchors.fill: parent
        anchors.margins: 2

        Repeater {
            model: root.options

            delegate: Item {
                required property int index
                required property var modelData

                width: root._segmentWidth
                height: parent.height

                Text {
                    anchors.centerIn: parent
                    text: typeof modelData === "string" ? modelData : (modelData.label || "")
                    font.family: Tokens.fontFamily
                    font.pixelSize: Tokens.textSmall
                    font.weight: index === root.currentIndex
                                 ? Tokens.weightSemiBold : Tokens.weightMedium
                    color: index === root.currentIndex
                           ? Tokens.textPrimary : Tokens.textSecondary

                    Behavior on color { ColorAnimation { duration: Motion.fast } }
                }

                TapHandler {
                    onTapped: {
                        root.currentIndex = index;
                        root.selected(index);
                    }
                }
            }
        }
    }
}
