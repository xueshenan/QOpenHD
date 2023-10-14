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

static constexpr auto MAX_FED_TIMESTAMPS_QUEUE_SIZE = 100;

MppDecoder::MppDecoder(QObject *parent) : QObject(parent) {
}

MppDecoder::~MppDecoder() {
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

int MppDecoder::decode_and_wait_for_frame(AVPacket *packet, std::optional<std::chrono::steady_clock::time_point> parse_time)
{
    const auto beforeFeedFrame = std::chrono::steady_clock::now();

    if (parse_time != std::nullopt) {
        const auto delay = beforeFeedFrame-parse_time.value();
        avg_parse_time.add(delay);
        avg_parse_time.custom_print_in_intervals(std::chrono::seconds(3),[](const std::string /*name*/, const std::string message) {
            DecodingStatistcs::instance().set_parse_and_enqueue_time(message.c_str());
        });
    }

    const auto beforeFeedFrameUs = getTimeUs();
    packet->pts = beforeFeedFrameUs;
    add_feed_timestamp(packet->pts);

    MppCtx ctx  = _dec_data.ctx;
    MppApi *mpi = _dec_data.mpi;

    MppPacket mpp_packet = _dec_data.packet;
    mpp_packet_set_data(mpp_packet, packet->data);
    mpp_packet_set_size(mpp_packet, packet->size);
    mpp_packet_set_pos(mpp_packet, packet->data);
    mpp_packet_set_length(mpp_packet, packet->size);

    MPP_RET ret = mpi->decode_put_packet(ctx, mpp_packet);
    if (ret == MPP_OK) {
        if (!_dec_data.first_pkt) {
            _dec_data.first_pkt = mpp_time();
        }
    }

    // Poll until we get the frame out
    bool gotFrame = false;
    int n_times_we_tried_getting_a_frame_this_time = 0;
    while (!gotFrame) {
        MppFrame frame = NULL;
        RK_S32 times = 5;
try_again:
        ret = mpi->decode_get_frame(ctx, &frame);
        if (ret == MPP_ERR_TIMEOUT) {
            if (times > 0) {
                times--;
                msleep(1);
                goto try_again;
            }
            printf("%p decode_get_frame failed too much time\n", ctx);
        }
        if (ret != MPP_OK) {
            qDebug() << "decode_get_frame failed ret " << ret;
            break;
        }

        gotFrame = true;

        // we got a new frame
        if (frame != NULL) {
            if (mpp_frame_get_info_change(frame)) {
                RK_U32 width = mpp_frame_get_width(frame);
                RK_U32 height = mpp_frame_get_height(frame);
                RK_U32 hor_stride = mpp_frame_get_hor_stride(frame);
                RK_U32 ver_stride = mpp_frame_get_ver_stride(frame);
                RK_U32 buf_size = mpp_frame_get_buf_size(frame);

                qDebug() <<"decode_get_frame get info changed found";
                qDebug() << "decoder require buffer w:h [" << width << ":" << height << "]"
                         << "stride [" << hor_stride << ":" << ver_stride << "]"
                         << "buf_size " << buf_size;

                /*
                 * NOTE: We can choose decoder's buffer mode here.
                 * There are three mode that decoder can support:
                 *
                 * Mode 1: Pure internal mode
                 * In the mode user will NOT call MPP_DEC_SET_EXT_BUF_GROUP
                 * control to decoder. Only call MPP_DEC_SET_INFO_CHANGE_READY
                 * to let decoder go on. Then decoder will use create buffer
                 * internally and user need to release each frame they get.
                 *
                 * Advantage:
                 * Easy to use and get a demo quickly
                 * Disadvantage:
                 * 1. The buffer from decoder may not be return before
                 * decoder is close. So memroy leak or crash may happen.
                 * 2. The decoder memory usage can not be control. Decoder
                 * is on a free-to-run status and consume all memory it can
                 * get.
                 * 3. Difficult to implement zero-copy display path.
                 *
                 * Mode 2: Half internal mode
                 * This is the mode current test code using. User need to
                 * create MppBufferGroup according to the returned info
                 * change MppFrame. User can use mpp_buffer_group_limit_config
                 * function to limit decoder memory usage.
                 *
                 * Advantage:
                 * 1. Easy to use
                 * 2. User can release MppBufferGroup after decoder is closed.
                 *    So memory can stay longer safely.
                 * 3. Can limit the memory usage by mpp_buffer_group_limit_config
                 * Disadvantage:
                 * 1. The buffer limitation is still not accurate. Memory usage
                 * is 100% fixed.
                 * 2. Also difficult to implement zero-copy display path.
                 *
                 * Mode 3: Pure external mode
                 * In this mode use need to create empty MppBufferGroup and
                 * import memory from external allocator by file handle.
                 * On Android surfaceflinger will create buffer. Then
                 * mediaserver get the file handle from surfaceflinger and
                 * commit to decoder's MppBufferGroup.
                 *
                 * Advantage:
                 * 1. Most efficient way for zero-copy display
                 * Disadvantage:
                 * 1. Difficult to learn and use.
                 * 2. Player work flow may limit this usage.
                 * 3. May need a external parser to get the correct buffer
                 * size for the external allocator.
                 *
                 * The required buffer size caculation:
                 * hor_stride * ver_stride * 3 / 2 for pixel data
                 * hor_stride * ver_stride / 2 for extra info
                 * Total hor_stride * ver_stride * 2 will be enough.
                 *
                 * For H.264/H.265 20+ buffers will be enough.
                 * For other codec 10 buffers will be enough.
                 */

                if (_dec_data.frm_grp == NULL) {
                    /* If buffer group is not set create one and limit it */
                    ret = mpp_buffer_group_get_internal(&_dec_data.frm_grp, MPP_BUFFER_TYPE_ION);
                    if (ret) {
                        printf("%p get mpp buffer group failed ret %d\n", ctx, ret);
                        break;
                    }

                    /* Set buffer to mpp decoder */
                    ret = mpi->control(ctx, MPP_DEC_SET_EXT_BUF_GROUP, _dec_data.frm_grp);
                    if (ret) {
                        printf("%p set buffer group failed ret %d\n", ctx, ret);
                        break;
                    }
                } else {
                    /* If old buffer group exist clear it */
                    ret = mpp_buffer_group_clear(_dec_data.frm_grp);
                    if (ret) {
                        printf("%p clear buffer group failed ret %d\n", ctx, ret);
                        break;
                    }
                }

                /* Use limit config to limit buffer count to 24 with buf_size */
                ret = mpp_buffer_group_limit_config(_dec_data.frm_grp, buf_size, 24);
                if (ret) {
                    printf("%p limit buffer group failed ret %d\n", ctx, ret);
                    break;
                }

                /*
                 * All buffer group config done. Set info change ready to let
                 * decoder continue decoding
                 */
                ret = mpi->control(ctx, MPP_DEC_SET_INFO_CHANGE_READY, NULL);
                if (ret) {
                    printf("%p info change ready failed ret %d\n", ctx, ret);
                    break;
                }
            }

            if (!use_frame_timestamps_for_latency) {
                const auto x_delay = std::chrono::steady_clock::now() - beforeFeedFrame;
                // qDebug()<<"(True) decode delay(wait):"<<((float)std::chrono::duration_cast<std::chrono::microseconds>(x_delay).count()/1000.0f)<<" ms";
                avg_decode_time.add(x_delay);
            }
            avg_decode_time.custom_print_in_intervals(std::chrono::seconds(3),[](const std::string name, const std::string message) {
                Q_UNUSED(name)
                qDebug()<<name.c_str()<<":"<<message.c_str();
                DecodingStatistcs::instance().set_decode_time(message.c_str());
            });

            RK_U32 width = mpp_frame_get_width(frame);
            RK_U32 height = mpp_frame_get_height(frame);
            RK_U32 hor_stride = mpp_frame_get_hor_stride(frame);
            RK_U32 ver_stride = mpp_frame_get_ver_stride(frame);
            MppFrameFormat fmt = mpp_frame_get_fmt(frame);      //yuv420sp
            MppBuffer buffer = mpp_frame_get_buffer(frame);
            if (buffer == NULL) {
                mpp_frame_deinit(&frame);
                break;
            }
            RK_U8 *base = (RK_U8 *)mpp_buffer_get_ptr(buffer);
            RK_U8 *base_y = base;
            RK_U8 *base_c = base + hor_stride * ver_stride;

            // alloc output frame and set info
            AVFrame *out_frame = av_frame_alloc();
            AVFrame *ref_frame = av_frame_alloc();
            out_frame->width = width;
            out_frame->height = height;
            out_frame->format = AV_PIX_FMT_YUV420P;
            int ret = av_frame_get_buffer(out_frame, 16);
            if (ret != 0) {
                char buf[1024] = {0};
                av_strerror(ret, buf, sizeof(buf));
                qDebug() << buf;
                //TODO need free
                break;
            }
            assert(out_frame->data[0] != NULL);
            auto y_pos = out_frame->data[0];
            for (int i = 0; i < height; i++, base_y += hor_stride) {
                memcpy(y_pos, base_y, out_frame->linesize[0]);
                y_pos += out_frame->linesize[0];
            }
            auto u_pos = out_frame->data[1];
            auto v_pos = out_frame->data[2];
            for (int i = 0; i < height / 2; i++, base_c += hor_stride) {
                for (int j = 0; j < width / 2; j++) {
                    *u_pos = base_c[j * 2];
                    *v_pos = base_c[j* 2 + 1];
                    u_pos++;
                    v_pos++;
                }
            }

            av_frame_ref(ref_frame, out_frame);

            if (out_frame == NULL) {
                // NOTE: It is a common practice to not care about OOM, and this is the best approach in my opinion.
                // but ffmpeg uses malloc and returns error codes, so we keep this practice here.
                qDebug()<<"can not alloc frame";
                av_frame_free(&out_frame);
                return AVERROR(ENOMEM);
            }

            out_frame->pts = beforeFeedFrameUs;
            // display frame
            on_new_frame(ref_frame);
FreeBuffer:
            av_frame_free(&ref_frame);
            av_frame_free(&out_frame);

            n_times_we_tried_getting_a_frame_this_time++;
            mpp_frame_deinit(&frame);
        }
    }

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
    bool ret = init_mpp_decoder();
    assert(ret);

    // this is always for primary video, unless switching is enabled
    auto stream_config = settings.primary_stream_config;

    // This thread pulls frame(s) from the rtp decoder and therefore should have high priority
    SchedulingHelper::setThreadParamsMaxRealtime();

    qDebug()<<"MppDecoder::open_and_decode_until_error_custom_rtp()-begin loop";
    _rtp_receiver=std::make_unique<RTPReceiver>(stream_config.udp_rtp_input_port,
                                              stream_config.udp_rtp_input_ip_address,
                                              stream_config.video_codec==1,
                                              settings.generic.dev_feed_incomplete_frames_to_decoder);

     reset_before_decode_start();
     DecodingStatistcs::instance().set_decoding_type("HW");
     AVPacket *pkt=av_packet_alloc();
     assert(pkt != NULL);
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
         auto buf = _rtp_receiver->get_next_frame(std::chrono::milliseconds(kDefaultFrameTimeout));
         if (buf == nullptr) {
             // No buff after X seconds
             continue;
         }
         pkt->data = (uint8_t*)buf->get_nal().getData();
         pkt->size = buf->get_nal().getSize();
         decode_and_wait_for_frame(pkt, buf->get_nal().creationTime);
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

bool MppDecoder::init_mpp_decoder() {
    MPP_RET ret = MPP_OK;

    memset(&_cmd_ctx, 0, sizeof(MpiDecTestCmd));
    _cmd_ctx.format = MPP_FMT_BUTT;
    _cmd_ctx.pkt_size = MPI_DEC_STREAM_SIZE;

    // base flow context
    MppCtx ctx          = NULL;
    MppApi *mpi         = NULL;

    // input
    MppPacket packet    = NULL;

    memset(&_dec_data, 0, sizeof(_dec_data));

    // config for runtime mode
    MppDecCfg cfg       = NULL;
    RK_U32 need_split   = 1;
    RK_S32 fast_out     = 1;

    ret = mpp_packet_init(&packet, NULL, 0);
    if (ret) {
        qDebug() << "mpp_packet_init failed";
        goto MPP_TEST_OUT;
    }

    ret = mpp_create(&ctx, &mpi);
    if (ret) {
        qDebug() << "mpp_create failed";
        goto MPP_TEST_OUT;
    }

    ret = mpp_init(ctx, MPP_CTX_DEC, MPP_VIDEO_CodingAVC);
    if (ret) {
        qDebug() << ctx << " mpp_init failed";
        goto MPP_TEST_OUT;
    }

    mpp_dec_cfg_init(&cfg);

    /* get default config from decoder context */
    ret = mpi->control(ctx, MPP_DEC_GET_CFG, cfg);
    if (ret) {
        qDebug() << ctx << " failed to get decoder cfg ret " << ret;
        goto MPP_TEST_OUT;
    }

    /*
     * split_parse is to enable mpp internal frame spliter when the input
     * packet is not aplited into frames.
     */
    ret = mpp_dec_cfg_set_u32(cfg, "base:split_parse", need_split);
    if (ret) {
        qDebug() << ctx << "failed to set split_parse ret " << ret;
        goto MPP_TEST_OUT;
    }

    ret = mpp_dec_cfg_set_u32(cfg, "base:fast_out", fast_out);
    if (ret) {
        qDebug() << ctx << "failed to set fast_out ret " << ret;
        goto MPP_TEST_OUT;
    }


    ret = mpi->control(ctx, MPP_DEC_SET_CFG, cfg);
    if (ret) {
        qDebug() << ctx << "failed to set cfg " << cfg << " ret " << ret;
        goto MPP_TEST_OUT;
    }

    _dec_data.ctx            = ctx;
    _dec_data.mpi            = mpi;
    _dec_data.packet         = packet;

    ret = mpi->reset(ctx);
    if (ret) {
       qDebug() <<ctx << " mpi->reset failed";
       goto MPP_TEST_OUT;
    }
    qDebug() << "init mpp success";
    return ret == MPP_OK;

MPP_TEST_OUT:
    if (packet != NULL) {
        mpp_packet_deinit(&packet);
        _dec_data.packet = NULL;
    }

    if (ctx != NULL) {
        mpp_destroy(ctx);
        ctx = NULL;
    }
    if (_dec_data.frm_grp) {
        mpp_buffer_group_put(_dec_data.frm_grp);
        _dec_data.frm_grp = NULL;
    }

    if (cfg) {
        mpp_dec_cfg_deinit(cfg);
        cfg = NULL;
    }
    return ret;
}
