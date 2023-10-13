#include "mpp_decoder.h"

#include <iostream>
#include <sstream>
#include <QFileInfo>

#include "qdebug.h"
#include "avcodec_helper.hpp"
#include "../common/TimeHelper.hpp"
#include "common/util_fs.h"

#include "texturerenderer.h"
#include "decodingstatistcs.h"
#include "common/SchedulingHelper.hpp"
#include "../util/WorkaroundMessageBox.h"
#include "../logging/hudlogmessagesmodel.h"
#include "../logging/logmessagesmodel.h"

#include "ExternalDecodeService.hpp"

static enum AVPixelFormat wanted_hw_pix_fmt = AV_PIX_FMT_NONE;
static enum AVPixelFormat get_hw_format(AVCodecContext */*ctx*/,const enum AVPixelFormat *pix_fmts){
    const enum AVPixelFormat *p;
    AVPixelFormat ret=AV_PIX_FMT_NONE;
    std::stringstream supported_formats;
    for (p = pix_fmts; *p != -1; p++) {
        const int tmp=(int)*p;
        supported_formats<<safe_av_get_pix_fmt_name(*p)<<"("<<tmp<<"),";
        if (*p == wanted_hw_pix_fmt){
          // matches what we want
          ret=*p;
        }
    }
    qDebug()<<"Supported (HW) pixel formats: "<<supported_formats.str().c_str();
    if(ret==AV_PIX_FMT_NONE){
      fprintf(stderr, "Failed to get HW surface format. Wanted: %s\n", av_get_pix_fmt_name(wanted_hw_pix_fmt));
    }
    return ret;
}

static constexpr auto MAX_FED_TIMESTAMPS_QUEUE_SIZE = 100;

MppDecoder::MppDecoder(QObject *parent) : QObject(parent)
{
}

MppDecoder::~MppDecoder()
{
    terminate();
}

void MppDecoder::init(bool /*primaryStream*/)
{
    qDebug() << "MppDecoder::init()";
    _last_video_settings = QOpenHDVideoHelper::read_config_from_settings();
    _decode_thread = std::make_unique<std::thread>([this]{
        this->constant_decode();
    });

    timer_check_settings_changed = std::make_unique<QTimer>();
    QObject::connect(timer_check_settings_changed.get(), &QTimer::timeout, this, &MppDecoder::timer_check_settings_changed_callback);
    timer_check_settings_changed->start(1000);
}

void MppDecoder::terminate()
{
    // Stop the timer, which can be done (almost) immediately (it's runnable doesn't block)
    timer_check_settings_changed->stop();
    timer_check_settings_changed = nullptr;
    // This will stop the constant_decode as soon as the current running decode_until_error loop returns
    _should_terminate = true;
    // This will break out of a running "decode until error" loop if there is one currently running
    request_restart = true;
    if (_decode_thread) {
        // Wait for everything to cleanup and stop
        _decode_thread->join();
    }
}

void MppDecoder::timer_check_settings_changed_callback()
{
    const auto new_settings = QOpenHDVideoHelper::read_config_from_settings();
    if (_last_video_settings != new_settings) {
        // We just request a restart from the video (break out of the current constant_decode() loop,
        // and restart with the new settings.
        request_restart = true;
        _last_video_settings = new_settings;
    }
}

void MppDecoder::constant_decode()
{
    while (!_should_terminate) {
        qDebug()<<"Start decode";
        const auto settings = QOpenHDVideoHelper::read_config_from_settings();
        // this is always for primary video, unless switching is enabled
        auto stream_config = settings.primary_stream_config;

        // Does h264 and h265 custom rtp parse, but uses avcodec for decode
        open_and_decode_until_error_custom_rtp(settings);

        qDebug()<<"Decode stopped,restarting";
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}


int MppDecoder::decode_and_wait_for_frame(AVPacket *packet,std::optional<std::chrono::steady_clock::time_point> parse_time)
{
    AVFrame *frame = nullptr;
    //qDebug()<<"Decode packet:"<<packet->pos<<" size:"<<packet->size<<" B";
    const auto beforeFeedFrame = std::chrono::steady_clock::now();
    if (parse_time != std::nullopt) {
        const auto delay = beforeFeedFrame-parse_time.value();
        avg_parse_time.add(delay);
        avg_parse_time.custom_print_in_intervals(std::chrono::seconds(3),[](const std::string /*name*/, const std::string message) {
            //qDebug()<<name.c_str()<<":"<<message.c_str();
            DecodingStatistcs::instance().set_parse_and_enqueue_time(message.c_str());
        });
    }
    const auto beforeFeedFrameUs = getTimeUs();
    packet->pts = beforeFeedFrameUs;
    add_feed_timestamp(packet->pts);

    const int ret_avcodec_send_packet = avcodec_send_packet(_decoder_ctx, packet);

    if (ret_avcodec_send_packet < 0) {
        fprintf(stderr, "Error during decoding\n");
        return ret_avcodec_send_packet;
    }
    // alloc output frame(s)
    if (!(frame = av_frame_alloc())) {
        // NOTE: It is a common practice to not care about OOM, and this is the best approach in my opinion.
        // but ffmpeg uses malloc and returns error codes, so we keep this practice here.
        qDebug()<<"can not alloc frame";
        av_frame_free(&frame);
        return AVERROR(ENOMEM);
    }
    int ret = 0;
    // Poll until we get the frame out
    bool gotFrame = false;
    int n_times_we_tried_getting_a_frame_this_time=0;
    while (!gotFrame) {
        ret = avcodec_receive_frame(_decoder_ctx, frame);
        if (ret == AVERROR_EOF) {
            qDebug()<<"Got EOF";
            break;
        } else if (ret == 0) {
            //debug_is_valid_timestamp(frame->pts);
            // we got a new frame
            if (!use_frame_timestamps_for_latency) {
                const auto x_delay=std::chrono::steady_clock::now() - beforeFeedFrame;
                //qDebug()<<"(True) decode delay(wait):"<<((float)std::chrono::duration_cast<std::chrono::microseconds>(x_delay).count()/1000.0f)<<" ms";
                avg_decode_time.add(x_delay);
            } else {
                const auto now_us=getTimeUs();
                const auto delay_us=now_us-frame->pts;
                //qDebug()<<"(True) decode delay(nowait):"<<((float)delay_us/1000.0f)<<" ms";
                //MLOGD<<"Frame pts:"<<frame->pts<<" Set to:"<<now<<"\n";
                //frame->pts=now;
                avg_decode_time.add(std::chrono::microseconds(delay_us));
            }
            gotFrame=true;
            frame->pts=beforeFeedFrameUs;
            // display frame
            on_new_frame(frame);
            avg_decode_time.custom_print_in_intervals(std::chrono::seconds(3),[](const std::string /*name*/,const std::string message){
                //qDebug()<<name.c_str()<<":"<<message.c_str();
                DecodingStatistcs::instance().set_decode_time(message.c_str());
            });
        } else if (ret == AVERROR(EAGAIN)) {
            break;
        } else {
            qDebug()<<"Got unlikely / weird error:"<<ret;
            break;
        }
        n_times_we_tried_getting_a_frame_this_time++;
    }
    av_frame_free(&frame);
    return 0;
}

int MppDecoder::decode_config_data(AVPacket *packet)
{
     const int ret_avcodec_send_packet = avcodec_send_packet(_decoder_ctx, packet);
     return ret_avcodec_send_packet;
}


bool MppDecoder::feed_rtp_frame_if_available()
{
    auto frame = _rtp_receiver->get_next_frame();
    if (frame) {
        {
            // parsing delay
            const auto delay = std::chrono::steady_clock::now()-frame->get_nal().creationTime;
            avg_parse_time.add(delay);
            avg_parse_time.custom_print_in_intervals(std::chrono::seconds(3),[](const std::string /*name*/,const std::string message) {
                //qDebug()<<name.c_str()<<":"<<message.c_str();
                DecodingStatistcs::instance().set_parse_and_enqueue_time(message.c_str());
            });
        }
        AVPacket *pkt = av_packet_alloc();
        pkt->data = (uint8_t*)frame->get_nal().getData();
        pkt->size = frame->get_nal().getSize();
        const auto beforeFeedFrameUs = getTimeUs();
        pkt->pts = beforeFeedFrameUs;
        add_feed_timestamp(pkt->pts);
        avcodec_send_packet(_decoder_ctx, pkt);
        av_packet_free(&pkt);
        return true;
    }
    return false;
}

void MppDecoder::on_new_frame(AVFrame *frame)
{
    {
        std::stringstream ss;
        ss<<safe_av_get_pix_fmt_name((AVPixelFormat)frame->format)<<" "<<frame->width<<"x"<<frame->height;
        DecodingStatistcs::instance().set_primary_stream_frame_format(QString(ss.str().c_str()));
        //qDebug()<<"Got frame:"<<ss.str().c_str();
    }

    // Once we got the first frame, reduce the log level
    av_log_set_level(AV_LOG_WARNING);

    //qDebug()<<debug_frame(frame).c_str();
    TextureRenderer::instance().queue_new_frame_for_display(frame);
    if (_last_frame_width == -1 || _last_frame_height == -1) {
        _last_frame_width = frame->width;
        _last_frame_height = frame->height;
    } else {
        if (_last_frame_width != frame->width || _last_frame_height != frame->height) {
            // PI and SW decoer will just slently start outputting garbage frames
            // if the width/ height changes during RTP streaming
            qDebug()<<"Need to restart the decoder, width / heght changed";
            request_restart=true;
        }
    }
}

void MppDecoder::reset_before_decode_start()
{
    avg_decode_time.reset();
    avg_parse_time.reset();
    DecodingStatistcs::instance().reset_all_to_default();
    _last_frame_width = -1;
    _last_frame_height = -1;
    _fed_timestamps_queue.clear();
}

// https://ffmpeg.org/doxygen/3.3/decode_video_8c-example.html
void MppDecoder::open_and_decode_until_error_custom_rtp(const QOpenHDVideoHelper::VideoStreamConfig &settings)
{
    // this is always for primary video, unless switching is enabled
    auto stream_config = settings.primary_stream_config;

    // This thread pulls frame(s) from the rtp decoder and therefore should have high priority
    SchedulingHelper::setThreadParamsMaxRealtime();
    av_log_set_level(AV_LOG_TRACE);
    assert(stream_config.video_codec == QOpenHDVideoHelper::VideoCodecH264 || stream_config.video_codec == QOpenHDVideoHelper::VideoCodecH265);
    if (stream_config.video_codec == QOpenHDVideoHelper::VideoCodecH264) {
        _decoder = avcodec_find_decoder_by_name("h264_rkmpp");
        if (_decoder == NULL) {
            _decoder = avcodec_find_decoder(AV_CODEC_ID_H264);
        }
    } else if (stream_config.video_codec == QOpenHDVideoHelper::VideoCodecH265) {
        _decoder = avcodec_find_decoder(AV_CODEC_ID_H265);
    }
    if (_decoder == NULL) {
        qDebug()<< "MppDecoder::open_and_decode_until_error_custom_rtp: Codec not found";
        return;
    }
    // ----------------------
    if (_decoder->id == AV_CODEC_ID_H264) {
        qDebug()<<"H264 decode";
        wanted_hw_pix_fmt = AV_PIX_FMT_YUV420P;
    } else if (_decoder->id == AV_CODEC_ID_H265) {
        qDebug()<<"H265 decode";
    }

    // ------------------------------------
    _decoder_ctx = avcodec_alloc_context3(_decoder);
    if (_decoder_ctx == NULL) {
        qDebug()<< "MppDecoder::open_and_decode_until_error_custom_rtp: Could not allocate video codec context";
        return;
    }
     // ----------------------------------
    // From moonlight-qt. However, on PI, this doesn't seem to make any difference, at least for H265 decode.
    // (I never measured h264, but don't think there it is different).
    // Always request low delay decoding
    _decoder_ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
    // Allow display of corrupt frames and frames missing references
    _decoder_ctx->flags |= AV_CODEC_FLAG_OUTPUT_CORRUPT;
    _decoder_ctx->flags2 |= AV_CODEC_FLAG2_SHOW_ALL;
    // --------------------------------------
    // --------------------------------------
    std::string selected_decoding_type="HW";
    _decoder_ctx->get_format  = get_hw_format;

    // A thread count of 1 reduces latency for both SW and HW decode
    _decoder_ctx->thread_count = 1;

    // ---------------------------------------
    if (avcodec_open2(_decoder_ctx, _decoder, NULL) < 0) {
     fprintf(stderr, "Could not open codec\n");
     avcodec_free_context(&_decoder_ctx);
     return;
    }

    qDebug()<<"MppDecoder::open_and_decode_until_error_custom_rtp()-begin loop";
    _rtp_receiver=std::make_unique<RTPReceiver>(stream_config.udp_rtp_input_port,
                                              stream_config.udp_rtp_input_ip_address,
                                              stream_config.video_codec==1,
                                              settings.generic.dev_feed_incomplete_frames_to_decoder);

     reset_before_decode_start();
     DecodingStatistcs::instance().set_decoding_type(selected_decoding_type.c_str());
     AVPacket *pkt=av_packet_alloc();
     assert(pkt != NULL);
     bool has_keyframe_data = false;
     while (true)
     {
         // We break out of this loop if someone requested a restart
         if (request_restart) {
             request_restart = false;
             goto finish;
         }
         // or the decode config changed and we need a restart
         if (_rtp_receiver->config_has_changed_during_decode) {
             qDebug()<<"Break/Restart,config has changed during decode";
             goto finish;
         }
         //std::this_thread::sleep_for(std::chrono::milliseconds(3000));
         if (!has_keyframe_data) {
              std::shared_ptr<std::vector<uint8_t>> keyframe_buf=_rtp_receiver->get_config_data();
              if (keyframe_buf == nullptr) {
                  std::this_thread::sleep_for(std::chrono::milliseconds(100));
                  continue;
              }
              qDebug()<<"Got decode data (before keyframe)";
              pkt->data = keyframe_buf->data();
              pkt->size = keyframe_buf->size();
              decode_config_data(pkt);
              has_keyframe_data=true;
              continue;
         } else {
             auto buf = _rtp_receiver->get_next_frame(std::chrono::milliseconds(kDefaultFrameTimeout));
             if (buf == nullptr) {
                 // No buff after X seconds
                 continue;
             }
             //qDebug()<<"Got decode data (after keyframe)";
             pkt->data = (uint8_t*)buf->get_nal().getData();
             pkt->size = buf->get_nal().getSize();
             decode_and_wait_for_frame(pkt,buf->get_nal().creationTime);
         }
     }
finish:
     qDebug()<<"MppDecoder::open_and_decode_until_error_custom_rtp()-end loop";
     _rtp_receiver = nullptr;
     avcodec_free_context(&_decoder_ctx);
}

void MppDecoder::add_feed_timestamp(int64_t ts)
{
    _fed_timestamps_queue.push_back(ts);
    if (_fed_timestamps_queue.size() >= MAX_FED_TIMESTAMPS_QUEUE_SIZE) {
        _fed_timestamps_queue.pop_front();
    }
}
