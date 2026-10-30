import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import DroneGroundControl



Rectangle{
    id: root
    color: "#2A3540"
    border.color: "#4A5B6B"
    radius: 10
    Layout.fillWidth: true
    Layout.fillHeight: true
    Layout.maximumWidth: 600

    required property DroneManager dm

    ListView{
        id: droneList
        anchors.fill: parent
        anchors.margins: 10
        clip: true
        spacing: 8
        model: root.dm

        ScrollBar.vertical: ScrollBar{}

        delegate: DroneListItem{
            width: droneList.width
            selected: root.dm.selectedDroneId === model.droneId
            onClicked: root.dm.selectedDroneId = model.droneId
        }
    }
}
