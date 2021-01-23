#pragma once
#include <SDL/SDL.h>
#undef main

extern "C" {
#include "libavutil/opt.h"
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
# include "libavfilter/avfilter.h"
# include "libavfilter/buffersink.h"
# include "libavfilter/buffersrc.h"
}

#include <memory>
#include <thread>
#include <mutex>
#include <string>
#include <functional>
#include <vector>
#include <queue>

using namespace std;

#ifndef CONFIG_AVFILTER
#undef BUILD_WITH_AUDIO_FILTER
#undef BUILD_WITH_VIDEO_FILTER
#endif

/* no AV correction is done if too big error */
#define AV_NOSYNC_THRESHOLD 10.0

void ff_init();

enum MediaStatus {
  MEDIA_STATUS_START = 1,
  MEDIA_STATUS_PAUSED,
  MEDIA_STATUS_RESUMED,
  MEDIA_STATUS_REWIND_END
};

enum {
  AV_SYNC_AUDIO_MASTER, /* default choice */
  AV_SYNC_VIDEO_MASTER,
  AV_SYNC_EXTERNAL_CLOCK, /* synchronize to an external clock */
};

#define VIDEO_PICTURE_QUEUE_SIZE 3
#define SUBPICTURE_QUEUE_SIZE 16
#define SAMPLE_QUEUE_SIZE 9
#define FRAME_QUEUE_SIZE FFMAX(SAMPLE_QUEUE_SIZE, FFMAX(VIDEO_PICTURE_QUEUE_SIZE, SUBPICTURE_QUEUE_SIZE))

typedef struct AudioParams {
    int freq{0};
    int channels{0};
    int64_t channel_layout{0};
    enum AVSampleFormat fmt{ AV_SAMPLE_FMT_NONE };
    int frame_size{0};
    int bytes_per_sec{0};
} AudioParams;


struct Clock {
    double pts{0};           /* clock base */
    double pts_drift{0};     /* clock base minus time at which we updated the clock */
    double last_updated{0};
    double speed{0};
    int serial{0};           /* clock is based on a packet with this serial */
    int paused{0};
    int *queue_serial{0};    /* pointer to the current packet queue serial, used for obsolete clock detection */

  Clock(int *queue_serial);
  double get_clock() const;
  double time_passed() const;
  void update();
  void set_clock_at(double pts, int serial, double time);
  void set_clock(double pts, int serial);
  void sync_clock_to_slave(const Clock *slave, double threshold = AV_NOSYNC_THRESHOLD);
  void set_clock_speed(double speed);
};

class PacketQueue {
public:
  PacketQueue(int& serial);
  ~PacketQueue();

  bool has_enough_packets(const AVRational& time_base) const;
  int size() const { return size_; }
  int packetsCount() const { return nb_packets; }
  bool empty() const { return nb_packets == 0; }

  int put(AVPacket *pkt, int specified_serial = -1);
  int put_nullpacket(int stream_index);
  int get(AVPacket *pkt, int *serial);
  void start();
  void nextSerial();
  void abort();

private:
  void flush();
  int put_private(std::unique_lock<std::mutex>& lk, AVPacket *pkt, int specified_serial);

private:
  std::queue<std::pair<int, AVPacket>> pkts_;
  int nb_packets{0};
  int size_{0};
  int64_t duration_{0};
  bool abort_request_{true};
  std::mutex mtx;
  std::condition_variable cond;

  int& serial_; // referenced streaming serial
};

/* Common struct for handling all types of decoded data and allocated render buffers. */
struct Frame {
  AVFrame *frame{nullptr};
  AVSubtitle sub{0};
  int serial{0};
  double pts{0};           /* presentation timestamp for the frame */
  double duration{0};      /* estimated duration of the frame */
  int64_t pos{0};          /* byte position of the frame in the input file */
  int width{0};
  int height{0};
  int format{0};
  AVRational sar{0};
  int uploaded{0};
};

struct SimpleFrame {
  SimpleFrame(AVFrame *src_frame, int serial, double pts, double duration) {
    frame = av_frame_alloc();
    av_frame_move_ref(frame, src_frame);
    av_frame_unref(src_frame);
    this->serial = serial;

    this->pts = pts;
    this->duration = duration;
  }

  ~SimpleFrame() {
    av_frame_free(&frame);
  }

  SimpleFrame(const SimpleFrame&) = delete;
  SimpleFrame& operator=(const SimpleFrame&) = delete;

  SimpleFrame(SimpleFrame&& other) {
    this->frame = other.frame;
    this->serial = other.serial;
    this->pts = other.pts;
    this->duration = other.duration;
    other.frame = nullptr;
  }

  SimpleFrame& operator=(SimpleFrame&& other) {
    this->frame = other.frame;
    this->serial = other.serial;
    this->pts = other.pts;
    this->duration = other.duration;
    other.frame = nullptr;
    return *this;
  }

  AVFrame *frame{nullptr};
  int serial{0};
  double pts{0};           /* presentation timestamp for the frame */
  double duration{0};      /* estimated duration of the frame */
};

struct FrameQueue {
  FrameQueue(const int& serial, int max_size, bool keep_last);
  ~FrameQueue();
  void abort();

  int64_t lastShownPosition() const;
  /* return the number of undisplayed frames in the queue */
  int nb_remaining() const {
    return size - rindex_shown;
  }

  Frame *peek_readable();
  Frame *peek_writable();
  void next();
  void push();
  Frame *peek();
  Frame *peek_next();
  Frame *peek_last();

  Frame queue[FRAME_QUEUE_SIZE]{0};
  int rindex{0};
  int windex{0};
  int size{0};
  int rindex_shown{0};
  std::mutex mtx;
  std::condition_variable cond;

private:
  const int& serial_;
  const int max_size_{0};
  const bool keep_last_{false};
  bool abort_request_{false};
};

using PacketGetter = std::function<int(AVMediaType codec_type, AVCodecID codec_id, AVPacket *pkt, int *serial)>;

class Decoder {
public:
  Decoder(const int& serial);
  virtual ~Decoder();

  void init(AVCodecContext *avctx);
  void destroy();

  void abort();
  bool finished() const;
  int decodeFrame(PacketGetter packet_getter, AVFrame *frame, AVSubtitle *sub, int& pkt_serial);

  template<class LoopFunc>
  void start(LoopFunc&& f) {
    abort();
    abort_request_ = false;
    tid_ = std::thread([this, f] {
      f(this, &this->finished_);
    });
  }

  bool valid() const { return !!avctx_; }
  const AVCodecContext* context() const { return avctx_; }

  int64_t start_pts{0};
  AVRational start_pts_tb{0};
  int64_t next_pts{0};
  AVRational next_pts_tb{0};

private:
  std::thread tid_;
  bool abort_request_{true};

  int finished_{0};
  const int& serial_;

  AVCodecContext *avctx_{nullptr};
  AVPacket pending_pkt_{0};
  bool packet_pending_{false};
};


// only for test
class SyncDecoder : public Decoder {
public:
  SyncDecoder();
  ~SyncDecoder();

  int decodeBuffer(const uint8_t* data, int size, AVFrame **frame_out);

  static SyncDecoder* open(const char* codec_name, const AVCodecParameters *par);
private:
  int placeHodler_{1};
  AVFrame *frame_{nullptr};
};

enum MediaCommand {
  MEDIA_CMD_QUIT = 1,
  MEDIA_CMD_PAUSE,
  MEDIA_CMD_VOLUMN, // arg0=0: mute arg0=1: up arg0=1-1: down
  MEDIA_CMD_NEXT_FRAME,
  MEDIA_CMD_PREV_FRAME,
  MEDIA_CMD_CHAPTER,
  MEDIA_CMD_SEEK,
  MEDIA_CMD_SPEED
};

/*
MEDIA_CMD_SEEK, arg0,
arg0 = 0, seek by pts
arg0 = 1, seek by pts relative, arg1 = delta
arg0 = 2, seek by frmae id, arg1 = int64(id)

*/

enum SeekMethod {
  SEEK_METHOD_NONE,
  SEEK_METHOD_POS,
  SEEK_METHOD_BYTES,
  SEEK_METHOD_REWIND,
  SEEK_METHOD_REWIND_CONTINUE
};

typedef struct MediaEvent {
    int event;
    int arg0;
    double arg1;
    double arg2;
} MediaEvent;

class EventQueue {
public:
  void set(MediaEvent *evt);
  bool get(MediaEvent *evt);

private:
  std::queue<MediaEvent> evts_;
  bool quit_{false};
  std::mutex mtx;
};


struct ConverterContext {
  ConverterContext(AVPixelFormat fmt);
  ~ConverterContext();

  int convert(AVFrame *frame);
  int convert(int src_format, int src_width, int src_height, const uint8_t * const*pixels, int* pitch);

  struct SwsContext *convert_ctx{nullptr};
  uint8_t* buffer{nullptr};
  int buffer_size_{0};

  AVFrame *frame_{nullptr};
  const AVPixelFormat target_fmt{AV_PIX_FMT_NONE};
};

struct Detection_t;

using OnStatus = std::function<void(MediaStatus)>;
using OnMetaInfo = std::function<void(
    double start_time,
    double duration,
    int width,
    int height,
    const char*)>;
using OnStatics = std::function<void(double fps, double tbr, double tbn, double tbc)>;
using OnClockUpdate = std::function<void(double timestamp)>;
using OnIYUVDisplay = std::function<void(AVFrame*, double pts, int64_t id)>;
using OnAIData = std::function<void(const Detection_t& det, double pts)>;
using OnLog = std::function<void(int, const string&)>;

class PlayBackContext {
public:
  virtual ~PlayBackContext();
  PlayBackContext();

  void eventLoop(int argc, char **argv);
  void sendEvent(int event, int arg0, double arg1, double arg2);

protected:
  const Clock& masterClock() const;
  int get_master_sync_type() const;
  double get_master_clock() const;
  void adjustExternalClockSpeed();

  int64_t ptsToFrameId(double pts) const;
  double frameIdToPts(int64_t id) const;

  void streamOpen();
  void streamClose();
  void streamComponentOpen(int stream_index);
  void streamComponentClose(int stream_index);

  static int decode_interrupt_cb(void *ctx);

  void doReadInThread();

  void audioOpen(int64_t wanted_channel_layout, int wanted_nb_channels, int wanted_sample_rate);
  static void sdl_audio_callback(void *opaque, Uint8 *stream, int len);
  int audio_decode_frame();
  int synchronize_audio(int nb_samples);

  void configureAudioFilters(bool force_output_format);
  int configure_video_filters(AVFilterGraph *graph, const char *vfilters, AVFrame *frame);

  void onPacketDrained();

  int onVideoFrameDecodedReversed(AVFrame *frame, int serial);

  void startVideoDecodeThread();
  void startAudioDecodeThread();
  void startSubtitleDecodeThread();

  void startDataDecode();
  void stopDataDecode();
  int receiveDataPacket(AVPacket *pkt, int& pkt_serial);
  int dealWithDataPacket(const AVPacket *pkt, const int pkt_serial);

  int pushPacket(AVPacket* pkt, int specified_serial = -1);
  void newSerial();

  int queuePicture(AVFrame *src_frame, double pts, double duration, int64_t pos, int serial);

  void refreshLoopWaitEvent(MediaEvent *event);
  int handleEvent(const MediaEvent& event, int& quit);

  int getVideoFrame(AVFrame *frame, int& pkt_serial);

  void video_refresh(double *remaining_time);
  void video_image_display();

  void video_refresh_rewind(double *remaining_time);
  double computeVideoTargetDelayReversed(const Frame *lastvp, const Frame *vp) const;
  double vpDurationReversed(const Frame *vp, const Frame *nextvp) const;

  double vp_duration(const Frame *vp, const Frame *nextvp) const;
  double compute_target_delay(double delay) const;

  void stream_toggle_pause();

  void videoRefreshShowStatus(int64_t& last_time) const;

  void sendSeekRequest(SeekMethod req, int64_t pos, int64_t rel = 0);

  void updateVolume(int sign, double step);
  void setVolume(int val);
  void setMute(bool v);
  void toggleMute();
  int getVolume() const;
  bool isMuted() const;

  void change_speed(double speed);

  void step_to_next_frame();
  void step_to_prev_frame();
  void togglePause();
  void seek_chapter(int incr);

  bool videoPacketIsAddonData(AVCodecID codec_id, const AVPacket *pkt) const;

protected:
  //bool rewindMode() const { return speed_ < 0; }
  bool rewindMode() const { return rewind_; }

protected:
  Clock audclk;
  Clock vidclk;
  Clock extclk;

  bool realtime_{false};

  AVFormatContext *ic{nullptr};

  // seeking & speed
  SeekMethod seekMethod_{SEEK_METHOD_NONE};
  bool rewind_{false};
  int64_t seek_pos{0};
  int64_t seek_rel{0};
  int read_pause_return{0};
  double speed_{1.0};
  double prev_speed_{0};
  bool stepping_{false};

  std::deque<SimpleFrame> rewindBuffer_;
  int64_t frameRewindTarget_;
  int64_t rewindEofPts_{0};
  int64_t syncVideoPts_{-1};

  double max_frame_duration{0};      // maximum duration of a frame - above this, we consider the jump a timestamp discontinuity
  int last_video_stream{-1};
  int last_audio_stream{-1};
  int last_subtitle_stream{-1};
  int last_data_stream{-1};

  int audio_stream{-1};
  AVStream *audio_st{nullptr};
  PacketQueue audioPacketQueue_;
  FrameQueue sampleQueue_;
  Decoder audioDecoder_;

  int audio_hw_buf_size{0};
  uint8_t *audio_buf{nullptr};
  uint8_t *audio_buf1{nullptr};
  unsigned int audio_buf_size{0}; /* in bytes */
  unsigned int audio_buf1_size{0};
  int audio_buf_index{0}; /* in bytes */
  int audio_write_buf_size{0};
  bool muted_{false};
  struct AudioParams audio_src;

  struct SwrContext *swr_ctx{nullptr};

  double audio_clock{0};
  int audio_clock_serial{-1};
  double audio_diff_cum{0}; /* used for AV difference average computation */
  double audio_diff_avg_coef{0};
  double audio_diff_threshold{0};
  int audio_diff_avg_count{0};

  int64_t audio_callback_time{0};

#if defined(BUILD_WITH_AUDIO_FILTER) || defined(BUILD_WITH_VIDEO_FILTER)
    struct AudioParams audio_filter_src;
#endif
    struct AudioParams audio_tgt;

  int video_stream{-1};
  AVStream *video_st{nullptr};
  PacketQueue videoPacketQueue_;
  FrameQueue pictureQueue_;
  Decoder videoDecoder_;

  int subtitle_stream{-1};
  AVStream *subtitle_st{nullptr};
  PacketQueue subtitlePacketQueue_;
  FrameQueue subtitleQueue_;
  Decoder subtitleDecoder_;

  int data_stream{-1};
  AVStream *data_st{nullptr};
  PacketQueue dataPacketQueue_;
  std::thread data_tid_;

  double frame_last_returned_time{0};
  double frame_last_filter_delay{0};

  int paused{0};
  int last_paused{0};
  int queue_attachments_req{0};

  SDL_AudioDeviceID audio_dev{0};

#if defined(BUILD_WITH_AUDIO_FILTER) || defined(BUILD_WITH_VIDEO_FILTER)
    int vfilter_idx{0};
    AVFilterContext *in_video_filter{nullptr};   // the first filter in the video chain
    AVFilterContext *out_video_filter{nullptr};  // the last filter in the video chain
    AVFilterContext *in_audio_filter{nullptr};   // the first filter in the audio chain
    AVFilterContext *out_audio_filter{nullptr};  // the last filter in the audio chain
    AVFilterGraph *agraph{nullptr};              // audio filter graph
#endif

  int64_t duration_{AV_NOPTS_VALUE};
  int64_t start_time_{0};

  int videoSerial_{0};
  int audioSerial_{0};
  int dataSerial_{0};
  int subtitleSerial_{0};

  bool abort_reading_{false};
  bool eof_{false};
  std::thread read_tid_;
  std::condition_variable continue_read_thread_;
  std::mutex wait_mtx;

  EventQueue evq_;

  // video
  int frame_drops_early{0};
  int frame_drops_late{0};

  bool drop_frame_mode{false};

  AVRational data_time_base_{1, AV_TIME_BASE};
  AVRational video_time_base_{1, AV_TIME_BASE};
  AVRational video_frame_rate_{0};
  double frame_duration_{0};

  bool force_refresh_{false};

  double frame_timer_{0};

  ConverterContext yuv_ctx_;
  ConverterContext sub_yuv_ctx_;

public:
  OnStatus onStatus;
  OnMetaInfo onMetaInfo;
  OnStatics onStatics;
  OnClockUpdate onClockUpdate;
  OnIYUVDisplay onIYUVDisplay;
  OnAIData onAIData;
  OnLog onLog;

public:
  AVDictionary *swr_opts{nullptr};
  AVDictionary *sws_dict{nullptr};
  AVDictionary *format_opts{nullptr};
  AVDictionary *codec_opts{nullptr};

  bool audio_disable{false};
  bool subtitle_disable{false};
  bool data_disable{false};

  string wanted_stream_spec[AVMEDIA_TYPE_NB];

  int64_t start_time{AV_NOPTS_VALUE};
  int64_t duration{AV_NOPTS_VALUE};
  int seek_by_bytes{-1};
  float seek_interval{10};
  int audio_volume{100};

  AVInputFormat *iformat{nullptr};
  string filename;

  bool fast{false};
  bool genpts{false};
  int lowres{0};

  int decoder_reorder_pts{-1};
  int av_sync_type{AV_SYNC_AUDIO_MASTER};
  int framedrop{-1};
  int infinite_buffer{-1};

#if defined(BUILD_WITH_AUDIO_FILTER) || defined(BUILD_WITH_VIDEO_FILTER)
  vector<string> vfilters_list;
  string afilters;
  int filter_nbthreads{0};
#endif

  string audio_codec_name;
  string subtitle_codec_name;
  string video_codec_name;

  bool showStatus{false};
};

//
//
struct Detection_t {
  double pts;
};
