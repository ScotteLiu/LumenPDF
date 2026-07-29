import QtQuick
import QtQuick.Shapes

// Renders one path from Icons on a 24x24 grid, scaled to `size`.
//
// Stroked rather than filled, so a single path definition works at any size
// and in any colour without a second asset.
Item {
    id: root

    property string path: ""
    property color color: "black"
    property real size: 20
    property real strokeWidth: 1.75

    implicitWidth: size
    implicitHeight: size

    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        asynchronous: false

        // The path data is authored on a 24-unit grid; scale it to whatever
        // size the caller asked for, and scale the stroke with it so the icon
        // keeps the same optical weight.
        transform: Scale {
            xScale: root.size / 24
            yScale: root.size / 24
        }

        ShapePath {
            strokeColor: root.color
            // The Shape is scaled by size/24, so express the stroke in path
            // units such that it lands on screen at exactly `strokeWidth`.
            strokeWidth: root.size > 0 ? root.strokeWidth * 24 / root.size : 0
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin

            PathSvg { path: root.path }
        }
    }
}
