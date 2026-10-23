import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import DroneGroundControl
import org.freedesktop.gstreamer.Qt6GLVideoItem 1.0


ApplicationWindow {
    id: root
    visible: true
    width: 1280
    height: 800
    color: "#202830"

    required property Receiver receiver

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

        Rectangle{
            id: droneList
            color: "#2A3540"
            border.color: "#4A5B6B"
            radius: 10
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.maximumWidth: 600
        }

        ColumnLayout{
            id: dataViews
            Layout.fillWidth: true
            Layout.fillHeight: true
            Rectangle{
                id: cameraStream
                color: "#2A3540"
                border.color: "#4A5B6B"
                radius: 10
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.maximumWidth: 1000
                Layout.maximumHeight: 1000
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 8
                    GstGLQt6VideoItem {
                        objectName: "videoItem"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: 120
                    }
                    Label {
                        Layout.fillWidth: true
                        text: root.receiver.status
                        color: "#eeeeee"
                        wrapMode: Text.Wrap
                    }
                    RowLayout {
                        Button { text: "Start"; enabled: !root.receiver.active; onClicked: root.receiver.start() }
                        Button { text: "Stop"; enabled: root.receiver.active; onClicked: root.receiver.stop() }
                    }
                }

            }
            Rectangle{
                id: droneMap
                color: "#2A3540"
                border.color: "#4A5B6B"
                radius: 10
                Layout.fillWidth: true
                Layout.fillHeight: true
            }
        }
    }
}
