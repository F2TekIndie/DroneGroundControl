import QtQuick
import QtQuick.Layouts
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

}
