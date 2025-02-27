#include "texturerenderer.h"

#include <cmath>
#include <algorithm>
#include <QSettings>

#include "videostreaming/vscommon/QOpenHDVideoHelper.hpp"
#include "videostreaming/vscommon/video_ratio_helper.hpp"

#include "avcodec_helper.hpp"
#include "decodingstatistcs.h"

static bool get_dev_draw_alternating_rgb_dummy_frames() {
    QSettings settings;
    return settings.value("dev_draw_alternating_rgb_dummy_frames", false).toBool();
}


TextureRenderer &TextureRenderer::instance() {
    static TextureRenderer renderer{};
    return renderer;
}

void TextureRenderer::initGL(QQuickWindow *window)
{
    if (!_initialized) {
        _initialized = true;
        QSGRendererInterface *rif = window->rendererInterface();
        if (rif->graphicsApi() == QSGRendererInterface::Software) {
            //runing Qt with param -platform linuxfb with render as software and cannot display gl render video
            qDebug() << "graphics render as software";
        }
        else if (rif->graphicsApi() == QSGRendererInterface::OpenGL) {
            qDebug() << "graphics render as opengl";
        }

        _gl_video_renderer=std::make_unique<GL_VideoRenderer>();
        qDebug()<<_gl_video_renderer->debug_info().c_str();
        _gl_video_renderer->init_gl();
        _dev_draw_alternating_rgb_dummy_frames = get_dev_draw_alternating_rgb_dummy_frames();
    }
}

void TextureRenderer::paint(QQuickWindow *window, int rotation_degree)
{
    const auto delta = std::chrono::steady_clock::now() - _last_frame;
    _last_frame = std::chrono::steady_clock::now();
    const auto frame_time_us = std::chrono::duration_cast<std::chrono::microseconds>(delta).count();
    const float frame_time_ms = ((float)frame_time_us)/1000.0f;
    Q_UNUSED(frame_time_ms)
//    qDebug()<<" TextureRenderer::paint() frame time:"<<frame_time_ms<<"ms";
    _render_count++;

    if (_clear_all_video_textures_next_frame) {
        remove_queued_frame_if_avalable();
        _gl_video_renderer->clean_video_textures_gl();
        DecodingStatistcs::instance().set_n_renderer_dropped_frames(0);
        DecodingStatistcs::instance().set_n_rendered_frames(-1);
        DecodingStatistcs::instance().set_decode_and_render_time("-1");
        _clear_all_video_textures_next_frame = false;
    }

    // Play nice with the RHI. Not strictly needed when the scenegraph uses
    // OpenGL directly.
    // Consti10: comp error, seems to work without, too
    if (window != nullptr) {
        window->beginExternalCommands();
    }

    AVFrame* new_frame = fetch_latest_decoded_frame();
    if (new_frame != nullptr) {
        // Note : the update might free the frame, so we gotta store the timestamp before !
        const auto frame_pts = new_frame->pts;
        // update the texture with this frame
        _gl_video_renderer->update_texture_gl(new_frame);
        av_frame_free(&new_frame);
        _display_stats.n_frames_rendered++;
        DecodingStatistcs::instance().set_n_rendered_frames(_display_stats.n_frames_rendered);

        const auto now_us = getTimeUs();
        const auto delay_us = now_us - frame_pts;
        _display_stats.decode_and_render.add(std::chrono::microseconds(delay_us));
        if (_display_stats.decode_and_render.time_since_last_log() > std::chrono::seconds(3)) {
            DecodingStatistcs::instance().set_decode_and_render_time(_display_stats.decode_and_render.getAvgReadable().c_str());
            _display_stats.decode_and_render.set_last_log();
            _display_stats.decode_and_render.reset();
        }
    }

    auto video_tex_width = _gl_video_renderer->get_current_video_width();
    auto video_tex_height = _gl_video_renderer->get_current_video_height();
    if (rotation_degree == 90 || rotation_degree == 270) {
        // just swap them around when rotated to get the right viewport
        std::swap(video_tex_width, video_tex_height);
    }
    if (video_tex_width > 0 && video_tex_height > 0) {
       const auto viewport = helper::ratio::calculate_viewport(_viewport_size.width(), _viewport_size.height(),video_tex_width,video_tex_height,QOpenHDVideoHelper::get_primary_video_scale_to_fit());
       glViewport(viewport.x,viewport.y,viewport.width,viewport.height);
    }
    glDisable(GL_DEPTH_TEST);

    _gl_video_renderer->draw_texture_gl(_dev_draw_alternating_rgb_dummy_frames, rotation_degree);

    // make sure we leave how we started / such that Qt rendering works normally
    glEnable(GL_DEPTH_TEST);
    glViewport(0, 0, _viewport_size.width(), _viewport_size.height());

    if (window != nullptr) {
        window->endExternalCommands();
    }
}

int TextureRenderer::queue_new_frame_for_display(AVFrame *src_frame)
{
    assert(src_frame);
    //std::cout<<"DRMPrimeOut::drmprime_out_display "<<src_frame->width<<"x"<<src_frame->height<<"\n";
    if ((src_frame->flags & AV_FRAME_FLAG_CORRUPT) != 0) {
      //fprintf(stderr, "Discard corrupt frame: fmt=%d, ts=%" PRId64 "\n", src_frame->format, src_frame->pts);
      qDebug()<<"Frame corrupt, but forwarding anyways";
      //return 0;
    }

    std::lock_guard<std::mutex> lock(_latest_frame_mutex);
    // We drop a frame that has (not yet) been consumed by the render thread to whatever is the newest available.
    if (_latest_frame != nullptr) {
      av_frame_free(&_latest_frame);
      _latest_frame = nullptr;
      // qDebug()<<"Dropping frame";
      _display_stats.n_frames_dropped++;
      DecodingStatistcs::instance().set_n_renderer_dropped_frames(_display_stats.n_frames_dropped);
    }
    AVFrame *frame = av_frame_alloc();
    assert(frame);
    if (av_frame_ref(frame, src_frame) != 0) {
      fprintf(stderr, "av_frame_ref error\n");
      av_frame_free(&frame);
      // don't forget to give up the lock
      return AVERROR(EINVAL);
    }
    _latest_frame = frame;
    return 0;
}

void TextureRenderer::remove_queued_frame_if_avalable()
{
    std::lock_guard<std::mutex> lock(_latest_frame_mutex);
    if (_latest_frame != nullptr) {
      av_frame_free(&_latest_frame);
      _latest_frame = nullptr;
    }
}


AVFrame *TextureRenderer::fetch_latest_decoded_frame()
{
    std::lock_guard<std::mutex> lock(_latest_frame_mutex);
    if (_latest_frame != nullptr) {
      // Make a copy and write nullptr to the thread-shared variable such that
      // it is not freed by the providing thread.
      AVFrame* new_frame = _latest_frame;
      _latest_frame = nullptr;
      return new_frame;
    }
    return nullptr;
}




