#ifndef MPP_DECODER_H
#define MPP_DECODER_H


#include <thread>
#include <memory>
#include <mutex>
#include <deque>
#include <optional>
#include <queue>
#include <atomic>
#include <qtimer.h>

extern "C" {
#include <rk_mpi.h>
#include "mpp_mem.h"
#include "mpp_env.h"
#include "mpp_time.h"
#include "mpp_common.h"
#include "mpplib/utils/mpi_dec_utils.h"
}

#include "avcodec_decoder.h"
#include "QOpenHDVideoHelper.hpp"
#include "rtp/rtpreceiver.h"

typedef struct {
    MppCtx          ctx;
    MppApi          *mpi;

    /* input */
    MppBufferGroup  frm_grp;
    MppPacket       packet;

    RK_S64          first_pkt;
    RK_S64          first_frm;

    size_t          max_usage;
    RK_S64          elapsed_time;
    RK_S64          delay;
} MpiDecLoopData;

/**
 * Decoding and display of primary video on all platforms except android
 * NOTE: On rpi, we actually don't use avcodec, but the decode service workaround (mmal)
 * since it is the only way to get (ish) low latency h264 video on rpi.
 */
class MppDecoder : public QObject
{
public:
    MppDecoder(QObject *parent = nullptr);
    ~MppDecoder();
    // called when app is created
    void init(bool primaryStream);
    // called when app terminates
    void terminate();
private:
    std::unique_ptr<std::thread> _decode_thread = nullptr;
private:
    // The logic of this decode "machine" is simple:
    // Start decoding as soon as enough config data has been received
    // Ccompletely restart the decoding in case an error occurs
    // or the settings changed (e.g. a switch of the video codec).
    void constant_decode();
    // feed one frame to the decoder, then wait until the frame is returned
    // (This gives the lowest latency on most decoders that have a "lockstep").
    // If we didn't get a frame out for X seconds after feeding a frame more than X times,
    // This will stop performing the lockstep. In this case, either the decoder cannot decode
    // the video without buffering (which is bad, but some IP camera(s) create such a stream)
    // or the underlying decode implementation (e.g. rpi foundation h264 !? investigate) has some quirks.
    int decode_and_wait_for_frame(std::shared_ptr<NALUBuffer> nalu_buffer, std::optional<std::chrono::steady_clock::time_point> parse_time=std::nullopt);
    // Just send data to the codec, do not check or wait for a frame
    bool decode_config_data(std::shared_ptr<std::vector<uint8_t>> config_data);
    // Called every time we get a new frame from the decoder, do what you wish here ;)
    void on_new_frame(AVFrame* frame);
    // simle restart, e.g. when the video codec or the video resolution has changed we need to break
    // out of a running "constant_decode_xx" loop
    std::atomic<bool> _request_restart = false;
    // Completely stop (Exit QOpenHD)
    bool _should_terminate=false;
    AvgCalculator avg_decode_time{"Decode"};
    AvgCalculator avg_parse_time{"Parse&Enqueue"};
    static constexpr std::chrono::milliseconds kDefaultFrameTimeout{33*2};
private:
    // Completely ineficient, but only way since QT settings callback(s) don't properly work
    // runs every 1 second, reads the settings and checks if they differ from the previosly
    // read settings. In case they differ, request a complete restart from the decoder.
    std::unique_ptr<QTimer> timer_check_settings_changed = nullptr;
    void timer_check_settings_changed_callback();
    QOpenHDVideoHelper::VideoStreamConfig _last_video_settings;
private:
    int _last_frame_width = -1;
    int _last_frame_height = -1;
private:
    std::unique_ptr<RTPReceiver> _rtp_receiver = nullptr;
private:
    // Custom rtp parse (and therefore limited to h264 and h265)
    // And always goes the mpp decode route.
    void open_and_decode_until_error(const QOpenHDVideoHelper::VideoStreamConfig &settings);
private:
    void reset_before_decode_start();
private:
    bool init_mpp_decoder();
private:
    MpiDecTestCmd _cmd_ctx;
    MpiDecLoopData _dec_data;
};

#endif // MPP_DECODER_H
