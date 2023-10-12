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

static enum AVPixelFormat wanted_hw_pix_fmt;

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
    m_last_video_settings=QOpenHDVideoHelper::read_config_from_settings();
    decode_thread = std::make_unique<std::thread>([this]{this->constant_decode();} );
    timer_check_settings_changed=std::make_unique<QTimer>();
    QObject::connect(timer_check_settings_changed.get(), &QTimer::timeout, this, &MppDecoder::timer_check_settings_changed_callback);
    timer_check_settings_changed->start(1000);
}

void MppDecoder::terminate()
{
    // Stop the timer, which can be done (almost) immediately (it's runnable doesn't block)
    timer_check_settings_changed->stop();
    timer_check_settings_changed=nullptr;
    // This will stop the constant_decode as soon as the current running decode_until_error loop returns
    m_should_terminate=true;
    // This will break out of a running "decode until error" loop if there is one currently running
    request_restart=true;
    if(decode_thread){
        // Wait for everything to cleanup and stop
        decode_thread->join();
    }
}

void MppDecoder::timer_check_settings_changed_callback()
{
    const auto new_settings=QOpenHDVideoHelper::read_config_from_settings();
    if(m_last_video_settings!=new_settings){
        // We just request a restart from the video (break out of the current constant_decode() loop,
        // and restart with the new settings.
        request_restart=true;
        m_last_video_settings=new_settings;
    }
}

void MppDecoder::constant_decode()
{
    while(!m_should_terminate){
        qDebug()<<"Start decode";
        const auto settings = QOpenHDVideoHelper::read_config_from_settings();
        // this is always for primary video, unless switching is enabled
        auto stream_config=settings.primary_stream_config;

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
    const auto beforeFeedFrame=std::chrono::steady_clock::now();
    if(parse_time!=std::nullopt){
        const auto delay=beforeFeedFrame-parse_time.value();
        avg_parse_time.add(delay);
        avg_parse_time.custom_print_in_intervals(std::chrono::seconds(3),[](const std::string /*name*/, const std::string message){
            //qDebug()<<name.c_str()<<":"<<message.c_str();
            DecodingStatistcs::instance().set_parse_and_enqueue_time(message.c_str());
        });
    }
    const auto beforeFeedFrameUs=getTimeUs();
    packet->pts=beforeFeedFrameUs;
    timestamp_add_fed(packet->pts);

    //m_ffmpeg_dequeue_or_queue_mutex.lock();
    const int ret_avcodec_send_packet = avcodec_send_packet(decoder_ctx, packet);
    //m_ffmpeg_dequeue_or_queue_mutex.unlock();
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
    int ret=0;
    // Poll until we get the frame out
    const auto loopUntilFrameBegin=std::chrono::steady_clock::now();
    bool gotFrame=false;
    int n_times_we_tried_getting_a_frame_this_time=0;
    while (!gotFrame){
        //m_ffmpeg_dequeue_or_queue_mutex.lock();
        ret = avcodec_receive_frame(decoder_ctx, frame);
        //m_ffmpeg_dequeue_or_queue_mutex.unlock();
        if(ret == AVERROR_EOF){
            qDebug()<<"Got EOF";
            break;
        }else if(ret==0){
            //debug_is_valid_timestamp(frame->pts);
            // we got a new frame
            if(!use_frame_timestamps_for_latency){
                const auto x_delay=std::chrono::steady_clock::now()-beforeFeedFrame;
                //qDebug()<<"(True) decode delay(wait):"<<((float)std::chrono::duration_cast<std::chrono::microseconds>(x_delay).count()/1000.0f)<<" ms";
                avg_decode_time.add(x_delay);
            }else{
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
        }else if(ret==AVERROR(EAGAIN)){
            // TODO FIXME REMOVE
            if(true){
                break;
            }
            if(n_no_output_frame_after_x_seconds>=2){
                // note decode latency is now wrong
                //qDebug()<<"Skipping decode lockstep due to no frame for more than X seconds\n";
                DecodingStatistcs::instance().set_doing_wait_for_frame_decode("No");
                if(n_times_we_tried_getting_a_frame_this_time>4){
                    break;
                }
            }
            //std::cout<<"avcodec_receive_frame returned:"<<ret<<"\n";
            // for some video files, the decoder does not output a frame every time a h264 frame has been fed
            // In this case, I unblock after X seconds, but we cannot measure the decode delay by using the before-after
            // approach. We can still measure it using the pts timestamp from av, but this one cannot necessarily be trusted 100%
            if(std::chrono::steady_clock::now()-loopUntilFrameBegin > std::chrono::seconds(2)){
              qDebug()<<"Got no frame after X seconds. Break, but decode delay will be reported wrong";
              n_no_output_frame_after_x_seconds++;
              use_frame_timestamps_for_latency=true;
              break;
            }
            // sleep a bit to not hog the CPU too much
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }else{
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
     const int ret_avcodec_send_packet = avcodec_send_packet(decoder_ctx, packet);
     return ret_avcodec_send_packet;
}


bool MppDecoder::feed_rtp_frame_if_available()
{
    auto frame=m_rtp_receiver->get_next_frame();
    if (frame) {
        {
            // parsing delay
            const auto delay=std::chrono::steady_clock::now()-frame->get_nal().creationTime;
            avg_parse_time.add(delay);
            avg_parse_time.custom_print_in_intervals(std::chrono::seconds(3),[](const std::string /*name*/,const std::string message){
                //qDebug()<<name.c_str()<<":"<<message.c_str();
                DecodingStatistcs::instance().set_parse_and_enqueue_time(message.c_str());
            });
        }
        AVPacket *pkt=av_packet_alloc();
        pkt->data=(uint8_t*)frame->get_nal().getData();
        pkt->size=frame->get_nal().getSize();
        const auto beforeFeedFrameUs=getTimeUs();
        pkt->pts=beforeFeedFrameUs;
        timestamp_add_fed(pkt->pts);
        avcodec_send_packet(decoder_ctx, pkt);
        av_packet_free(&pkt);
        return true;
    }
    return false;
}

void MppDecoder::fetch_frame_or_feed_input_packet(){
    AVPacket *pkt=av_packet_alloc();
    bool keep_fetching_frames_or_input_packets=true;
    while(keep_fetching_frames_or_input_packets){
        if(request_restart){
            keep_fetching_frames_or_input_packets=false;
            request_restart=false;
            continue;
        }
        AVFrame* frame= av_frame_alloc();
        assert(frame);
        const int ret = avcodec_receive_frame(decoder_ctx, frame);
        //m_ffmpeg_dequeue_or_queue_mutex.unlock();
        if(ret == AVERROR_EOF){
            qDebug()<<"Got EOF";
            keep_fetching_frames_or_input_packets=false;
        }else if(ret==0){
            timestamp_debug_valid(frame->pts);
            // we got a new frame
            const auto now_us=getTimeUs();
            const auto delay_us=now_us-frame->pts;
            //qDebug()<<"(True) decode delay(nowait):"<<((float)delay_us/1000.0f)<<" ms";
            //frame->pts=now;
            avg_decode_time.add(std::chrono::microseconds(delay_us));
            // display frame
            on_new_frame(frame);
            avg_decode_time.custom_print_in_intervals(std::chrono::seconds(3),[](const std::string /*name*/,const std::string message){
                 //qDebug()<<name.c_str()<<":"<<message.c_str();
                 DecodingStatistcs::instance().set_decode_time(message.c_str());
            });
            av_frame_free(&frame);
            frame= av_frame_alloc();
        }else if(ret==AVERROR(EAGAIN)){
            //qDebug()<<"Needs more data";
            // Get more encoded data
            const bool success=feed_rtp_frame_if_available();
            if(!success){
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }else{
            qDebug()<<"Weird decoder error:"<<ret;
            keep_fetching_frames_or_input_packets=false;
        }
    }
    av_packet_free(&pkt);
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
    if(last_frame_width==-1 || last_frame_height==-1){
        last_frame_width=frame->width;
        last_frame_height=frame->height;
    }else{
        if(last_frame_width!=frame->width || last_frame_height!=frame->height){
            // PI and SW decoer will just slently start outputting garbage frames
            // if the width/ height changes during RTP streaming
            qDebug()<<"Need to restart the decoder, width / heght changed";
            request_restart=true;
        }
    }
    //drm_prime_out->queue_new_frame_for_display(frame);
}

void MppDecoder::reset_before_decode_start()
{
    n_no_output_frame_after_x_seconds=0;
    last_frame_width=-1;
    last_frame_height=-1;
    avg_decode_time.reset();
    avg_parse_time.reset();
    DecodingStatistcs::instance().reset_all_to_default();
    last_frame_width=-1;
    last_frame_height=-1;
    m_fed_timestamps_queue.clear();
}

// https://ffmpeg.org/doxygen/3.3/decode_video_8c-example.html
void MppDecoder::open_and_decode_until_error_custom_rtp(const QOpenHDVideoHelper::VideoStreamConfig settings)
{
    // this is always for primary video, unless switching is enabled
    auto stream_config=settings.primary_stream_config;

    // This thread pulls frame(s) from the rtp decoder and therefore should have high priority
    SchedulingHelper::setThreadParamsMaxRealtime();
    av_log_set_level(AV_LOG_TRACE);
     assert(stream_config.video_codec==QOpenHDVideoHelper::VideoCodecH264 || stream_config.video_codec==QOpenHDVideoHelper::VideoCodecH265);
     if(stream_config.video_codec==QOpenHDVideoHelper::VideoCodecH264){
         decoder = avcodec_find_decoder(AV_CODEC_ID_H264);
     }else if(stream_config.video_codec==QOpenHDVideoHelper::VideoCodecH265){
         decoder = avcodec_find_decoder(AV_CODEC_ID_H265);
     }
     if (!decoder) {
         qDebug()<< "MppDecoder::open_and_decode_until_error_custom_rtp: Codec not found";
         return;
     }
     // ----------------------
     bool use_pi_hw_decode=false;
     if (decoder->id == AV_CODEC_ID_H264) {
         qDebug()<<"H264 decode";
         qDebug()<<all_hw_configs_for_this_codec(decoder).c_str();
         if(!stream_config.enable_software_video_decoder){
             auto tmp = avcodec_find_decoder_by_name("h264_mmal");
             if(tmp!=nullptr){
                 decoder = tmp;
                 wanted_hw_pix_fmt = AV_PIX_FMT_MMAL;
                 use_pi_hw_decode=true;
             }else{
                 wanted_hw_pix_fmt = AV_PIX_FMT_YUV420P;
             }
         }else{
             wanted_hw_pix_fmt = AV_PIX_FMT_YUV420P;
         }
     }else if(decoder->id==AV_CODEC_ID_H265){
         qDebug()<<"H265 decode";
         if(!stream_config.enable_software_video_decoder){
             qDebug()<<all_hw_configs_for_this_codec(decoder).c_str();
             // HW format used by rpi h265 HW decoder
             wanted_hw_pix_fmt = AV_PIX_FMT_DRM_PRIME;
             use_pi_hw_decode=true;
         }else{

         }
     }
     // ------------------------------------
     decoder_ctx = avcodec_alloc_context3(decoder);
     if (!decoder_ctx) {
         qDebug()<< "MppDecoder::open_and_decode_until_error_custom_rtp: Could not allocate video codec context";
         return;
     }
     // ----------------------------------
    // From moonlight-qt. However, on PI, this doesn't seem to make any difference, at least for H265 decode.
    // (I never measured h264, but don't think there it is different).
    // Always request low delay decoding
    decoder_ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
    // Allow display of corrupt frames and frames missing references
    decoder_ctx->flags |= AV_CODEC_FLAG_OUTPUT_CORRUPT;
    decoder_ctx->flags2 |= AV_CODEC_FLAG2_SHOW_ALL;
    // --------------------------------------
    // --------------------------------------
    std::string selected_decoding_type="?";

    selected_decoding_type="HW";

    // A thread count of 1 reduces latency for both SW and HW decode
    decoder_ctx->thread_count = 1;

    // ---------------------------------------

     if (avcodec_open2(decoder_ctx, decoder, NULL) < 0) {
         fprintf(stderr, "Could not open codec\n");
         avcodec_free_context(&decoder_ctx);
         return;
     }

     qDebug()<<"MppDecoder::open_and_decode_until_error_custom_rtp()-begin loop";
     m_rtp_receiver=std::make_unique<RTPReceiver>(stream_config.udp_rtp_input_port,stream_config.udp_rtp_input_ip_address,stream_config.video_codec==1,settings.generic.dev_feed_incomplete_frames_to_decoder);

     reset_before_decode_start();
     DecodingStatistcs::instance().set_decoding_type(selected_decoding_type.c_str());
     AVPacket *pkt=av_packet_alloc();
     assert(pkt!=nullptr);
     bool has_keyframe_data=false;
     while(true){
         // We break out of this loop if someone requested a restart
         if(request_restart){
             request_restart=false;
             goto finish;
         }
         // or the decode config changed and we need a restart
         if(m_rtp_receiver->config_has_changed_during_decode){
             qDebug()<<"Break/Restart,config has changed during decode";
             goto finish;
         }
         //std::this_thread::sleep_for(std::chrono::milliseconds(3000));
         if(!has_keyframe_data){
              std::shared_ptr<std::vector<uint8_t>> keyframe_buf=m_rtp_receiver->get_config_data();
              if(keyframe_buf==nullptr){
                  std::this_thread::sleep_for(std::chrono::milliseconds(100));
                  continue;
              }
              qDebug()<<"Got decode data (before keyframe)";
              pkt->data=keyframe_buf->data();
              pkt->size=keyframe_buf->size();
              decode_config_data(pkt);
              has_keyframe_data=true;
              continue;
         }else{
             auto buf =m_rtp_receiver->get_next_frame(std::chrono::milliseconds(kDefaultFrameTimeout));
             if(buf==nullptr){
                 // No buff after X seconds
                 continue;
             }
             //qDebug()<<"Got decode data (after keyframe)";
             pkt->data=(uint8_t*)buf->get_nal().getData();
             pkt->size=buf->get_nal().getSize();
             decode_and_wait_for_frame(pkt,buf->get_nal().creationTime);
             //fetch_frame_or_feed_input_packet();
         }
     }
finish:
     qDebug()<<"MppDecoder::open_and_decode_until_error_custom_rtp()-end loop";
     m_rtp_receiver=nullptr;
     avcodec_free_context(&decoder_ctx);
}

void MppDecoder::timestamp_add_fed(int64_t ts)
{
    m_fed_timestamps_queue.push_back(ts);
    if(m_fed_timestamps_queue.size()>=MAX_FED_TIMESTAMPS_QUEUE_SIZE){
        m_fed_timestamps_queue.pop_front();
    }
}

bool MppDecoder::timestamp_check_valid(int64_t ts)
{
    for(const auto& el:m_fed_timestamps_queue){
        if(el==ts)return true;
    }
    return false;
}

void MppDecoder::timestamp_debug_valid(int64_t ts)
{
    const bool valid=timestamp_check_valid(ts);
    if(valid){
        qDebug()<<"Is a valid timestamp";
    }else{
        qDebug()<<"Is not a valid timestamp";
    }
}

void MppDecoder::dirty_generic_decode_via_external_decode_service(const QOpenHDVideoHelper::VideoStreamConfig& settings)
{
    qopenhd::decode::service::decode_via_external_decode_service(settings,request_restart);
}
