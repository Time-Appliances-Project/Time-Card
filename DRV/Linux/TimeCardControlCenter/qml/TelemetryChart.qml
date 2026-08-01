import QtQuick

Item {
    id: root

    property var samples: []
    property color lineColor: "#31d7d1"
    property color fillColor: "#163d43"
    property color gridColor: "#17343e"

    implicitHeight: 180
    onSamplesChanged: chart.requestPaint()
    onWidthChanged: chart.requestPaint()
    onHeightChanged: chart.requestPaint()

    Canvas {
        id: chart
        anchors.fill: parent

        onPaint: {
            var context = getContext("2d")
            context.clearRect(0, 0, width, height)

            context.strokeStyle = root.gridColor
            context.lineWidth = 1
            for (var row = 1; row < 4; ++row) {
                var gy = row * height / 4
                context.beginPath()
                context.moveTo(0, gy)
                context.lineTo(width, gy)
                context.stroke()
            }

            var values = root.samples || []
            if (values.length < 2)
                return

            var maximum = 1
            for (var index = 0; index < values.length; ++index)
                maximum = Math.max(maximum, Math.abs(Number(values[index])))

            function xFor(i) { return i * width / Math.max(1, values.length - 1) }
            function yFor(value) { return height / 2 - Number(value) * (height * 0.42) / maximum }

            context.strokeStyle = "#31515c"
            context.beginPath()
            context.moveTo(0, height / 2)
            context.lineTo(width, height / 2)
            context.stroke()

            context.beginPath()
            context.moveTo(xFor(0), height)
            for (var fillIndex = 0; fillIndex < values.length; ++fillIndex)
                context.lineTo(xFor(fillIndex), yFor(values[fillIndex]))
            context.lineTo(xFor(values.length - 1), height)
            context.closePath()
            context.fillStyle = root.fillColor
            context.fill()

            context.beginPath()
            for (var lineIndex = 0; lineIndex < values.length; ++lineIndex) {
                var x = xFor(lineIndex)
                var y = yFor(values[lineIndex])
                if (lineIndex === 0)
                    context.moveTo(x, y)
                else
                    context.lineTo(x, y)
            }
            context.strokeStyle = root.lineColor
            context.lineWidth = 2
            context.stroke()
        }
    }
}
