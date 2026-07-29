import QtQuick
import Lumen

// A hairline. Always exactly one physical pixel, never scaled by the device
// pixel ratio -- a 2px "1px" line is the fastest way to look cheap.
Rectangle {
    property bool vertical: false

    implicitWidth: vertical ? 1 / Screen.devicePixelRatio : parent.width
    implicitHeight: vertical ? parent.height : 1 / Screen.devicePixelRatio
    color: Tokens.separator
}
