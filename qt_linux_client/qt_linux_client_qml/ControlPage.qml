import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: root
    color: "#f5f7fa"
    border.color: "#d0d7de"
    radius: 6

    property bool controlsEnabled: true
    property string controlHintText: "控制链路: HTTP"

    signal ledToggleRequested()
    signal buzzerToggleRequested()
    signal relayToggleRequested()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        Label {
            text: root.controlHintText
            color: "#4b5563"
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Button {
                text: "LED 切换"
                enabled: root.controlsEnabled
                onClicked: root.ledToggleRequested()
            }

            Button {
                text: "蜂鸣器切换"
                enabled: root.controlsEnabled
                onClicked: root.buzzerToggleRequested()
            }

            Button {
                text: "继电器切换"
                enabled: root.controlsEnabled
                onClicked: root.relayToggleRequested()
            }

            Item { Layout.fillWidth: true }
        }

        Item { Layout.fillHeight: true }
    }
}
