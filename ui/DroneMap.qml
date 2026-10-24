import QtQuick
import QtQuick.Layouts
import QtLocation
import QtPositioning
import DroneGroundControl



Rectangle{
    id: root
    color: "#2A3540"
    border.color: "#4A5B6B"
    radius: 10
    Layout.fillWidth: true
    Layout.fillHeight: true

    required property DroneManager dm

    Plugin{
        id: mapPlugin
        name: "osm"
    }
    Map{
        id: map
        anchors.fill: parent
        plugin: mapPlugin
        center: QtPositioning.coordinate(59, 10)
        zoomLevel: 13

        WheelHandler {
            id: wheel
            // workaround for QTBUG-87646 / QTBUG-112394 / QTBUG-112432:
            // Magic Mouse pretends to be a trackpad but doesn't work with PinchHandler
            // and we don't yet distinguish mice and trackpads on Wayland either
            acceptedDevices: Qt.platform.pluginName === "cocoa" || Qt.platform.pluginName === "wayland" ? PointerDevice.Mouse | PointerDevice.TouchPad : PointerDevice.Mouse
            rotationScale: 1/120
            property: "zoomLevel"
        }
        DragHandler {
            id: drag
            target: null
            onTranslationChanged: (delta) => map.pan(-delta.x, -delta.y)
        }
    }
}
