// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#include "QSGVideoTextureItem.h"

#include <QtQuick/qquickwindow.h>
#include <QtCore/QRunnable>

#include "../util/qrenderstats.h"


QSGVideoTextureItem::QSGVideoTextureItem():
    _renderer(nullptr)
{
    connect(this, &QQuickItem::windowChanged, this, &QSGVideoTextureItem::handleWindowChanged);
#ifdef ENABLE_MPP_DECODER
    _decoder = std::make_unique<MppDecoder>(nullptr);
#else
    _decoder = std::make_unique<AVCodecDecoder>(nullptr);
#endif
    _decoder->init(true);
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
        //X
        //QRenderStats::instance().registerOnWindow(window());
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
        const auto screen_rotation=QOpenHDVideoHelper::get_display_rotation();
        _renderer->paint(window(),screen_rotation);
    }
    // always trigger a repaint, otherwise QT "thinks" nothing has changed since it doesn't
    // know about the OpenGL commands we do here
    window()->update();
    //window()->requestUpdate();
}



