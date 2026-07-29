import QtQuick
import QtQuick.Shapes

// A rounded rectangle with *continuous* corner curvature -- the shape Apple
// uses everywhere and the single detail that most separates a considered UI
// from a default one.
//
// A normal Rectangle joins a straight edge to a circular arc, so curvature
// jumps discontinuously at the join and the eye reads a subtle crease. Here
// each corner is a superellipse quadrant, so curvature ramps in smoothly.
//
// Usage is drop-in for Rectangle:
//
//     Squircle {
//         radius: Tokens.radiusMedium
//         fillColor: Tokens.surface
//         Text { anchors.centerIn: parent; text: "hello" }
//     }
Item {
    id: root

    default property alias content: contentItem.data

    property real radius: 12
    property color fillColor: "transparent"
    property color strokeColor: "transparent"
    property real strokeWidth: 0

    // 2.0 is a plain circular corner; ~5.0 matches Apple's continuous corner.
    // Higher values push the corner further towards a square.
    property real curvature: 5.0

    // Segments per corner. 10 is indistinguishable from smooth at any size a
    // control is realistically drawn at, and keeps the vertex count trivial.
    readonly property int _segments: 10

    Shape {
        id: shape
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer   // analytic AA, no MSAA cost
        asynchronous: false

        ShapePath {
            fillColor: root.fillColor
            strokeColor: root.strokeColor
            strokeWidth: root.strokeWidth
            joinStyle: ShapePath.RoundJoin
            capStyle: ShapePath.RoundCap

            PathPolyline {
                path: root._buildPath(root.width, root.height, root.radius, root.curvature)
            }
        }
    }

    Item {
        id: contentItem
        anchors.fill: parent
    }

    // Returns the closed outline as a point list. Recomputed whenever the item
    // resizes; at ~40 points per shape that is far cheaper than it looks, and
    // it keeps the whole thing free of custom C++ or shader compilation.
    function _buildPath(w, h, r, n) {
        if (w <= 0 || h <= 0)
            return [];

        const rad = Math.min(r, Math.min(w, h) / 2);
        if (rad <= 0)
            return [Qt.point(0, 0), Qt.point(w, 0), Qt.point(w, h), Qt.point(0, h), Qt.point(0, 0)];

        const exponent = 2.0 / n;
        const steps = root._segments;
        const points = [];

        // One superellipse quadrant, expressed as offsets from the corner's
        // inner centre. Shared by all four corners via sign flips.
        const quadrant = [];
        for (let i = 0; i <= steps; ++i) {
            const theta = (Math.PI / 2) * (i / steps);
            quadrant.push({
                x: rad * Math.pow(Math.abs(Math.cos(theta)), exponent),
                y: rad * Math.pow(Math.abs(Math.sin(theta)), exponent)
            });
        }

        // Top-left: from (0, rad) round to (rad, 0)
        for (let i = 0; i <= steps; ++i)
            points.push(Qt.point(rad - quadrant[i].x, rad - quadrant[i].y));

        // Top-right: from (w - rad, 0) round to (w, rad)
        for (let i = steps; i >= 0; --i)
            points.push(Qt.point(w - rad + quadrant[i].x, rad - quadrant[i].y));

        // Bottom-right
        for (let i = 0; i <= steps; ++i)
            points.push(Qt.point(w - rad + quadrant[i].x, h - rad + quadrant[i].y));

        // Bottom-left
        for (let i = steps; i >= 0; --i)
            points.push(Qt.point(rad - quadrant[i].x, h - rad + quadrant[i].y));

        points.push(points[0]); // close
        return points;
    }
}
