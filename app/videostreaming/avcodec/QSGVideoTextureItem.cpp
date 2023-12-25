// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#include "QSGVideoTextureItem.h"

#include <QtQuick/qquickwindow.h>
#include <QtCore/QRunnable>

#include "util/qrenderstats.h"
#include "logging/logmessagesmodel.h"

QSGVideoTextureItem::QSGVideoTextureItem():
    _renderer(nullptr)
{
    connect(this, &QQuickItem::windowChanged, this, &QSGVideoTextureItem::handleWindowChanged);
#ifdef ENABLE_MPP_DECODER
    const auto settings = QOpenHDVideoHelper::read_config_from_settings();
    // this is always for primary video, unless switching is enabled
    auto stream_config = settings.primary_stream_config;
    if (stream_config.enable_software_video_decoder) {
        _use_sw_decoder = true;
        LogMessagesModel::instanceOHD().addLogMessage("Video", "Use avcodec software decoder");
        _sw_decoder = std::make_unique<AVCodecDecoder>(nullptr);
        _sw_decoder->init(true);
    }
    else {
        _use_sw_decoder = false;
        LogMessagesModel::instanceOHD().addLogMessage("Video", "Use mpp hardware decoder");
        _hw_decoder = std::make_unique<MppDecoder>(nullptr);
        _hw_decoder->init(true);
    }
#else
    // force use sw decoder
    _use_sw_decoder = true;
    LogMessagesModel::instanceOHD().addLogMessage("Video", "Use avcodec software decoder");
    _sw_decoder = std::make_unique<AVCodecDecoder>(nullptr);
    _sw_decoder->init(true);
#endif
}


void QSGVideoTextureItem::handleWindowChanged(QQuickWindow *win)
{
    qDebug()<<"QSGVideoTextureItem::handleWindowChanged";
    if (win != nullptr) {
        connect(win, &QQuickWindow::beforeSynchronizing, this, &QSGVideoTextureItem::sync, Qt::DirectConnection);
        //connect(win, &QQuickWindow::sceneGraphInvalidated, this, &QSGVideoTextureItem::cleanup, Qt::DirectConnection);
        // Ensure we start with cleared to black. The squircle's blend mode relies on this.
        // We do not need that when rendering a texture, which is what we actually want (squircle is just the example where I started with,
        // since I had to start somehow ;)
        //win->setColor(Qt::black);
    }
}


void QSGVideoTextureItem::releaseResources()
{
     qDebug()<<"QSGVideoTextureItem::releaseResources";
}


void QSGVideoTextureItem::sync()
{
    if (_renderer == nullptr) {
        _renderer = &TextureRenderer::instance();
        connect(window(), &QQuickWindow::beforeRendering, this, &QSGVideoTextureItem::QQuickWindow_beforeRendering, Qt::DirectConnection);
        connect(window(), &QQuickWindow::beforeRenderPassRecording, this, &QSGVideoTextureItem::QQuickWindow_beforeRenderPassRecording, Qt::DirectConnection);
    }
    _renderer->setViewportSize(window()->size() * window()->devicePixelRatio());
}

void QSGVideoTextureItem::QQuickWindow_beforeRendering()
{
    if (_renderer != nullptr) {
        _renderer->initGL(window());
    }
}

void QSGVideoTextureItem::QQuickWindow_beforeRenderPassRecording()
{
    if (_renderer != nullptr) {
        //qDebug()<<"Rotation:"<<QQuickItem::rotation();
        _renderer->paint(window(), QOpenHDVideoHelper::get_display_rotation());
    }
    // always trigger a repaint, otherwise QT "thinks" nothing has changed since it doesn't
    // know about the OpenGL commands we do here
    window()->update();
    //window()->requestUpdate();
}



