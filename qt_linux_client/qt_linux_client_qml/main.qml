import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15

ApplicationWindow {
    visible: true
    width: 1100
    height: 720
    title: "ESP32 IoT Gateway Client"

    function twoDigit(value) {
        return value < 10 ? "0" + value : "" + value
    }

    function appendLog(message) {
        var now = new Date()
        var timeText = twoDigit(now.getHours()) + ":" +
                       twoDigit(now.getMinutes()) + ":" +
                       twoDigit(now.getSeconds())
        logPanel.appendLine("[" + timeText + "] " + message)
    }

    Connections {
        target: appController

        function onLogMessage(message) {
            appendLog(message)
        }

        function onConfigDataLoaded(configData) {
            configPage.wifiSsid = configData["wifi_ssid"] || ""
            configPage.mqttHost = configData["mqtt_host"] || ""
            configPage.mqttPort = Number(configData["mqtt_port"] || 1883)
            configPage.mqttTlsEnabled = Boolean(configData["mqtt_use_tls"])
            configPage.mqttUsername = configData["mqtt_username"] || ""
            configPage.deviceId = configData["device_id"] || ""
            configPage.samplePeriod = Number(configData["sample_period_ms"] || 2000)
            configPage.modbusEnabled = Boolean(configData["modbus_enabled"])
            configPage.modbusSlaveAddr = Number(configData["modbus_slave_addr"] || 1)
            configPage.modbusBaudRate = Number(configData["modbus_baud_rate"] || 9600)
            configPage.modbusStartRegister = Number(configData["modbus_start_register"] || 0)
            configPage.modbusRegisterCount = Number(configData["modbus_register_count"] || 4)
            configPage.modbusPollPeriod = Number(configData["modbus_poll_period_ms"] || 2000)
            configPage.clearSensitiveInputs()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        TopBar {
            id: topBar
            Layout.fillWidth: true
            statusText: appController.connectionStatus

            onConnectClicked: {
                appController.connectToDevice(hostText, portValue, tokenText)

                if (tokenText.trim() !== "") {
                    appController.loadConfig()
                    appController.loadModbusStatus()
                }
            }

            onDisconnectClicked: appController.disconnectFromDevice()
        }

        TabBar {
            id: tabBar
            Layout.fillWidth: true

            TabButton { text: "Dashboard" }
            TabButton { text: "Control" }
            TabButton { text: "Config" }
            TabButton { text: "OTA" }
            TabButton { text: "Modbus" }
            TabButton { text: "Logs" }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabBar.currentIndex

            DashboardPage {
                temperatureText: appController.dashboardTemperature
                humidityText: appController.dashboardHumidity
                lightText: appController.dashboardLight
                wifiText: appController.dashboardWifi
                mqttText: appController.dashboardMqtt
                deviceStateText: appController.dashboardState
                uptimeText: appController.dashboardUptime
                firmwareText: appController.dashboardFirmware
                deviceIdText: appController.dashboardDeviceId
                errorText: appController.dashboardError
                ledText: appController.dashboardLed
                buzzerText: appController.dashboardBuzzer
                relayText: appController.dashboardRelay
                telemetryPoints: appController.telemetryPoints
            }

            ControlPage {
                controlsEnabled: appController.controlsEnabled
                controlHintText: appController.controlHintText
                onLedToggleRequested: appController.toggleLed()
                onBuzzerToggleRequested: appController.toggleBuzzer()
                onRelayToggleRequested: appController.toggleRelay()
            }

            ConfigPage {
                id: configPage
                onReloadRequested: appController.loadConfig()
                onSaveRequested: {
                    appController.saveConfig({
                        "wifi_ssid": wifiSsid,
                        "wifi_password": wifiPassword,
                        "mqtt_host": mqttHost,
                        "mqtt_port": mqttPort,
                        "mqtt_use_tls": mqttTlsEnabled,
                        "mqtt_username": mqttUsername,
                        "mqtt_password": mqttPassword,
                        "device_id": deviceId,
                        "api_token": apiToken,
                        "sample_period_ms": samplePeriod,
                        "modbus_enabled": modbusEnabled,
                        "modbus_slave_addr": modbusSlaveAddr,
                        "modbus_baud_rate": modbusBaudRate,
                        "modbus_start_register": modbusStartRegister,
                        "modbus_register_count": modbusRegisterCount,
                        "modbus_poll_period_ms": modbusPollPeriod
                    })
                    configPage.clearSensitiveInputs()
                }
            }

            OtaPage {
                onStartOtaRequested: appController.startOta(otaUrl)
                onRebootRequested: appController.rebootDevice()
            }

            ModbusPage {
                statusText: appController.modbusStatusText
                onRefreshRequested: appController.loadModbusStatus()
            }

            Rectangle {
                color: "#f5f7fa"
                border.color: "#d0d7de"
                radius: 6

                ScrollView {
                    id: logsScroll
                    anchors.fill: parent
                    anchors.margins: 8
                    clip: true

                    TextArea {
                        width: logsScroll.availableWidth
                        readOnly: true
                        wrapMode: TextArea.Wrap
                        text: logPanel.logText
                        background: null
                    }
                }
            }
        }

        LogPanel {
            id: logPanel
            Layout.fillWidth: true
            Layout.preferredHeight: 130
        }
    }

    Component.onCompleted: {
        topBar.hostText = appController.savedHost
        topBar.portValue = appController.savedPort
        topBar.tokenText = appController.savedToken

        appendLog("主界面初始化完成")
        appendLog("已恢复最近一次连接参数")
    }
}
