import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window

    width: 1440
    height: 920
    minimumWidth: 1080
    minimumHeight: 720
    visible: true
    title: "OCP Time Card Control Center"
    color: "#07161d"

    property int currentPage: 0
    // qmllint disable unqualified
    property var appController: controller
    // qmllint enable unqualified

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 236
            color: "#091b23"
            border.color: "#15323c"
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 18
                spacing: 12

                RowLayout {
                    Layout.fillWidth: true
                    Layout.bottomMargin: 22
                    spacing: 11

                    Rectangle {
                        Layout.preferredWidth: 40
                        Layout.preferredHeight: 40
                        radius: 10
                        color: "#173d44"
                        border.color: "#2a7074"

                        Text {
                            anchors.centerIn: parent
                            text: "TC"
                            color: "#67f1e8"
                            font.pixelSize: 14
                            font.bold: true
                        }
                    }

                    ColumnLayout {
                        spacing: 0
                        Label {
                            text: "TIME CARD"
                            color: "#f1fbfc"
                            font.pixelSize: 15
                            font.weight: Font.Bold
                            font.letterSpacing: 1.4
                        }
                        Label {
                            text: "CONTROL CENTER"
                            color: "#62838f"
                            font.pixelSize: 9
                            font.letterSpacing: 1.1
                        }
                    }
                }

                Label {
                    text: "WORKSPACES"
                    color: "#4f727e"
                    font.pixelSize: 9
                    font.weight: Font.Bold
                    font.letterSpacing: 1.3
                    Layout.leftMargin: 12
                }

                NavButton {
                    Layout.fillWidth: true
                    text: "Overview"
                    symbol: "◉"
                    selected: window.currentPage === 0
                    onClicked: window.currentPage = 0
                }

                NavButton {
                    Layout.fillWidth: true
                    text: "GNSS and UART"
                    symbol: "⌁"
                    selected: window.currentPage === 1
                    onClicked: window.currentPage = 1
                }

                NavButton {
                    Layout.fillWidth: true
                    text: "oscillatord"
                    symbol: "◇"
                    selected: window.currentPage === 2
                    onClicked: window.currentPage = 2
                }

                Item { Layout.fillHeight: true }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: statusColumn.implicitHeight + 26
                    radius: 12
                    color: window.appController.offsetValid ? "#102f2c" : "#2b2520"
                    border.color: window.appController.offsetValid ? "#24584f" : "#574331"

                    ColumnLayout {
                        id: statusColumn
                        anchors.fill: parent
                        anchors.margins: 13
                        spacing: 5

                        Label {
                            text: window.appController.connectionState
                            color: window.appController.offsetValid ? "#7ce8bd" : "#e6b477"
                            font.pixelSize: 10
                            font.bold: true
                        }
                        Label {
                            Layout.fillWidth: true
                            text: window.appController.backendName
                            color: "#7897a3"
                            font.pixelSize: 10
                            wrapMode: Text.Wrap
                        }
                        Label {
                            text: window.appController.lastUpdated.length === 0 ? "Starting telemetry..." : (window.appController.timingValid ? "Sampled " + window.appController.lastUpdated : "Polled " + window.appController.lastUpdated + "; no timing sample")
                            color: "#587681"
                            font.pixelSize: 9
                        }
                    }
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 78
                color: "#081920"
                border.color: "#15323c"
                border.width: 1

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 26
                    anchors.rightMargin: 26
                    spacing: 16

                    ColumnLayout {
                        spacing: 2
                        Label {
                            text: ["Precision timing overview", "GNSS and serial endpoints", "Oscillator discipline"][window.currentPage]
                            color: "#edf9fb"
                            font.pixelSize: 20
                            font.weight: Font.DemiBold
                        }
                        Label {
                            text: window.appController.selectedDevice + "  •  " + window.appController.pciIdentity
                            color: "#6f909c"
                            font.pixelSize: 11
                        }
                    }

                    Item { Layout.fillWidth: true }

                    ComboBox {
                        id: deviceSelector
                        Layout.preferredWidth: 150
                        model: window.appController.availableDevices
                        currentIndex: Math.max(0, window.appController.availableDevices.indexOf(window.appController.selectedDevice))
                        enabled: count > 0
                        onActivated: window.appController.selectedDevice = currentText
                    }

                    Button {
                        text: "Refresh"
                        onClicked: window.appController.refresh()
                    }
                }
            }

            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: window.currentPage

                ScrollView {
                    id: overviewScroll
                    clip: true
                    contentWidth: availableWidth
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                    ColumnLayout {
                        width: overviewScroll.availableWidth
                        spacing: 16

                        Item { Layout.preferredHeight: 8 }

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.leftMargin: 24
                            Layout.rightMargin: 24
                            spacing: 14

                            MetricTile {
                                Layout.fillWidth: true
                                label: "PHC to system"
                                value: window.appController.offsetText
                                detail: window.appController.timestampMethod
                                tone: !window.appController.offsetValid ? "#f0bf65" : (Math.abs(window.appController.offsetNanoseconds) < 1000 ? "#66e3aa" : "#f0bf65")
                            }
                            MetricTile {
                                Layout.fillWidth: true
                                label: "PPS supervisor"
                                value: window.appController.gnssState
                                detail: "Clock source  " + window.appController.clockSource
                                tone: window.appController.gnssLocked ? "#66e3aa" : "#f0bf65"
                            }
                            MetricTile {
                                Layout.fillWidth: true
                                label: "Sample window"
                                value: window.appController.sampleWindowText
                                detail: window.appController.ptpDevice
                                tone: window.appController.sampleWindowValid ? "#5fb8ff" : "#f0bf65"
                            }
                            MetricTile {
                                Layout.fillWidth: true
                                label: "UTC to TAI"
                                value: window.appController.utcTaiOffset
                                detail: "Applied to PHC comparison"
                                tone: "#b794f6"
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.leftMargin: 24
                            Layout.rightMargin: 24
                            spacing: 16

                            Panel {
                                Layout.fillWidth: true
                                title: "Precision clock"
                                subtitle: "UTC presentation with TAI-aware comparison"

                                KeyValueRow { label: "Time Card PHC"; value: window.appController.phcTime; valueColor: "#6ce8df" }
                                KeyValueRow { label: "Linux system"; value: window.appController.systemTime }
                                KeyValueRow { label: "Clock source"; value: window.appController.clockSource }
                                KeyValueRow { label: "FPGA offset"; value: window.appController.clockOffset }
                                KeyValueRow { label: "FPGA drift"; value: window.appController.clockDrift }
                            }

                            Panel {
                                Layout.fillWidth: true
                                title: "Hardware identity"
                                subtitle: "ptp_ocp discovery and endpoints"
                                accent: "#5fb8ff"

                                KeyValueRow { label: "Device"; value: window.appController.selectedDevice }
                                KeyValueRow { label: "PCI identity"; value: window.appController.pciIdentity }
                                KeyValueRow { label: "Serial"; value: window.appController.serialNumber }
                                KeyValueRow { label: "PTP node"; value: window.appController.ptpDevice }
                                KeyValueRow { label: "PPS node"; value: window.appController.ppsDevice }
                                KeyValueRow { label: "Sysfs"; value: window.appController.sysfsPath }
                            }
                        }

                        Panel {
                            Layout.fillWidth: true
                            Layout.leftMargin: 24
                            Layout.rightMargin: 24
                            Layout.bottomMargin: window.appController.windowHistory.length === 0 && window.appController.error.length === 0 ? 24 : 0
                            title: "PHC offset history"
                            subtitle: "Last 200 one-second samples, nanoseconds"
                            accent: "#31d7d1"

                            TelemetryChart {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 190
                                samples: window.appController.offsetHistory
                            }
                        }

                        Panel {
                            Layout.fillWidth: true
                            Layout.leftMargin: 24
                            Layout.rightMargin: 24
                            Layout.bottomMargin: window.appController.error.length === 0 ? 24 : 0
                            visible: window.appController.windowHistory.length > 0
                            title: "Sampling window history"
                            subtitle: "Last 60 bracketed samples, nanoseconds"
                            accent: "#5fb8ff"

                            TelemetryChart {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 145
                                samples: window.appController.windowHistory
                                lineColor: "#5fb8ff"
                            }
                        }

                        Panel {
                            Layout.fillWidth: true
                            Layout.leftMargin: 24
                            Layout.rightMargin: 24
                            Layout.bottomMargin: 24
                            visible: window.appController.error.length > 0
                            title: "Diagnostic"
                            accent: "#f0bf65"

                            Label {
                                Layout.fillWidth: true
                                text: window.appController.error
                                color: "#dfbd81"
                                wrapMode: Text.Wrap
                            }
                        }
                    }
                }

                ScrollView {
                    id: gnssScroll
                    clip: true
                    contentWidth: availableWidth
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                    ColumnLayout {
                        width: gnssScroll.availableWidth
                        spacing: 16

                        Item { Layout.preferredHeight: 8 }

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.leftMargin: 24
                            Layout.rightMargin: 24
                            spacing: 14

                            MetricTile {
                                Layout.fillWidth: true
                                label: "PPS supervisor"
                                value: window.appController.gnssState
                                detail: window.appController.gnssLocked ? "Supervisor reports sync" : "Not a receiver-fix indicator"
                                tone: window.appController.gnssLocked ? "#66e3aa" : "#f0bf65"
                            }
                            MetricTile {
                                Layout.fillWidth: true
                                label: "ToD protocol"
                                value: window.appController.todProtocol
                                detail: window.appController.todBaudRate + " baud"
                                tone: "#5fb8ff"
                            }
                            MetricTile {
                                Layout.fillWidth: true
                                label: "Clock source"
                                value: window.appController.clockSource
                                detail: "Current FPGA selection"
                                tone: "#b794f6"
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.leftMargin: 24
                            Layout.rightMargin: 24
                            spacing: 16

                            Panel {
                                Layout.fillWidth: true
                                title: "Serial endpoints"
                                subtitle: "Read-only discovery from ptp_ocp sysfs"

                                KeyValueRow { label: "Primary GNSS"; value: window.appController.ttyGnss }
                                KeyValueRow { label: "Secondary GNSS"; value: window.appController.ttyGnss2 }
                                KeyValueRow { label: "Atomic clock"; value: window.appController.ttyMac }
                                KeyValueRow { label: "NMEA output"; value: window.appController.ttyNmea }
                                KeyValueRow { label: "I2C adapter"; value: window.appController.i2cDevice }
                                KeyValueRow { label: "mRO-50 bridge"; value: window.appController.mro50Device }
                            }

                            Panel {
                                Layout.fillWidth: true
                                title: "Detected capabilities"
                                subtitle: "Workspaces are gated by hardware resources"
                                accent: "#5fb8ff"

                                Flow {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: childrenRect.height
                                    spacing: 8

                                    Repeater {
                                        model: window.appController.capabilities
                                        delegate: Rectangle {
                                            id: capabilityChip
                                            required property string modelData
                                            width: capabilityLabel.implicitWidth + 20
                                            height: 30
                                            radius: 15
                                            color: "#16333d"
                                            border.color: "#285361"

                                            Label {
                                                id: capabilityLabel
                                                anchors.centerIn: parent
                                                text: capabilityChip.modelData
                                                color: "#a8ced5"
                                                font.pixelSize: 11
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        Panel {
                            Layout.fillWidth: true
                            Layout.leftMargin: 24
                            Layout.rightMargin: 24
                            Layout.bottomMargin: 24
                            title: "UART monitoring"
                            subtitle: "Next implementation slice"
                            accent: "#b794f6"

                            Label {
                                Layout.fillWidth: true
                                text: "The backend has discovered the Linux UART endpoints. Passive QSerialPort capture and UBX/NMEA decoding will be added without requiring the dashboard to run as root."
                                color: "#9ab4bd"
                                wrapMode: Text.Wrap
                                lineHeight: 1.35
                            }
                        }
                    }
                }

                ScrollView {
                    id: oscillatorScroll
                    clip: true
                    contentWidth: availableWidth
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                    ColumnLayout {
                        width: oscillatorScroll.availableWidth
                        spacing: 16

                        Item { Layout.preferredHeight: 8 }

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.leftMargin: 24
                            Layout.rightMargin: 24
                            spacing: 14

                            MetricTile {
                                Layout.fillWidth: true
                                label: "Monitoring service"
                                value: window.appController.oscillatordVersion
                                detail: window.appController.oscillatordAvailable ? "127.0.0.1 monitoring API" : window.appController.oscillatordError
                                tone: window.appController.oscillatordAvailable ? "#66e3aa" : "#f0bf65"
                            }
                            MetricTile {
                                Layout.fillWidth: true
                                label: "Discipline state"
                                value: window.appController.disciplineStatus
                                detail: window.appController.disciplineAvailable ? Math.round(window.appController.convergenceProgress) + "% convergence" : "Not reported by service"
                                tone: "#31d7d1"
                            }
                            MetricTile {
                                Layout.fillWidth: true
                                label: "GNSS"
                                value: window.appController.oscillatordGnssSummary
                                detail: "Reported by oscillatord"
                                tone: "#5fb8ff"
                            }
                        }

                        Panel {
                            Layout.fillWidth: true
                            Layout.leftMargin: 24
                            Layout.rightMargin: 24
                            title: "Oscillator discipline"
                            subtitle: "Read-only status request { request: 0 }"

                            KeyValueRow { label: "Service"; value: window.appController.oscillatordVersion }
                            KeyValueRow { label: "State"; value: window.appController.disciplineStatus }
                            KeyValueRow { label: "Oscillator"; value: window.appController.oscillatorSummary }
                            KeyValueRow { label: "GNSS"; value: window.appController.oscillatordGnssSummary }

                            ProgressBar {
                                Layout.fillWidth: true
                                visible: window.appController.disciplineAvailable
                                from: 0
                                to: 1
                                value: Math.max(0, Math.min(1, window.appController.convergenceProgress / 100))
                            }

                            Button {
                                text: "Refresh oscillatord"
                                onClicked: window.appController.refreshOscillatord()
                            }
                        }

                        Panel {
                            Layout.fillWidth: true
                            Layout.leftMargin: 24
                            Layout.rightMargin: 24
                            Layout.bottomMargin: 24
                            visible: !window.appController.oscillatordAvailable
                            title: "Monitoring endpoint"
                            accent: "#f0bf65"

                            Label {
                                Layout.fillWidth: true
                                text: "Start oscillatord with monitoring enabled on 127.0.0.1:2958, or pass --oscillatord-host and --oscillatord-port. Control requests remain disabled in this milestone."
                                color: "#dfbd81"
                                wrapMode: Text.Wrap
                            }
                        }
                    }
                }
            }
        }
    }
}
