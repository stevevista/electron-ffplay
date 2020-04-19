/**
 * @file
 * portting from fftools/ffplay.c
 * Copyright (c) 2003 Fabrice Bellard
 */
extern "C" {
  #include "libavutil/intreadwrite.h"
}

#include "player.h"

/* Include only the enabled headers since some compilers (namely, Sun
   Studio) will not omit unused inline functions and create undefined
   references to libraries that are not being built. */
extern "C" {
#include "config.h"
#include "libavutil/log.h"
#include "libavutil/avstring.h"
#include "libavutil/eval.h"
#include "libavutil/pixdesc.h"
#include "libavutil/parseutils.h"
#include "libavutil/imgutils.h"
#include "libavformat/network.h"
#include "libavfilter/avfilter.h"
#include "libswscale/swscale.h"
#include "libavresample/avresample.h"
#include "libswresample/swresample.h"
}

#include <chrono>
using namespace std::chrono_literals;

void ff_init() {
  av_log_set_flags(AV_LOG_SKIP_REPEATED);
  // av_log_set_callback(log_callback_null);
  avformat_network_init();

  /* Try to work around an occasional ALSA buffer underflow issue when the
         * period size is NPOT due to ALSA resampling by forcing the buffer size. */
  if (!SDL_getenv("SDL_AUDIO_ALSA_SET_BUFFER_SIZE"))
    SDL_setenv("SDL_AUDIO_ALSA_SET_BUFFER_SIZE","1", 1);

  if (SDL_Init(SDL_INIT_AUDIO)) {
    throw runtime_error(string("Could not initialize SDL - ") + SDL_GetError());
  }
}

typedef struct OptionDef {
    const char *name;
    int flags;
#define HAS_ARG    0x0001
#define OPT_BOOL   0x0002
#define OPT_EXPERT 0x0004
#define OPT_VIDEO  0x0010
#define OPT_AUDIO  0x0020
#define OPT_INT    0x0080
#define OPT_FLOAT  0x0100
#define OPT_SUBTITLE 0x0200
#define OPT_INT64  0x0400
#define OPT_EXIT   0x0800
#define OPT_DATA   0x1000
#define OPT_PERFILE  0x2000     /* the option is per-file (currently ffmpeg-only).
                                   implied by OPT_OFFSET or OPT_SPEC */
#define OPT_OFFSET 0x4000       /* option is specified as an offset in a passed optctx */
#define OPT_TIME  0x10000
#define OPT_DOUBLE 0x20000
#define OPT_INPUT  0x40000
#define OPT_OUTPUT 0x80000
    int (*func_arg)(void *, const char *, const char *);
    const char *help;
    const char *argname;
} OptionDef;

static int64_t parse_time_or_die(const char *context, const char *timestr,
                          int is_duration)
{
    int64_t us;
    if (av_parse_time(&us, timestr, is_duration) < 0) {
        av_log(NULL, AV_LOG_FATAL, "Invalid %s specification for %s: %s\n",
               is_duration ? "duration" : "date", context, timestr);
    }
    return us;
}

static double parse_number_or_die(const char *context, const char *numstr, int type,
                           double min, double max)
{
    char *tail;
    const char *error;
    double d = av_strtod(numstr, &tail);
    if (*tail)
        error = "Expected number for %s but found: %s\n";
    else if (d < min || d > max)
        error = "The value for %s was %s which is not within %f - %f\n";
    else if (type == OPT_INT64 && (int64_t)d != d)
        error = "Expected int64 for %s but found %s\n";
    else if (type == OPT_INT && (int)d != d)
        error = "Expected int for %s but found %s\n";
    else
        return d;

    return 0;
}

static const AVOption *opt_find(void *obj, const char *name, const char *unit,
                            int opt_flags, int search_flags)
{
    const AVOption *o = av_opt_find(obj, name, unit, opt_flags, search_flags);
    if(o && !o->flags)
        return NULL;
    return o;
}

#define FLAGS (o->type == AV_OPT_TYPE_FLAGS && (arg[0]=='-' || arg[0]=='+')) ? AV_DICT_APPEND : 0
static int opt_default(void *optctx, const char *opt, const char *arg)
{
  auto ctx = (PlayBackContext*)optctx;
  const AVOption *o;
    int consumed = 0;
    char opt_stripped[128];
    const char *p;
    const AVClass *cc = avcodec_get_class(), *fc = avformat_get_class();
#if CONFIG_AVRESAMPLE
    const AVClass *rc = avresample_get_class();
#endif
#if CONFIG_SWSCALE
    const AVClass *sc = sws_get_class();
#endif
#if CONFIG_SWRESAMPLE
    const AVClass *swr_class = swr_get_class();
#endif

    if (!strcmp(opt, "debug") || !strcmp(opt, "fdebug"))
        av_log_set_level(AV_LOG_DEBUG);

    if (!(p = strchr(opt, ':')))
        p = opt + strlen(opt);
    av_strlcpy(opt_stripped, opt, FFMIN(sizeof(opt_stripped), p - opt + 1));

    if ((o = opt_find(&cc, opt_stripped, NULL, 0,
                         AV_OPT_SEARCH_CHILDREN | AV_OPT_SEARCH_FAKE_OBJ)) ||
        ((opt[0] == 'v' || opt[0] == 'a' || opt[0] == 's') &&
         (o = opt_find(&cc, opt + 1, NULL, 0, AV_OPT_SEARCH_FAKE_OBJ)))) {
        av_dict_set(&ctx->codec_opts, opt, arg, FLAGS);
        consumed = 1;
    }
    if ((o = opt_find(&fc, opt, NULL, 0,
                         AV_OPT_SEARCH_CHILDREN | AV_OPT_SEARCH_FAKE_OBJ))) {
        av_dict_set(&ctx->format_opts, opt, arg, FLAGS);
        if (consumed)
            av_log(NULL, AV_LOG_VERBOSE, "Routing option %s to both codec and muxer layer\n", opt);
        consumed = 1;
    }
#if CONFIG_SWSCALE
    if (!consumed && (o = opt_find(&sc, opt, NULL, 0,
                         AV_OPT_SEARCH_CHILDREN | AV_OPT_SEARCH_FAKE_OBJ))) {
        struct SwsContext *sws = sws_alloc_context();
        int ret = av_opt_set(sws, opt, arg, 0);
        sws_freeContext(sws);
        if (!strcmp(opt, "srcw") || !strcmp(opt, "srch") ||
            !strcmp(opt, "dstw") || !strcmp(opt, "dsth") ||
            !strcmp(opt, "src_format") || !strcmp(opt, "dst_format")) {
            av_log(NULL, AV_LOG_ERROR, "Directly using swscale dimensions/format options is not supported, please use the -s or -pix_fmt options\n");
            return AVERROR(EINVAL);
        }
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Error setting option %s.\n", opt);
            return ret;
        }

        av_dict_set(&ctx->sws_dict, opt, arg, FLAGS);

        consumed = 1;
    }
#else
    if (!consumed && !strcmp(opt, "sws_flags")) {
        av_log(NULL, AV_LOG_WARNING, "Ignoring %s %s, due to disabled swscale\n", opt, arg);
        consumed = 1;
    }
#endif
#if CONFIG_SWRESAMPLE
    if (!consumed && (o=opt_find(&swr_class, opt, NULL, 0,
                                    AV_OPT_SEARCH_CHILDREN | AV_OPT_SEARCH_FAKE_OBJ))) {
        struct SwrContext *swr = swr_alloc();
        int ret = av_opt_set(swr, opt, arg, 0);
        swr_free(&swr);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Error setting option %s.\n", opt);
            return ret;
        }
        av_dict_set(&ctx->swr_opts, opt, arg, FLAGS);
        consumed = 1;
    }
#endif
#if CONFIG_AVRESAMPLE
    if ((o=opt_find(&rc, opt, NULL, 0,
                       AV_OPT_SEARCH_CHILDREN | AV_OPT_SEARCH_FAKE_OBJ))) {
        av_dict_set(&resample_opts, opt, arg, FLAGS);
        consumed = 1;
    }
#endif

    if (consumed)
        return 0;
    return AVERROR_OPTION_NOT_FOUND;
}

static int opt_loglevel(void *optctx, const char *opt, const char *arg)
{
    const struct { const char *name; int level; } log_levels[] = {
        { "quiet"  , AV_LOG_QUIET   },
        { "panic"  , AV_LOG_PANIC   },
        { "fatal"  , AV_LOG_FATAL   },
        { "error"  , AV_LOG_ERROR   },
        { "warning", AV_LOG_WARNING },
        { "info"   , AV_LOG_INFO    },
        { "verbose", AV_LOG_VERBOSE },
        { "debug"  , AV_LOG_DEBUG   },
        { "trace"  , AV_LOG_TRACE   },
    };
    const char *token;
    char *tail;
    int flags = av_log_get_flags();
    int level = av_log_get_level();
    int cmd, i = 0;

    while (*arg) {
        token = arg;
        if (*token == '+' || *token == '-') {
            cmd = *token++;
        } else {
            cmd = 0;
        }
        if (!i && !cmd) {
            flags = 0;  /* missing relative prefix, build absolute value */
        }
        if (!strncmp(token, "repeat", 6)) {
            if (cmd == '-') {
                flags |= AV_LOG_SKIP_REPEATED;
            } else {
                flags &= ~AV_LOG_SKIP_REPEATED;
            }
            arg = token + 6;
        } else if (!strncmp(token, "level", 5)) {
            if (cmd == '-') {
                flags &= ~AV_LOG_PRINT_LEVEL;
            } else {
                flags |= AV_LOG_PRINT_LEVEL;
            }
            arg = token + 5;
        } else {
            break;
        }
        i++;
    }
    if (!*arg) {
        goto end;
    } else if (*arg == '+') {
        arg++;
    } else if (!i) {
        flags = av_log_get_flags();  /* level value without prefix, reset flags */
    }

    for (i = 0; i < FF_ARRAY_ELEMS(log_levels); i++) {
        if (!strcmp(log_levels[i].name, arg)) {
            level = log_levels[i].level;
            goto end;
        }
    }

    level = strtol(arg, &tail, 10);
    if (*tail) {
      throw runtime_error(string("Invalid loglevel ") + log_levels[0].name);
    }

end:
    av_log_set_flags(flags);
    av_log_set_level(level);
    return 0;
}

static int opt_max_alloc(void *optctx, const char *opt, const char *arg)
{
    char *tail;
    size_t max;

    max = strtol(arg, &tail, 10);
    if (*tail) {
      throw runtime_error(string("Invalid max_alloc ") + arg);
    }
    av_max_alloc(max);
    return 0;
}

static int opt_cpuflags(void *optctx, const char *opt, const char *arg)
{
    int ret;
    unsigned flags = av_get_cpu_flags();

    if ((ret = av_parse_cpu_caps(&flags, arg)) < 0)
        return ret;

    av_force_cpu_flags(flags);
    return 0;
}

static int opt_frame_size(void *optctx, const char *opt, const char *arg)
{
  av_log(NULL, AV_LOG_WARNING, "Option -s is deprecated, use -video_size.\n");
  return opt_default(NULL, "video_size", arg);
}

static int opt_audio_disable(void *optctx, const char *opt, const char *arg)
{
  auto ctx = (PlayBackContext*)optctx;
  ctx->audio_disable = true;
  return 0;
}

static int opt_subtitle_disable(void *optctx, const char *opt, const char *arg)
{
  auto ctx = (PlayBackContext*)optctx;
  ctx->subtitle_disable = true;
  return 0;
}

static int opt_data_disable(void *optctx, const char *opt, const char *arg)
{
  auto ctx = (PlayBackContext*)optctx;
  ctx->data_disable = true;
  return 0;
}

static int opt_ast(void *optctx, const char *opt, const char *arg)
{
  auto ctx = (PlayBackContext*)optctx;
  ctx->wanted_stream_spec[AVMEDIA_TYPE_AUDIO] = arg;
  return 0;
}

static int opt_vst(void *optctx, const char *opt, const char *arg)
{
  auto ctx = (PlayBackContext*)optctx;
  ctx->wanted_stream_spec[AVMEDIA_TYPE_VIDEO] = arg;
  return 0;
}

static int opt_sst(void *optctx, const char *opt, const char *arg)
{
  auto ctx = (PlayBackContext*)optctx;
  ctx->wanted_stream_spec[AVMEDIA_TYPE_SUBTITLE] = arg;
  return 0;
}

static int opt_seek(void *optctx, const char *opt, const char *arg)
{
  auto ctx = (PlayBackContext*)optctx;
  ctx->start_time = parse_time_or_die(opt, arg, 1);
  return 0;
}

static int opt_duration(void *optctx, const char *opt, const char *arg)
{
  auto ctx = (PlayBackContext*)optctx;
  ctx->duration = parse_time_or_die(opt, arg, 1);
  return 0;
}

static int opt_seek_by_bytes(void *optctx, const char *opt, const char *arg)
{
  auto ctx = (PlayBackContext*)optctx;
  ctx->seek_by_bytes = static_cast<int>(parse_number_or_die(opt, arg, OPT_INT64, INT_MIN, INT_MAX));
  return 0;
}

static int opt_seek_interval(void *optctx, const char *opt, const char *arg)
{
  auto ctx = (PlayBackContext*)optctx;
  ctx->seek_interval = static_cast<float>(parse_number_or_die(opt, arg, OPT_FLOAT, -INFINITY, INFINITY));
  return 0;
}

static int opt_volume(void *optctx, const char *opt, const char *arg)
{
  auto ctx = (PlayBackContext*)optctx;
  ctx->audio_volume = static_cast<int>(parse_number_or_die(opt, arg, OPT_INT64, INT_MIN, INT_MAX));
  return 0;
}

static int opt_format(void *optctx, const char *opt, const char *arg)
{
  auto ctx = (PlayBackContext*)optctx;
  ctx->iformat = av_find_input_format(arg);
  if (!ctx->iformat) {
    throw runtime_error(string("Unknown input format: ") + arg);
  }
  return 0;
}

static int opt_fast(void *optctx, const char *opt, const char *arg)
{
  auto ctx = (PlayBackContext*)optctx;
  ctx->fast = true;
  return 0;
}

static int opt_genpts(void *optctx, const char *opt, const char *arg)
{
  auto ctx = (PlayBackContext*)optctx;
  ctx->genpts = true;
  return 0;
}

static int opt_drp(void *optctx, const char *opt, const char *arg)
{
  auto ctx = (PlayBackContext*)optctx;
  ctx->decoder_reorder_pts = static_cast<int>(parse_number_or_die(opt, arg, OPT_INT64, INT_MIN, INT_MAX));
  return 0;
}

static int opt_lowres(void *optctx, const char *opt, const char *arg)
{
  auto ctx = (PlayBackContext*)optctx;
  ctx->lowres = static_cast<int>(parse_number_or_die(opt, arg, OPT_INT64, INT_MIN, INT_MAX));
  return 0;
}

static int opt_sync(void *optctx, const char *opt, const char *arg)
{
  auto ctx = (PlayBackContext*)optctx;
    if (!strcmp(arg, "audio"))
      ctx->av_sync_type = AV_SYNC_AUDIO_MASTER;
    else if (!strcmp(arg, "video"))
      ctx->av_sync_type = AV_SYNC_VIDEO_MASTER;
    else if (!strcmp(arg, "ext"))
      ctx->av_sync_type = AV_SYNC_EXTERNAL_CLOCK;
    else {
      throw runtime_error(string("Unknown sync type: %s\n") + arg);
    }
    return 0;
}

static int opt_framedrop(void *optctx, const char *opt, const char *arg)
{
  auto ctx = (PlayBackContext*)optctx;
  ctx->framedrop = 1;
  return 0;
}

static int opt_infbuf(void *optctx, const char *opt, const char *arg)
{
  auto ctx = (PlayBackContext*)optctx;
  ctx->infinite_buffer = 1;
  return 0;
}

#if defined(BUILD_WITH_AUDIO_FILTER) || defined(BUILD_WITH_VIDEO_FILTER)
static int opt_add_vfilter(void *optctx, const char *opt, const char *arg)
{
  auto ctx = (PlayBackContext*)optctx;
  ctx->vfilters_list.push_back(arg);
  return 0;
}

static int opt_afilters(void *optctx, const char *opt, const char *arg)
{
  auto ctx = (PlayBackContext*)optctx;
  ctx->afilters = arg;
  return 0;
}

static int opt_filter_nbthreads(void *optctx, const char *opt, const char *arg)
{
  auto ctx = (PlayBackContext*)optctx;
  ctx->filter_nbthreads = static_cast<int>(parse_number_or_die(opt, arg, OPT_INT64, INT_MIN, INT_MAX));
  return 0;
}
#endif

static int opt_dummy(void *optctx, const char *opt, const char *arg)
{
  return 0;
}

static int opt_codec(void *optctx, const char *opt, const char *arg)
{
  auto ctx = (PlayBackContext*)optctx;
   const char *spec = strchr(opt, ':');
   if (!spec) {
       av_log(NULL, AV_LOG_ERROR,
              "No media specifier was specified in '%s' in option '%s'\n",
               arg, opt);
       return AVERROR(EINVAL);
   }
   spec++;
   switch (spec[0]) {
   case 'a' :    ctx->audio_codec_name = arg; break;
   case 's' : ctx->subtitle_codec_name = arg; break;
   case 'v' :    ctx->video_codec_name = arg; break;
   default:
       av_log(NULL, AV_LOG_ERROR,
              "Invalid media specifier '%s' in option '%s'\n", spec, opt);
       return AVERROR(EINVAL);
   }
   return 0;
}

static int opt_acodec(void *optctx, const char *opt, const char *arg)
{
  auto ctx = (PlayBackContext*)optctx;
  ctx->audio_codec_name = arg;
  return 0;
}

static int opt_scodec(void *optctx, const char *opt, const char *arg)
{
  auto ctx = (PlayBackContext*)optctx;
  ctx->subtitle_codec_name = arg;
  return 0;
}

static int opt_vcodec(void *optctx, const char *opt, const char *arg)
{
  auto ctx = (PlayBackContext*)optctx;
  ctx->video_codec_name = arg;
  return 0;
}

static void opt_input_file(void *optctx, const char *filename)
{
  auto ctx = (PlayBackContext*)optctx;
  ctx->filename = filename;
}

static int opt_show_status(void *optctx, const char *opt, const char *arg)
{
  auto ctx = (PlayBackContext*)optctx;
  ctx->showStatus = true;
  return 0;
}

static const OptionDef options[] = {
    { "loglevel",    HAS_ARG,              opt_loglevel,          "set logging level", "loglevel" },
    { "v",           HAS_ARG,              opt_loglevel,          "set logging level", "loglevel" },
    { "max_alloc",   HAS_ARG,              opt_max_alloc,         "set maximum size of a single allocated block", "bytes" },
    { "cpuflags",    HAS_ARG | OPT_EXPERT, opt_cpuflags,          "force specific cpu flags", "flags" },
    { "s",           HAS_ARG | OPT_VIDEO,  opt_frame_size,        "set frame size (WxH or abbreviation)", "size" },
    { "an",          OPT_BOOL,             opt_audio_disable,     "disable audio" },
    { "sn",          OPT_BOOL,             opt_subtitle_disable,  "disable subtitling" },
    { "dn",          OPT_BOOL,             opt_data_disable,      "disable data stream" },
    { "ast",         HAS_ARG | OPT_EXPERT, opt_ast,               "select desired audio stream", "stream_specifier" },
    { "vst",         HAS_ARG | OPT_EXPERT, opt_vst,               "select desired video stream", "stream_specifier" },
    { "sst",         HAS_ARG | OPT_EXPERT, opt_sst,               "select desired subtitle stream", "stream_specifier" },
    { "ss",          HAS_ARG,              opt_seek,              "seek to a given position in seconds", "pos" },
    { "t",           HAS_ARG,              opt_duration,          "play  \"duration\" seconds of audio/video", "duration" },
    { "bytes",       HAS_ARG,              opt_seek_by_bytes,     "seek by bytes 0=off 1=on -1=auto", "val" },
    { "seek_interval", HAS_ARG,            opt_seek_interval,     "set seek interval for left/right keys, in seconds", "seconds" },
    { "volume",      HAS_ARG,              opt_volume,            "set startup volume 0=min 100=max", "volume" },
    { "f",           HAS_ARG,              opt_format,            "force format", "fmt" },
    { "fast",        OPT_BOOL | OPT_EXPERT,opt_fast,              "non spec compliant optimizations", "" },
    { "genpts",      OPT_BOOL | OPT_EXPERT,opt_genpts,            "generate pts", "" },
    { "drp",         HAS_ARG | OPT_EXPERT, opt_drp,               "let decoder reorder pts 0=off 1=on -1=auto", ""},
    { "lowres",      HAS_ARG | OPT_EXPERT, opt_lowres,            "", "" },
    { "sync",        HAS_ARG | OPT_EXPERT, opt_sync,              "set audio-video sync. type (type=audio/video/ext)", "type" },
    { "framedrop",   OPT_BOOL | OPT_EXPERT, opt_framedrop,        "drop frames when cpu is too slow", "" },
    { "infbuf",      OPT_BOOL | OPT_EXPERT, opt_infbuf,           "don't limit the input buffer size (useful with realtime streams)", "" },
#if defined(BUILD_WITH_AUDIO_FILTER) || defined(BUILD_WITH_VIDEO_FILTER)
    { "vf",          OPT_EXPERT | HAS_ARG, opt_add_vfilter,       "set video filters", "filter_graph" },
    { "af",          HAS_ARG,              opt_afilters,          "set audio filters", "filter_graph" },
    { "filter_threads", HAS_ARG | OPT_INT | OPT_EXPERT, opt_filter_nbthreads, "number of filter threads per graph" },
#endif
    { "i",           OPT_BOOL,             opt_dummy,             "read specified file", "input_file"},
    { "default",     HAS_ARG | OPT_AUDIO | OPT_VIDEO | OPT_EXPERT, opt_default, "generic catch all option", "" },
    { "codec",       HAS_ARG,              opt_codec,             "force decoder", "decoder_name" },
    { "acodec",      HAS_ARG | OPT_EXPERT, opt_acodec,            "force audio decoder",    "decoder_name" },
    { "scodec",      HAS_ARG | OPT_EXPERT, opt_scodec,            "force subtitle decoder", "decoder_name" },
    { "vcodec",      HAS_ARG | OPT_EXPERT, opt_vcodec,            "force video decoder",    "decoder_name" },
    { "stats",       OPT_BOOL | OPT_EXPERT,opt_show_status,       "show status", "" },
    { NULL, },
};

static const OptionDef *find_option(const OptionDef *po, const char *name)
{
    const char *p = strchr(name, ':');
    int len = p ? p - name : strlen(name);

    while (po->name) {
        if (!strncmp(name, po->name, len) && strlen(po->name) == len)
            break;
        po++;
    }
    return po;
}

static int parse_option(void *optctx, const char *opt, const char *arg,
                 const OptionDef *options)
{
    const OptionDef *po;
    int ret;

    po = find_option(options, opt);
    if (!po->name && opt[0] == 'n' && opt[1] == 'o') {
        /* handle 'no' bool option */
        po = find_option(options, opt + 2);
        if ((po->name && (po->flags & OPT_BOOL)))
            arg = "0";
    } else if (po->flags & OPT_BOOL)
        arg = "1";

    if (!po->name)
        po = find_option(options, "default");
    if (!po->name) {
        av_log(NULL, AV_LOG_ERROR, "Unrecognized option '%s'\n", opt);
        return AVERROR(EINVAL);
    }
    if (po->flags & HAS_ARG && !arg) {
        av_log(NULL, AV_LOG_ERROR, "Missing argument for option '%s'\n", opt);
        return AVERROR(EINVAL);
    }

    ret = po->func_arg(optctx, opt, arg);
    if (ret < 0)
        return ret;

    return !!(po->flags & HAS_ARG);
}

static void parse_options(void *optctx, int argc, char **argv, const OptionDef *options,
                   void (*parse_arg_function)(void *, const char*))
{
    const char *opt;
    int optindex, handleoptions = 1, ret;

    /* parse options */
    optindex = 1;
    while (optindex < argc) {
        opt = argv[optindex++];

        if (handleoptions && opt[0] == '-' && opt[1] != '\0') {
            if (opt[1] == '-' && opt[2] == '\0') {
                handleoptions = 0;
                continue;
            }
            opt++;

            if ((ret = parse_option(optctx, opt, argv[optindex], options)) < 0)
                return;
            optindex += ret;
        } else {
            if (parse_arg_function)
              parse_arg_function(optctx, opt);
        }
    }
}

#define MAX_QUEUE_SIZE (15 * 1024 * 1024)
#define MIN_FRAMES 25
#define EXTERNAL_CLOCK_MIN_FRAMES 2
#define EXTERNAL_CLOCK_MAX_FRAMES 10

/* Minimum SDL audio buffer size, in samples. */
#define SDL_AUDIO_MIN_BUFFER_SIZE 512
/* Calculate actual buffer size keeping in mind not cause too frequent audio callbacks */
#define SDL_AUDIO_MAX_CALLBACKS_PER_SEC 30

/* Step size for volume control in dB */
#define SDL_VOLUME_STEP (0.75)

/* no AV sync correction is done if below the minimum AV sync threshold */
#define AV_SYNC_THRESHOLD_MIN 0.04
/* AV sync correction is done if above the maximum AV sync threshold */
#define AV_SYNC_THRESHOLD_MAX 0.1
/* If a frame duration is longer than this, it will not be duplicated to compensate AV sync */
#define AV_SYNC_FRAMEDUP_THRESHOLD 0.1

/* maximum audio speed change to get correct sync */
#define SAMPLE_CORRECTION_PERCENT_MAX 10

/* external clock speed adjustment constants for realtime sources based on buffer fullness */
#define EXTERNAL_CLOCK_SPEED_MIN  0.900
#define EXTERNAL_CLOCK_SPEED_MAX  1.010
#define EXTERNAL_CLOCK_SPEED_STEP 0.001

/* we use about AUDIO_DIFF_AVG_NB A-V differences to make the average */
#define AUDIO_DIFF_AVG_NB   20

/* polls for possible required screen refresh at least this often, should be less than 1/fps */
#define REFRESH_RATE 0.01

#define USE_ONEPASS_SUBTITLE_RENDER 1

static AVPacket special_flush_pkt{0};

class GlobalInit { 
public:
  GlobalInit() {
    av_init_packet(&special_flush_pkt);
  	special_flush_pkt.data = (uint8_t *)&special_flush_pkt;
  }
};

static GlobalInit __init;

static constexpr int SERIAL_HELPER_PACKET = 999999;

///
Clock::Clock(int *queue_serial)
{
  this->speed = 1.0;
  this->paused = 0;
  this->queue_serial = queue_serial;
  set_clock(NAN, -1);
}

double Clock::time_passed() const {
  double time = av_gettime_relative() / 1000000.0;
  return (time - last_updated) * speed;
}

void Clock::update() {
  double time = av_gettime_relative() / 1000000.0;
  this->last_updated = time;
  this->pts_drift = this->pts - time;
}

double Clock::get_clock() const
{
    if (*this->queue_serial != this->serial)
        return NAN;
    if (this->paused) {
        return this->pts;
    } else {
        double time = av_gettime_relative() / 1000000.0;
        return this->pts_drift + time - (time - this->last_updated) * (1.0 - this->speed);
    }
}

void Clock::set_clock_at(double pts, int serial, double time)
{
    this->pts = pts;
    this->last_updated = time;
    this->pts_drift = this->pts - time;
    this->serial = serial;
}

void Clock::set_clock(double pts, int serial)
{
    double time = av_gettime_relative() / 1000000.0;
    set_clock_at(pts, serial, time);
}

void Clock::sync_clock_to_slave(const Clock *slave, double threshold)
{
    double clock = get_clock();
    double slave_clock = slave->get_clock();
    if (!isnan(slave_clock) && (isnan(clock) || speed < 0 || fabs(clock - slave_clock) > threshold))
      set_clock(slave_clock, slave->serial);
}

void Clock::set_clock_speed(double speed)
{
  set_clock(get_clock(), this->serial);
  this->speed = speed;
}

///
PacketQueue::PacketQueue(int& serial)
: serial_(serial)
{
}

PacketQueue::~PacketQueue() {
  flush();
}

int PacketQueue::put_private(std::unique_lock<std::mutex>& lk, AVPacket *pkt, int specified_serial) {

  if (abort_request_)
    return -1;

  if (pkt == &special_flush_pkt)
		serial_++;

  if (specified_serial < 0)
    specified_serial = serial_;
	pkts_.emplace(std::pair<int, AVPacket>{ specified_serial, *pkt });

	this->nb_packets++;
	size_ += pkt->size + sizeof(*pkt);
	duration_ += pkt->duration;
    /* XXX: should duplicate packet data in DV case */
  lk.unlock();
	this->cond.notify_one();
  return 0;
}

int PacketQueue::put(AVPacket *pkt, int specified_serial)
{
  std::unique_lock<std::mutex> lk(mtx);
  int ret = put_private(lk, pkt, specified_serial);

  if (pkt != &special_flush_pkt && ret < 0)
    av_packet_unref(pkt);

  return ret;
}

int PacketQueue::put_nullpacket(int stream_index)
{
  AVPacket pkt1, *pkt = &pkt1;
  av_init_packet(pkt);
  pkt->data = NULL;
  pkt->size = 0;
  pkt->stream_index = stream_index;
  return put(pkt);
}

void PacketQueue::flush() {
  std::lock_guard<std::mutex> lk(mtx);
	while (!pkts_.empty()) {
		av_packet_unref(&pkts_.front().second);
		pkts_.pop();
	}
    
  nb_packets = 0;
  size_ = 0;
  duration_ = 0;
}

void PacketQueue::nextSerial() {
  if (abort_request_) {
    return;
  }
  flush();
  start();
}

void PacketQueue::abort()
{
	std::unique_lock<std::mutex> lk(mtx);
  abort_request_ = true;
	lk.unlock();
	cond.notify_one();

  flush();
}

void PacketQueue::start()
{
  std::unique_lock<std::mutex> lk(mtx);
  abort_request_ = false;
  put_private(lk, &special_flush_pkt, -1);
}

/* return < 0 if aborted, 0 if no packet and > 0 if packet.  */
int PacketQueue::get(AVPacket *pkt, int *serial)
{
  std::unique_lock<std::mutex> lk(mtx);
  cond.wait(lk, [this] {
    return abort_request_ || !pkts_.empty();
  });
    
  if (abort_request_) {
    return -1;
  }

  *serial = pkts_.front().first;
  *pkt = pkts_.front().second;
	pkts_.pop();
  this->nb_packets--;
  size_ -= pkt->size + sizeof(*pkt);
  duration_ -= pkt->duration;
  return 1;
}

bool PacketQueue::has_enough_packets(const AVRational& time_base) const {
  return abort_request_ ||
           nb_packets > MIN_FRAMES && (!duration_ || av_q2d(time_base) * duration_ > 1.0);
}

///
FrameQueue::FrameQueue(const int& serial, int max_size, bool keep_last)
: serial_(serial)
, max_size_(FFMIN(max_size, FRAME_QUEUE_SIZE))
, keep_last_(keep_last)
{
  for (int i = 0; i < max_size_; i++)
    this->queue[i].frame = av_frame_alloc();
}

FrameQueue::~FrameQueue() {
  for (int i = 0; i < max_size_; i++) {
    Frame *vp = &queue[i];
    av_frame_unref(vp->frame);
    avsubtitle_free(&vp->sub);
    av_frame_free(&vp->frame);
  }
}

void FrameQueue::abort() {
  std::unique_lock<std::mutex> lk(mtx);
  abort_request_ = true;
  lk.unlock();
  cond.notify_one();
}

Frame *FrameQueue::peek_readable()
{
    /* wait until we have a readable a new frame */
	std::unique_lock<std::mutex> lk(mtx);
	cond.wait(lk, [this] {
		return this->size - this->rindex_shown > 0 || abort_request_;
	});

  if (abort_request_)
    return nullptr;

  lk.unlock();

  return &this->queue[(this->rindex + this->rindex_shown) % this->max_size_];
}

Frame *FrameQueue::peek_writable()
{
    /* wait until we have space to put a new frame */
	std::unique_lock<std::mutex> lk(mtx);
	cond.wait(lk, [this] {
		return this->size < this->max_size_ || abort_request_;
	});

  if (abort_request_)
    return nullptr;

  lk.unlock();

  return &this->queue[this->windex];
}

void FrameQueue::next()
{
  if (keep_last_ && !this->rindex_shown) {
    this->rindex_shown = 1;
    return;
  }
  av_frame_unref(this->queue[this->rindex].frame);
  avsubtitle_free(&this->queue[this->rindex].sub);
  if (++this->rindex == max_size_)
    this->rindex = 0;

  {
    std::lock_guard<std::mutex> lk(mtx);
    this->size--;
  }

	cond.notify_one();
}

void FrameQueue::push()
{
  if (++this->windex == max_size_)
    this->windex = 0;

  {
    std::lock_guard<std::mutex> lk(mtx);
    this->size++;
  }

  cond.notify_one();
}

Frame *FrameQueue::peek()
{
  return &this->queue[(this->rindex + this->rindex_shown) % max_size_];
}

Frame *FrameQueue::peek_next()
{
  return &this->queue[(this->rindex + this->rindex_shown + 1) % max_size_];
}

Frame *FrameQueue::peek_last()
{
  return &this->queue[this->rindex];
}

/* return last shown position */
int64_t FrameQueue::lastShownPosition() const
{
  auto fp = &this->queue[this->rindex];
  if (this->rindex_shown && fp->serial == serial_)
    return fp->pos;
  else
    return -1;
}

///

void Decoder::abort() {
  abort_request_ = true;
  if (tid_.joinable()) {
    tid_.join();
  }
}

bool Decoder::finished() const {
  return finished_ == serial_;
}

Decoder::~Decoder() {
  destroy();
}

Decoder::Decoder(const int& serial)
: serial_(serial)
{
}

void Decoder::init(AVCodecContext *avctx) {
  finished_ = 0;
  packet_pending_ = false;
  this->start_pts_tb = {0};
  this->next_pts = 0;
  this->next_pts_tb = {0};

  avctx_ = avctx;
  this->start_pts = AV_NOPTS_VALUE;
  abort_request_ = false;
}

void Decoder::destroy() {
  av_packet_unref(&pending_pkt_);
  avcodec_free_context(&avctx_);
}

int Decoder::decodeFrame(PacketGetter packet_getter, AVFrame *frame, AVSubtitle *sub, int& pkt_serial) {
  int ret = AVERROR(EAGAIN);
  for (;;) {
    AVPacket pkt;

    if (serial_ == pkt_serial || SERIAL_HELPER_PACKET == pkt_serial) {
      do {
        if (abort_request_)
          return -1;

        switch (avctx_->codec_type) {
          case AVMEDIA_TYPE_VIDEO:
            ret = avcodec_receive_frame(avctx_, frame);
            break;
          case AVMEDIA_TYPE_AUDIO:
            ret = avcodec_receive_frame(avctx_, frame);
            if (ret >= 0) {
              AVRational tb = AVRational{1, frame->sample_rate};
              if (frame->pts != AV_NOPTS_VALUE)
                frame->pts = av_rescale_q(frame->pts, avctx_->pkt_timebase, tb);
              else if (next_pts != AV_NOPTS_VALUE)
                frame->pts = av_rescale_q(next_pts, next_pts_tb, tb);
              if (frame->pts != AV_NOPTS_VALUE) {
                next_pts = frame->pts + frame->nb_samples;
                next_pts_tb = tb;
              }
            }
            break;
        }

        if (ret == AVERROR_EOF) {
          finished_ = pkt_serial;
          avcodec_flush_buffers(avctx_);
          return 0;
        }
        if (ret >= 0)
          return 1;
    
      } while (ret != AVERROR(EAGAIN));
    }

    do {
      if (packet_pending_) {
        av_packet_move_ref(&pkt, &pending_pkt_);
        packet_pending_ = false;
      } else {
        if (packet_getter(avctx_->codec_type, avctx_->codec_id, &pkt, &pkt_serial) < 0)
          return -1; // failed
      }

      if (serial_ == pkt_serial || SERIAL_HELPER_PACKET == pkt_serial)
        break;
      av_packet_unref(&pkt);
    } while (true);

    if (pkt.data == special_flush_pkt.data) {
      avcodec_flush_buffers(avctx_);
      finished_ = 0;
      next_pts = start_pts;
      next_pts_tb = start_pts_tb;
    } else {
      if (avctx_->codec_type == AVMEDIA_TYPE_SUBTITLE) {
        int got_frame = 0;
        ret = avcodec_decode_subtitle2(avctx_, sub, &got_frame, &pkt);
        if (ret < 0) {
          ret = AVERROR(EAGAIN);
        } else {
          if (got_frame && !pkt.data) {
            packet_pending_ = true;
            av_packet_move_ref(&pending_pkt_, &pkt);
          }
          ret = got_frame ? 0 : (pkt.data ? AVERROR(EAGAIN) : AVERROR_EOF);
        }
      } else {
        if (avcodec_send_packet(avctx_, &pkt) == AVERROR(EAGAIN)) {
          av_log(avctx_, AV_LOG_ERROR, "Receive_frame and send_packet both returned EAGAIN, which is an API violation.\n");
          packet_pending_ = true;
          av_packet_move_ref(&pending_pkt_, &pkt);
        }
      }
      av_packet_unref(&pkt);
    }
  }
}

///
SyncDecoder::SyncDecoder()
: Decoder(placeHodler_)
{
  frame_ = av_frame_alloc();
}

SyncDecoder::~SyncDecoder() {
  av_frame_free(&frame_);
}

int SyncDecoder::decodeBuffer(const uint8_t* data, int size, AVFrame **frame_out) {
  int pkt_serial = -1; // force get packet !
  int ret = decodeFrame([this, data, size](AVMediaType, AVCodecID, AVPacket *pkt, int *serial) {
    av_init_packet(pkt);
    pkt->data = (uint8_t*)data;
    pkt->size = size;
    *serial = placeHodler_;
    return 0;
  }, frame_, nullptr, pkt_serial);

  if (ret > 0) {
    if (frame_out) {
      *frame_out = frame_;
    }
  }

  return ret;
}

SyncDecoder* SyncDecoder::open(const char* codec_name, const AVCodecParameters *par) {
  int ret = 0;

  auto codec = avcodec_find_decoder_by_name(codec_name);
  if (!codec) {
    av_log(NULL, AV_LOG_WARNING, "No codec could be found with name '%s'\n", codec_name);
    return nullptr;
  }

  auto avctx = avcodec_alloc_context3(codec);
  if (!avctx)
    return nullptr;

  if (par) {
    (const_cast<AVCodecParameters*>(par))->codec_type = avctx->codec_type;
    (const_cast<AVCodecParameters*>(par))->codec_id = avctx->codec_id;
    (const_cast<AVCodecParameters*>(par))->codec_tag = avctx->codec_tag;

		avcodec_parameters_to_context(avctx, par);
  }

  avctx->pkt_timebase = {1, AV_TIME_BASE};

  if ((ret = avcodec_open2(avctx, codec, NULL)) < 0) {
    avcodec_free_context(&avctx);
    return nullptr;
  }

  auto decoder = new SyncDecoder();
  decoder->init(avctx);
  return decoder;
}

///
void EventQueue::set(MediaEvent *evt) {
	std::unique_lock<std::mutex> lk(mtx);
	if (evt->event == MEDIA_CMD_QUIT) {
		quit_ = true;
		return;
	}
	evts_.emplace(*evt);
}

bool EventQueue::get(MediaEvent *evt) {
	std::unique_lock<std::mutex> lk(mtx);

	if (quit_) {
		evt->event = MEDIA_CMD_QUIT;
		return true;
	}
	else {
		if (evts_.empty()) {
			return false;
		}
		*evt = evts_.front();
		evts_.pop();
		return true;
	}
}

///
ConverterContext::ConverterContext(AVPixelFormat fmt)
: target_fmt(fmt)
{
  frame_ = av_frame_alloc();
}

ConverterContext::~ConverterContext() {
  if (convert_ctx)
    sws_freeContext(convert_ctx);
  av_frame_unref(frame_);
  av_frame_free(&frame_);
  av_freep(&buffer);
}

int ConverterContext::convert(AVFrame *src_frame) {

  return convert(src_frame->format, src_frame->width, src_frame->height, (const uint8_t * const *)src_frame->data, src_frame->linesize);
}

int ConverterContext::convert(int src_format, int src_width, int src_height, const uint8_t * const*pixels, int* pitch) {

  int buffer_size = av_image_fill_arrays(frame_->data, frame_->linesize, buffer, target_fmt,
                        src_width, src_height, 1);
  if (buffer_size > buffer_size_) {
    av_freep(&buffer);
    buffer = (uint8_t*)av_malloc(buffer_size);
    buffer_size_ = buffer_size;

    av_image_fill_arrays(frame_->data, frame_->linesize, buffer, target_fmt,
                        src_width, src_height, 1);
  }

  convert_ctx = sws_getCachedContext(convert_ctx,
                        src_width, src_height, (AVPixelFormat)src_format, src_width, src_height,
                        target_fmt, SWS_BICUBIC, NULL, NULL, NULL);

  if (convert_ctx) {
    frame_->format = target_fmt;
    frame_->width = src_width;
    frame_->height = src_height;
    int r = sws_scale(convert_ctx, pixels, pitch,
                                        0, src_height, frame_->data, frame_->linesize);
    if (r <= 0) {
            return -1;
    }
    return 0;
  }
  return -1;
}

///
PlayBackContext::PlayBackContext()
: audclk(&audioSerial_)
, vidclk(&videoSerial_)
, extclk(&extclk.serial)
, audioPacketQueue_(audioSerial_)
, sampleQueue_(audioSerial_, SAMPLE_QUEUE_SIZE, true)
, audioDecoder_(audioSerial_)
, videoPacketQueue_(videoSerial_)
, pictureQueue_(videoSerial_, VIDEO_PICTURE_QUEUE_SIZE, true)
, videoDecoder_(videoSerial_)
, subtitlePacketQueue_(subtitleSerial_)
, subtitleQueue_(subtitleSerial_, SUBPICTURE_QUEUE_SIZE, false)
, subtitleDecoder_(subtitleSerial_)
, dataPacketQueue_(dataSerial_)
, yuv_ctx_(AV_PIX_FMT_YUV420P)
, sub_yuv_ctx_(AV_PIX_FMT_YUV420P)
{
  // default settings
  av_dict_set(&this->sws_dict, "flags", "bicubic", 0);
}

PlayBackContext::~PlayBackContext() {
  streamClose();

  av_dict_free(&this->sws_dict);
  av_dict_free(&this->swr_opts);
  av_dict_free(&this->format_opts);
  av_dict_free(&this->codec_opts);
}

void PlayBackContext::eventLoop(int argc, char **argv) {
  parse_options(this, argc, argv, options, opt_input_file);

  if (this->filename.empty()) {
    throw runtime_error("An input file must be specified.");
  }

  streamOpen();

  MediaEvent event;
  int quit = 0;

  while (!quit) {
    refreshLoopWaitEvent(&event);
    handleEvent(event, quit);
  }

  if (onClockUpdate && duration_ != AV_NOPTS_VALUE) {
    auto endTime = get_master_clock();
    auto dur = (duration_ / (double)AV_TIME_BASE);
    if (fabs(endTime - dur) < 1.0)
      endTime = dur;
    onClockUpdate(endTime);
  }
}

void PlayBackContext::refreshLoopWaitEvent(MediaEvent *event) {
  double remaining_time = 0.0;
  while (!evq_.get(event)) {
    if (remaining_time > 0.0)
      av_usleep((int64_t)(remaining_time * 1000000.0));
        
    remaining_time = REFRESH_RATE;
    if (!this->paused || force_refresh_) {
      if (!this->paused && this->realtime_)
        adjustExternalClockSpeed();

      if (this->video_st) {
        video_refresh(&remaining_time);

        /* display picture */
        if (force_refresh_ && pictureQueue_.rindex_shown)
          video_image_display();
      }

      force_refresh_ = false;

      static int64_t last_time;
      videoRefreshShowStatus(last_time);
    }
  }
}

int PlayBackContext::handleEvent(const MediaEvent& event, int& quit) {
  double incr, pos;

  switch (event.event) {
  case MEDIA_CMD_QUIT:
    quit = 1;
    return 1;

  case MEDIA_CMD_VOLUMN:
    if (event.arg0 == 0) {
      toggleMute();
    } else if (event.arg0 == 1) {
      for (int i=0; i<event.arg0; i++)
        updateVolume(1, SDL_VOLUME_STEP);
    } else if (event.arg0 == -1) {
      for (int i=0; i< -event.arg0; i++)
        updateVolume(-1, SDL_VOLUME_STEP);
    } else {
      setVolume((int)(event.arg1 * 100));
    }
    return 1;

  case MEDIA_CMD_PAUSE:
    togglePause();
    return 1;

  case MEDIA_CMD_NEXT_FRAME:
    step_to_next_frame();
    return 1;

  case MEDIA_CMD_PREV_FRAME:
    step_to_prev_frame();
    return 1;

  case MEDIA_CMD_SPEED:
    change_speed(event.arg1);
    return 1;
  case MEDIA_CMD_CHAPTER:
    if (event.arg0 > 0) {
      if (this->ic->nb_chapters <= 1) {
                    incr = 600.0;
                    goto do_seek;
      }
      seek_chapter(1);
    } else if (event.arg0 < 0) {
      if (this->ic->nb_chapters <= 1) {
        incr = -600.0;
        goto do_seek;
      }
      seek_chapter(-1);
    }
    return 1;

  case MEDIA_CMD_SEEK:
    if (event.arg0 == 0 || event.arg0 == 2) {
      // seek by pts or frameId
      auto target_pts = event.arg1;
      if (event.arg0 == 2) {
        auto id = static_cast<int64_t>(event.arg1);
        target_pts = frameIdToPts(id);
      }
      if (!this->seek_by_bytes) {
        sendSeekRequest(SEEK_METHOD_POS, static_cast<int64_t>(target_pts * AV_TIME_BASE));
      }
      return 1;
    }

    // relative seek
    if (event.arg0 == 1)
		  incr = event.arg1;
do_seek:
    if (this->seek_by_bytes) {
      pos = -1;
      if (pos < 0 && this->video_stream >= 0)
        pos = (double)pictureQueue_.lastShownPosition();
      if (pos < 0 && this->audio_stream >= 0)
        pos = (double)sampleQueue_.lastShownPosition();
      if (pos < 0)
        pos = (double)avio_tell(this->ic->pb);
      if (this->ic->bit_rate)
        incr *= this->ic->bit_rate / 8.0;
      else
        incr *= 180000.0;
      pos += incr;
      sendSeekRequest(SEEK_METHOD_BYTES, (int64_t)pos, (int64_t)(incr));
    } else {
      pos = get_master_clock();
      if (isnan(pos))
        pos = (double)this->seek_pos / AV_TIME_BASE;
      pos += incr;
      if (pos < start_time_ / (double)AV_TIME_BASE)
        pos = start_time_ / (double)AV_TIME_BASE;
      sendSeekRequest(SEEK_METHOD_POS, (int64_t)(pos * AV_TIME_BASE), (int64_t)(incr * AV_TIME_BASE));
    }
    return 1;
  }
  return 0;
}

static int check_stream_specifier(AVFormatContext *s, AVStream *st, const char *spec)
{
  int ret = avformat_match_stream_specifier(s, st, spec);
  if (ret < 0)
        av_log(s, AV_LOG_ERROR, "Invalid stream specifier: %s.\n", spec);
  return ret;
}

static AVDictionary *filter_codec_opts(AVDictionary *opts, enum AVCodecID codec_id,
                                AVFormatContext *s, AVStream *st, AVCodec *codec)
{
    AVDictionary    *ret = NULL;
    AVDictionaryEntry *t = NULL;
    int            flags = AV_OPT_FLAG_DECODING_PARAM;
    char          prefix = 0;
    const AVClass    *cc = avcodec_get_class();

    if (!codec)
        codec            = avcodec_find_decoder(codec_id);

    switch (st->codecpar->codec_type) {
    case AVMEDIA_TYPE_VIDEO:
        prefix  = 'v';
        flags  |= AV_OPT_FLAG_VIDEO_PARAM;
        break;
    case AVMEDIA_TYPE_AUDIO:
        prefix  = 'a';
        flags  |= AV_OPT_FLAG_AUDIO_PARAM;
        break;
    case AVMEDIA_TYPE_SUBTITLE:
        prefix  = 's';
        flags  |= AV_OPT_FLAG_SUBTITLE_PARAM;
        break;
    }

    while (t = av_dict_get(opts, "", t, AV_DICT_IGNORE_SUFFIX)) {
        char *p = strchr(t->key, ':');

        /* check stream specification in opt name */
        if (p)
            switch (check_stream_specifier(s, st, p + 1)) {
            case  1: *p = 0; break;
            case  0:         continue;
            default:         return NULL;
            }

        if (av_opt_find(&cc, t->key, NULL, flags, AV_OPT_SEARCH_FAKE_OBJ) ||
            !codec ||
            (codec->priv_class &&
             av_opt_find(&codec->priv_class, t->key, NULL, flags,
                         AV_OPT_SEARCH_FAKE_OBJ)))
            av_dict_set(&ret, t->key, t->value, 0);
        else if (t->key[0] == prefix &&
                 av_opt_find(&cc, t->key + 1, NULL, flags,
                             AV_OPT_SEARCH_FAKE_OBJ))
            av_dict_set(&ret, t->key + 1, t->value, 0);

        if (p)
            *p = ':';
    }
    return ret;
}

static AVDictionary **setup_find_stream_info_opts(AVFormatContext *s,
                                           AVDictionary *codec_opts)
{
    int i;
    AVDictionary **opts;

    if (!s->nb_streams)
        return NULL;
    opts = (AVDictionary**)av_mallocz_array(s->nb_streams, sizeof(*opts));
    if (!opts) {
        av_log(NULL, AV_LOG_ERROR,
               "Could not alloc memory for stream options.\n");
        return NULL;
    }
    for (i = 0; i < s->nb_streams; i++) {
        opts[i] = filter_codec_opts(codec_opts, s->streams[i]->codecpar->codec_id,
                                    s, s->streams[i], NULL);
    }
    return opts;
}

static bool is_realtime(AVFormatContext *s)
{
    if(   !strcmp(s->iformat->name, "rtp")
       || !strcmp(s->iformat->name, "rtsp")
       || !strcmp(s->iformat->name, "sdp")
    )
      return true;

    if(s->pb && (   !strncmp(s->url, "rtp:", 4)
                 || !strncmp(s->url, "udp:", 4)
                )
    )
        return true;
    return false;
}

static void print_fps(char *dst, size_t size, double d, const char *postfix)
{
	uint64_t v = lrintf(d * 100);
	if (!v)
		av_strlcatf(dst, size, "%1.4f %s", d, postfix);
	else if (v % 100)
		av_strlcatf(dst, size, "%3.2f %s", d, postfix);
	else if (v % (100 * 1000))
		av_strlcatf(dst, size, "%1.0f %s", d, postfix);
	else
		av_strlcatf(dst, size, "%1.0fk %s", d / 1000, postfix);
}

static void dump_metadata(char *dst, size_t size, AVDictionary *m, const char *indent)
{
	if (m && !(av_dict_count(m) == 1 && av_dict_get(m, "language", NULL, 0))) {
		AVDictionaryEntry *tag = NULL;

		av_strlcatf(dst, size, "%sMetadata:\n", indent);
		while ((tag = av_dict_get(m, "", tag, AV_DICT_IGNORE_SUFFIX)))
			if (strcmp("language", tag->key)) {
				const char *p = tag->value;
				av_strlcatf(dst, size,
					"%s  %-16s: ", indent, tag->key);
				while (*p) {
					char tmp[256];
					size_t len = strcspn(p, "\x8\xa\xb\xc\xd");
					av_strlcpy(tmp, p, FFMIN(sizeof(tmp), len + 1));
					av_strlcatf(dst, size, "%s", tmp);
					p += len;
					if (*p == 0xd) av_strlcatf(dst, size, " ");
					if (*p == 0xa) av_strlcatf(dst, size, "\n%s  %-16s: ", indent, "");
					if (*p) p++;
				}
				av_strlcatf(dst, size, "\n");
			}
	}
}

/* "user interface" functions */
static void dump_stream_format(char *dst, size_t size, AVFormatContext *ic, int i)
{
	char buf[256];
	int flags = ic->iformat->flags;
	AVStream *st = ic->streams[i];
	AVDictionaryEntry *lang = av_dict_get(st->metadata, "language", NULL, 0);
	char *separator = (char*)ic->dump_separator;
	AVCodecContext *avctx;
	int ret;

	avctx = avcodec_alloc_context3(NULL);
	if (!avctx)
		return;

	ret = avcodec_parameters_to_context(avctx, st->codecpar);
	if (ret < 0) {
		avcodec_free_context(&avctx);
		return;
	}

	// Fields which are missing from AVCodecParameters need to be taken from the AVCodecContext
	avctx->properties = st->codec->properties;
	avctx->codec = st->codec->codec;
	avctx->qmin = st->codec->qmin;
	avctx->qmax = st->codec->qmax;
	avctx->coded_width = st->codec->coded_width;
	avctx->coded_height = st->codec->coded_height;

	if (separator)
		av_opt_set(avctx, "dump_separator", separator, 0);
	avcodec_string(buf, sizeof(buf), avctx, 0);
	avcodec_free_context(&avctx);

	av_strlcatf(dst, size, "    Stream #%d", i);

	/* the pid is an important information, so we display it */
	/* XXX: add a generic system */
	if (flags & AVFMT_SHOW_IDS)
		av_strlcatf(dst, size, "[0x%x]", st->id);
	if (lang)
		av_strlcatf(dst, size, "(%s)", lang->value);
	av_strlcatf(dst, size, ", %d, %d/%d", st->codec_info_nb_frames,
		st->time_base.num, st->time_base.den);
	av_strlcatf(dst, size, ": %s", buf);

	if (st->sample_aspect_ratio.num &&
		av_cmp_q(st->sample_aspect_ratio, st->codecpar->sample_aspect_ratio)) {
		AVRational display_aspect_ratio;
		av_reduce(&display_aspect_ratio.num, &display_aspect_ratio.den,
			st->codecpar->width  * (int64_t)st->sample_aspect_ratio.num,
			st->codecpar->height * (int64_t)st->sample_aspect_ratio.den,
			1024 * 1024);
		av_strlcatf(dst, size, ", SAR %d:%d DAR %d:%d",
			st->sample_aspect_ratio.num, st->sample_aspect_ratio.den,
			display_aspect_ratio.num, display_aspect_ratio.den);
	}

	if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
		int fps = st->avg_frame_rate.den && st->avg_frame_rate.num;
		int tbr = st->r_frame_rate.den && st->r_frame_rate.num;
		int tbn = st->time_base.den && st->time_base.num;
		int tbc = st->codec->time_base.den && st->codec->time_base.num;

		if (fps || tbr || tbn || tbc)
			av_strlcatf(dst, size, "%s", separator);

		if (fps)
			print_fps(dst, size, av_q2d(st->avg_frame_rate), tbr || tbn || tbc ? "fps, " : "fps");
		if (tbr)
			print_fps(dst, size, av_q2d(st->r_frame_rate), tbn || tbc ? "tbr, " : "tbr");
		if (tbn)
			print_fps(dst, size, 1 / av_q2d(st->time_base), tbc ? "tbn, " : "tbn");
		if (tbc)
			print_fps(dst, size, 1 / av_q2d(st->codec->time_base), "tbc");
	}

	if (st->disposition & AV_DISPOSITION_DEFAULT)
		av_strlcatf(dst, size, " (default)");
	if (st->disposition & AV_DISPOSITION_DUB)
		av_strlcatf(dst, size, " (dub)");
	if (st->disposition & AV_DISPOSITION_ORIGINAL)
		av_strlcatf(dst, size, " (original)");
	if (st->disposition & AV_DISPOSITION_COMMENT)
		av_strlcatf(dst, size, " (comment)");
	if (st->disposition & AV_DISPOSITION_LYRICS)
		av_strlcatf(dst, size, " (lyrics)");
	if (st->disposition & AV_DISPOSITION_KARAOKE)
		av_strlcatf(dst, size, " (karaoke)");
	if (st->disposition & AV_DISPOSITION_FORCED)
		av_strlcatf(dst, size, " (forced)");
	if (st->disposition & AV_DISPOSITION_HEARING_IMPAIRED)
		av_strlcatf(dst, size, " (hearing impaired)");
	if (st->disposition & AV_DISPOSITION_VISUAL_IMPAIRED)
		av_strlcatf(dst, size, " (visual impaired)");
	if (st->disposition & AV_DISPOSITION_CLEAN_EFFECTS)
		av_strlcatf(dst, size, " (clean effects)");
	if (st->disposition & AV_DISPOSITION_ATTACHED_PIC)
		av_strlcatf(dst, size, " (attached pic)");
	if (st->disposition & AV_DISPOSITION_TIMED_THUMBNAILS)
		av_strlcatf(dst, size, " (timed thumbnails)");
	if (st->disposition & AV_DISPOSITION_CAPTIONS)
		av_strlcatf(dst, size, " (captions)");
	if (st->disposition & AV_DISPOSITION_DESCRIPTIONS)
		av_strlcatf(dst, size, " (descriptions)");
	if (st->disposition & AV_DISPOSITION_METADATA)
		av_strlcatf(dst, size, " (metadata)");
	if (st->disposition & AV_DISPOSITION_DEPENDENT)
		av_strlcatf(dst, size, " (dependent)");
	if (st->disposition & AV_DISPOSITION_STILL_IMAGE)
		av_strlcatf(dst, size, " (still image)");
	av_strlcatf(dst, size, "\n");

	dump_metadata(dst, size, st->metadata, "    ");
}

static void report_source_format(char* dst, size_t size, AVFormatContext *ic,
                    const char *url)
{
  int i;

  uint8_t *printed = ic->nb_streams ? (uint8_t*)av_mallocz(ic->nb_streams) : NULL;
  if (ic->nb_streams && !printed)
    return;

  av_strlcatf(dst, size, "Input, %s, from '%s':\n",
           ic->iformat->name, url);
  dump_metadata(dst, size, ic->metadata, "  ");

  {
    av_strlcatf(dst, size, "  Duration: ");
    if (ic->duration != AV_NOPTS_VALUE) {
            int hours, mins, secs, us;
            int64_t duration = ic->duration + (ic->duration <= INT64_MAX - 5000 ? 5000 : 0);
            secs  = duration / AV_TIME_BASE;
            us    = duration % AV_TIME_BASE;
            mins  = secs / 60;
            secs %= 60;
            hours = mins / 60;
            mins %= 60;
            av_strlcatf(dst, size, "%02d:%02d:%02d.%02d", hours, mins, secs,
                   (100 * us) / AV_TIME_BASE);
        } else {
            av_strlcatf(dst, size, "N/A");
        }
        if (ic->start_time != AV_NOPTS_VALUE) {
            int secs, us;
            av_strlcatf(dst, size, ", start: ");
            secs = llabs(ic->start_time / AV_TIME_BASE);
            us   = llabs(ic->start_time % AV_TIME_BASE);
            av_strlcatf(dst, size, "%s%d.%06d",
                   ic->start_time >= 0 ? "" : "-",
                   secs,
                   (int) av_rescale(us, 1000000, AV_TIME_BASE));
        }
        av_strlcatf(dst, size, ", bitrate: ");
        if (ic->bit_rate)
            av_strlcatf(dst, size, "%lld kb/s", ic->bit_rate / 1000);
        else
            av_strlcatf(dst, size, "N/A");
        av_strlcatf(dst, size, "\n");
  }

    for (i = 0; i < ic->nb_chapters; i++) {
        AVChapter *ch = ic->chapters[i];
        av_strlcatf(dst, size, "    Chapter #%d: ", i);
        av_strlcatf(dst, size,
               "start %f, ", ch->start * av_q2d(ch->time_base));
        av_strlcatf(dst, size,
               "end %f\n", ch->end * av_q2d(ch->time_base));

        dump_metadata(dst, size, ch->metadata, "    ");
    }

    if (ic->nb_programs) {
        int j, k, total = 0;
        for (j = 0; j < ic->nb_programs; j++) {
            AVDictionaryEntry *name = av_dict_get(ic->programs[j]->metadata,
                                                  "name", NULL, 0);
            av_strlcatf(dst, size, "  Program %d %s\n", ic->programs[j]->id,
                   name ? name->value : "");
            dump_metadata(dst, size, ic->programs[j]->metadata, "    ");
            for (k = 0; k < ic->programs[j]->nb_stream_indexes; k++) {
                dump_stream_format(dst, size, ic, ic->programs[j]->stream_index[k]);
                printed[ic->programs[j]->stream_index[k]] = 1;
            }
            total += ic->programs[j]->nb_stream_indexes;
        }
        if (total < ic->nb_streams)
            av_strlcatf(dst, size, "  No Program\n");
    }

    for (i = 0; i < ic->nb_streams; i++)
        if (!printed[i])
            dump_stream_format(dst, size, ic, i);

    av_free(printed);
}

static inline
int64_t get_valid_channel_layout(int64_t channel_layout, int channels)
{
    if (channel_layout && av_get_channel_layout_nb_channels(channel_layout) == channels)
        return channel_layout;
    else
        return 0;
}

void PlayBackContext::streamOpen() {
  bool scan_all_pmts_set = false;
  AVDictionaryEntry *t;
  int st_index[AVMEDIA_TYPE_NB];

  ic = avformat_alloc_context();
  if (!ic) {
    throw runtime_error("Could not allocate context.");
  }


  audio_volume = av_clip(audio_volume, 0, 100);
  audio_volume = av_clip(SDL_MIX_MAXVOLUME * audio_volume / 100, 0, SDL_MIX_MAXVOLUME);
  muted_ = false;

  memset(st_index, -1, sizeof(st_index));
  this->last_video_stream = this->video_stream = -1;
  this->last_audio_stream = this->audio_stream = -1;
  this->last_subtitle_stream = this->subtitle_stream = -1;
  this->last_data_stream = this->data_stream = -1;
  this->eof_ = false;

  ic->interrupt_callback.callback = decode_interrupt_cb;
  ic->interrupt_callback.opaque = this;
  if (!av_dict_get(format_opts, "scan_all_pmts", NULL, AV_DICT_MATCH_CASE)) {
    av_dict_set(&format_opts, "scan_all_pmts", "1", AV_DICT_DONT_OVERWRITE);
    scan_all_pmts_set = true;
  }

  int err = avformat_open_input(&ic, filename.c_str(), iformat, &format_opts);
  if (err < 0) {
    char errbuf[128] = {0};
    const char *errbuf_ptr = errbuf;

    if (av_strerror(err, errbuf, sizeof(errbuf)) < 0)
      errbuf_ptr = strerror(AVUNERROR(err));

    throw runtime_error(errbuf_ptr);
  }

  if (scan_all_pmts_set)
    av_dict_set(&format_opts, "scan_all_pmts", NULL, AV_DICT_MATCH_CASE);

  if ((t = av_dict_get(format_opts, "", NULL, AV_DICT_IGNORE_SUFFIX))) {
    throw runtime_error(string("Option ") + t->key + " not found.");
  }

  if (genpts)
    ic->flags |= AVFMT_FLAG_GENPTS;

  av_format_inject_global_side_data(ic);

  // find_stream_info
  {
    AVDictionary **opts = setup_find_stream_info_opts(ic, codec_opts);
    int orig_nb_streams = ic->nb_streams;

    err = avformat_find_stream_info(ic, opts);

    for (int i = 0; i < orig_nb_streams; i++)
      av_dict_free(&opts[i]);
    av_freep(&opts);

    if (err < 0) {
      throw runtime_error("could not find codec parameters.");
    }
  }

  if (ic->pb)
    ic->pb->eof_reached = 0; // FIXME hack, ffplay maybe should not use avio_feof() to test for the end

  if (seek_by_bytes < 0)
        seek_by_bytes = !!(ic->iformat->flags & AVFMT_TS_DISCONT) && strcmp("ogg", ic->iformat->name);

  max_frame_duration = (ic->iformat->flags & AVFMT_TS_DISCONT) ? 10.0 : 3600.0;

  /* if seeking requested, we execute it */
  if (start_time != AV_NOPTS_VALUE) {
        int64_t timestamp;

        timestamp = start_time;
        /* add the stream start time */
        if (ic->start_time != AV_NOPTS_VALUE)
            timestamp += ic->start_time;
        int ret = avformat_seek_file(ic, -1, INT64_MIN, timestamp, INT64_MAX, 0);
        if (ret < 0) {
            av_log(NULL, AV_LOG_WARNING, "%s: could not seek to position %0.3f\n",
                    filename.c_str(), (double)timestamp / AV_TIME_BASE);
        }
  }

  realtime_ = is_realtime(ic);

  duration_ = ic->duration;
  if (duration_ != AV_NOPTS_VALUE && duration_ <= INT64_MAX - 5000)
    duration_ +=  5000;
  start_time_ = ic->start_time != AV_NOPTS_VALUE ? ic->start_time : 0;

  char meta_info[2048] = {0};
  int res_width = 0;
  int res_height = 0;
  double fps = 0;
  double tbr = 0;
  double tbn = 0;
  double tbc = 0;

  report_source_format(meta_info, sizeof(meta_info), ic, this->filename.c_str());

  for (int i = 0; i < ic->nb_streams; i++) {
        AVStream *st = ic->streams[i];
        enum AVMediaType type = st->codecpar->codec_type;
        st->discard = AVDISCARD_ALL;
        if (type >= 0 && wanted_stream_spec[type].size() && st_index[type] == -1)
            if (avformat_match_stream_specifier(ic, st, wanted_stream_spec[type].c_str()) > 0)
                st_index[type] = i;
    }
  for (int i = 0; i < AVMEDIA_TYPE_NB; i++) {
        if (wanted_stream_spec[i].size() && st_index[i] == -1) {
            av_log(NULL, AV_LOG_ERROR, "Stream specifier %s does not match any %s stream\n", wanted_stream_spec[i].c_str(), av_get_media_type_string((AVMediaType)i));
            st_index[i] = INT_MAX;
        }
    }

  st_index[AVMEDIA_TYPE_VIDEO] =
            av_find_best_stream(ic, AVMEDIA_TYPE_VIDEO,
                                st_index[AVMEDIA_TYPE_VIDEO], -1, NULL, 0);
  if (!audio_disable)
        st_index[AVMEDIA_TYPE_AUDIO] =
            av_find_best_stream(ic, AVMEDIA_TYPE_AUDIO,
                                st_index[AVMEDIA_TYPE_AUDIO],
                                st_index[AVMEDIA_TYPE_VIDEO],
                                NULL, 0);
  if (!subtitle_disable)
        st_index[AVMEDIA_TYPE_SUBTITLE] =
            av_find_best_stream(ic, AVMEDIA_TYPE_SUBTITLE,
                                st_index[AVMEDIA_TYPE_SUBTITLE],
                                (st_index[AVMEDIA_TYPE_AUDIO] >= 0 ?
                                 st_index[AVMEDIA_TYPE_AUDIO] :
                                 st_index[AVMEDIA_TYPE_VIDEO]),
                                NULL, 0);

  if (!data_disable)
    st_index[AVMEDIA_TYPE_DATA] =
      av_find_best_stream(ic, AVMEDIA_TYPE_DATA,
        st_index[AVMEDIA_TYPE_DATA], -1, NULL, 0);

  if (st_index[AVMEDIA_TYPE_VIDEO] >= 0) {
    AVStream *st = ic->streams[st_index[AVMEDIA_TYPE_VIDEO]];
    AVCodecParameters *codecpar = st->codecpar;
    AVRational sar = av_guess_sample_aspect_ratio(ic, st, NULL);
    res_width = codecpar->width;
    res_height = codecpar->height;

    if (codecpar->width) {
      av_strlcatf(meta_info, sizeof(meta_info), "Window Size: %dx%d, sar:%d,%d\n", codecpar->width, codecpar->height, sar.num, sar.den);
    }
    
    if (st->avg_frame_rate.den && st->avg_frame_rate.num) {
      fps = av_q2d(st->avg_frame_rate);
    }

    if (st->r_frame_rate.den && st->r_frame_rate.num) {
      tbr = av_q2d(st->r_frame_rate);
    }

    if (st->time_base.den && st->time_base.num) {
      tbn = av_q2d(st->time_base);
    }

    if (st->codec->time_base.den && st->codec->time_base.num) {
      tbc = av_q2d(st->codec->time_base);
    }
  }

  /* open the streams */
  if (st_index[AVMEDIA_TYPE_AUDIO] >= 0) {
    streamComponentOpen(st_index[AVMEDIA_TYPE_AUDIO]);
  }

  if (st_index[AVMEDIA_TYPE_VIDEO] >= 0) {
    streamComponentOpen(st_index[AVMEDIA_TYPE_VIDEO]);
  }

  if (st_index[AVMEDIA_TYPE_SUBTITLE] >= 0) {
    streamComponentOpen(st_index[AVMEDIA_TYPE_SUBTITLE]);
  }

  if (st_index[AVMEDIA_TYPE_DATA] >= 0) {
    streamComponentOpen(st_index[AVMEDIA_TYPE_DATA]);
  }

  if (this->video_stream < 0 && this->audio_stream < 0) {
    throw runtime_error("No stream in media.");
  }

  if (infinite_buffer < 0 && this->realtime_)
    infinite_buffer = 1;

  if (this->video_stream >= 0 && this->data_stream < 0) {
    // in case data is embedded in H264/265 SEI
    data_time_base_ = this->video_st->time_base;
    startDataDecode();
  }

  if (this->onMetaInfo)
    this->onMetaInfo(
      start_time_/(double)AV_TIME_BASE,
      duration_ == AV_NOPTS_VALUE ? 0.0 : duration_/(double)AV_TIME_BASE,
      res_width,
      res_height,
      meta_info);

  if (onStatics) {
    onStatics(fps, tbr, tbn, tbc);
  }

  abort_reading_ = false;
  read_tid_ = std::thread([this] {
    doReadInThread();
  });
}

static int stream_has_enough_packets(AVStream *st, PacketQueue *queue) {
    return !st ||
           (st->disposition & AV_DISPOSITION_ATTACHED_PIC) ||
           queue->has_enough_packets(st->time_base);
}

void PlayBackContext::doReadInThread() {
  int ret = 0;
  AVPacket pkt1, *pkt = &pkt1;
  int pkt_in_play_range = 0;
  int64_t stream_start_time;
  int64_t pkt_ts;
  int64_t rewindStartPts;
  int64_t rewindEndPts;

  while (!abort_reading_) {
    if (this->paused != this->last_paused) {
      this->last_paused = this->paused;
      if (this->paused)
        this->read_pause_return = av_read_pause(ic);
      else
        av_read_play(ic);
    }

    if (this->paused &&
                (!strcmp(ic->iformat->name, "rtsp") ||
                 (ic->pb && !strncmp(this->filename.c_str(), "mmsh:", 5)))) {
            /* wait 10 ms to avoid trying to get another packet */
            /* XXX: horrible */
      SDL_Delay(10);
      continue;
    }

    if (seekMethod_ == SEEK_METHOD_POS) {
      int64_t seek_target = this->seek_pos;
      syncVideoPts_ = av_rescale_q(seek_target, AVRational{ 1, AV_TIME_BASE }, video_time_base_);

      if (rewindMode()) {
        // change the seek mode to rewind seek
        // 
        int64_t convert_pos = av_rescale_q(seek_target, AVRational{ 1, AV_TIME_BASE }, video_time_base_);
        this->seek_pos = convert_pos;
        frameRewindTarget_ = convert_pos;
        seekMethod_ = SEEK_METHOD_REWIND;
      } else {
        ret = avformat_seek_file(this->ic, -1, INT64_MIN, seek_target, INT64_MAX, 0);
        if (ret < 0) {
          av_log(NULL, AV_LOG_ERROR,
                        "%s: error while seeking\n", this->ic->url);
        } else {
          newSerial();
          this->extclk.set_clock(seek_target / (double)AV_TIME_BASE, 0);
        }
      }
    } else if (seekMethod_ == SEEK_METHOD_BYTES) {
      if (rewindMode()) {
        seekMethod_ = SEEK_METHOD_NONE;
        // do nothing
      } else {
        int64_t seek_target = this->seek_pos;
        int64_t seek_min    = this->seek_rel > 0 ? seek_target - this->seek_rel + 2: INT64_MIN;
        int64_t seek_max    = this->seek_rel < 0 ? seek_target - this->seek_rel - 2: INT64_MAX;
        // FIXME the +-2 is due to rounding being not done in the correct direction in generation
        //      of the seek_pos/seek_rel variables

        ret = avformat_seek_file(this->ic, -1, seek_min, seek_target, seek_max, AVSEEK_FLAG_BYTE);
        if (ret < 0) {
          av_log(NULL, AV_LOG_ERROR,
                        "%s: error while seeking\n", this->ic->url);
        } else {
          newSerial();
          this->extclk.set_clock(NAN, 0);
        }
      }
    }

    if (seekMethod_ == SEEK_METHOD_POS || seekMethod_ == SEEK_METHOD_BYTES) {
      seekMethod_ = SEEK_METHOD_NONE;
      this->queue_attachments_req = 1;
      this->eof_ = false;

      // read till target pos get
      int specified_serial;
      bool v_syned = video_stream < 0;
      bool a_syned = audio_stream < 0;
      while (!a_syned || !v_syned) {
        ret = av_read_frame(ic, pkt);
        if (ret < 0) {
          break;
        }

        int64_t pos = av_rescale_q(pkt->pts, ic->streams[pkt->stream_index]->time_base, AVRational{ 1, AV_TIME_BASE });
        if (pos >= this->seek_pos) {
          specified_serial = -1;
          if (pkt->stream_index == this->audio_stream) {
            a_syned = true;
          } else if (pkt->stream_index == this->video_stream) {
           v_syned = true;
          }
        } else {
          specified_serial = SERIAL_HELPER_PACKET; // discard on present
        }

        pushPacket(pkt, specified_serial);
      }

      if (this->paused) {
        stream_toggle_pause();
        stepping_ = true;
      }
    }

    if (seekMethod_ == SEEK_METHOD_REWIND) {
      rewindEndPts = this->seek_pos;
      int64_t pos = av_rescale_q(rewindEndPts - 1, video_time_base_, AVRational{ 1, AV_TIME_BASE });
      ret = av_seek_frame(this->ic, -1, pos,  AVSEEK_FLAG_FRAME | AVSEEK_FLAG_BACKWARD); // AVSEEK_FLAG_BACKWARD AVSEEK_FLAG_ANY
      if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR,
                       "%s: error while seeking\n", this->ic->url);
      } else {
        newSerial();

        rewind_ = true;
        rewindEofPts_ = 0;

        this->extclk.set_clock(pos / (double)AV_TIME_BASE, 0);

        // till we got first video frame
        for (;;) {
          ret = av_read_frame(ic, pkt);
          if (ret < 0) {
            break;
          }

          pushPacket(pkt);

          if (pkt->stream_index == this->video_stream) {
            rewindStartPts = pkt->pts;
            break;
          }
        }
      }
      seekMethod_ = SEEK_METHOD_NONE;
      this->queue_attachments_req = 1;
      this->eof_ = false;
    } else if (seekMethod_ == SEEK_METHOD_REWIND_CONTINUE) {
      int64_t seek_target = this->seek_pos;
      ret = av_seek_frame(this->ic, -1, seek_target,  AVSEEK_FLAG_FRAME | AVSEEK_FLAG_BACKWARD); // AVSEEK_FLAG_BACKWARD AVSEEK_FLAG_ANY
      if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR,
                       "%s: error while seeking\n", this->ic->url);
      } else {
        // till we got first video frame
        for (;;) {
          ret = av_read_frame(ic, pkt);
          if (ret < 0) {
            break;
          }

          pushPacket(pkt);
          if (pkt->stream_index == this->video_stream) {
            rewindStartPts = pkt->pts;
            break;
          }
        }
      }
  
      seekMethod_ = SEEK_METHOD_NONE;
      this->queue_attachments_req = 1;
      this->eof_ = false;
    }
  
    if (this->queue_attachments_req) {
      if (this->video_st && this->video_st->disposition & AV_DISPOSITION_ATTACHED_PIC) {
                AVPacket copy;
                if ((ret = av_packet_ref(&copy, &this->video_st->attached_pic)) < 0)
                    goto fail;
        videoPacketQueue_.put(&copy);
        videoPacketQueue_.put_nullpacket(this->video_stream);
      }
      this->queue_attachments_req = 0;
    }

    /* if the queue are full, no need to read more */
    if (infinite_buffer<1 &&
              (audioPacketQueue_.size() + videoPacketQueue_.size() + subtitlePacketQueue_.size() > MAX_QUEUE_SIZE
            || (stream_has_enough_packets(this->audio_st, &audioPacketQueue_) &&
                stream_has_enough_packets(this->video_st, &videoPacketQueue_) &&
                stream_has_enough_packets(this->subtitle_st, &subtitlePacketQueue_)))) {
            /* wait 10 ms */
            std::unique_lock<std::mutex> lk(this->wait_mtx);
            continue_read_thread_.wait_for(lk, 10ms);
            continue;
    }

    if (!this->paused &&
            (!this->audio_st || (!rewindMode() && audioDecoder_.finished() && sampleQueue_.nb_remaining() == 0)) &&
            (!this->video_st || (!rewindMode() && videoDecoder_.finished() && pictureQueue_.nb_remaining() == 0) || (rewindMode() && videoDecoder_.finished() && pictureQueue_.nb_remaining() == 0 && rewindBuffer_.empty()))) {
          ret = AVERROR_EOF;
          goto fail;
    }
  
    ret = av_read_frame(ic, pkt);
    if (ret < 0) {
            if ((ret == AVERROR_EOF || avio_feof(ic->pb)) && !eof_) {
                if (this->video_stream >= 0)
                    videoPacketQueue_.put_nullpacket(this->video_stream);
                if (this->audio_stream >= 0)
                    audioPacketQueue_.put_nullpacket(this->audio_stream);
                if (this->subtitle_stream >= 0)
                    subtitlePacketQueue_.put_nullpacket(this->subtitle_stream);
                if (this->data_stream >= 0)
                    dataPacketQueue_.put_nullpacket(this->data_stream);
                eof_ = true;
            }
            if (ic->pb && ic->pb->error)
                break;

          std::unique_lock<std::mutex> lk(this->wait_mtx);
          continue_read_thread_.wait_for(lk, 10ms);
          continue;
    } else {
            this->eof_ = false;
    }
        
    /* check if packet is in play range specified by user, then queue, otherwise discard */
    stream_start_time = ic->streams[pkt->stream_index]->start_time;
        pkt_ts = pkt->pts == AV_NOPTS_VALUE ? pkt->dts : pkt->pts;
        pkt_in_play_range = duration == AV_NOPTS_VALUE ||
                (pkt_ts - (stream_start_time != AV_NOPTS_VALUE ? stream_start_time : 0)) *
                av_q2d(ic->streams[pkt->stream_index]->time_base) -
                (double)(start_time != AV_NOPTS_VALUE ? start_time : 0) / 1000000
                <= ((double)duration / 1000000);
    if (pkt->stream_index == this->audio_stream && pkt_in_play_range) {
            audioPacketQueue_.put(pkt);
    } else if (pkt->stream_index == this->video_stream && pkt_in_play_range
                   && !(this->video_st->disposition & AV_DISPOSITION_ATTACHED_PIC)) {

      if (this->drop_frame_mode) {
            // restore when key frame
        if (pkt->flags & AV_PKT_FLAG_KEY) {
          this->drop_frame_mode = false;
        }
      }
      
      if (rewindMode()) {
        if (pkt->pts >= rewindEndPts) {
          if (rewindStartPts <= start_time_) {
				    videoPacketQueue_.put(pkt); // this packet as a mark
						videoPacketQueue_.put_nullpacket(this->video_stream);
            rewindEofPts_ = rewindStartPts;

            av_read_pause(ic);
            while (rewindMode() && !abort_reading_) {
              std::unique_lock<std::mutex> lk(this->wait_mtx);
              continue_read_thread_.wait_for(lk, 10ms);
            }
            av_read_play(ic);
            continue;
          }

          rewindEndPts = rewindStartPts;
          int64_t pos = av_rescale_q(rewindEndPts - 1, video_time_base_, AVRational{ 1, AV_TIME_BASE });
          this->seek_pos = pos;
          seekMethod_ = SEEK_METHOD_REWIND_CONTINUE;
          videoPacketQueue_.put(pkt); // this packet as a mark
          continue;
        }
      }

      if (this->drop_frame_mode) {
        av_packet_unref(pkt);
      } else {
        videoPacketQueue_.put(pkt);
      }

    } else if (pkt->stream_index == this->subtitle_stream && pkt_in_play_range) {
            subtitlePacketQueue_.put(pkt);
        }  else if (pkt->stream_index == this->data_stream) {
          dataPacketQueue_.put(pkt);
        } else {
            av_packet_unref(pkt);
    }
  }

fail:
  {
    MediaEvent ev;
    ev.event = MEDIA_CMD_QUIT;
		evq_.set(&ev);
  }
}

struct AVCodecContextRelease {
  AVCodecContext *avctx_;
  AVCodecContextRelease(AVCodecContext *avctx):avctx_(avctx)  {}

  ~AVCodecContextRelease() {
    if (avctx_)
      avcodec_free_context(&avctx_);
  }

  void giveup() {
    avctx_ = nullptr;
  }
};

void PlayBackContext::streamComponentOpen(int stream_index) {
  AVDictionary *opts = NULL;
  AVDictionaryEntry *t = NULL;
  string forced_codec_name;
  int sample_rate, nb_channels;
  int64_t channel_layout;

  int stream_lowres = this->lowres;

  // should not happen
  if (stream_index < 0 || stream_index >= ic->nb_streams)
    throw runtime_error("stream index out of range.");

  auto avctx = avcodec_alloc_context3(NULL);
  if (!avctx)
    throw runtime_error("avcodec_alloc_context3 out of memory.");

  AVCodecContextRelease ctxLk(avctx);

  int ret = avcodec_parameters_to_context(avctx, ic->streams[stream_index]->codecpar);
  if (ret < 0)
    throw runtime_error("avcodec_parameters_to_context fail");

  avctx->pkt_timebase = ic->streams[stream_index]->time_base;

  if (avctx->codec_type == AVMEDIA_TYPE_DATA) {
    // Data stream without really codec
  } else {

    auto codec = avcodec_find_decoder(avctx->codec_id);

    switch(avctx->codec_type){
          case AVMEDIA_TYPE_AUDIO   : last_audio_stream    = stream_index; forced_codec_name =    audio_codec_name; break;
          case AVMEDIA_TYPE_SUBTITLE: last_subtitle_stream = stream_index; forced_codec_name = subtitle_codec_name; break;
          case AVMEDIA_TYPE_VIDEO   : last_video_stream    = stream_index; forced_codec_name =    video_codec_name; break;
    }

    if (forced_codec_name.size())
          codec = avcodec_find_decoder_by_name(forced_codec_name.c_str());
    if (!codec) {
      if (forced_codec_name.size()) throw runtime_error(string("No codec could be found with name ") + forced_codec_name);
      else  throw runtime_error(string("No decoder could be found for codec ") + avcodec_get_name(avctx->codec_id));
    }

    avctx->codec_id = codec->id;
    if (stream_lowres > codec->max_lowres) {
          av_log(avctx, AV_LOG_WARNING, "The maximum value for lowres supported by the decoder is %d\n",
                  codec->max_lowres);
          stream_lowres = codec->max_lowres;
    }
    avctx->lowres = stream_lowres;

    if (this->fast)
          avctx->flags2 |= AV_CODEC_FLAG2_FAST;

    opts = filter_codec_opts(codec_opts, avctx->codec_id, ic, ic->streams[stream_index], codec);
    if (!av_dict_get(opts, "threads", NULL, 0))
          av_dict_set(&opts, "threads", "auto", 0);
    if (stream_lowres)
          av_dict_set_int(&opts, "lowres", stream_lowres, 0);
    if (avctx->codec_type == AVMEDIA_TYPE_VIDEO || avctx->codec_type == AVMEDIA_TYPE_AUDIO)
          av_dict_set(&opts, "refcounted_frames", "1", 0);
    if ((ret = avcodec_open2(avctx, codec, &opts)) < 0) {
      throw runtime_error("avcodec_open2 fail");
    }
    if ((t = av_dict_get(opts, "", NULL, AV_DICT_IGNORE_SUFFIX))) {
      throw runtime_error(string("Option ") + t->key + " not found.");
    }
  }

  this->eof_ = false;
  ic->streams[stream_index]->discard = AVDISCARD_DEFAULT;

  switch (avctx->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
#ifdef BUILD_WITH_AUDIO_FILTER
        {
            AVFilterContext *sink;

            this->audio_filter_src.freq           = avctx->sample_rate;
            this->audio_filter_src.channels       = avctx->channels;
            this->audio_filter_src.channel_layout = get_valid_channel_layout(avctx->channel_layout, avctx->channels);
            this->audio_filter_src.fmt            = avctx->sample_fmt;
            configureAudioFilters(false);
            sink = this->out_audio_filter;
            sample_rate    = av_buffersink_get_sample_rate(sink);
            nb_channels    = av_buffersink_get_channels(sink);
            channel_layout = av_buffersink_get_channel_layout(sink);
        }
#else
        sample_rate    = avctx->sample_rate;
        nb_channels    = avctx->channels;
        channel_layout = avctx->channel_layout;
#endif

        /* prepare audio output */
        try {
          audioOpen(channel_layout, nb_channels, sample_rate);
        } catch (exception&) {
          // open filed
          // skip but not raise error
          break;
        }
        this->audio_hw_buf_size = ret;
        this->audio_src = this->audio_tgt;
        this->audio_buf_size  = 0;
        this->audio_buf_index = 0;

        /* init averaging filter */
        this->audio_diff_avg_coef  = exp(log(0.01) / AUDIO_DIFF_AVG_NB);
        this->audio_diff_avg_count = 0;
        /* since we do not have a precise anough audio FIFO fullness,
           we correct audio sync only if larger than this threshold */
        this->audio_diff_threshold = (double)(this->audio_hw_buf_size) / this->audio_tgt.bytes_per_sec;

        this->audio_stream = stream_index;
        this->audio_st = ic->streams[stream_index];

        if ((this->ic->iformat->flags & (AVFMT_NOBINSEARCH | AVFMT_NOGENSEARCH | AVFMT_NO_BYTE_SEEK)) && !this->ic->iformat->read_seek) {
         audioDecoder_.start_pts = this->audio_st->start_time;
         audioDecoder_.start_pts_tb = this->audio_st->time_base;
        }

        // start audio thread
        audioPacketQueue_.start();

        audioDecoder_.init(avctx);
        ctxLk.giveup();

        startAudioDecodeThread();
        SDL_PauseAudioDevice(audio_dev, 0);
        break;
    case AVMEDIA_TYPE_VIDEO:
        this->video_stream = stream_index;
        this->video_st = ic->streams[stream_index];

        video_frame_rate_ = av_guess_frame_rate(this->ic, this->video_st, NULL);
        frame_duration_ = (video_frame_rate_.num && video_frame_rate_.den ? av_q2d(AVRational{video_frame_rate_.den, video_frame_rate_.num}) : 0);
        video_time_base_ = this->video_st->time_base;

        videoPacketQueue_.start();

        videoDecoder_.init(avctx);
        ctxLk.giveup();

        startVideoDecodeThread();
        this->queue_attachments_req = 1;
        break;
    case AVMEDIA_TYPE_SUBTITLE:
        this->subtitle_stream = stream_index;
        this->subtitle_st = ic->streams[stream_index];

        subtitlePacketQueue_.start();

        subtitleDecoder_.init(avctx);
        ctxLk.giveup();

        startSubtitleDecodeThread();
        break;
    case AVMEDIA_TYPE_DATA:
        this->data_stream = stream_index;
        this->data_st = ic->streams[stream_index];
        data_time_base_ = this->data_st->time_base;
        startDataDecode();
        break;
    default:
        break;
  }

  av_dict_free(&opts);
}

void PlayBackContext::streamClose() {
  abort_reading_ = true;
	if (read_tid_.joinable()) {
		read_tid_.join();
	}

  /* close each stream */
  if (this->audio_stream >= 0)
        streamComponentClose(this->audio_stream);
  if (this->video_stream >= 0)
        streamComponentClose(this->video_stream);
  if (this->subtitle_stream >= 0)
        streamComponentClose(this->subtitle_stream);

  // close anyway
  stopDataDecode();

  avformat_close_input(&ic);

#ifdef BUILD_WITH_AUDIO_FILTER
  avfilter_graph_free(&this->agraph);
#endif
}

void PlayBackContext::streamComponentClose(int stream_index) {
  AVCodecParameters *codecpar;

  if (stream_index < 0 || stream_index >= ic->nb_streams)
        return;
  codecpar = ic->streams[stream_index]->codecpar;

  switch (codecpar->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
      audioPacketQueue_.abort();
      sampleQueue_.abort();
      audioDecoder_.abort();

      if (audio_dev) {
        SDL_CloseAudioDevice(audio_dev);
        audio_dev = 0;
      }
      // also destroy codec
      audioDecoder_.destroy();
      swr_free(&this->swr_ctx);
      av_freep(&this->audio_buf1);
      this->audio_buf1_size = 0;
      this->audio_buf = NULL;
      break;
    case AVMEDIA_TYPE_VIDEO:
      videoPacketQueue_.abort();
      pictureQueue_.abort();
      videoDecoder_.abort();
      videoDecoder_.destroy();
      break;
    case AVMEDIA_TYPE_SUBTITLE:
      subtitlePacketQueue_.abort();
      subtitleQueue_.abort();
      subtitleDecoder_.abort();
      subtitleDecoder_.destroy();
      break;
    case AVMEDIA_TYPE_DATA:
      stopDataDecode();
      break;
  }

  ic->streams[stream_index]->discard = AVDISCARD_ALL;
  switch (codecpar->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
        this->audio_st = NULL;
        this->audio_stream = -1;
        break;
    case AVMEDIA_TYPE_VIDEO:
        this->video_st = NULL;
        this->video_stream = -1;
        break;
    case AVMEDIA_TYPE_SUBTITLE:
        this->subtitle_st = NULL;
        this->subtitle_stream = -1;
        break;
    case AVMEDIA_TYPE_DATA:
      this->data_st = NULL;
      this->data_stream = -1;
      break;
    default:
        break;
    }
}

#if defined(BUILD_WITH_AUDIO_FILTER) || defined(BUILD_WITH_VIDEO_FILTER)
static int configure_filtergraph(AVFilterGraph *graph, const char *filtergraph,
                                 AVFilterContext *source_ctx, AVFilterContext *sink_ctx)
{
    int ret, i;
    int nb_filters = graph->nb_filters;
    AVFilterInOut *outputs = NULL, *inputs = NULL;

    if (filtergraph && filtergraph[0]) {
        outputs = avfilter_inout_alloc();
        inputs  = avfilter_inout_alloc();
        if (!outputs || !inputs) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        outputs->name       = av_strdup("in");
        outputs->filter_ctx = source_ctx;
        outputs->pad_idx    = 0;
        outputs->next       = NULL;

        inputs->name        = av_strdup("out");
        inputs->filter_ctx  = sink_ctx;
        inputs->pad_idx     = 0;
        inputs->next        = NULL;

        if ((ret = avfilter_graph_parse_ptr(graph, filtergraph, &inputs, &outputs, NULL)) < 0)
            goto fail;
    } else {
        if ((ret = avfilter_link(source_ctx, 0, sink_ctx, 0)) < 0)
            goto fail;
    }

    /* Reorder the filters to ensure that inputs of the custom filters are merged first */
    for (i = 0; i < graph->nb_filters - nb_filters; i++)
        FFSWAP(AVFilterContext*, graph->filters[i], graph->filters[i + nb_filters]);

    ret = avfilter_graph_config(graph, NULL);
fail:
    avfilter_inout_free(&outputs);
    avfilter_inout_free(&inputs);
    return ret;
}

int PlayBackContext::configure_video_filters(AVFilterGraph *graph, const char *vfilters, AVFrame *frame)
{
    enum AVPixelFormat pix_fmts[2];
    char sws_flags_str[512] = "";
    char buffersrc_args[256];
    int ret;
    AVFilterContext *filt_src = NULL, *filt_out = NULL, *last_filter = NULL;
    AVCodecParameters *codecpar = this->video_st->codecpar;
    AVRational fr = av_guess_frame_rate(this->ic, this->video_st, NULL);
    AVDictionaryEntry *e = NULL;
    int nb_pix_fmts = 0;
    int i;

    pix_fmts[0] = AV_PIX_FMT_YUV420P;
    pix_fmts[1] = AV_PIX_FMT_NONE;

    while ((e = av_dict_get(sws_dict, "", e, AV_DICT_IGNORE_SUFFIX))) {
        if (!strcmp(e->key, "sws_flags")) {
            av_strlcatf(sws_flags_str, sizeof(sws_flags_str), "%s=%s:", "flags", e->value);
        } else
            av_strlcatf(sws_flags_str, sizeof(sws_flags_str), "%s=%s:", e->key, e->value);
    }
    if (strlen(sws_flags_str))
        sws_flags_str[strlen(sws_flags_str)-1] = '\0';

    graph->scale_sws_opts = av_strdup(sws_flags_str);

    snprintf(buffersrc_args, sizeof(buffersrc_args),
             "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
             frame->width, frame->height, frame->format,
             this->video_st->time_base.num, this->video_st->time_base.den,
             codecpar->sample_aspect_ratio.num, FFMAX(codecpar->sample_aspect_ratio.den, 1));
    if (fr.num && fr.den)
        av_strlcatf(buffersrc_args, sizeof(buffersrc_args), ":frame_rate=%d/%d", fr.num, fr.den);

    if ((ret = avfilter_graph_create_filter(&filt_src,
                                            avfilter_get_by_name("buffer"),
                                            "ffplay_buffer", buffersrc_args, NULL,
                                            graph)) < 0)
        goto fail;

    ret = avfilter_graph_create_filter(&filt_out,
                                       avfilter_get_by_name("buffersink"),
                                       "ffplay_buffersink", NULL, NULL, graph);
    if (ret < 0)
        goto fail;

    if ((ret = av_opt_set_int_list(filt_out, "pix_fmts", pix_fmts,  AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN)) < 0)
        goto fail;

    last_filter = filt_out;

/* Note: this macro adds a filter before the lastly added filter, so the
 * processing order of the filters is in reverse */
#define INSERT_FILT(name, arg) do {                                          \
    AVFilterContext *filt_ctx;                                               \
                                                                             \
    ret = avfilter_graph_create_filter(&filt_ctx,                            \
                                       avfilter_get_by_name(name),           \
                                       "ffplay_" name, arg, NULL, graph);    \
    if (ret < 0)                                                             \
        goto fail;                                                           \
                                                                             \
    ret = avfilter_link(filt_ctx, 0, last_filter, 0);                        \
    if (ret < 0)                                                             \
        goto fail;                                                           \
                                                                             \
    last_filter = filt_ctx;                                                  \
} while (0)

    if ((ret = configure_filtergraph(graph, vfilters, filt_src, last_filter)) < 0)
        goto fail;

    this->in_video_filter  = filt_src;
    this->out_video_filter = filt_out;

fail:
    return ret;
}

void PlayBackContext::configureAudioFilters(bool force_output_format)
{
    static const enum AVSampleFormat sample_fmts[] = { AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_NONE };
    int sample_rates[2] = { 0, -1 };
    int64_t channel_layouts[2] = { 0, -1 };
    int channels[2] = { 0, -1 };
    AVFilterContext *filt_asrc = NULL, *filt_asink = NULL;
    char aresample_swr_opts[512] = "";
    AVDictionaryEntry *e = NULL;
    char asrc_args[256];
    int ret;

    avfilter_graph_free(&this->agraph);
    if (!(this->agraph = avfilter_graph_alloc()))
      throw runtime_error("avfilter_graph_alloc out of memory");

    this->agraph->nb_threads = filter_nbthreads;

    while ((e = av_dict_get(swr_opts, "", e, AV_DICT_IGNORE_SUFFIX)))
        av_strlcatf(aresample_swr_opts, sizeof(aresample_swr_opts), "%s=%s:", e->key, e->value);
    if (strlen(aresample_swr_opts))
        aresample_swr_opts[strlen(aresample_swr_opts)-1] = '\0';
    av_opt_set(this->agraph, "aresample_swr_opts", aresample_swr_opts, 0);

    ret = snprintf(asrc_args, sizeof(asrc_args),
                   "sample_rate=%d:sample_fmt=%s:channels=%d:time_base=%d/%d",
                   this->audio_filter_src.freq, av_get_sample_fmt_name(this->audio_filter_src.fmt),
                   this->audio_filter_src.channels,
                   1, this->audio_filter_src.freq);
    if (this->audio_filter_src.channel_layout)
        snprintf(asrc_args + ret, sizeof(asrc_args) - ret,
                 ":channel_layout=0x%llx", this->audio_filter_src.channel_layout);

    ret = avfilter_graph_create_filter(&filt_asrc,
                                       avfilter_get_by_name("abuffer"), "ffplay_abuffer",
                                       asrc_args, NULL, this->agraph);
    if (ret < 0)
        goto end;


    ret = avfilter_graph_create_filter(&filt_asink,
                                       avfilter_get_by_name("abuffersink"), "ffplay_abuffersink",
                                       NULL, NULL, this->agraph);
    if (ret < 0)
        goto end;

    if ((ret = av_opt_set_int_list(filt_asink, "sample_fmts", sample_fmts,  AV_SAMPLE_FMT_NONE, AV_OPT_SEARCH_CHILDREN)) < 0)
        goto end;
    if ((ret = av_opt_set_int(filt_asink, "all_channel_counts", 1, AV_OPT_SEARCH_CHILDREN)) < 0)
        goto end;

    if (force_output_format) {
        channel_layouts[0] = this->audio_tgt.channel_layout;
        channels       [0] = this->audio_tgt.channels;
        sample_rates   [0] = this->audio_tgt.freq;
        if ((ret = av_opt_set_int(filt_asink, "all_channel_counts", 0, AV_OPT_SEARCH_CHILDREN)) < 0)
            goto end;
        if ((ret = av_opt_set_int_list(filt_asink, "channel_layouts", channel_layouts,  -1, AV_OPT_SEARCH_CHILDREN)) < 0)
            goto end;
        if ((ret = av_opt_set_int_list(filt_asink, "channel_counts" , channels       ,  -1, AV_OPT_SEARCH_CHILDREN)) < 0)
            goto end;
        if ((ret = av_opt_set_int_list(filt_asink, "sample_rates"   , sample_rates   ,  -1, AV_OPT_SEARCH_CHILDREN)) < 0)
            goto end;
    }


    if ((ret = configure_filtergraph(this->agraph, afilters.c_str(), filt_asrc, filt_asink)) < 0)
        goto end;

    this->in_audio_filter  = filt_asrc;
    this->out_audio_filter = filt_asink;

end:
    if (ret < 0) {
      avfilter_graph_free(&this->agraph);
      throw runtime_error("failed create audio filter " + afilters);
    }
}

#endif

void PlayBackContext::audioOpen(int64_t wanted_channel_layout, int wanted_nb_channels, int wanted_sample_rate)
{
    SDL_AudioSpec wanted_spec, spec;
    const char *env;
    static const int next_nb_channels[] = {0, 0, 1, 6, 2, 6, 4, 6};
    static const int next_sample_rates[] = {0, 44100, 48000, 96000, 192000};
    int next_sample_rate_idx = FF_ARRAY_ELEMS(next_sample_rates) - 1;

    auto audio_hw_params = &this->audio_tgt;

    env = SDL_getenv("SDL_AUDIO_CHANNELS");
    if (env) {
        wanted_nb_channels = atoi(env);
        wanted_channel_layout = av_get_default_channel_layout(wanted_nb_channels);
    }
    if (!wanted_channel_layout || wanted_nb_channels != av_get_channel_layout_nb_channels(wanted_channel_layout)) {
        wanted_channel_layout = av_get_default_channel_layout(wanted_nb_channels);
        wanted_channel_layout &= ~AV_CH_LAYOUT_STEREO_DOWNMIX;
    }
    wanted_nb_channels = av_get_channel_layout_nb_channels(wanted_channel_layout);
    wanted_spec.channels = wanted_nb_channels;
    wanted_spec.freq = wanted_sample_rate;
    if (wanted_spec.freq <= 0 || wanted_spec.channels <= 0) {
      throw runtime_error("Invalid sample rate or channel count!");
    }
    while (next_sample_rate_idx && next_sample_rates[next_sample_rate_idx] >= wanted_spec.freq)
        next_sample_rate_idx--;
    wanted_spec.format = AUDIO_S16SYS;
    wanted_spec.silence = 0;
    wanted_spec.samples = FFMAX(SDL_AUDIO_MIN_BUFFER_SIZE, 2 << av_log2(wanted_spec.freq / SDL_AUDIO_MAX_CALLBACKS_PER_SEC));
    wanted_spec.callback = sdl_audio_callback;
    wanted_spec.userdata = this;
    while (!(audio_dev = SDL_OpenAudioDevice(NULL, 0, &wanted_spec, &spec, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_CHANNELS_CHANGE))) {
        av_log(NULL, AV_LOG_WARNING, "SDL_OpenAudio (%d channels, %d Hz): %s\n",
               wanted_spec.channels, wanted_spec.freq, SDL_GetError());
        wanted_spec.channels = next_nb_channels[FFMIN(7, wanted_spec.channels)];
        if (!wanted_spec.channels) {
            wanted_spec.freq = next_sample_rates[next_sample_rate_idx--];
            wanted_spec.channels = wanted_nb_channels;
            if (!wanted_spec.freq) {
              throw runtime_error("No more combinations to try, audio open failed!");
            }
        }
        wanted_channel_layout = av_get_default_channel_layout(wanted_spec.channels);
    }
    if (spec.format != AUDIO_S16SYS) {
      throw runtime_error("SDL advised audio format %d is not supported!");
    }
    if (spec.channels != wanted_spec.channels) {
        wanted_channel_layout = av_get_default_channel_layout(spec.channels);
        if (!wanted_channel_layout) {
          throw runtime_error("SDL advised channel count %d is not supported!");
        }
    }

    audio_hw_params->fmt = AV_SAMPLE_FMT_S16;
    audio_hw_params->freq = spec.freq;
    audio_hw_params->channel_layout = wanted_channel_layout;
    audio_hw_params->channels =  spec.channels;
    audio_hw_params->frame_size = av_samples_get_buffer_size(NULL, audio_hw_params->channels, 1, audio_hw_params->fmt, 1);
    audio_hw_params->bytes_per_sec = av_samples_get_buffer_size(NULL, audio_hw_params->channels, audio_hw_params->freq, audio_hw_params->fmt, 1);
    if (audio_hw_params->bytes_per_sec <= 0 || audio_hw_params->frame_size <= 0) {
      throw runtime_error("av_samples_get_buffer_size failed!");
    }
}

/* prepare a new audio buffer */
void PlayBackContext::sdl_audio_callback(void *opaque, Uint8 *stream, int len)
{
    PlayBackContext *is = (PlayBackContext*)opaque;
    int audio_size, len1;

    is->audio_callback_time = av_gettime_relative();

    while (len > 0) {
        if (is->audio_buf_index >= is->audio_buf_size) {
           audio_size = is->audio_decode_frame();
           if (audio_size < 0) {
                /* if error, just output silence */
               is->audio_buf = NULL;
               is->audio_buf_size = SDL_AUDIO_MIN_BUFFER_SIZE / is->audio_tgt.frame_size * is->audio_tgt.frame_size;
           } else {
               is->audio_buf_size = audio_size;
           }
           is->audio_buf_index = 0;
        }
        len1 = is->audio_buf_size - is->audio_buf_index;
        if (len1 > len)
            len1 = len;
        if (!is->muted_ && is->audio_buf && is->audio_volume == SDL_MIX_MAXVOLUME)
            memcpy(stream, (uint8_t *)is->audio_buf + is->audio_buf_index, len1);
        else {
            memset(stream, 0, len1);
            if (!is->muted_ && is->audio_buf)
                SDL_MixAudioFormat(stream, (uint8_t *)is->audio_buf + is->audio_buf_index, AUDIO_S16SYS, len1, is->audio_volume);
        }
        len -= len1;
        stream += len1;
        is->audio_buf_index += len1;
    }

    is->audio_write_buf_size = is->audio_buf_size - is->audio_buf_index;
    /* Let's assume the audio driver that is used by SDL has two periods. */
    if (!isnan(is->audio_clock) && is->speed_ > 0) {
      is->audclk.set_clock_at(is->audio_clock - (double)(2 * is->audio_hw_buf_size + is->audio_write_buf_size) / is->audio_tgt.bytes_per_sec, is->audio_clock_serial, is->audio_callback_time / 1000000.0);
      is->extclk.sync_clock_to_slave(&is->audclk);
    }
}

/**
 * Decode one audio frame and return its uncompressed size.
 *
 * The processed audio frame is decoded, converted if required, and
 * stored in is->audio_buf, with size in bytes given by the return
 * value.
 */
int PlayBackContext::audio_decode_frame()
{
  int data_size, resampled_data_size;
  int64_t dec_channel_layout;
  av_unused double audio_clock0;
  int wanted_nb_samples;
  Frame *af;

  if (this->paused || this->speed_ < 0.0)
    return -1;

  do {
#if defined(_WIN32)
        while (sampleQueue_.nb_remaining() == 0) {
            if ((av_gettime_relative() - audio_callback_time) > 1000000LL * this->audio_hw_buf_size / this->audio_tgt.bytes_per_sec / 2)
                return -1;
            av_usleep (1000);
        }
#endif
        if (!(af = sampleQueue_.peek_readable()))
            return -1;
        sampleQueue_.next();
  } while (af->serial != audioSerial_);

  data_size = av_samples_get_buffer_size(NULL, af->frame->channels,
                                           af->frame->nb_samples,
                                           (AVSampleFormat)af->frame->format, 1);

  dec_channel_layout =
        (af->frame->channel_layout && af->frame->channels == av_get_channel_layout_nb_channels(af->frame->channel_layout)) ?
        af->frame->channel_layout : av_get_default_channel_layout(af->frame->channels);
  wanted_nb_samples = synchronize_audio(af->frame->nb_samples);

  if (af->frame->format        != this->audio_src.fmt            ||
        dec_channel_layout       != this->audio_src.channel_layout ||
        af->frame->sample_rate   != this->audio_src.freq           ||
        (wanted_nb_samples       != af->frame->nb_samples && !this->swr_ctx)) {
        swr_free(&this->swr_ctx);
        this->swr_ctx = swr_alloc_set_opts(NULL,
                                         this->audio_tgt.channel_layout, this->audio_tgt.fmt, this->audio_tgt.freq,
                                         dec_channel_layout,  (AVSampleFormat)af->frame->format, af->frame->sample_rate,
                                         0, NULL);
        if (!this->swr_ctx || swr_init(this->swr_ctx) < 0) {
            av_log(NULL, AV_LOG_ERROR,
                   "Cannot create sample rate converter for conversion of %d Hz %s %d channels to %d Hz %s %d channels!\n",
                    af->frame->sample_rate, av_get_sample_fmt_name((AVSampleFormat)af->frame->format), af->frame->channels,
                    this->audio_tgt.freq, av_get_sample_fmt_name(this->audio_tgt.fmt), this->audio_tgt.channels);
            swr_free(&this->swr_ctx);
            return -1;
        }
        this->audio_src.channel_layout = dec_channel_layout;
        this->audio_src.channels       = af->frame->channels;
        this->audio_src.freq = af->frame->sample_rate;
        this->audio_src.fmt = (AVSampleFormat)af->frame->format;
  }

  if (this->swr_ctx) {
        const uint8_t **in = (const uint8_t **)af->frame->extended_data;
        uint8_t **out = &this->audio_buf1;
        int out_count = (int64_t)wanted_nb_samples * this->audio_tgt.freq / af->frame->sample_rate + 256;
        int out_size  = av_samples_get_buffer_size(NULL, this->audio_tgt.channels, out_count, this->audio_tgt.fmt, 0);
        int len2;
        if (out_size < 0) {
            av_log(NULL, AV_LOG_ERROR, "av_samples_get_buffer_size() failed\n");
            return -1;
        }
        if (wanted_nb_samples != af->frame->nb_samples) {
            if (swr_set_compensation(this->swr_ctx, (wanted_nb_samples - af->frame->nb_samples) * this->audio_tgt.freq / af->frame->sample_rate,
                                        wanted_nb_samples * this->audio_tgt.freq / af->frame->sample_rate) < 0) {
                av_log(NULL, AV_LOG_ERROR, "swr_set_compensation() failed\n");
                return -1;
            }
        }
        av_fast_malloc(&this->audio_buf1, &this->audio_buf1_size, out_size);
        if (!this->audio_buf1)
            return AVERROR(ENOMEM);
        len2 = swr_convert(this->swr_ctx, out, out_count, in, af->frame->nb_samples);
        if (len2 < 0) {
            av_log(NULL, AV_LOG_ERROR, "swr_convert() failed\n");
            return -1;
        }
        if (len2 == out_count) {
            av_log(NULL, AV_LOG_WARNING, "audio buffer is probably too small\n");
            if (swr_init(this->swr_ctx) < 0)
                swr_free(&this->swr_ctx);
        }
        this->audio_buf = this->audio_buf1;
        resampled_data_size = len2 * this->audio_tgt.channels * av_get_bytes_per_sample(this->audio_tgt.fmt);
    } else {
        this->audio_buf = af->frame->data[0];
        resampled_data_size = data_size;
    }

    audio_clock0 = this->audio_clock;
    /* update the audio clock with the pts */
    if (!isnan(af->pts))
        this->audio_clock = af->pts + (double) af->frame->nb_samples / af->frame->sample_rate;
    else
        this->audio_clock = NAN;

    this->audio_clock_serial = af->serial;
#ifdef DEBUG
    {
        static double last_clock;
        printf("audio: delay=%0.3f clock=%0.3f clock0=%0.3f\n",
               this->audio_clock - last_clock,
               this->audio_clock, audio_clock0);
        last_clock = this->audio_clock;
    }
#endif
  return resampled_data_size;
}

/* return the wanted number of samples to get better sync if sync_type is video
 * or external master clock */
int PlayBackContext::synchronize_audio(int nb_samples)
{
    int wanted_nb_samples = nb_samples;

    if (this->speed_ > 0 && this->speed_ != 1.0) {
      wanted_nb_samples = nb_samples / this->speed_;
    }

    /* if not master, then we try to remove or add samples to correct the clock */
    if (get_master_sync_type() != AV_SYNC_AUDIO_MASTER) {
        double diff, avg_diff;
        int min_nb_samples, max_nb_samples;

        diff = this->audclk.get_clock() - get_master_clock();

        if (!isnan(diff) && fabs(diff) < AV_NOSYNC_THRESHOLD) {
            this->audio_diff_cum = diff + this->audio_diff_avg_coef * this->audio_diff_cum;
            if (this->audio_diff_avg_count < AUDIO_DIFF_AVG_NB) {
                /* not enough measures to have a correct estimate */
                this->audio_diff_avg_count++;
            } else {
                /* estimate the A-V difference */
                avg_diff = this->audio_diff_cum * (1.0 - this->audio_diff_avg_coef);

                if (fabs(avg_diff) >= this->audio_diff_threshold) {
                    wanted_nb_samples = nb_samples + (int)(diff * this->audio_src.freq);
                    min_nb_samples = ((nb_samples * (100 - SAMPLE_CORRECTION_PERCENT_MAX) / 100));
                    max_nb_samples = ((nb_samples * (100 + SAMPLE_CORRECTION_PERCENT_MAX) / 100));
                    wanted_nb_samples = av_clip(wanted_nb_samples, min_nb_samples, max_nb_samples);
                }
                av_log(NULL, AV_LOG_TRACE, "diff=%f adiff=%f sample_diff=%d apts=%0.3f %f\n",
                        diff, avg_diff, wanted_nb_samples - nb_samples,
                        this->audio_clock, this->audio_diff_threshold);
            }
        } else {
            /* too big difference : may be initial PTS errors, so
               reset A-V filter */
            this->audio_diff_avg_count = 0;
            this->audio_diff_cum       = 0;
        }
    }

    return wanted_nb_samples;
}


const Clock& PlayBackContext::masterClock() const {
  switch (get_master_sync_type()) {
  case AV_SYNC_VIDEO_MASTER:
    return this->vidclk;
  case AV_SYNC_AUDIO_MASTER:
    return this->audclk;
  default:
    return this->extclk;
  }
}

int PlayBackContext::get_master_sync_type() const {
  if (this->speed_ != 1.0) {
    return AV_SYNC_EXTERNAL_CLOCK;
  }

  if (this->av_sync_type == AV_SYNC_VIDEO_MASTER) {
        if (this->video_st)
            return AV_SYNC_VIDEO_MASTER;
        else
            return AV_SYNC_AUDIO_MASTER;
  } else if (this->av_sync_type == AV_SYNC_AUDIO_MASTER) {
        if (this->audio_st)
            return AV_SYNC_AUDIO_MASTER;
        else
            return AV_SYNC_EXTERNAL_CLOCK;
  } else {
        return AV_SYNC_EXTERNAL_CLOCK;
  }
}

/* get the current master clock value */
double PlayBackContext::get_master_clock() const
{
  return masterClock().get_clock();
}

void PlayBackContext::onPacketDrained() {
  continue_read_thread_.notify_one();
}

static inline
int cmp_audio_fmts(enum AVSampleFormat fmt1, int64_t channel_count1,
                   enum AVSampleFormat fmt2, int64_t channel_count2)
{
    /* If channel count == 1, planar and non-planar formats are the same */
    if (channel_count1 == 1 && channel_count2 == 1)
        return av_get_packed_sample_fmt(fmt1) != av_get_packed_sample_fmt(fmt2);
    else
        return channel_count1 != channel_count2 || fmt1 != fmt2;
}

int PlayBackContext::queuePicture(AVFrame *src_frame, double pts, double duration, int64_t pos, int serial)
{
    Frame *vp;

    if (serial == SERIAL_HELPER_PACKET) {
      av_frame_unref(src_frame);
      return 0;
    }

#if defined(DEBUG_SYNC)
    printf("frame_type=%c pts=%0.3f\n",
           av_get_picture_type_char(src_frame->pict_type), pts);
#endif

  if (!(vp = pictureQueue_.peek_writable()))
    return -1;

  vp->sar = src_frame->sample_aspect_ratio;
  vp->uploaded = 0;

  vp->width = src_frame->width;
  vp->height = src_frame->height;
  vp->format = src_frame->format;

  vp->pts = pts;
  vp->duration = duration;
  vp->pos = pos;
  vp->serial = serial;

  av_frame_move_ref(vp->frame, src_frame);
  pictureQueue_.push();
  return 0;
}

void PlayBackContext::startVideoDecodeThread() {

  videoDecoder_.start([this](Decoder* decoder, int *pfinished) {
    int ret;
    AVFrame *frame = av_frame_alloc();
    int pkt_serial = -1;

    double pts;
    double duration;

    AVRational tb = this->video_st->time_base;
    AVRational frame_rate = av_guess_frame_rate(ic, this->video_st, NULL);

#ifdef BUILD_WITH_VIDEO_FILTER
    AVFilterGraph *graph = NULL;
    AVFilterContext *filt_out = NULL, *filt_in = NULL;
    int last_w = 0;
    int last_h = 0;
    enum AVPixelFormat last_format = (AVPixelFormat)-2;
    int last_serial = -1;
    int last_vfilter_idx = 0;
#endif

    for (;;) {
      ret = getVideoFrame(frame, pkt_serial);
      if (ret < 0)
        goto the_end;
      if (!ret)
        continue;

      if (rewindMode()) {
        ret = onVideoFrameDecodedReversed(frame, pkt_serial);
        continue;
      }

#ifdef BUILD_WITH_VIDEO_FILTER
      if (   last_w != frame->width
            || last_h != frame->height
            || last_format != frame->format
            || last_serial != pkt_serial
            || last_vfilter_idx != this->vfilter_idx) {
        av_log(NULL, AV_LOG_DEBUG,
                   "Video frame changed from size:%dx%d format:%s serial:%d to size:%dx%d format:%s serial:%d\n",
                   last_w, last_h,
                   (const char *)av_x_if_null(av_get_pix_fmt_name(last_format), "none"), last_serial,
                   frame->width, frame->height,
                   (const char *)av_x_if_null(av_get_pix_fmt_name((AVPixelFormat)frame->format), "none"), pkt_serial);
        avfilter_graph_free(&graph);
        graph = avfilter_graph_alloc();
        if (!graph) {
          ret = AVERROR(ENOMEM);
          goto the_end;
        }
        graph->nb_threads = filter_nbthreads;
        if ((ret = configure_video_filters(graph, vfilters_list.size() > this->vfilter_idx ? vfilters_list[this->vfilter_idx].c_str() : nullptr, frame)) < 0) {
          MediaEvent ev;
          ev.event = MEDIA_CMD_QUIT;
          evq_.set(&ev);
          goto the_end;
        }
        filt_in  = this->in_video_filter;
        filt_out = this->out_video_filter;
        last_w = frame->width;
        last_h = frame->height;
        last_format = (AVPixelFormat)frame->format;
        last_serial = pkt_serial;
        last_vfilter_idx = this->vfilter_idx;
        frame_rate = av_buffersink_get_frame_rate(filt_out);
      }

      ret = av_buffersrc_add_frame(filt_in, frame);
      if (ret < 0)
        goto the_end;

      while (ret >= 0) {
        this->frame_last_returned_time = av_gettime_relative() / 1000000.0;

        ret = av_buffersink_get_frame_flags(filt_out, frame, 0);
        if (ret < 0) {
          if (ret == AVERROR_EOF)
            *pfinished = pkt_serial;
          ret = 0;
          break;
        }

        this->frame_last_filter_delay = av_gettime_relative() / 1000000.0 - this->frame_last_returned_time;
        if (fabs(this->frame_last_filter_delay) > AV_NOSYNC_THRESHOLD / 10.0)
          this->frame_last_filter_delay = 0;
        tb = av_buffersink_get_time_base(filt_out);
#endif
        duration = (frame_rate.num && frame_rate.den ? av_q2d(AVRational{frame_rate.den, frame_rate.num}) : 0);
        pts = (frame->pts == AV_NOPTS_VALUE) ? NAN : frame->pts * av_q2d(tb);
        ret = queuePicture(frame, pts, duration, frame->pkt_pos, pkt_serial);
        av_frame_unref(frame);
#ifdef BUILD_WITH_VIDEO_FILTER
        if (videoSerial_ != pkt_serial)
          break;
      }
#endif

      if (ret < 0)
        goto the_end;
    }
the_end:
#ifdef BUILD_WITH_VIDEO_FILTER
    avfilter_graph_free(&graph);
#endif
    av_frame_free(&frame);
  });
}

int PlayBackContext::getVideoFrame(AVFrame *frame, int& pkt_serial) {
  int got_picture;

  if ((got_picture = videoDecoder_.decodeFrame(
        [this](AVMediaType, AVCodecID codec_id, AVPacket *pkt, int *serial) {
          if (videoPacketQueue_.empty())
            onPacketDrained();

          if (videoPacketQueue_.get(pkt, serial) < 0)
            return -1;

          if (videoPacketIsAddonData(codec_id, pkt)) {
            // ai detection data embedded as sei packet
            AVPacket copy = { 0 };
            if (av_packet_ref(&copy, pkt) >= 0) {
              dataPacketQueue_.put(&copy);
            }
          }

          return 0;
        }, frame, nullptr, pkt_serial)) < 0)
    return -1;

  if (got_picture) {
    double dpts = NAN;

    if (decoder_reorder_pts == -1) {
      frame->pts = frame->best_effort_timestamp;
    } else if (!decoder_reorder_pts) {
      frame->pts = frame->pkt_dts;
    }

    if (frame->pts != AV_NOPTS_VALUE)
      dpts = av_q2d(this->video_st->time_base) * frame->pts;

    frame->sample_aspect_ratio = av_guess_sample_aspect_ratio(this->ic, this->video_st, frame);

    if (!rewindMode() && (framedrop>0 || (framedrop && get_master_sync_type() != AV_SYNC_VIDEO_MASTER))) {
            if (frame->pts != AV_NOPTS_VALUE) {
                double diff = dpts - get_master_clock();
                if (!isnan(diff) && fabs(diff) < AV_NOSYNC_THRESHOLD &&
                    diff - this->frame_last_filter_delay < 0 &&
                    pkt_serial == this->vidclk.serial &&
                    !videoPacketQueue_.empty()) {
                    this->frame_drops_early++;
                    if (this->speed_ > 1.0 || this->speed_ < -1.0) {
                      this->drop_frame_mode = true;
                    }
                    av_frame_unref(frame);
                    got_picture = 0;
                }
            }
    }
  }

  return got_picture;
}

void PlayBackContext::startAudioDecodeThread() {
  audioDecoder_.start([this](Decoder* decoder, int *pfinished) {
    AVFrame *frame = av_frame_alloc();
    Frame *af;
    int pkt_serial = -1;
#ifdef BUILD_WITH_AUDIO_FILTER
    int last_serial = -1;
    int64_t dec_channel_layout;
    int reconfigure;
#endif
    int got_frame = 0;
    AVRational tb;
    int ret = 0;

    do {
      if ((got_frame = decoder->decodeFrame(
        [this](AVMediaType, AVCodecID, AVPacket *pkt, int *serial) {
          if (audioPacketQueue_.empty())
            onPacketDrained();

          return audioPacketQueue_.get(pkt, serial);

        }, frame, nullptr, pkt_serial)) < 0)
        goto the_end;

      if (got_frame) {
        tb = AVRational{1, frame->sample_rate};

#ifdef BUILD_WITH_AUDIO_FILTER
        dec_channel_layout = get_valid_channel_layout(frame->channel_layout, frame->channels);

        reconfigure =
                    cmp_audio_fmts(this->audio_filter_src.fmt, this->audio_filter_src.channels,
                                   (AVSampleFormat)frame->format, frame->channels)    ||
                    this->audio_filter_src.channel_layout != dec_channel_layout ||
                    this->audio_filter_src.freq           != frame->sample_rate ||
                    pkt_serial               != last_serial;

        if (reconfigure) {
                    char buf1[1024], buf2[1024];
                    av_get_channel_layout_string(buf1, sizeof(buf1), -1, this->audio_filter_src.channel_layout);
                    av_get_channel_layout_string(buf2, sizeof(buf2), -1, dec_channel_layout);
                    av_log(NULL, AV_LOG_DEBUG,
                           "Audio frame changed from rate:%d ch:%d fmt:%s layout:%s serial:%d to rate:%d ch:%d fmt:%s layout:%s serial:%d\n",
                           this->audio_filter_src.freq, this->audio_filter_src.channels, av_get_sample_fmt_name(this->audio_filter_src.fmt), buf1, last_serial,
                           frame->sample_rate, frame->channels, av_get_sample_fmt_name((AVSampleFormat)frame->format), buf2, pkt_serial);

                    this->audio_filter_src.fmt            = (AVSampleFormat)frame->format;
                    this->audio_filter_src.channels       = frame->channels;
                    this->audio_filter_src.channel_layout = dec_channel_layout;
                    this->audio_filter_src.freq           = frame->sample_rate;
                    last_serial                         = pkt_serial;
                    try {
                      configureAudioFilters(true);
                    } catch (exception& e) {
                      goto the_end;
                    }
        }

        if ((ret = av_buffersrc_add_frame(this->in_audio_filter, frame)) < 0)
          goto the_end;

        while ((ret = av_buffersink_get_frame_flags(this->out_audio_filter, frame, 0)) >= 0) {
          tb = av_buffersink_get_time_base(this->out_audio_filter);
#endif
          if (!(af = sampleQueue_.peek_writable()))
            goto the_end;

          af->pts = (frame->pts == AV_NOPTS_VALUE) ? NAN : frame->pts * av_q2d(tb);
          af->pos = frame->pkt_pos;
          af->serial = pkt_serial;
          af->duration = av_q2d(AVRational{frame->nb_samples, frame->sample_rate});

          av_frame_move_ref(af->frame, frame);
          sampleQueue_.push();

#ifdef BUILD_WITH_AUDIO_FILTER
          if (audioSerial_ != pkt_serial)
            break;
        }
        if (ret == AVERROR_EOF)
          *pfinished = pkt_serial;
#endif
        tb = AVRational{1, frame->sample_rate};
      }
    } while (ret >= 0 || ret == AVERROR(EAGAIN) || ret == AVERROR_EOF);

the_end:
#ifdef BUILD_WITH_AUDIO_FILTER
    avfilter_graph_free(&this->agraph);
#endif
    av_frame_free(&frame);
  });
}

void PlayBackContext::startSubtitleDecodeThread() {
  subtitleDecoder_.start([this](Decoder* decoder, int *pfinished) {
    Frame *sp;
    int got_subtitle;
    double pts;
    int pkt_serial = -1;

    for (;;) {
      if (!(sp = subtitleQueue_.peek_writable()))
        return;

      if ((got_subtitle = decoder->decodeFrame(
        [this](AVMediaType, AVCodecID codec_id, AVPacket *pkt, int *serial) {
          if (subtitlePacketQueue_.empty())
            onPacketDrained();

          return subtitlePacketQueue_.get(pkt, serial);
        }, nullptr, &sp->sub, pkt_serial)) < 0)
        break;

      pts = 0;

      if (got_subtitle && sp->sub.format == 0) {
            if (sp->sub.pts != AV_NOPTS_VALUE)
                pts = sp->sub.pts / (double)AV_TIME_BASE;
            sp->pts = pts;
            sp->serial = pkt_serial;
            sp->width = decoder->context()->width;
            sp->height = decoder->context()->height;
            sp->uploaded = 0;

            /* now we can update the picture count */
            subtitleQueue_.push();
      } else if (got_subtitle) {
            avsubtitle_free(&sp->sub);
      }
    }
  });
}


void PlayBackContext::adjustExternalClockSpeed() {
  if (get_master_sync_type() == AV_SYNC_EXTERNAL_CLOCK && this->speed_ == 1.0) {
    if (video_st && videoPacketQueue_.packetsCount() <= EXTERNAL_CLOCK_MIN_FRAMES ||
        audio_st && audioPacketQueue_.packetsCount() <= EXTERNAL_CLOCK_MIN_FRAMES) {
      this->extclk.set_clock_speed(FFMAX(EXTERNAL_CLOCK_SPEED_MIN, this->extclk.speed - EXTERNAL_CLOCK_SPEED_STEP));
    } else if ((!video_st || videoPacketQueue_.packetsCount() > EXTERNAL_CLOCK_MAX_FRAMES) &&
                (!audio_st || audioPacketQueue_.packetsCount() > EXTERNAL_CLOCK_MAX_FRAMES)) {
      this->extclk.set_clock_speed(FFMIN(EXTERNAL_CLOCK_SPEED_MAX, this->extclk.speed + EXTERNAL_CLOCK_SPEED_STEP));
    } else {
      double speed = this->extclk.speed;
      if (speed != 1.0)
        this->extclk.set_clock_speed(speed + EXTERNAL_CLOCK_SPEED_STEP * (1.0 - speed) / fabs(1.0 - speed));
    }
  }
}

void PlayBackContext::video_refresh(double *remaining_time) {
  double time;

retry:
  if (rewindMode()) {
    video_refresh_rewind(remaining_time);
    return;
  }

  if (pictureQueue_.nb_remaining() == 0) {
    // nothing to do, no picture to display in the queue
  } else {
    double last_duration, duration, delay;
    Frame *vp, *lastvp;

    /* dequeue the picture */
    lastvp = pictureQueue_.peek_last();
    vp = pictureQueue_.peek();

    if (vp->serial != videoSerial_) {
      pictureQueue_.next();
      goto retry;
    }

    if (syncVideoPts_ >= 0) {
      if (vp->frame->pts < syncVideoPts_) {
        pictureQueue_.next();
        goto retry;
      }

      syncVideoPts_ = -1;
    }

    if (lastvp->serial != vp->serial)
      frame_timer_ = av_gettime_relative() / 1000000.0;

    if (this->paused)
      return;

    /* compute nominal last_duration */
    last_duration = vp_duration(lastvp, vp);
    delay = compute_target_delay(last_duration);

    time = av_gettime_relative()/1000000.0;
    if (time < this->frame_timer_ + delay) {
      *remaining_time = FFMIN(this->frame_timer_ + delay - time, *remaining_time);
      return;
    }

    frame_timer_ += delay;
    if (delay > 0 && time - frame_timer_ > AV_SYNC_THRESHOLD_MAX)
      frame_timer_ = time;

    {
      std::lock_guard<std::mutex> lk(pictureQueue_.mtx);
      if (!isnan(vp->pts)) {
        /* update current video pts */
        vidclk.set_clock(vp->pts, vp->serial);
        extclk.sync_clock_to_slave(&vidclk);
      }
    }

    // should drop some frames
    if (pictureQueue_.nb_remaining() > 1) {
      Frame *nextvp = pictureQueue_.peek_next();
      duration = vp_duration(vp, nextvp);
      if(!this->stepping_) {
        if (rewindMode()) {
          if((this->framedrop>0 || (this->framedrop && get_master_sync_type() != AV_SYNC_VIDEO_MASTER)) && time < frame_timer_ - duration) {
            this->frame_drops_late++;
            pictureQueue_.next();
            goto retry;
          }
        }else if((this->framedrop>0 || (this->framedrop && get_master_sync_type() != AV_SYNC_VIDEO_MASTER)) && time > frame_timer_ + duration){
          this->frame_drops_late++;
          pictureQueue_.next();
          goto retry;
        }
      }
    }

    pictureQueue_.next();
    force_refresh_ = true;

    if (this->stepping_ && !this->paused)
      stream_toggle_pause();
  }
}

int64_t PlayBackContext::ptsToFrameId(double pts) const {
  return  static_cast<int64_t>(pts / (frame_duration_ == 0 ? 60.0 : frame_duration_));
}

double PlayBackContext::frameIdToPts(int64_t id) const {
  return id * (frame_duration_ == 0 ? 60.0 : frame_duration_);
}

void PlayBackContext::video_image_display()
{
  Frame *sp = nullptr;
	Frame *vp = pictureQueue_.peek_last();
	if (!vp->uploaded) {
    if (onIYUVDisplay) {
      auto frame = vp->frame;
      if (frame->format != yuv_ctx_.target_fmt) {
        if (yuv_ctx_.convert(frame) < 0) {
          vp->uploaded = 1;
          return;
        }
        // av_frame_unref(frame);
        frame = yuv_ctx_.frame_;
      }
      onIYUVDisplay(frame, vp->pts, ptsToFrameId(vp->pts));
    }
		vp->uploaded = 1;
	}

  if (this->subtitle_st) {
    if (subtitleQueue_.nb_remaining() > 0) {
      sp = subtitleQueue_.peek();

      if (vp->pts >= sp->pts + ((float) sp->sub.start_display_time / 1000)) {
        if (!sp->uploaded) {
          if (!sp->width || !sp->height) {
            sp->width = vp->width;
            sp->height = vp->height;
          }

          for (int i = 0; i < sp->sub.num_rects; i++) {
            AVSubtitleRect *sub_rect = sp->sub.rects[i];

            sub_rect->x = av_clip(sub_rect->x, 0, sp->width );
            sub_rect->y = av_clip(sub_rect->y, 0, sp->height);
            sub_rect->w = av_clip(sub_rect->w, 0, sp->width  - sub_rect->x);
            sub_rect->h = av_clip(sub_rect->h, 0, sp->height - sub_rect->y);

            if (sub_yuv_ctx_.convert(AV_PIX_FMT_PAL8, sub_rect->w, sub_rect->h, (const uint8_t * const *)sub_rect->data, sub_rect->linesize) >= 0) {

            }
          }
          sp->uploaded = 1;
        }
      } else
        sp = nullptr;
    }
  }
}

double PlayBackContext::vp_duration(const Frame *vp, const Frame *nextvp) const {
  if (vp->serial == nextvp->serial) {
    double duration = nextvp->pts - vp->pts;
    if (isnan(duration) || duration <= 0 || duration > this->max_frame_duration)
      return vp->duration;
    else
      return duration;
  } else {
    return 0.0;
  }
}

double PlayBackContext::compute_target_delay(double delay) const {
  double sync_threshold, diff = 0;

  /* update delay to follow master synchronisation source */
  if (get_master_sync_type() != AV_SYNC_VIDEO_MASTER) {
        /* if video is slave, we try to correct big delays by
           duplicating or deleting a frame */
        diff = vidclk.get_clock() - get_master_clock();

        /* skip or repeat frame. We take into account the
           delay to compute the threshold. I still don't know
           if it is the best guess */
        sync_threshold = FFMAX(AV_SYNC_THRESHOLD_MIN, FFMIN(AV_SYNC_THRESHOLD_MAX, delay));
        if (!isnan(diff) && fabs(diff) < this->max_frame_duration) {
            if (diff <= -sync_threshold)
                delay = FFMAX(0, delay + diff);
            else if (diff >= sync_threshold && delay > AV_SYNC_FRAMEDUP_THRESHOLD)
                delay = delay + diff;
            else if (diff >= sync_threshold)
                delay = 2 * delay;
        }
  }

  av_log(NULL, AV_LOG_TRACE, "video: delay=%0.3f A-V=%f\n",
            delay, -diff);

  return delay;
}

void PlayBackContext::videoRefreshShowStatus(int64_t& last_time) const {
  auto cur_time = av_gettime_relative();
  if (!last_time || (cur_time - last_time) >= 30000) {

    if (onClockUpdate) {
      onClockUpdate(get_master_clock());
    }

    if (showStatus && onLog)
    {
      auto aqsize = audioPacketQueue_.size();
      auto vqsize = videoPacketQueue_.size();
      auto sqsize = subtitlePacketQueue_.size();
      double av_diff = 0;
      if (this->audio_st && this->video_st)
        av_diff = this->audclk.get_clock() - this->vidclk.get_clock();
      else if (this->video_st)
        av_diff = get_master_clock() - this->vidclk.get_clock();
      else if (this->audio_st)
        av_diff = get_master_clock() - this->audclk.get_clock();
              
      char buffer[512] = {0};
      snprintf(buffer, sizeof(buffer),
                    "%7.2f %s:%7.3f fd=%4d aq=%5dKB vq=%5dKB f=%lld/%lld   \r",
                    get_master_clock(),
                    (audio_st && video_st) ? "A-V" : (video_st ? "M-V" : (audio_st ? "M-A" : "   ")),
                    av_diff,
                    this->frame_drops_early + this->frame_drops_late,
                    aqsize / 1024,
                    vqsize / 1024,
                    video_st ? videoDecoder_.context()->pts_correction_num_faulty_dts : 0,
                    video_st ? videoDecoder_.context()->pts_correction_num_faulty_pts : 0);
      // fflush(stderr);
      onLog(0, buffer);
    }

    last_time = cur_time;
  }
}


int PlayBackContext::decode_interrupt_cb(void *ctx)
{
  auto is = (PlayBackContext*)ctx;
  return is->abort_reading_;
}

void PlayBackContext::sendEvent(int event, int arg0, double arg1, double arg2) {
  MediaEvent e;
  e.event = event;
  e.arg0 = arg0;
  e.arg1 = arg1;
  e.arg2 = arg2;
	evq_.set(&e);
}

void PlayBackContext::startDataDecode() {

  stopDataDecode();

  dataPacketQueue_.start();
  data_tid_ = thread([this] {
    int pkt_serial;
    AVPacket pkt1, *pkt = &pkt1;
    av_init_packet(pkt);

    for (;;) {
      int ret = receiveDataPacket(pkt, pkt_serial);
      if (ret < 0)
        break;

      ret = dealWithDataPacket(pkt, pkt_serial);

      av_packet_unref(pkt);
      if (ret < 0)
        break;
    }
  });
}

void PlayBackContext::stopDataDecode() {
  dataPacketQueue_.abort();
  if (data_tid_.joinable()) {
    data_tid_.join();
  }
}

int PlayBackContext::receiveDataPacket(AVPacket *pkt, int& pkt_serial) {
  do {
    if (dataPacketQueue_.empty())
      onPacketDrained();
  
    int ret = dataPacketQueue_.get(pkt, &pkt_serial);
    if (ret < 0)
      return ret; // failed

    if (pkt_serial == dataSerial_)
      return 0;

    // discard
    av_packet_unref(pkt);
  } while (true);
}

int PlayBackContext::pushPacket(AVPacket* pkt, int specified_serial) {
  if (pkt->stream_index == this->audio_stream) {
    return audioPacketQueue_.put(pkt, specified_serial);
  } else if (pkt->stream_index == this->video_stream) {
    return videoPacketQueue_.put(pkt, specified_serial);
  } else if (pkt->stream_index == this->subtitle_stream) {
    return subtitlePacketQueue_.put(pkt, specified_serial);
  } else if (pkt->stream_index == this->data_stream) {
    return dataPacketQueue_.put(pkt, specified_serial);
  }
  return -1;
}

void PlayBackContext::newSerial() {
  audioPacketQueue_.nextSerial();
  videoPacketQueue_.nextSerial();
  subtitlePacketQueue_.nextSerial();
  dataPacketQueue_.nextSerial();
}

void PlayBackContext::sendSeekRequest(SeekMethod req, int64_t pos, int64_t rel) {
  if (seekMethod_ == SEEK_METHOD_NONE) {
    this->seek_pos = pos;
    this->seek_rel = rel;
    seekMethod_ = req;
    continue_read_thread_.notify_one();
  }
}

void PlayBackContext::updateVolume(int sign, double step) {
  double volume_level = audio_volume ? (20 * log(audio_volume / (double)SDL_MIX_MAXVOLUME) / log(10)) : -1000.0;
  int new_volume = lrint(SDL_MIX_MAXVOLUME * pow(10.0, (volume_level + sign * step) / 20.0));
  audio_volume = av_clip(audio_volume == new_volume ? (audio_volume + sign) : new_volume, 0, SDL_MIX_MAXVOLUME);
}

int PlayBackContext::getVolume() const {
  return audio_volume;
}
  
bool PlayBackContext::isMuted() const {
  return muted_;
}
  
void PlayBackContext::setVolume(int val) {
  audio_volume = val;
}

void PlayBackContext::setMute(bool v) {
  muted_ = v;
}

void PlayBackContext::toggleMute() {
  muted_ = !muted_;
}

void PlayBackContext::change_speed(double speed) {

  // do not rewind without video
  if (speed <= 0 && !video_st) {
    return;
  }

  // double tm = vidclk.pts;
  auto prev_paused = this->paused;
  if (!prev_paused) {
    this->stream_toggle_pause();
  }

  auto& prev = masterClock();
  auto prevIsRewindMode = rewindMode();

  this->speed_ = speed;
  this->extclk.set_clock_speed(speed);
  this->extclk.sync_clock_to_slave(&prev, 0.0);

  audclk.set_clock_speed(speed < 0 ? -1.0 : 1.0);
  vidclk.set_clock_speed(speed < 0 ? -1.0 : 1.0);

  if (speed < 0) {
    if (seekMethod_ == SEEK_METHOD_NONE) {
      auto vp = pictureQueue_.peek();
      frameRewindTarget_ = vp->frame->pkt_pts;
      sendSeekRequest(SEEK_METHOD_REWIND, vp->frame->pkt_pts);
    }
  } else {
    if (prevIsRewindMode) {
      rewind_ = false;
      //int64_t target_pos = (int64_t)(tm * AV_TIME_BASE);
      auto vp = pictureQueue_.peek();
      int64_t target_pos = av_rescale_q(vp->frame->pkt_pts, video_time_base_, AVRational{1, AV_TIME_BASE});
      sendSeekRequest(SEEK_METHOD_POS, target_pos);
    }
  }

  if (!prev_paused) {
    this->stream_toggle_pause();
  }
}

void PlayBackContext::step_to_next_frame()
{
  if (this->speed_ != 1.0) {
    if (prev_speed_ == 0)
      prev_speed_ = this->speed_;
    change_speed(1.0);
  }

  /* if the stream is paused unpause it, then step */
  if (this->paused)
    this->stream_toggle_pause();

  this->stepping_ = true;
}

void PlayBackContext::step_to_prev_frame() {
  if (this->speed_ != -1.0) {
    if (prev_speed_ == 0)
      prev_speed_ = this->speed_;
    change_speed(-1.0);
  }

  if (this->paused)
    this->stream_toggle_pause();

  this->stepping_ = true;
}

void PlayBackContext::togglePause()
{
  if (prev_speed_ != 0) {
    change_speed(prev_speed_);
    prev_speed_ = 0;
  }
  stream_toggle_pause();
  stepping_ = false;
}

void PlayBackContext::seek_chapter(int incr)
{
  int64_t pos = get_master_clock() * AV_TIME_BASE;
  int i;

  if (!this->ic->nb_chapters)
    return;

  /* find the current chapter */
  for (i = 0; i < this->ic->nb_chapters; i++) {
    AVChapter *ch = this->ic->chapters[i];
    if (av_compare_ts(pos, AVRational { 1, AV_TIME_BASE }, ch->start, ch->time_base) < 0) {
            i--;
            break;
    }
  }

  i += incr;
  i = FFMAX(i, 0);
  if (i >= this->ic->nb_chapters)
    return;

  av_log(NULL, AV_LOG_VERBOSE, "Seeking to chapter %d.\n", i);
  sendSeekRequest(SEEK_METHOD_POS, av_rescale_q(this->ic->chapters[i]->start, this->ic->chapters[i]->time_base,
	    AVRational{ 1, AV_TIME_BASE }));
}

void PlayBackContext::stream_toggle_pause()
{
  if (this->paused) {
    frame_timer_ += this->vidclk.time_passed();
    if (this->read_pause_return != AVERROR(ENOSYS)) {
      this->vidclk.paused = 0;
    }
    this->vidclk.update();
  }
  this->extclk.update();
  this->paused = this->audclk.paused = this->vidclk.paused = this->extclk.paused = !this->paused;

  if (onStatus) {
    onStatus(paused ? MEDIA_STATUS_PAUSED : MEDIA_STATUS_RESUMED);
  }
}

int PlayBackContext::onVideoFrameDecodedReversed(AVFrame *frame, int serial) {
  if (serial != videoSerial_) {
    av_frame_unref(frame);
    return 0;
  }

  if (frame->pkt_pts < frameRewindTarget_) {
    // bufferring
    AVRational tb = video_time_base_;
    double pts = (frame->pts == AV_NOPTS_VALUE) ? NAN : frame->pts * av_q2d(tb);
    rewindBuffer_.push_back(SimpleFrame(frame, serial, pts, frame_duration_));
  } else {
    av_frame_unref(frame); 

    frameRewindTarget_ = rewindBuffer_.empty() ? 0 : rewindBuffer_.front().frame->pkt_pts;
  
    // reverse put to frame queue
    while (!rewindBuffer_.empty()) {
      auto svp = std::move(rewindBuffer_.back());
      rewindBuffer_.pop_back();

      int ret = queuePicture(svp.frame, svp.pts, svp.duration, svp.frame->pkt_pos, svp.serial);
      av_frame_unref(svp.frame);
      if (ret < 0)
        return ret;
    }
  }
  return 0;
}

double PlayBackContext::computeVideoTargetDelayReversed(const Frame *lastvp, const Frame *vp) const {

  auto delay = vpDurationReversed(lastvp, vp);

  /* update delay to follow master synchronisation source */
  double diff =  get_master_clock() - vidclk.get_clock();

  /* skip or repeat frame. We take into account the
           delay to compute the threshold. I still don't know
           if it is the best guess */
  double sync_threshold = FFMAX(AV_SYNC_THRESHOLD_MIN, FFMIN(AV_SYNC_THRESHOLD_MAX, delay));
  if (!isnan(diff) && fabs(diff) < this->max_frame_duration) {
    if (diff <= -sync_threshold)
      delay = FFMAX(0, delay + diff);
    else if (diff >= sync_threshold && delay > AV_SYNC_FRAMEDUP_THRESHOLD)
                delay = delay + diff;
    else if (diff >= sync_threshold)
                delay = 2 * delay;
  }

  return delay;
}

double PlayBackContext::vpDurationReversed(const Frame *vp, const Frame *nextvp) const {
  if (vp->serial == nextvp->serial) {
    double duration = vp->pts - nextvp->pts;
    if (isnan(duration) || duration <= 0 || duration > this->max_frame_duration)
      return vp->duration;
    else
      return duration;
  } else {
    return 0.0;
  }
}

void PlayBackContext::video_refresh_rewind(double *remaining_time) {
retry:
  if (!rewindMode()) {
    return;
  }

  if (pictureQueue_.nb_remaining() == 0) {
    // nothing to do, no picture to display in the queue
  } else {
    /* dequeue the picture */
    auto lastvp = pictureQueue_.peek_last();
    auto vp = pictureQueue_.peek();

    if (vp->serial != videoSerial_) {
      pictureQueue_.next();
      goto retry;
    }

    if (lastvp->serial != vp->serial)
      frame_timer_ = av_gettime_relative() / 1000000.0;

    if (this->paused)
      return;

	  double time = av_gettime_relative() / 1000000.0;

    /* compute nominal last_duration */
    auto delay = computeVideoTargetDelayReversed(lastvp, vp);

    if (time < frame_timer_ + delay) {
      *remaining_time = FFMIN(frame_timer_ + delay - time, *remaining_time);
      return;
    }

    frame_timer_ += delay;
    if (delay > 0 && time - frame_timer_ > AV_SYNC_THRESHOLD_MAX)
      frame_timer_ = time;

    {
      std::lock_guard<std::mutex> lk(pictureQueue_.mtx);
      if (!isnan(vp->pts)) {
        /* update current video pts */
        this->vidclk.set_clock(vp->pts, vp->serial);
        this->extclk.sync_clock_to_slave(&this->vidclk);
      }
    }

    pictureQueue_.next();
    force_refresh_ = true;

    if (rewindEofPts_ >= vp->frame->pkt_pts && !this->paused) {
      stream_toggle_pause();
      change_speed(1.0);
      if (onStatus) {
        onStatus(MEDIA_STATUS_REWIND_END);
      }
    } else {
      if (this->stepping_ && !this->paused)
        stream_toggle_pause();
    }
  }
}

bool PlayBackContext::videoPacketIsAddonData(AVCodecID codec_id, const AVPacket *pkt) const {
  return false;
}

int PlayBackContext::dealWithDataPacket(const AVPacket *pkt, const int pkt_serial) {
  return 0;
}
