#include "qrenderstats.h"

#include <qapplication.h>

QRenderStats::QRenderStats(QObject *parent)
    : QObject{parent}
{

}

QRenderStats &QRenderStats::instance()
{
    static QRenderStats instance{};
    return instance;
}

void QRenderStats::register_to_root_window(QQmlApplicationEngine& engine)
{
    auto rootObjects = engine.rootObjects();
    if (rootObjects.length() < 1) {
        qWarning(" QRenderStats::register_to_root_window failed,no root objects");
        return;
    }
     QQuickWindow* window = static_cast<QQuickWindow *>(rootObjects.first());
     registerOnWindow(window);
}

void QRenderStats::registerOnWindow(QQuickWindow *window)
{
    connect(window, &QQuickWindow::beforeRendering, this, &QRenderStats::m_QQuickWindow_beforeRendering, Qt::DirectConnection);
    connect(window, &QQuickWindow::afterRendering, this, &QRenderStats::m_QQuickWindow_afterRendering, Qt::DirectConnection);
    connect(window, &QQuickWindow::beforeRenderPassRecording, this, &QRenderStats::m_QQuickWindow_beforeRenderPassRecording, Qt::DirectConnection);
    connect(window, &QQuickWindow::afterRenderPassRecording, this, &QRenderStats::m_QQuickWindow_afterRenderPassRecording, Qt::DirectConnection);
}

void QRenderStats::set_display_width_height(int width, int height)
{
    std::stringstream ss;
    ss<<width<<"x"<<height;
    set_display_width_height_str(ss.str().c_str());
}

void QRenderStats::set_window_width_height(int width, int height)
{
    std::stringstream ss;
    ss<<width<<"x"<<height;
    set_window_width_height_str(ss.str().c_str());
}

void QRenderStats::m_QQuickWindow_beforeRendering()
{
}

void QRenderStats::m_QQuickWindow_afterRendering()
{
}

void QRenderStats::m_QQuickWindow_beforeRenderPassRecording()
{
    _avg_renderpass_time.start();

    // Calculate frame time by calculating the delta between calls to render pass recording
    const auto delta=std::chrono::steady_clock::now()-_last_frame;
    _last_frame=std::chrono::steady_clock::now();
    _avg_render_frame_delta.add(delta);
    _avg_render_frame_delta.recalculate_in_fixed_time_intervals(std::chrono::seconds(1),[this](const AvgCalculator& self){
        const auto main_stats=QString(self.getAvgReadable().c_str());
//        qDebug() << "QRenderStats render frame interval:" << main_stats;
        set_main_render_stats(main_stats);
    });
}

void QRenderStats::m_QQuickWindow_afterRenderPassRecording()
{
    _avg_renderpass_time.stop();
    _avg_renderpass_time.recalculate_in_fixed_time_intervals(std::chrono::seconds(1),[this](const AvgCalculator& self){
        const auto stats=QString(self.getAvgReadable().c_str());
        //qDebug() << "QRenderStats render pass time:" << main_stats;
        set_qt_renderpass_time(stats);
    });
}
