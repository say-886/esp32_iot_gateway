import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: root
    color: "#f5f7fa"
    border.color: "#d0d7de"
    radius: 6

    property alias otaUrl: otaUrlField.text

    signal startOtaRequested()
    signal rebootRequested()

    RowLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 16

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 10

            Label {
                text: "固件 URL"
                font.pixelSize: 16
                font.bold: true
            }

            TextField {
                id: otaUrlField
                Layout.fillWidth: true
                placeholderText: "https://example.com/esp32_iot_gateway.bin"
            }

            Label {
                text: "只支持 HTTPS 固件地址。升级过程中请保持设备供电稳定，不要断电。"
                wrapMode: Text.Wrap
                color: "#5f6b7a"
                Layout.fillWidth: true
            }
        }

        ColumnLayout {
            spacing: 10

            Button {
                text: "开始 OTA"
                onClicked: root.startOtaRequested()
            }

            Button {
                text: "重启设备"
                onClicked: root.rebootRequested()
            }
        }
    }
}
