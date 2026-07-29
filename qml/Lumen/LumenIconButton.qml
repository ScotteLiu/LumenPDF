import QtQuick
import QtQuick.Templates as T
import Lumen

// Square icon button for toolbars. Takes a glyph string rather than an image
// so the toolbar stays resolution-independent and themeable until a proper
// icon set is drawn.
T.Button {
    id: control

    property string glyph: ""
    property bool active: false          // sticky "on" state, e.g. sidebar toggle
    property string tooltip: ""

    implicitWidth: Tokens.controlHeightLarge
    implicitHeight: Tokens.controlHeightLarge

    hoverEnabled: true

    scale: control.pressed ? Motion.pressScale : 1.0
    Behavior on scale {
        SpringAnimation {
            spring: Motion.spring
            damping: Motion.damping
            epsilon: Motion.epsilon
        }
    }

    opacity: control.enabled ? 1.0 : 0.35
    Behavior on opacity {
        NumberAnimation { duration: Motion.fast; easing.type: Easing.OutCubic }
    }

    background: Squircle {
        radius: Tokens.radiusSmall
        fillColor: control.active ? Tokens.accentSubtle
                 : control.pressed ? Tokens.pressOverlay
                 : control.hovered ? Tokens.hoverOverlay
                 : "transparent"

        Behavior on fillColor {
            ColorAnimation { duration: Motion.instant; easing.type: Easing.OutCubic }
        }
    }

    contentItem: Text {
        text: control.glyph
        font.family: Tokens.fontFamily
        font.pixelSize: Tokens.iconSize
        color: control.active ? Tokens.accent : Tokens.textPrimary
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter

        Behavior on color {
            ColorAnimation { duration: Motion.instant; easing.type: Easing.OutCubic }
        }
    }

    // Hover tooltip -- delayed, so sweeping across the toolbar shows nothing.
    Loader {
        active: control.hovered && control.tooltip.length > 0
        asynchronous: true
        sourceComponent: Item {
            Timer {
                running: true
                interval: 480
                onTriggered: tip.opacity = 1.0
            }

            Squircle {
                id: tip
                parent: control
                y: control.height + Tokens.space2
                x: (control.width - width) / 2
                width: tipText.implicitWidth + Tokens.space3 * 2
                height: tipText.implicitHeight + Tokens.space2
                z: 100
                opacity: 0
                radius: Tokens.radiusSmall
                fillColor: Tokens.surfaceElevated
                strokeColor: Tokens.separator
                strokeWidth: 1

                Behavior on opacity {
                    NumberAnimation { duration: Motion.fast; easing.type: Easing.OutCubic }
                }

                Text {
                    id: tipText
                    anchors.centerIn: parent
                    text: control.tooltip
                    font.family: Tokens.fontFamily
                    font.pixelSize: Tokens.textSmall
                    color: Tokens.textSecondary
                }
            }
        }
    }
}
