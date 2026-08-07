import QtQuick 2.15
import QtQuick.Controls 2.15

Rectangle {
    color: "#f5f7fa"
    border.color: "#d0d7de"
    radius: 6

    property alias logText: logArea.text

    function appendLine(message) {
        logArea.text += message + "\n"
        Qt.callLater(function() {
            var flickable = scrollView.contentItem
            if (flickable) {
                flickable.contentY = Math.max(0, flickable.contentHeight - flickable.height)
            }
        })
    }

    ScrollView {
        id: scrollView
        anchors.fill: parent
        anchors.margins: 8
        clip: true
        ScrollBar.vertical.policy: ScrollBar.AsNeeded

        TextArea {
            id: logArea
            width: scrollView.availableWidth
            readOnly: true
            selectByMouse: true
            wrapMode: TextArea.Wrap
            text: ""
            background: null
        }
    }
}
