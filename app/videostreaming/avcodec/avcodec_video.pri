INCLUDEPATH += $$PWD

LIBS += -lavcodec -lavutil -lavformat
# TODO dirty
LIBS += -lGLESv2 -lEGL

# just using the something something webrtc from stephen was the easiest solution.
#include(../../lib/h264/h264.pri)

SOURCES += \
    $$PWD/QSGVideoTextureItem.cpp \
    $$PWD/gl/gl_shaders.cpp \
    $$PWD/gl/gl_videorenderer.cpp \
    $$PWD/mpp_decoder.cpp \
    $$PWD/texturerenderer.cpp \
    $$PWD/avcodec_decoder.cpp \

HEADERS += \
    $$PWD/QSGVideoTextureItem.h \    $$PWD/gl/gl_shaders.h \
    $$PWD/gl/gl_videorenderer.h \
    $$PWD/mpp_decoder.h \
    $$PWD/texturerenderer.h \
    $$PWD/avcodec_decoder.h \


# dirty way to check if we are on rpi and therefore should use the external decode service
CONFIG += link_pkgconfig
packagesExist(mmal) {
   DEFINES += IS_PLATFORM_RPI
}

# can be used in c++, also set to be exposed in qml
DEFINES += QOPENHD_ENABLE_VIDEO_VIA_AVCODEC

LinuxBuild {
    contains(QMAKE_HOST.arch, aarch64) {
        message("compile for linux arm64 with mpp support")
        DEFINES += ENABLE_MPP_DECODER
    }
}
