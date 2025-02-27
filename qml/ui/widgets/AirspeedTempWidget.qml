import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Layouts 1.12
import Qt.labs.settings 1.0

import OpenHD 1.0

BaseWidget {
    id: airspeedTempWidget
    width: 30
    height: 30

    visible: settings.show_airspeed_temp

    widgetIdentifier: "airspeed_temp_widget"
    bw_verbose_name: "AIRSPEED SENSOR TEMP"


    defaultAlignment: 1
    defaultXOffset: 12
    defaultYOffset: 32
    defaultHCenter: false
    defaultVCenter: false

    hasWidgetDetail: true

    widgetDetailComponent: ScrollView {

        contentHeight: idBaseWidgetDefaultUiControlElements.height
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        clip: true

        BaseWidgetDefaultUiControlElements{
            id: idBaseWidgetDefaultUiControlElements
            Item {
                width: parent.width
                height: 42
                Text {
                    id: airspeedtempSettingsTitle
                    text: qsTr("AIRSPEED SENSOR TEMPERATURE")
                    color: "white"
                    height: parent.height - 10
                    width: parent.width
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    font.pixelSize: detailPanelFontPixels
                    verticalAlignment: Text.AlignVCenter
                }
                Rectangle {
                    id: airspeedtempSettingsTitleUL
                    y: 34
                    width: parent.width
                    height: 3
                    color: "white"
                    radius: 5
                }
            }

            Item {
                width: parent.width
                height: 32
                Text {
                    text: qsTr("Declutter Upon Arm")
                    color: "white"
                    height: parent.height
                    font.bold: true
                    font.pixelSize: detailPanelFontPixels
                    anchors.left: parent.left
                    verticalAlignment: Text.AlignVCenter
                }
                Switch {
                    width: 32
                    height: parent.height
                    anchors.rightMargin: 6
                    anchors.right: parent.right
                    checked: settings.airspeed_temp_declutter
                    onCheckedChanged: settings.airspeed_temp_declutter = checked
                }
            }
            Item {
                id: airspeed_temp_warn_label
                width: parent.width
                height: 32
                Text {
                    text: qsTr("Warn Temp")
                    color: "white"
                    height: parent.height
                    font.bold: true
                    font.pixelSize: detailPanelFontPixels
                    anchors.left: parent.left
                    verticalAlignment: Text.AlignVCenter
                }
                Text {
                    text: settings.airspeed_temp_warn
                    color: settings.color_warn
                    height: parent.height
                    font.bold: true
                    font.pixelSize: detailPanelFontPixels
                    anchors.left: parent.right
                    verticalAlignment: Text.AlignVCenter
                }
                Slider {
                    id: airspeed_temp_warn_Slider
                    orientation: Qt.Horizontal
                    from: 0
                    value: settings.airspeed_temp_warn
                    to: 9
                    stepSize: 1
                    height: parent.height
                    anchors.rightMargin: 0
                    anchors.right: parent.right
                    width: parent.width - 96

                    onValueChanged: {
                        settings.airspeed_temp_warn = Math.round(
                                    airspeed_temp_warn_Slider.value * 10) / 10.0
                    }
                }
            }
            Item {
                id: airspeed_temp_caution_label
                width: parent.width
                height: 32
                Text {
                    text: qsTr("Caution Temp")
                    color: "white"
                    height: parent.height
                    font.bold: true
                    font.pixelSize: detailPanelFontPixels
                    anchors.left: parent.left
                    verticalAlignment: Text.AlignVCenter
                }
                Text {
                    text: settings.airspeed_temp_caution
                    color: settings.color_caution
                    height: parent.height
                    font.bold: true
                    font.pixelSize: detailPanelFontPixels
                    anchors.left: airspeed_temp_caution_label.right
                    verticalAlignment: Text.AlignVCenter
                }
                Slider {
                    id: airspeed_temp_caution_Slider
                    orientation: Qt.Horizontal
                    from: 10
                    value: settings.airspeed_temp_caution
                    to: 20
                    stepSize: 1
                    height: parent.height
                    anchors.rightMargin: 0
                    anchors.right: parent.right
                    width: parent.width - 96

                    onValueChanged: {
                        settings.airspeed_temp_caution = Math.round(
                                    airspeed_temp_caution_Slider.value * 10) / 10.0
                    }
                }
            }
        }
    }

    Item {
        id: widgetInner

        anchors.fill: parent
        scale: bw_current_scale

        Text {
            id: temp_glyph
            color: _fcMavlinkSystem.airspeed_sensor_temperature_degree <= settings.airspeed_temp_caution ? (_fcMavlinkSystem.airspeed_sensor_temperature_degree <= settings.airspeed_temp_warn ? settings.color_warn : settings.color_caution) : settings.color_shape
            opacity: bw_current_opacity
            text: _fcMavlinkSystem.airspeed_sensor_temperature_degree <= settings.airspeed_temp_caution ? (_fcMavlinkSystem.airspeed_sensor_temperature_degree <= settings.airspeed_temp_warn ? "\uf2c7" : "\uf2c9") : "\uf2cb"
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            font.family: "Font Awesome 5 Free"
            horizontalAlignment: Text.AlignLeft
            font.pixelSize: 16
            verticalAlignment: Text.AlignTop
            wrapMode: Text.NoWrap
            elide: Text.ElideRight
            style: Text.Outline
            styleColor: settings.color_glow
        }

        Text {
            id: airspeed_temp
            color: {
                if (_fcMavlinkSystem.airspeed_sensor_temperature_degree <= settings.airspeed_temp_warn) {
                    widgetInner.visible = true
                    return settings.color_warn
                } else if (_fcMavlinkSystem.airspeed_sensor_temperature_degree < settings.airspeed_temp_caution) {
                    widgetInner.visible = true
                    return settings.color_caution
                } else if (settings.airspeed_temp_declutter == true
                           && _fcMavlinkSystem.armed == true) {
                    widgetInner.visible = false
                    return settings.color_text
                } else {
                    widgetInner.visible = true
                    return settings.color_text
                }
            }
            opacity: bw_current_opacity
            text: _fcMavlinkSystem.airspeed_sensor_temperature_degree == 99 ? qsTr("N/A") : _fcMavlinkSystem.airspeed_sensor_temperature_degree + "°"
            anchors.left: temp_glyph.right
            anchors.leftMargin: 2
            anchors.bottom: parent.bottom
            horizontalAlignment: Text.AlignRight
            font.pixelSize: 14
            font.family: settings.font_text
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.NoWrap
            elide: Text.ElideRight
            style: Text.Outline
            styleColor: settings.color_glow
        }
    }
}

