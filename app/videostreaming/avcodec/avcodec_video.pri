INCLUDEPATH += $$PWD

LIBS += -lavcodec -lavutil -lavformat
# TODO dirty
LIBS += -lGLESv2 -lEGL

SOURCES += \
    $$PWD/QSGVideoTextureItem.cpp \
    $$PWD/gl/gl_shaders.cpp \
    $$PWD/gl/gl_videorenderer.cpp \
    $$PWD/texturerenderer.cpp \
    $$PWD/avcodec_decoder.cpp \

HEADERS += \
    $$PWD/QSGVideoTextureItem.h \
    $$PWD/gl/gl_shaders.h \
    $$PWD/gl/gl_videorenderer.h \
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

        SOURCES += \
            $$PWD/mpp_decoder.cpp \
            $$PWD/mpplib/utils/mpi_dec_utils.c \
            $$PWD/mpplib/utils/mpp_opt.c \
            $$PWD/mpplib/utils/utils.c \

        HEADERS += \
            $$PWD/mpp_decoder.h \
            $$PWD/mpplib/base/inc/mpp_trie.h \
            $$PWD/mpplib/osal/inc/mpp_allocator.h \
            $$PWD/mpplib/osal/inc/mpp_callback.h \
            $$PWD/mpplib/osal/inc/mpp_common.h \
            $$PWD/mpplib/osal/inc/mpp_compat_impl.h \
            $$PWD/mpplib/osal/inc/mpp_debug.h \
            $$PWD/mpplib/osal/inc/mpp_dev_defs.h \
            $$PWD/mpplib/osal/inc/mpp_device.h \
            $$PWD/mpplib/osal/inc/mpp_env.h \
            $$PWD/mpplib/osal/inc/mpp_eventfd.h \
            $$PWD/mpplib/osal/inc/mpp_hash.h \
            $$PWD/mpplib/osal/inc/mpp_list.h \
            $$PWD/mpplib/osal/inc/mpp_lock.h \
            $$PWD/mpplib/osal/inc/mpp_mem.h \
            $$PWD/mpplib/osal/inc/mpp_mem_pool.h \
            $$PWD/mpplib/osal/inc/mpp_platform.h \
            $$PWD/mpplib/osal/inc/mpp_queue.h \
            $$PWD/mpplib/osal/inc/mpp_runtime.h \
            $$PWD/mpplib/osal/inc/mpp_server.h \
            $$PWD/mpplib/osal/inc/mpp_service.h \
            $$PWD/mpplib/osal/inc/mpp_service_api.h \
            $$PWD/mpplib/osal/inc/mpp_soc.h \
            $$PWD/mpplib/osal/inc/mpp_thread.h \
            $$PWD/mpplib/osal/inc/mpp_time.h \
            $$PWD/mpplib/osal/inc/mpp_trace.h \
            $$PWD/mpplib/osal/inc/osal_2str.h \
            $$PWD/mpplib/osal/inc/vcodec_service.h \
            $$PWD/mpplib/osal/inc/vcodec_service_api.h \
            $$PWD/mpplib/utils/mpi_dec_utils.h \
            $$PWD/mpplib/utils/mpp_opt.h \
            $$PWD/mpplib/utils/utils.h \

#        INCLUDEPATH += /usr/include/rockchip/
#        LIBS += -lrockchip_mpp

        INCLUDEPATH += /usr/local/include/rockchip/
        LIBS += -L/usr/local/lib/ -lrockchip_mpp

        INCLUDEPATH += $$PWD/mpplib/osal/inc/
        INCLUDEPATH += $$PWD/mpplib/base/inc/
        LIBS += -L$$PWD/mpplib/osal/ -losal
        LIBS += -L$$PWD/mpplib/base/ -lmpp_base
    }
}
