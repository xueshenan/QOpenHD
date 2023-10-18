import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Layouts 1.12
import QtQuick.Dialogs 1.0
import QtQuick.Controls.Material 2.12

import Qt.labs.settings 1.0

import OpenHD 1.0

import "../../ui" as Ui
import "../elements"

ScrollView {
    id: appVideoSettingsView
    width: parent.width
    height: parent.height
    contentHeight: videoColumn.height

    clip: true
    visible: appSettingsBar.currentIndex == 4

    Item {
        anchors.fill: parent

        Column {
            id: videoColumn
            spacing: 0
            anchors.left: parent.left
            anchors.right: parent.right

            ListModel {
                id: itemsVideoCodec
                ListElement { text: "H264"; }
                ListElement { text: "H265";  }
            }

            SettingBaseElement{
                m_short_description: "Video codec"
                m_long_description: "Video codec of stream."
                ComboBox {
                    id: selectVideoCodecPrimary
                    width: 320
                    height: elementHeight
                    anchors.right: parent.right
                    anchors.rightMargin: Qt.inputMethod.visible ? 96 : 36
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.horizontalCenter: parent.horizonatalCenter
                    model: itemsVideoCodec
                    Component.onCompleted: {
                        // out of bounds checking
                        if(settings.qopenhd_primary_video_codec>2 || settings.qopenhd_primary_video_codec<0){
                            settings.qopenhd_primary_video_codec=0;
                        }
                        currentIndex = settings.qopenhd_primary_video_codec;
                    }
                    onCurrentIndexChanged:{
                        console.debug("VideoCodec:"+itemsVideoCodec.get(currentIndex).text + ", "+currentIndex)
                        settings.qopenhd_primary_video_codec=currentIndex;
                    }
                }
            }
            SettingBaseElement{
                m_short_description: "Use software decoder"
                m_long_description: "Force software decode for video stream. Can fix bug(s) in rare hardware incompability cases."
                Switch {
                    width: 32
                    height: elementHeight
                    anchors.rightMargin: Qt.inputMethod.visible ? 96 : 36

                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    checked: settings.qopenhd_primary_video_force_sw
                    onCheckedChanged: settings.qopenhd_primary_video_force_sw = checked
                }
            }
            SettingBaseElement{
                m_short_description: "Video port"
                m_long_description: "Video port for video stream data"
                SpinBox {
                    height: elementHeight
                    width: 210
                    font.pixelSize: 14
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    from: 1
                    to: 6900
                    stepSize: 1
                    editable: true
                    anchors.rightMargin: Qt.inputMethod.visible ? 78 : 18
                    value: settings.qopenhd_primary_video_rtp_input_port
                    onValueChanged: settings.qopenhd_primary_video_rtp_input_port = value
                }
            }

            SettingBaseElement{
                m_short_description: "Scale video to fit"
                m_long_description: "Fit the video to the exact screen size (discards actual video aspect ratio,aka video is a bit distorted). Not supported on all platforms / implementations. Might require a restart."

                Switch {
                    width: 32
                    height: elementHeight
                    anchors.rightMargin: Qt.inputMethod.visible ? 96 : 36

                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    checked: settings.primary_video_scale_to_fit
                    onCheckedChanged: settings.primary_video_scale_to_fit = checked
                }
            }

            Rectangle {
                width: parent.width
                height: rowHeight
                color: (Positioner.index % 2 == 0) ? "#8cbfd7f3" : "#00000000"

                Text {
                    text: qsTr("dev_draw_alternating_rgb_dummy_frames")
                    font.weight: Font.Bold
                    font.pixelSize: 13
                    anchors.leftMargin: 8
                    verticalAlignment: Text.AlignVCenter
                    anchors.verticalCenter: parent.verticalCenter
                    width: 224
                    height: elementHeight
                    anchors.left: parent.left
                }
                Switch {
                    width: 32
                    height: elementHeight
                    anchors.rightMargin: Qt.inputMethod.visible ? 96 : 36
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    checked: settings.dev_draw_alternating_rgb_dummy_frames
                    onCheckedChanged: settings.dev_draw_alternating_rgb_dummy_frames = checked
                }
            }

            SettingBaseElement{
                m_short_description: "dev_always_use_generic_external_decode_service"
                //m_long_description: "Video decode is not done via QOpenHD, but rather in an extra service (started and stopped by QOpenHD). For platforms other than rpi"
                Switch {
                    width: 32
                    height: elementHeight
                    anchors.rightMargin: Qt.inputMethod.visible ? 96 : 36
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    checked: settings.dev_always_use_generic_external_decode_service
                    onCheckedChanged: settings.dev_always_use_generic_external_decode_service = checked
                }
            }
        }
    }
}
