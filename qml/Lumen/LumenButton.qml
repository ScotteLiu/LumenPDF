import QtQuick
import QtQuick.Templates as T
import Lumen

// Primary/secondary/plain button.
//
// Three visual variants, one behaviour. Press feedback is a scale-down on a
// spring rather than a colour flash -- it reads as physical, and it is the
// same gesture language the page view uses for zoom.
T.Button {
    id: control

    enum Variant {
        Primary,
        Secondary,
        Plain
    }

    property int variant: LumenButton.Variant.Secondary

    implicitHeight: Tokens.controlHeight
    implicitWidth: Math.max(72, label.implicitWidth + Tokens.space5 * 2)

    padding: Tokens.space4
    font.family: Tokens.fontFamily
    font.pixelSize: Tokens.textBody
    font.weight: Tokens.weightMedium

    scale: control.pressed ? Motion.pressScale : 1.0
    Behavior on scale {
        SpringAnimation {
            spring: Motion.spring
            damping: Motion.damping
            epsilon: Motion.epsilon
        }
    }

    opacity: control.enabled ? 1.0 : 0.4
    Behavior on opacity {
        NumberAnimation { duration: Motion.fast; easing.type: Easing.OutCubic }
    }

    background: Squircle {
        radius: Tokens.radiusSmall
        curvature: 5.0

        fillColor: {
            switch (control.variant) {
            case LumenButton.Variant.Primary:
                return control.pressed ? Tokens.accentPressed : Tokens.accent;
            case LumenButton.Variant.Secondary:
                return control.pressed ? Tokens.pressOverlay
                     : control.hovered ? Tokens.hoverOverlay
                     : Tokens.surfaceElevated;
            default:
                return control.pressed ? Tokens.pressOverlay
                     : control.hovered ? Tokens.hoverOverlay
                     : "transparent";
            }
        }

        strokeColor: control.variant === LumenButton.Variant.Secondary
                     ? Tokens.separator : "transparent"
        strokeWidth: control.variant === LumenButton.Variant.Secondary ? 1 : 0

        Behavior on fillColor {
            ColorAnimation { duration: Motion.instant; easing.type: Easing.OutCubic }
        }
    }

    contentItem: Text {
        id: label
        text: control.text
        font: control.font
        color: control.variant === LumenButton.Variant.Primary
               ? Tokens.textOnAccent : Tokens.textPrimary
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    // Keyboard focus ring, drawn outside the shape so it never shifts layout.
    Squircle {
        anchors.fill: parent
        anchors.margins: -3
        radius: Tokens.radiusSmall + 3
        strokeColor: Tokens.accent
        strokeWidth: 2
        visible: control.visualFocus
        opacity: control.visualFocus ? 1.0 : 0.0
        Behavior on opacity {
            NumberAnimation { duration: Motion.fast; easing.type: Easing.OutCubic }
        }
    }
}
