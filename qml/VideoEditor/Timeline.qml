import QtQuick 1.1
import com.nokia.meego 1.0
import VideoEditor 1.0

Page {
    id: timeline
    width: 854
    height: 480

    VideoEditor {
        id: videoeditor
    }

    Rectangle {
        anchors.fill: parent
        color: "black"
    }


    Item {
        id: preview
        width: 512
        height: 288
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top

        Rectangle {
            id: previewBackground

            anchors.fill: parent

            // This is the RGB hex value for the color key in omapxvsink
            color: "#080810"
        }

        Text {
            id: previewText
            text: qsTr("Video preview")
            font.bold: true
            anchors.verticalCenter: parent.verticalCenter
            anchors.horizontalCenter: parent.horizontalCenter
            font.pixelSize: 32
            color: "white"
        }
    }

    Item {
        id: leftButtons
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: preview.left
        anchors.bottom: timelineBar.top

        Button {
            id: projectsButton
            text: "Projects"
            anchors.topMargin: 10
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            anchors.bottomMargin: 10
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: parent.height / 2.0 - 15
        }
        Button {
            id: addMediaButton
            text: "Add Media"
            anchors.topMargin: 10
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            anchors.bottomMargin: 10
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: projectsButton.bottom
            anchors.bottom: parent.bottom

            onClicked: {
                var component = Qt.createComponent("VideoGallery.qml")
                pageStack.push(component);
            }
        }
    }

    Item {
        id: rightButtons
        anchors.bottom: timelineBar.top
        anchors.left: preview.right
        anchors.right: parent.right
        anchors.top: parent.top

        Button {
            id: playButton
            text: "Play"
            anchors.topMargin: 10
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            anchors.bottomMargin: 10
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: parent.height / 2.0 - 15
        }
        Button {
            id: cameraButton
            text: "Camera"
            anchors.topMargin: 10
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            anchors.bottomMargin: 10
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: playButton.bottom
            anchors.bottom: parent.bottom
        }
    }

    Item {
        id: timelineBar

        anchors.top: preview.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right

        Rectangle {
            id: timelineBackground
            anchors.fill: parent
            color: "grey"
        }

        ListView {
            id: list
            model: videoeditor

            width: parent.width
            height: parent.height

            orientation: ListView.Horizontal

            boundsBehavior: Flickable.StopAtBounds

            delegate: Column {
                anchors.verticalCenter: parent.verticalCenter
                Text {
                    text: "URI: " + url
                    font.pointSize: 26
                    color: "white"
                }
            }

            // x: parent.width / 2 - flickable.contentWidth * flickable.visibleArea.xPosition / (1.0 - flickable.visibleArea.widthRatio)
        }

        Rectangle {
            id: playhead

            anchors.top: parent.top
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom

            width: 3
            height: parent.height

            color: "red"
        }
    }
}
