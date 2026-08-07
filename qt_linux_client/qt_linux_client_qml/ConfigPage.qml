import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: root
    color: "#f5f7fa"
    border.color: "#d0d7de"
    radius: 6

    property alias wifiSsid: wifiSsidField.text
    property alias wifiPassword: wifiPasswordField.text
    property alias mqttHost: mqttHostField.text
    property alias mqttPort: mqttPortField.value
    property alias mqttTlsEnabled: mqttTlsCheck.checked
    property alias mqttUsername: mqttUsernameField.text
    property alias mqttPassword: mqttPasswordField.text
    property alias deviceId: deviceIdField.text
    property alias apiToken: apiTokenField.text
    property alias samplePeriod: samplePeriodField.value
    property alias modbusEnabled: modbusEnabledCheck.checked
    property alias modbusSlaveAddr: modbusSlaveAddrField.value
    property alias modbusBaudRate: modbusBaudRateField.value
    property alias modbusStartRegister: modbusStartRegisterField.value
    property alias modbusRegisterCount: modbusRegisterCountField.value
    property alias modbusPollPeriod: modbusPollPeriodField.value

    function clearSensitiveInputs() {
        wifiPasswordField.text = ""
        mqttPasswordField.text = ""
        apiTokenField.text = ""
    }

    signal reloadRequested()
    signal saveRequested()

    ScrollView {
        id: configScroll
        anchors.fill: parent
        anchors.margins: 12
        clip: true

        ColumnLayout {
            width: configScroll.availableWidth
            spacing: 12

            Label {
                text: "设备配置"
                font.pixelSize: 18
                font.bold: true
            }

            Label {
                text: "密码和 Token 仅在你主动输入时提交，读取配置时不会明文回填。"
                color: "#4b5563"
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }

            GridLayout {
                Layout.fillWidth: true
                columns: 2
                rowSpacing: 10
                columnSpacing: 12

                Label { text: "WiFi SSID" }
                TextField {
                    id: wifiSsidField
                    Layout.fillWidth: true
                    placeholderText: "WiFi SSID"
                }

                Label { text: "新 WiFi 密码" }
                TextField {
                    id: wifiPasswordField
                    Layout.fillWidth: true
                    echoMode: TextInput.Password
                    placeholderText: "留空表示不修改"
                }

                Label { text: "MQTT Host" }
                TextField {
                    id: mqttHostField
                    Layout.fillWidth: true
                    placeholderText: "MQTT Host"
                }

                Label { text: "MQTT Port" }
                SpinBox {
                    id: mqttPortField
                    from: 1
                    to: 65535
                    value: 1883
                }

                Label { text: "MQTT Username" }
                TextField {
                    id: mqttUsernameField
                    Layout.fillWidth: true
                    placeholderText: "MQTT Username"
                }

                Label { text: "新 MQTT 密码" }
                TextField {
                    id: mqttPasswordField
                    Layout.fillWidth: true
                    echoMode: TextInput.Password
                    placeholderText: "留空表示不修改"
                }

                Label { text: "Device ID" }
                TextField {
                    id: deviceIdField
                    Layout.fillWidth: true
                    placeholderText: "Device ID"
                }

                Label { text: "新 API Token" }
                TextField {
                    id: apiTokenField
                    Layout.fillWidth: true
                    echoMode: TextInput.Password
                    placeholderText: "留空表示不修改"
                }

                Label { text: "采样周期 (ms)" }
                SpinBox {
                    id: samplePeriodField
                    from: 500
                    to: 60000
                    value: 2000
                    editable: true
                }

                CheckBox {
                    id: mqttTlsCheck
                    text: "启用 MQTT TLS"
                    Layout.columnSpan: 2
                }

                CheckBox {
                    id: modbusEnabledCheck
                    text: "启用 Modbus"
                    Layout.columnSpan: 2
                }

                Label { text: "Modbus 从站地址" }
                SpinBox {
                    id: modbusSlaveAddrField
                    from: 1
                    to: 247
                    value: 1
                    editable: true
                }

                Label { text: "Modbus 波特率" }
                SpinBox {
                    id: modbusBaudRateField
                    from: 1200
                    to: 1000000
                    value: 9600
                    stepSize: 1200
                    editable: true
                }

                Label { text: "起始寄存器" }
                SpinBox {
                    id: modbusStartRegisterField
                    from: 0
                    to: 65535
                    value: 0
                    editable: true
                }

                Label { text: "寄存器数量" }
                SpinBox {
                    id: modbusRegisterCountField
                    from: 1
                    to: 16
                    value: 4
                    editable: true
                }

                Label { text: "轮询周期 (ms)" }
                SpinBox {
                    id: modbusPollPeriodField
                    from: 500
                    to: 60000
                    value: 2000
                    editable: true
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                Button {
                    text: "重新加载配置"
                    onClicked: root.reloadRequested()
                }

                Button {
                    text: "保存配置"
                    onClicked: root.saveRequested()
                }

                Button {
                    text: "清空敏感输入"
                    onClicked: root.clearSensitiveInputs()
                }

                Item { Layout.fillWidth: true }
            }
        }
    }
}
