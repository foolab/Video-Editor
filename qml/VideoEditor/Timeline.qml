import QtQuick 1.1
import com.nokia.meego 1.0
import VideoEditor 1.0

Page {
    id: timeline
    width: 854
    height: 480

    VideoEditor {
        id: videoeditor

        //onError: {
        //    console.debug("Error: " + message + " (" + debug + ")");
        //}
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
            text: qsTr("Video preview\n(Tap to play)")
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
            anchors.topMargin: 16
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            anchors.bottomMargin: 16
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: parent.height / 2.0 - 24
        }
        Button {
            id: addMediaButton
            text: "Add Media"
            anchors.topMargin: 16
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            anchors.bottomMargin: 16
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

    Sheet {
        id: exportFile
        x: 0

        width: parent.width
        anchors.top: timelineBar.bottom
        anchors.topMargin: 0
        anchors.bottom: timelineBar.top
        anchors.bottomMargin: 0

        acceptButtonText: "Save"
        rejectButtonText: "Cancel"

        content: TextInput {
            id: filename

            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.topMargin: 16

            font.pointSize: 22
            maximumLength: 64 // FIXME - set to maximum filename length

            //validator: RegExpValidator
            //inputMask: something

            text: videoeditor.getFileName()

            activeFocusOnPress: false
            MouseArea {
                anchors.fill: parent
                onClicked: {
                    if (!filename.activeFocus) {
                        filename.forceActiveFocus()
                        filename.openSoftwareInputPanel();
                    } else {
                        filename.focus = false;
                    }
                }
                onPressAndHold: filename.closeSoftwareInputPanel();
            }
        }
        onAccepted: {
            videoeditor.setFileName(filename.text)
            filename.closeSoftwareInputPanel()
            videoeditor.render()
        }
        onRejected: {
            videoeditor.setFileName(filename.text)
            filename.closeSoftwareInputPanel()
        }
    }

    Item {
        id: rightButtons
        anchors.bottom: timelineBar.top
        anchors.left: preview.right
        anchors.right: parent.right
        anchors.top: parent.top

        Button {
            id: exportButton
            text: "Export"
            anchors.topMargin: 16
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            anchors.bottomMargin: 16
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: parent.height / 2.0 - 24

            onClicked: {
                exportFile.open()
            }
        }
        Button {
            id: cameraButton
            text: "Camera"
            anchors.topMargin: 16
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            anchors.bottomMargin: 16
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: exportButton.bottom
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

            width: parent.width - 32
            height: parent.height - 32
            anchors.margins: 16
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.verticalCenter: parent.verticalCenter
            highlightRangeMode: ListView.ApplyRange
            spacing: 16

            orientation: ListView.Horizontal

            boundsBehavior: Flickable.StopAtBounds

            delegate: Column {
                anchors.verticalCenter: parent.verticalCenter
                Rectangle {
                    color: "light grey"
                    width: list.width / 3
                    height: list.height
                    Text {
                        width: parent.width - 16
                        height: parent.height - 16
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.verticalCenter: parent.verticalCenter
                        text: uri
                        font.pointSize: 20
                        color: "black"
                        wrapMode: Text.WrapAnywhere
                        maximumLineCount: 4
                        elide: Text.ElideRight
                        horizontalAlignment: Text.AlignHCenter
                    }
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
