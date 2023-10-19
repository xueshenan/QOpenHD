#ifndef QOPENHDVIDEOHELPER_H
#define QOPENHDVIDEOHELPER_H

#include <QSettings>
#include <qqmlapplicationengine.h>
#include <qquickitem.h>
#include <qquickwindow.h>

#include <sstream>
#include <fstream>
#include <qfileinfo.h>

namespace QOpenHDVideoHelper {

// Must be in sync with OpenHD
static constexpr auto kDefault_udp_rtp_input_ip_address="127.0.0.1";
static constexpr auto kDefault_udp_rtp_input_port_primary=5600;

// Supported video codecs
typedef enum VideoCodec {
    VideoCodecH264=0,
    VideoCodecH265=1
} VideoCodec;

static VideoCodec intToVideoCodec(int videoCodec) {
    if (videoCodec == 0) {
        return VideoCodecH264;
    }
    if (videoCodec == 1) {
        return VideoCodecH265;
    }
    qDebug() << "VideoCodec::intToVideoCodec::somethingWrong,using H264 as default";
    return VideoCodecH264;
}

static std::string video_codec_to_string(const VideoCodec& codec) {
    switch(codec) {
    case VideoCodec::VideoCodecH264:
        return "h264";
        break;
    case VideoCodec::VideoCodecH265:
        return "h265";
        break;
    }
    return "h264";
}

/**
 * Dirty - settings mostly for developing / handling differen platforms
 * Not seperated for primary / secondary stream
 */
struct GenericVideoSettings {
    //
    bool dev_enable_custom_pipeline = false;
    std::string dev_custom_pipeline = "";
    // feed incomplete frame(s) to the decoder, in contrast to only only feeding intact frames,
    // but not caring about missing previous frames
    bool dev_feed_incomplete_frames_to_decoder = false;
    // platforms other than RPI
    bool dev_always_use_generic_external_decode_service = false;
    // On embedded devices, video is commonly rendered on a special surface, independent of QOpenHD
    // r.n only the rpi mmal impl. supports proper video rotation
    int extra_screen_rotation = 0;

    // 2 configs are equal if all members are exactly the same.
    bool operator==(const GenericVideoSettings &o) const {
       return  this->dev_enable_custom_pipeline==o.dev_enable_custom_pipeline &&
               this->dev_custom_pipeline==o.dev_custom_pipeline &&
               this->dev_feed_incomplete_frames_to_decoder == o.dev_feed_incomplete_frames_to_decoder &&
               this->dev_always_use_generic_external_decode_service==o.dev_always_use_generic_external_decode_service &&
               this->extra_screen_rotation == o.extra_screen_rotation;
     }
    bool operator !=(const GenericVideoSettings &o) const {
        return !(*this==o);
    }
};

/**
 * @brief Settings for the video stream
 */
struct VideoStreamConfigXX {
    // the ip address where we receive udp rtp video data from
    std::string udp_rtp_input_ip_address = kDefault_udp_rtp_input_ip_address;
    // the port where to receive udp rtp video data from
    int udp_rtp_input_port = kDefault_udp_rtp_input_port_primary;
    // the video codec the received rtp data should be intepreted as.
    VideoCodec video_codec = VideoCodecH264;
    // force sw decoding (only makes a difference if on this platform/compile-time configuration a HW decoder is chosen by default)
    bool enable_software_video_decoder = false;

    // 2 configs are equal if all members are exactly the same.
    bool operator == (const VideoStreamConfigXX &o) const {
        return this->udp_rtp_input_port == o.udp_rtp_input_port
                && this->video_codec == o.video_codec
                && this->enable_software_video_decoder == o.enable_software_video_decoder
                && this->udp_rtp_input_ip_address == o.udp_rtp_input_ip_address;
    }
    bool operator != (const VideoStreamConfigXX &o) const {
        return !(*this==o);
    }
};

struct VideoStreamConfig {
    GenericVideoSettings generic;
    VideoStreamConfigXX primary_stream_config;
    bool operator==(const VideoStreamConfig &o) const {
        return this->generic == o.generic
               && this->primary_stream_config == o.primary_stream_config;
    }
    bool operator !=(const VideoStreamConfig &o) const {
        return !(*this==o);
    }
};


static VideoStreamConfigXX read_from_settingsXX() {
    QSettings settings;
    QOpenHDVideoHelper::VideoStreamConfigXX _videoStreamConfig;

    _videoStreamConfig.udp_rtp_input_port = settings.value("qopenhd_primary_video_rtp_input_port", kDefault_udp_rtp_input_port_primary).toInt();
    _videoStreamConfig.udp_rtp_input_ip_address = settings.value("qopenhd_primary_video_rtp_input_ip", kDefault_udp_rtp_input_ip_address).toString().toStdString();

    const int tmp_video_codec = settings.value("qopenhd_primary_video_codec", 0).toInt();
    _videoStreamConfig.video_codec = QOpenHDVideoHelper::intToVideoCodec(tmp_video_codec);
    _videoStreamConfig.enable_software_video_decoder = settings.value("qopenhd_primary_video_force_sw", 0).toBool();

    return _videoStreamConfig;
}

// Kinda UI, kinda video related
static int get_display_rotation(){
    QSettings settings{};
    return settings.value("general_screen_rotation", 0).toInt();
}

// See setting description
// do not preserve aspect ratio of primary video
// default false (do preserve video aspect ratio)
static bool get_primary_video_scale_to_fit() {
    QSettings settings{};
    return settings.value("primary_video_scale_to_fit", false).toBool();
}

static GenericVideoSettings read_generic_from_settings() {
    QSettings settings;
    GenericVideoSettings _videoStreamConfig;

    _videoStreamConfig.dev_enable_custom_pipeline = settings.value("dev_enable_custom_pipeline",false).toBool();
    //
    _videoStreamConfig.dev_always_use_generic_external_decode_service = settings.value("dev_always_use_generic_external_decode_service", false).toBool();
    _videoStreamConfig.extra_screen_rotation=get_display_rotation();
    // QML text input sucks, so we read a file. Not ideal, but for testing only anyways
    {
        _videoStreamConfig.dev_custom_pipeline="";
        if(_videoStreamConfig.dev_enable_custom_pipeline){
            std::ifstream file("/usr/local/share/qopenhd/custom_pipeline.txt");
            if(file.is_open()){
                std::stringstream buffer;
                buffer << file.rdbuf();
                _videoStreamConfig.dev_custom_pipeline=buffer.str();
            }else{
                qDebug()<<"dev_enable_custom_pipeline but no file";
            }
        }
    }
    //_videoStreamConfig.dev_custom_pipeline=settings.value("dev_custom_pipeline","").toString().toStdString();
    return _videoStreamConfig;
}

static VideoStreamConfig read_config_from_settings() {
    VideoStreamConfig ret;
    ret.generic = read_generic_from_settings();
    ret.primary_stream_config = read_from_settingsXX();
    return ret;
}


// FFMPEG needs a ".sdp" file to do rtp udp h264,h265
// for h264/5 we map h264/5 to 96 (general)
static std::string create_udp_rtp_sdp_file(const VideoStreamConfigXX& video_stream_config) {
    std::stringstream ss;
    ss<<"c=IN IP4 "<<video_stream_config.udp_rtp_input_ip_address<<"\n";
    //ss<<"v=0\n";
    //ss<<"t=0 0\n";
    //ss<<"width=1280\n";
    //ss<<"height=720\n";
    ss<<"m=video "<<video_stream_config.udp_rtp_input_port<<" RTP/UDP 96\n";
    if(video_stream_config.video_codec==QOpenHDVideoHelper::VideoCodec::VideoCodecH264){
        ss<<"a=rtpmap:96 H264/90000\n";
    }else{
        assert(video_stream_config.video_codec==QOpenHDVideoHelper::VideoCodec::VideoCodecH265);
        ss<<"a=rtpmap:96 H265/90000\n";
    }
    return ss.str();
}

static void write_file_to_tmp(const std::string filename,const std::string content) {
    std::ofstream _t(filename);
    _t << content;
    _t.close();
}

static constexpr auto kRTP_FILENAME="/tmp/rtp_custom.sdp";

static void write_udp_rtp_sdp_file_to_tmp(const VideoStreamConfigXX& video_stream_config) {
    write_file_to_tmp(kRTP_FILENAME,create_udp_rtp_sdp_file(video_stream_config));
}

static std::string get_udp_rtp_sdp_filename(const VideoStreamConfigXX& video_stream_config) {
    write_udp_rtp_sdp_file_to_tmp(video_stream_config);
    return kRTP_FILENAME;
}

static int get_qopenhd_n_cameras() {
    QSettings settings;
    const int num_cameras = settings.value("dev_qopenhd_n_cameras", 1).toInt();
    return num_cameras;
}

// We autmatically (over) write the video codec once we get camera telemetry data
static int get_qopenhd_camera_video_codec(bool /*secondary*/) {
    QSettings settings;
    int codec_in_qopenhd = settings.value("qopenhd_primary_video_codec", 0).toInt();
    return codec_in_qopenhd;
}

static void set_qopenhd_camera_video_codec(bool /*secondary*/,int codec){
    QSettings settings;
    settings.setValue("qopenhd_primary_video_codec",(int)codec);
}

}
#endif // QOPENHDVIDEOHELPER_H
