#ifndef TEXTURERENDERER_H
#define TEXTURERENDERER_H

#include <memory>
#include <chrono>
#include <mutex>
#include <QObject>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>

#include "gl/gl_videorenderer.h"
#include "common/TimeHelper.hpp"

class TextureRenderer : public QObject
{
    Q_OBJECT
public:
    // DIRTY, FIXME
    static TextureRenderer& instance();

    void setViewportSize(const QSize &size) { _viewport_size = size; }
    // create and link the shaders
    void initGL(QQuickWindow *window);
    // draw function
    // @param window: just needed to call the begin/end-externalCommands on it
    void paint(QQuickWindow *window,int rotation_degree);
    // adds a new frame to be picked up by the GL thread
    int queue_new_frame_for_display(AVFrame * src_frame);
    // remoe the currently queued frame if there is one (be carefull to not forget that the
    // GL thread can pick up a queued frame at any time).
    void remove_queued_frame_if_avalable();
    // If we switch from a decode method that requires OpenGL to a decode method
    // that uses the HW composer, we need to become "transparent" again - or rather
    // not draw any video with OpenGL, which will have the same effect
    void clear_all_video_textures_next_frame(){
        _clear_all_video_textures_next_frame = true;
    }
private:
    QSize _viewport_size;
    int _index = 0;
    // last frame draw time
    std::chrono::steady_clock::time_point _last_frame = std::chrono::steady_clock::now();
    //
    std::unique_ptr<GL_VideoRenderer> _gl_video_renderer = nullptr;
    //
    bool _initialized = false;
    int _render_count = 0;
private:
    std::mutex _latest_frame_mutex;
    AVFrame* _latest_frame = nullptr;
    AVFrame* fetch_latest_decoded_frame();
private:
    struct DisplayStats{
        int n_frames_rendered = 0;
        int n_frames_dropped = 0;
        // Delay between frame was given to the egl render <-> we uploaded it to the texture (if not dropped)
        //AvgCalculator delay_until_uploaded{"Delay until uploaded"};
        // Delay between frame was given to the egl renderer <-> swap operation returned (it is handed over to the hw composer)
        //AvgCalculator delay_until_swapped{"Delay until swapped"};
        AvgCalculator decode_and_render{"Decode and render"}; //Time picked up by GL Thread
      };
    DisplayStats _display_stats;
    bool _dev_draw_alternating_rgb_dummy_frames = false;
    bool _clear_all_video_textures_next_frame = false;
};

#endif // TEXTURERENDERER_H
