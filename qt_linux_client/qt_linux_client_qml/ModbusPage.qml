import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: root
    color: "#f5f7fa"
    border.color: "#d0d7de"
    radius: 6

    property string statusText: "Modbus 状态将在这里显示。"

    signal refreshRequested()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 10

        Button {
            text: "刷新 Modbus 状态"
            onClicked: root.refreshRequested()
        }

        TextArea {
            Layout.fillWidth: true
            Layout.fillHeight: true
            readOnly: true
            wrapMode: TextArea.Wrap
            text: root.statusText
        }
    }
}
