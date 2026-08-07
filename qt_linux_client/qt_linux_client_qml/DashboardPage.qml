import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: root
    color: "#f5f7fa"
    border.color: "#d0d7de"
    radius: 6

    property string temperatureText: "-- °C"
    property string humidityText: "-- %"
    property string lightText: "-- Lux"
    property string wifiText: "--"
    property string mqttText: "--"
    property string deviceStateText: "--"
    property string uptimeText: "--"
    property string firmwareText: "--"
    property string deviceIdText: "--"
    property string errorText: "--"
    property string ledText: "--"
    property string buzzerText: "--"
    property string relayText: "--"
    property var telemetryPoints: []

    ScrollView {
        id: dashboardScroll
        anchors.fill: parent
        anchors.margins: 12
        clip: true

        ColumnLayout {
            width: dashboardScroll.availableWidth
            spacing: 12

            GridLayout {
                Layout.fillWidth: true
                columns: 3
                rowSpacing: 10
                columnSpacing: 10

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 82
                    color: "#ffffff"
                    border.color: "#d0d7de"
                    radius: 6

                    Column {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 6

                        Label { text: "温度"; color: "#6b7280" }
                        Label { text: root.temperatureText; font.pixelSize: 20; font.bold: true }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 82
                    color: "#ffffff"
                    border.color: "#d0d7de"
                    radius: 6

                    Column {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 6

                        Label { text: "湿度"; color: "#6b7280" }
                        Label { text: root.humidityText; font.pixelSize: 20; font.bold: true }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 82
                    color: "#ffffff"
                    border.color: "#d0d7de"
                    radius: 6

                    Column {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 6

                        Label { text: "光照"; color: "#6b7280" }
                        Label { text: root.lightText; font.pixelSize: 20; font.bold: true }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 78
                    color: "#ffffff"
                    border.color: "#d0d7de"
                    radius: 6

                    Column {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 6

                        Label { text: "WiFi"; color: "#6b7280" }
                        Label { text: root.wifiText; font.bold: true }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 78
                    color: "#ffffff"
                    border.color: "#d0d7de"
                    radius: 6

                    Column {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 6

                        Label { text: "MQTT"; color: "#6b7280" }
                        Label {
                            text: root.mqttText
                            font.bold: true
                            wrapMode: Text.Wrap
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 78
                    color: "#ffffff"
                    border.color: "#d0d7de"
                    radius: 6

                    Column {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 6

                        Label { text: "设备状态"; color: "#6b7280" }
                        Label { text: root.deviceStateText; font.bold: true }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 78
                    color: "#ffffff"
                    border.color: "#d0d7de"
                    radius: 6

                    Column {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 6

                        Label { text: "运行时间"; color: "#6b7280" }
                        Label { text: root.uptimeText; font.bold: true }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 78
                    color: "#ffffff"
                    border.color: "#d0d7de"
                    radius: 6

                    Column {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 6

                        Label { text: "固件版本"; color: "#6b7280" }
                        Label {
                            text: root.firmwareText
                            font.bold: true
                            wrapMode: Text.Wrap
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 78
                    color: "#ffffff"
                    border.color: "#d0d7de"
                    radius: 6

                    Column {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 6

                        Label { text: "设备 ID"; color: "#6b7280" }
                        Label {
                            text: root.deviceIdText
                            font.bold: true
                            wrapMode: Text.Wrap
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 78
                    color: "#ffffff"
                    border.color: "#d0d7de"
                    radius: 6

                    Column {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 6

                        Label { text: "错误状态"; color: "#6b7280" }
                        Label {
                            text: root.errorText
                            font.bold: true
                            wrapMode: Text.Wrap
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 78
                    color: "#ffffff"
                    border.color: "#d0d7de"
                    radius: 6

                    Column {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 6

                        Label { text: "LED"; color: "#6b7280" }
                        Label { text: root.ledText; font.bold: true }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 78
                    color: "#ffffff"
                    border.color: "#d0d7de"
                    radius: 6

                    Column {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 6

                        Label { text: "Buzzer"; color: "#6b7280" }
                        Label { text: root.buzzerText; font.bold: true }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 78
                    color: "#ffffff"
                    border.color: "#d0d7de"
                    radius: 6

                    Column {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 6

                        Label { text: "Relay"; color: "#6b7280" }
                        Label { text: root.relayText; font.bold: true }
                    }
                }
            }

            Label {
                text: "本地历史记录: " + (root.telemetryPoints ? root.telemetryPoints.length : 0) + " 条"
                color: "#4b5563"
            }

            TelemetryChart {
                Layout.fillWidth: true
                title: "温度历史"
                points: root.telemetryPoints
                valueKey: "temperature"
                unit: "°C"
                lineColor: "#ef4444"
            }

            TelemetryChart {
                Layout.fillWidth: true
                title: "湿度历史"
                points: root.telemetryPoints
                valueKey: "humidity"
                unit: "%"
                lineColor: "#2563eb"
            }

            TelemetryChart {
                Layout.fillWidth: true
                title: "光照历史"
                points: root.telemetryPoints
                valueKey: "light"
                unit: "Lux"
                decimals: 0
                lineColor: "#d97706"
            }
        }
    }
}
