import QtQuick
import QtQuick.Layouts
import QtQuick.Controls



Dialog{
    id: root
    required property var drone
    property bool hoverMode: false
    property bool triggerHovered: false
    property Item hoverTarget

    function updateHover(){
        if ( !hoverMode )
            return
        if ( triggerHovered ){
            closeTimer.stop()
            if ( !visible )
                openTimer.restart()
        }
        else{
            openTimer.stop()
            if ( !dialogHover.hovered )
                closeTimer.restart()
        }
        if ( dialogHover.hovered )
            closeTimer.stop()
    }

    onTriggerHoveredChanged: updateHover()
    onAboutToShow:{
        if ( hoverMode && hoverTarget ){
            const point = hoverTarget.mapToItem( parent, hoverTarget.width + 8, 0 )
            x = Math.max( 8, Math.min( point.x, parent.width - width - 8 ) )
            y = Math.max( 8, Math.min( point.y, parent.height - height - 8 ) )
        }
    }

    Timer{
        id: openTimer
        interval: 400
        onTriggered: root.open()
    }

    Timer{
        id: closeTimer
        interval: 300
        onTriggered:{
            if ( !root.triggerHovered && !dialogHover.hovered )
                root.close()
        }
    }

    HoverHandler{
        id: dialogHover
        parent: root.background
        onHoveredChanged: root.updateHover()
    }

    parent: Overlay.overlay
    anchors.centerIn: hoverMode ? null : parent
    width: Math.min( 520, parent.width - 32 )
    height: Math.min( implicitHeight, parent.height - 32 )
    title: "Drone details"
    modal: !hoverMode
    focus: !hoverMode
    padding: 10

    palette.windowText: "#FFFFFF"
    palette.text: "#FFFFFF"
    palette.buttonText: "#FFFFFF"
    palette.base: "#202830"
    palette.window: "#2A3540"
    palette.button: "#202830"
    palette.highlight: "#4A5B6B"

    background: Rectangle{
        color: "#2A3540"
        border.color: "#4A5B6B"
        radius: 10
    }

    header: Label{
        text: root.title
        color: "#FFFFFF"
        font.bold: true
        padding: 10
        wrapMode: Text.Wrap
    }

    footer: DialogButtonBox{
        padding: 10
        background: Item{}
        visible: !root.hoverMode

        Button{
            id: closeButton
            text: "Close"
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole

            contentItem: Label{
                text: closeButton.text
                color: "#FFFFFF"
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }

            background: Rectangle{
                implicitWidth: 80
                implicitHeight: 36
                color: closeButton.down ? "#4A5B6B" : closeButton.hovered ? "#3A4B5B" : "#202830"
                border.color: closeButton.visualFocus ? "#FFFFFF" : "#4A5B6B"
                radius: 10
            }
        }
    }

    contentItem: ScrollView{
        id: scroll
        implicitHeight: details.implicitHeight
        contentWidth: availableWidth
        clip: true

        ColumnLayout{
            id: details
            width: scroll.availableWidth
            spacing: 10

            Repeater{
                model: [
                    ["ID", root.drone.droneId],
                    ["Client ID", root.drone.clientId],
                    ["Name", root.drone.name],
                    ["Type", root.drone.type],
                    ["State", root.drone.stateName],
                    ["Online", root.drone.online ? "Yes" : "No"],
                    ["Has position", root.drone.hasPosition ? "Yes" : "No"],
                    ["Latitude", root.drone.hasPosition ? root.drone.latitude : "Unavailable"],
                    ["Longitude", root.drone.hasPosition ? root.drone.longitude : "Unavailable"]
                ]

                Label{
                    required property var modelData
                    Layout.fillWidth: true
                    text: modelData[0] + ": " + modelData[1]
                    color: "#FFFFFF"
                    wrapMode: Text.WrapAnywhere
                }
            }
        }
    }
}
