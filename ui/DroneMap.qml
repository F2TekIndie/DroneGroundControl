import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
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
    property var selectedPosition: QtPositioning.coordinate()

    function showSelectedDrone(){
        const position = dm.selectedDronePosition()
        selectedPosition = position.latitude !== undefined
            ? QtPositioning.coordinate( position.latitude, position.longitude )
            : QtPositioning.coordinate()
        if ( selectedPosition.isValid )
            map.center = selectedPosition
    }

    Component.onCompleted: showSelectedDrone()

    Connections{
        target: root.dm
        function onSelectedDroneIdChanged(){
            droneDialog.close()
            root.showSelectedDrone()
        }
    }

    DroneDialog{
        id: droneDialog
        objectName: "droneHoverDialog"
        drone: ({})
        hoverMode: true
        hoverTarget: selectedDrone
        triggerHovered: infoActive.hovered && droneMarker.visible
        onAboutToShow: drone = root.dm.selectedDroneData()
    }

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

        MapQuickItem{
            id: droneMarker
            coordinate: root.selectedPosition
            visible: root.selectedPosition.isValid
            anchorPoint.x: 8
            anchorPoint.y: 8

            HoverHandler{
                id: infoActive
            }

            sourceItem: Rectangle{
                id: selectedDrone
                objectName: "droneMarker"
                width: 16
                height: 16
                radius: 8
                color: "red"
                border.color: "#4A5B6B"
                border.width: 3

            }
        }

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
