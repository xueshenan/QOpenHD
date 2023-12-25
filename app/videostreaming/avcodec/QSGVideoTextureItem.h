// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#ifndef QSGVideoTextureItem_H
#define QSGVideoTextureItem_H

#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>

#include "texturerenderer.h"

#include "avcodec_decoder.h"
#ifdef ENABLE_MPP_DECODER
#include "mpp_decoder.h"
#endif

// QSG stands for QT Screne Graph (an abbreviation they recommend)
// Hoock into the QT Scene graph and draw video directly with (custom) OpenGL.
// This is the only way to skip the the "intermediate" rgba texture as
// required (for examle) qmlglsink. See the qt "Squircle" documentation
class QSGVideoTextureItem : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT
public:
    QSGVideoTextureItem();

public slots:
    void sync();

private slots:
    void handleWindowChanged(QQuickWindow *win);

private:
    void releaseResources() override;

    TextureRenderer* _renderer = nullptr;
public slots:
    void QQuickWindow_beforeRendering();
    void QQuickWindow_beforeRenderPassRecording();
private:
#ifdef ENABLE_MPP_DECODER
    std::unique_ptr<MppDecoder> _hw_decoder = nullptr;
#endif
    std::unique_ptr<AVCodecDecoder> _sw_decoder = nullptr;
    bool _use_sw_decoder = false;
};

#endif // QSGVideoTextureItem_H
