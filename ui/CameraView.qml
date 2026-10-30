import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.freedesktop.gstreamer.Qt6GLVideoItem 1.0
import DroneGroundControl



Rectangle{
    required property Receiver receiver

    id: root
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
        Item{
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 120
            clip: true

            GstGLQt6VideoItem {
                id: videoItem
                objectName: "videoItem"
                anchors.fill: parent
            }
            Label {
                id: overlayText
                objectName: "overlayText"
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.margins: 8
                color: "yellow"
                text: "Here could be some live drone information."
                wrapMode: Text.Wrap
            }
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
