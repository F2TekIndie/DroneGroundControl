import QtQuick
import QtQuick.Layouts
import QtQuick.Controls



Rectangle{
    id: root
    required property var model
    property bool selected: false
    signal clicked()

    implicitHeight: details.implicitHeight + 20
    color: selected ? "#4A5B6B" : hover.hovered ? "#3A4B5B" : "#202830"
    border.color: "#4A5B6B"
    radius: 10

    HoverHandler{
        id: hover
        cursorShape: Qt.PointingHandCursor
    }

    TapHandler{
        acceptedButtons: Qt.LeftButton
        onTapped:{
            root.clicked()
        }
    }
    TapHandler{
        acceptedButtons: Qt.RightButton
        onTapped:{
            droneDialog.open()
        }
    }

    DroneDialog{
        id: droneDialog
        drone: root.model
    }

    ColumnLayout{
        id: details
        anchors.fill: parent
        anchors.margins: 10
        spacing: 4

        Label{
            Layout.fillWidth: true
            text: "ID: " + root.model.droneId
            color: "#FFFFFF"
            wrapMode: Text.WrapAnywhere
        }

        Label{
            Layout.fillWidth: true
            text: "Name: " + root.model.name
            color: "#FFFFFF"
            wrapMode: Text.Wrap
        }

        Label{
            Layout.fillWidth: true
            text: "Type: " + root.model.type
            color: "#FFFFFF"
            wrapMode: Text.Wrap
        }
    }
}
