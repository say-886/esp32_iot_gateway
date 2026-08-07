import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

RowLayout {
    id: root
    spacing: 8

    property alias hostText: hostField.text
    property alias portValue: portField.value
    property alias tokenText: tokenField.text
    property alias statusText: statusLabel.text

    signal connectClicked()
    signal disconnectClicked()

    Label { text: "设备 IP:" }

    TextField {
        id: hostField
        text: "10.135.247.46"
        Layout.fillWidth: true
        placeholderText: "请输入设备 IP"
    }

    Label { text: "端口:" }

    SpinBox {
        id: portField
        from: 1
        to: 65535
        value: 80
    }

    Label { text: "Token:" }

    TextField {
        id: tokenField
        Layout.preferredWidth: 180
        echoMode: TextInput.Password
        placeholderText: "API Token"
    }

    Button {
        text: "连接"
        onClicked: root.connectClicked()
    }

    Button {
        text: "断开"
        onClicked: root.disconnectClicked()
    }

    Label {
        id: statusLabel
        text: "未连接"
    }
}
