import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: root
    color: "#ffffff"
    border.color: "#d0d7de"
    radius: 6

    property var points: []
    property string title: "历史曲线"
    property string valueKey: "temperature"
    property string unit: ""
    property color lineColor: "#2563eb"
    property int decimals: 1

    readonly property int pointCount: points ? points.length : 0
    readonly property var chartRange: calculateRange()
    readonly property var latestPoint: pointCount > 0 ? points[pointCount - 1] : null

    function calculateRange() {
        if (!points || points.length === 0) {
            return { min: 0, max: 1 }
        }

        var minimum = Number(points[0][valueKey])
        var maximum = minimum

        for (var i = 1; i < points.length; ++i) {
            var current = Number(points[i][valueKey])
            if (current < minimum) {
                minimum = current
            }
            if (current > maximum) {
                maximum = current
            }
        }

        if (minimum === maximum) {
            var padding = minimum === 0 ? 1 : Math.abs(minimum) * 0.1
            minimum -= padding
            maximum += padding
        }

        return { min: minimum, max: maximum }
    }

    function formatValue(value) {
        if (value === undefined || value === null || isNaN(Number(value))) {
            return "--"
        }

        var text = Number(value).toFixed(decimals)
        return unit === "" ? text : text + " " + unit
    }

    function formatTime(index) {
        if (!points || index < 0 || index >= points.length) {
            return "--"
        }

        var timestamp = points[index].timestamp
        if (!timestamp) {
            return "--"
        }

        var dateValue = new Date(timestamp)
        if (isNaN(dateValue.getTime())) {
            return String(timestamp)
        }

        return Qt.formatDateTime(dateValue, "hh:mm:ss")
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        RowLayout {
            Layout.fillWidth: true

            Label {
                text: root.title
                font.pixelSize: 15
                font.bold: true
            }

            Item { Layout.fillWidth: true }

            Label {
                text: root.latestPoint ? ("最新值: " + root.formatValue(root.latestPoint[root.valueKey])) : "等待数据"
                color: "#4b5563"
            }
        }

        Canvas {
            id: chartCanvas
            Layout.fillWidth: true
            Layout.preferredHeight: 120

            onPaint: {
                var ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)

                var paddingLeft = 12
                var paddingTop = 10
                var paddingRight = 12
                var paddingBottom = 18
                var plotWidth = width - paddingLeft - paddingRight
                var plotHeight = height - paddingTop - paddingBottom

                ctx.strokeStyle = "#e5e7eb"
                ctx.lineWidth = 1
                for (var row = 0; row < 4; ++row) {
                    var y = paddingTop + row * plotHeight / 3
                    ctx.beginPath()
                    ctx.moveTo(paddingLeft, y)
                    ctx.lineTo(width - paddingRight, y)
                    ctx.stroke()
                }

                if (!root.points || root.points.length < 2) {
                    ctx.fillStyle = "#9ca3af"
                    ctx.font = "13px sans-serif"
                    ctx.textAlign = "center"
                    ctx.fillText("等待更多历史数据", width / 2, height / 2)
                    return
                }

                var minimum = root.chartRange.min
                var maximum = root.chartRange.max
                var valueRange = maximum - minimum

                function pointX(index) {
                    return paddingLeft + index * plotWidth / (root.points.length - 1)
                }

                function pointY(value) {
                    return paddingTop + plotHeight - ((value - minimum) / valueRange) * plotHeight
                }

                ctx.strokeStyle = root.lineColor
                ctx.lineWidth = 2
                ctx.beginPath()
                for (var i = 0; i < root.points.length; ++i) {
                    var value = Number(root.points[i][root.valueKey])
                    var x = pointX(i)
                    var yValue = pointY(value)
                    if (i === 0) {
                        ctx.moveTo(x, yValue)
                    } else {
                        ctx.lineTo(x, yValue)
                    }
                }
                ctx.stroke()

                var lastValue = Number(root.points[root.points.length - 1][root.valueKey])
                var lastX = pointX(root.points.length - 1)
                var lastY = pointY(lastValue)
                ctx.fillStyle = root.lineColor
                ctx.beginPath()
                ctx.arc(lastX, lastY, 3, 0, Math.PI * 2)
                ctx.fill()
            }

            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
        }

        RowLayout {
            Layout.fillWidth: true

            Label {
                text: "最小值: " + root.formatValue(root.chartRange.min)
                color: "#6b7280"
            }

            Item { Layout.fillWidth: true }

            Label {
                text: root.pointCount > 0 ? (root.formatTime(0) + " - " + root.formatTime(root.pointCount - 1)) : "--"
                color: "#6b7280"
            }

            Item { Layout.fillWidth: true }

            Label {
                text: "最大值: " + root.formatValue(root.chartRange.max)
                color: "#6b7280"
            }
        }
    }

    onPointsChanged: chartCanvas.requestPaint()
    onValueKeyChanged: chartCanvas.requestPaint()
    onLineColorChanged: chartCanvas.requestPaint()

    Component.onCompleted: chartCanvas.requestPaint()
}
