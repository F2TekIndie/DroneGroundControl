import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import DroneGroundControl



ApplicationWindow {
    id: root
    visible: true
    width: 1280
    height: 800
    color: "#202830"

    required property Receiver receiver
    required property DroneManager dm

    onClosing:{
        receiver.stop()
    }

    menuBar: MenuBar{
        id: menu
    }
    header: ToolBar{
        id: header
    }
    footer: TabBar{
        id: footer
    }

    RowLayout {
        id: main
        anchors.fill: parent

        DroneList{
            id: droneList
            dm: root.dm
        }

        ColumnLayout{
            id: dataViews
            Layout.fillWidth: true
            Layout.fillHeight: true

            CameraView{
                id: cameraStream
                receiver: root.receiver
            }

            DroneMap{
                id: droneMap
                dm: root.dm
            }
        }
    }
}
