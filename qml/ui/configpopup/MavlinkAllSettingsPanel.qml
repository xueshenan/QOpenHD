import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Layouts 1.12
import QtQuick.Dialogs 1.0
import QtQuick.Controls.Material 2.12

import Qt.labs.settings 1.0

import OpenHD 1.0

import "../../ui" as Ui
import "../elements"

// Parent panel for OpenHD Mavlink air and ground settings (!!!! NOT QOPENHD SETTINGS !!!)
Rectangle {
    width: parent.width
    height: parent.height

    property int rowHeight: 64
    property int elementHeight: 48
    property int elementComboBoxWidth: 300

    //color: "green"
    color: "transparent"

    // Tab bar for selecting items in stack layout
    TabBar {
          id: selectItemInStackLayoutBar
          width: parent.width
          TabButton {
              text: qsTr("WB Link")
          }
          TabButton {
              text: qsTr("Air Camera")
          }
          TabButton {
              text: qsTr("Air")
          }
          TabButton {
              text: qsTr("Ground")
          }
    }

    // placed right below the top bar
    StackLayout {
          width: parent.width
          height: parent.height-selectItemInStackLayoutBar.height
          anchors.top: selectItemInStackLayoutBar.bottom
          anchors.left: selectItemInStackLayoutBar.left
          anchors.bottom: parent.bottom
          currentIndex: selectItemInStackLayoutBar.currentIndex

          MavlinkExtraWBParamPanel{
              id: xX_WBLinkSettings
          }
          MavlinkParamPanel{
              id: x1_AirCameraSettingsPanel
              m_name: "Camera"
              m_instanceMavlinkSettingsModel: _airCameraSettingsModel
              m_instanceCheckIsAvlie: _ohdSystemAir
          }
          MavlinkParamPanel{
              id: x2_AirSettingsPanel
              m_name: "Air"
              m_instanceMavlinkSettingsModel: _airPiSettingsModel
              m_instanceCheckIsAvlie: _ohdSystemAir
          }
          MavlinkParamPanel{
              id: x3_GroundSettingsPanel
              m_name: "Ground"
              m_instanceMavlinkSettingsModel: _groundPiSettingsModel
              m_instanceCheckIsAvlie: _ohdSystemGround
          }
      }

}
