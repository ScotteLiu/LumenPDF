import QtQuick
import QtQuick.Templates as T
import Lumen

// Overlay scrollbar: invisible until the view moves or the pointer is near it,
// then thickens on hover. Never occupies layout space.
T.ScrollBar {
    id: control

    implicitWidth: 14
    padding: 3
    minimumSize: 0.06

    policy: T.ScrollBar.AsNeeded

    contentItem: Squircle {
        implicitWidth: control.hovered || control.pressed ? 9 : 5
        radius: width / 2
        fillColor: control.pressed ? Tokens.textSecondary : Tokens.textTertiary

        opacity: (control.policy === T.ScrollBar.AlwaysOn
                  || control.active
                  || control.hovered) ? 0.85 : 0.0

        Behavior on implicitWidth {
            NumberAnimation { duration: Motion.fast; easing.type: Easing.OutCubic }
        }
        Behavior on opacity {
            NumberAnimation { duration: Motion.normal; easing.type: Easing.OutCubic }
        }
        Behavior on fillColor {
            ColorAnimation { duration: Motion.instant }
        }
    }
}
