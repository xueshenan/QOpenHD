#!/bin/sh

export QT_QPA_EGLFS_KMS_ATOMIC=1
export QT_LOGGING_RULES=qt.qpa.egl*=true
export QT_QPA_EGLFS_INTEGRATION=eglfs_kms
export QT_QPA_EGLFS_KMS_PLANE_INDEX=0
#export QT_QPA_EGLFS_KMS_ZPOS=0
export QT_QPA_EGLFS_FORCE888=1
export QT_QPA_EGLFS_SWAPINTERVAL=0
export QT_QPA_EGLFS_DEBUG=1
export QT_QPA_EGLFS_KMS_CONFIG=/usr/local/share/qopenhd/rock_qt_eglfs_kms_config.json

./QOpenHD -platform eglfs
