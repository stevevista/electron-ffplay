#include <napi.h>
#include <uv.h>

#include "player.h"
#include <unordered_map>
#include <set>

#ifndef NAPI_CPP_EXCEPTIONS
#error ThreadSafeCallback needs napi exception support
#endif

#if defined(_MSC_VER) && !_HAS_EXCEPTIONS
#error Please define _HAS_EXCEPTIONS=1, otherwise exception handling will not work properly
#endif


static constexpr size_t MAX_CACHE_DETECTION_OBJECTS = 512;

/*
{
  format
  bit_rate
  bits_per_coded_sample
  bits_per_raw_sample
  profile
  level
  width
  height
  channel_layout
  channels
  sample_rate
  frame_size
}
*/
AVCodecParameters* getCodecParams(Napi::Value nval) {
  if (!nval.IsObject()) {
    return nullptr;
  }

  auto params = avcodec_parameters_alloc();

  Napi::Object props = nval.As<Napi::Object>();
  auto keys = props.GetPropertyNames();
  for (uint32_t i = 0; i < keys.Length(); i++) {
    Napi::Value kobj = keys[i];
    auto vobj = props.Get(kobj);
    std::string k = kobj.As<Napi::String>();

    if (k == "format" && vobj.IsNumber()) {
      params->format = vobj.As<Napi::Number>().Int32Value();
    } else if (k == "bit_rate" && vobj.IsNumber()) {
      params->bit_rate = vobj.As<Napi::Number>().Int64Value();
    } else if (k == "bits_per_coded_sample" && vobj.IsNumber()) {
      params->bits_per_coded_sample = vobj.As<Napi::Number>().Int32Value();
    } else if (k == "bits_per_raw_sample" && vobj.IsNumber()) {
      params->bits_per_raw_sample = vobj.As<Napi::Number>().Int32Value();
    } else if (k == "profile" && vobj.IsNumber()) {
      params->profile = vobj.As<Napi::Number>().Int32Value();
    } else if (k == "level" && vobj.IsNumber()) {
      params->level = vobj.As<Napi::Number>().Int32Value();
    } else if (k == "width" && vobj.IsNumber()) {
      params->width = vobj.As<Napi::Number>().Int32Value();
    } else if (k == "height" && vobj.IsNumber()) {
      params->height = vobj.As<Napi::Number>().Int32Value();
    } else if (k == "channel_layout" && vobj.IsNumber()) {
      params->channel_layout = vobj.As<Napi::Number>().Int64Value();
    } else if (k == "channels" && vobj.IsNumber()) {
      params->channels = vobj.As<Napi::Number>().Int32Value();
    } else if (k == "sample_rate" && vobj.IsNumber()) {
      params->sample_rate = vobj.As<Napi::Number>().Int32Value();
    } else if (k == "frame_size" && vobj.IsNumber()) {
      params->frame_size = vobj.As<Napi::Number>().Int32Value();
    }
  }

  return params;
}

//
//

class DecoderObject : public Napi::ObjectWrap<DecoderObject> {
public:
  static Napi::Object Init(Napi::Env env, Napi::Object exports);
  static Napi::Object NewInstance(const Napi::CallbackInfo& info);

  DecoderObject(const Napi::CallbackInfo& info);
  ~DecoderObject() {}

private:
  Napi::Value Decode(const Napi::CallbackInfo& info);
  Napi::Value Close(const Napi::CallbackInfo& info);

private:
  static Napi::FunctionReference constructor;
  shared_ptr<SyncDecoder> decoder_;
};

Napi::FunctionReference DecoderObject::constructor;

Napi::Object DecoderObject::Init(Napi::Env env, Napi::Object exports) {
  Napi::HandleScope scope(env);

  Napi::Function func = DefineClass(env, "Decoder", {
    InstanceMethod("decode", &Decode),
    InstanceMethod("close", &Close),
  });

  constructor = Napi::Persistent(func);
  constructor.SuppressDestruct();

  exports.Set("Decoder", func);
  return exports;
}


DecoderObject::DecoderObject(const Napi::CallbackInfo& info)
: Napi::ObjectWrap<DecoderObject>(info) {
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);

  if (info.Length() < 1 || !info[0].IsString()) {
    throw Napi::TypeError::New(info.Env(), "must specify a codec name.");
  }

  string name = info[0].As<Napi::String>();

  AVCodecParameters* params = nullptr;
  if (info.Length() > 1) {
    params = getCodecParams(info[1]);
  }

  auto codec = SyncDecoder::open(name.c_str(), params);
  if (params) {
    avcodec_parameters_free(&params);
  }
  if (!codec) {
    throw Napi::TypeError::New(info.Env(), "cannot open codec.");
  }

  decoder_ = shared_ptr<SyncDecoder>(codec);
}

Napi::Value DecoderObject::Decode(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1 || !info[0].IsBuffer()) {
    throw Napi::TypeError::New(info.Env(), "must specify a uint8buffer.");
  }

  if (!decoder_) {
    return env.Undefined();
  }

  auto buf = info[0].As<Napi::Buffer<uint8_t>>();

  int out_type = 0;  // 0 padded yuv 1 unpad yuv 2 rgba
  int offset = 0;
  int buf_size = -1;

  if (info.Length() > 1 && info[1].IsNumber()) {
    buf_size = info[1].As<Napi::Number>().Int32Value();
  }

  if (info.Length() > 2 && info[2].IsNumber()) {
    offset = info[2].As<Napi::Number>().Int32Value();
  }

  if (info.Length() > 3 && info[3].IsNumber()) {
    out_type = info[3].As<Napi::Number>().Int32Value();
  }

  auto ptr = buf.Data();

  if (buf_size < 0) {
    buf_size = (int)buf.Length();
  }

  AVFrame* got_frame_ = nullptr;
  int ret = decoder_->decodeBuffer(ptr + offset, buf_size, &got_frame_);

  if (ret < 0) {
    // AVERROR_INVALIDDATA
    std::string errstr;
    if (AVERROR_INVALIDDATA == ret) {
      errstr = "invalid data";
    } else {
      errstr = "send packet failed. code: " + std::to_string(ret);
    }
    throw Napi::TypeError::New(info.Env(), errstr);
  }

  if (got_frame_) {
    auto frame = got_frame_;

    if (frame->width > 0 && frame->height > 0) {
      // picture
      auto out = Napi::Array::New(env);
      uint32_t i = 0;

      
      auto _width = frame->width;
      auto _height = frame->height;
      auto _ystride = frame->linesize[0];
      auto _ustride = frame->linesize[1];
      auto _vstride = frame->linesize[2];
      auto width = Napi::Number::New(env, _width);
      auto height = Napi::Number::New(env, _height);

      out.Set(i++, width);
      out.Set(i++, height);

      if (out_type == 0) {
        auto ystride = Napi::Number::New(env, frame->linesize[0]);
        auto ustride = Napi::Number::New(env, frame->linesize[1]);
        auto vstride = Napi::Number::New(env, frame->linesize[2]);
        auto y = Napi::Buffer<uint8_t>::Copy(env, (const uint8_t*)frame->data[0], frame->linesize[0]*_height);
        auto u = Napi::Buffer<uint8_t>::Copy(env, (const uint8_t*)frame->data[1], frame->linesize[1]*_height/2);
        auto v = Napi::Buffer<uint8_t>::Copy(env, (const uint8_t*)frame->data[2], frame->linesize[2]*_height/2);

        out.Set(i++, ystride);
        out.Set(i++, ustride);
        out.Set(i++, vstride);
        out.Set(i++, y);
        out.Set(i++, u);
        out.Set(i++, v);
      } else if (out_type == 1) {
        auto ysize = _width * _height;
        auto usize = _width /2 * _height / 2;
        auto vsize = _width /2 * _height / 2;
        auto data = Napi::Buffer<uint8_t>::New(env, ysize + usize + vsize);
        auto ptr = data.Data();
        auto src = frame->data[0];
        for (int h=0; h < _height; h++) {
          memcpy(ptr, src, _width);
          ptr += _width;
          src += _ystride;
        }

        src = frame->data[1];
        for (int h=0; h < _height/2; h++) {
          memcpy(ptr, src, _width/2);
          ptr += _width/2;
          src += _ustride;
        }

        src = frame->data[2];
        for (int h=0; h < _height/2; h++) {
          memcpy(ptr, src, _width/2);
          ptr += _width/2;
          src += _vstride;
        }
        out.Set(i++, data);
      }

      av_frame_unref(frame);
      return out;
    }
  }

  return env.Undefined();
}

Napi::Value DecoderObject::Close(const Napi::CallbackInfo& info) {
  if (decoder_) {
    decoder_.reset();
  }
  return info.Env().Undefined();
}

////////////////////////////////////////////////////////////////
class ThreadSafeCallback {
public:
  // The argument function is responsible for providing napi_values which will
  // be used for invoking the callback. Since this touches JS state it must run
  // in the NodeJS main loop.
  using arg_func_t = std::function<void(napi_env, std::vector<napi_value>&)>;

  // Must be called from Node event loop because it calls napi_create_reference and uv_async_init
  ThreadSafeCallback(const Napi::Function& callback, Napi::Value receiver) {
    callback_ = Napi::Persistent(callback);
    receiver_ = Napi::Persistent(receiver);
    uv_async_init(uv_default_loop(), &handle_, &static_async_callback);
    handle_.data = this;
  }

  void call(arg_func_t arg_function) {
    std::lock_guard<std::mutex> lock(mutex_);
    function_pairs_.push_back(arg_function);
    uv_async_send(&handle_);
  }

  void close() {
    std::lock_guard<std::mutex> lock(mutex_);
    close_ = true;
    uv_async_send(&handle_);
  }

protected:
  static void static_async_callback(uv_async_t *handle) {
    try {
      static_cast<ThreadSafeCallback *>(handle->data)->async_callback();
    } catch (std::exception& e) {
      Napi::Error::Fatal("", e.what());
    } catch (...) {
      Napi::Error::Fatal("", "ERROR: Unknown exception during async callback");
    }
  }

  void async_callback() {
    auto env = callback_.Env();
    while (true) {
      std::vector<arg_func_t> func_pairs;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        if (function_pairs_.empty())
          break;
        else
          func_pairs.swap(function_pairs_);
      }

      for (const auto &func : func_pairs) {
        Napi::HandleScope scope(env);
        std::vector<napi_value> args;
        func(env, args);

        Napi::Error error(env, nullptr);
        try {
          callback_.MakeCallback(receiver_.Value(), args);
        } catch (Napi::Error& err) {
          error = std::move(err);
        }

        if (!error.IsEmpty())
          throw std::runtime_error(error.Message());
      }
    }

    if (close_)
      uv_close(reinterpret_cast<uv_handle_t *>(&handle_), [](uv_handle_t *handle) {
        delete static_cast<ThreadSafeCallback *>(handle->data);
      });
  }

  Napi::FunctionReference callback_;
  Napi::Reference<Napi::Value> receiver_;
  uv_async_t handle_;
  std::vector<arg_func_t> function_pairs_;
  bool close_{false};
  std::mutex mutex_;

protected:
  // Cannot be copied or assigned
  ThreadSafeCallback(ThreadSafeCallback&& other) = delete;
  ThreadSafeCallback(const ThreadSafeCallback&) = delete;
  ThreadSafeCallback& operator=(const ThreadSafeCallback&) = delete;
  ThreadSafeCallback& operator=(ThreadSafeCallback&&) = delete;
};

class PlayBackObject : public Napi::ObjectWrap<PlayBackObject> {
public:
  static Napi::Object Init(Napi::Env env, Napi::Object exports);
  static Napi::Object NewInstance(const Napi::CallbackInfo& info);

  PlayBackObject(const Napi::CallbackInfo& info);
  ~PlayBackObject() {}

private:
  Napi::Value Send(const Napi::CallbackInfo& info);

private:
  void iyuv_callback(ThreadSafeCallback* safe_callback, AVFrame* frame, double pts, int64_t id);

private:
  static Napi::FunctionReference constructor;
  PlayBackContext* ctx_{nullptr};
  vector<string> vargs_;

  mutable std::mutex mtxPlaying_;

  std::deque<Detection_t> pending_dets_;
  mutable mutex mtx_;
  condition_variable cond_;
};

Napi::FunctionReference PlayBackObject::constructor;

Napi::Object PlayBackObject::Init(Napi::Env env, Napi::Object exports) {
  Napi::HandleScope scope(env);

  Napi::Function func = DefineClass(env, "PlayBack", {
    InstanceMethod("send", &Send)
  });

  constructor = Napi::Persistent(func);
  constructor.SuppressDestruct();

  exports.Set("PlayBack", func);
  return exports;
}

PlayBackObject::PlayBackObject(const Napi::CallbackInfo& info)
: Napi::ObjectWrap<PlayBackObject>(info) {
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);

  Napi::Function emit =
      info.This().As<Napi::Object>().Get("emit").As<Napi::Function>();

  vargs_.push_back("ffplay");

  for (uint32_t i = 0; i < info.Length(); i++) {
    if (info[i].IsString()) {
      vargs_.push_back(info[i].As<Napi::String>());
    } else if (info[i].IsNumber()) {
      auto dval = info[2].As<Napi::Number>().DoubleValue();
      vargs_.push_back(to_string(dval));
    } else {
      // ignore non-string non-number args
    }
  }

  if (vargs_.size() == 0) {
    throw Napi::TypeError::New(info.Env(), "missing parameters");
  }

  ctx_ = new PlayBackContext();

  //auto safe_callback = new ThreadSafeCallback(callback);
  auto safe_callback = new ThreadSafeCallback(emit, info.This());

  ctx_->onLog = [this, safe_callback](int level, const string& msg) {
    safe_callback->call([level, msg](Napi::Env env, std::vector<napi_value>& args) {
      // This will run in main thread and needs to construct the
      // arguments for the call
      args = { Napi::String::New(env, "log"), Napi::Number::New(env, level), Napi::String::New(env, msg) };
    });
  };

  ctx_->onClockUpdate = [this, safe_callback](double timestamp) {
    safe_callback->call([timestamp](Napi::Env env, std::vector<napi_value>& args) {
      // This will run in main thread and needs to construct the
      // arguments for the call
      args = { Napi::String::New(env, "time"), Napi::Number::New(env, timestamp) };
    });
  };

  ctx_->onStatus = [this, safe_callback](MediaStatus status) {
    safe_callback->call([status](Napi::Env env, std::vector<napi_value>& args) {
      // This will run in main thread and needs to construct the
      // arguments for the call
      napi_value jsstatus;
      switch (status) {
      case MEDIA_STATUS_START:
        jsstatus = Napi::String::New(env, "start");
        break;
      case MEDIA_STATUS_PAUSED:
        jsstatus = Napi::String::New(env, "paused");
        break;
      case MEDIA_STATUS_RESUMED:
        jsstatus = Napi::String::New(env, "resumed");
        break;
      case MEDIA_STATUS_REWIND_END:
        jsstatus = Napi::String::New(env, "rewind_end");
        break;
      default:
        jsstatus = Napi::String::New(env, "unknown");
        break;
      }
      args = { Napi::String::New(env, "status"), jsstatus };
    });
  };

  ctx_->onMetaInfo = [this, safe_callback](
    double start_time, 
    double duration, 
    int res_width,
    int res_height,
    const char* str) {
    std::string metainfo = str;
    safe_callback->call([start_time, duration, res_width, res_height, metainfo](Napi::Env env, std::vector<napi_value>& args) {
      // This will run in main thread and needs to construct the
      // arguments for the call
      auto info = Napi::Object::New(env);
      info.Set(Napi::String::New(env, "start_time"), Napi::Number::New(env, start_time));
      info.Set(Napi::String::New(env, "duration"), Napi::Number::New(env, duration));
      info.Set(Napi::String::New(env, "width"), Napi::Number::New(env, res_width));
      info.Set(Napi::String::New(env, "height"), Napi::Number::New(env, res_height));
      info.Set(Napi::String::New(env, "info"), Napi::String::New(env, metainfo));
      args = { Napi::String::New(env, "meta"), info };
    });
  };

  ctx_->onStatics = [this, safe_callback](double fps, double tbr, double tbn, double tbc) {
    
    safe_callback->call([=](Napi::Env env, std::vector<napi_value>& args) {
      auto info = Napi::Object::New(env);
      info.Set(Napi::String::New(env, "fps"), Napi::Number::New(env, fps));
      info.Set(Napi::String::New(env, "tbr"), Napi::Number::New(env, tbr));
      info.Set(Napi::String::New(env, "tbn"), Napi::Number::New(env, tbn));
      info.Set(Napi::String::New(env, "tbc"), Napi::Number::New(env, tbc));
      args = { Napi::String::New(env, "statics"), info };
    });
  };

  ctx_->onIYUVDisplay = [this, safe_callback](AVFrame* frame, double pts, int64_t id) {
    iyuv_callback(safe_callback, frame, pts, id);
  };

  ctx_->onAIData = [this](const Detection_t& detection, double pts) {
  };

  // play
  std::thread([this, safe_callback] {

    vector<char*> argv_;
    argv_.resize(vargs_.size());
    char **_argv = &argv_[0];

    int argc = (int)argv_.size();
    for (int i = 0; i < argc; i++) {
      _argv[i] = (char*)vargs_[i].c_str();
    }

    char **argv = &argv_[0];

    try {
      ctx_->eventLoop(argc, argv);
    } catch (const exception& e) {
      string err = e.what();
      safe_callback->call([err](Napi::Env env, std::vector<napi_value>& args) {
        // This will run in main thread and needs to construct the
        // arguments for the call
        args = { Napi::String::New(env, "error"), Napi::Error::New(env, err).Value() };
      });
    } catch (...) {
      safe_callback->call([](Napi::Env env, std::vector<napi_value>& args) {
        // This will run in main thread and needs to construct the
        // arguments for the call
        args = { Napi::String::New(env, "error"), Napi::Error::New(env, "unexpect fatal error").Value() };
      });
    }

    safe_callback->call([](Napi::Env env, std::vector<napi_value>& args) {
      // This will run in main thread and needs to construct the
      // arguments for the call
      args = { Napi::String::New(env, "end") };
    });

    // close context
    {
      std::lock_guard<std::mutex> lk(mtxPlaying_);
      delete ctx_;
      ctx_ = nullptr;
      safe_callback->close();
    }
  }).detach();
}

Napi::Value PlayBackObject::Send(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1 || !info[0].IsString()) {
    throw Napi::TypeError::New(env, "need at least one parameters: (event, [...args])");
  }

  string eventStr = info[0].As<Napi::String>();
  int arg0 = 0;
  double arg1 = 0;
  double arg2 = 0;
  int event;

  if (info.Length() > 1  && info[1].IsNumber()) {
    arg0 = info[1].As<Napi::Number>().Int32Value();
  }

  if (info.Length() > 2  && info[2].IsNumber()) {
    arg1 = info[2].As<Napi::Number>().DoubleValue();
  }

  if (info.Length() > 3 && info[3].IsNumber()) {
    arg2 = info[3].As<Napi::Number>().DoubleValue();
  }

  if (eventStr == "quit") {
    event = MEDIA_CMD_QUIT;
  } else if (eventStr == "pause") {
    event = MEDIA_CMD_PAUSE;
  } else if (eventStr == "mute") {
    event = MEDIA_CMD_VOLUMN;
    arg0 = 0;
  } else if (eventStr == "volume") {
    event = MEDIA_CMD_VOLUMN;
  } else if (eventStr == "seek") {
    event = MEDIA_CMD_SEEK;
  } else if (eventStr == "next_frame") {
    event = MEDIA_CMD_NEXT_FRAME;
  } else if (eventStr == "prev_frame") {
    event = MEDIA_CMD_PREV_FRAME;
  } else if (eventStr == "chapter") {
    event = MEDIA_CMD_CHAPTER;
  } else if (eventStr == "speed") {
    event = MEDIA_CMD_SPEED;
  }

  {
    lock_guard<mutex> lock(mtxPlaying_);
    if (!ctx_) {
      return Napi::Boolean::New(env, false);
    }

    ctx_->sendEvent(event, arg0, arg1, arg2);
  }

  return Napi::Boolean::New(env, true);
}

void PlayBackObject::iyuv_callback(ThreadSafeCallback* safe_callback, AVFrame* frame, double pts, int64_t id) {

  unique_lock<mutex> lock(mtx_);

  lock.unlock();

  const auto width = frame->width;
  const auto height = frame->height;

  // output yuv
  if (width > 0 && height > 0) {
    safe_callback->call([this, frame, width, height, id](Napi::Env env, std::vector<napi_value>& args) {
      // This will run in main thread and needs to construct the
      // arguments for the call
      napi_value jswidth = Napi::Number::New(env, width);
      napi_value jsheight = Napi::Number::New(env, height);
      napi_value frame_id = Napi::Number::New(env, id);

      auto ystride = Napi::Number::New(env, frame->linesize[0]);
      auto ustride = Napi::Number::New(env, frame->linesize[1]);
      auto vstride = Napi::Number::New(env, frame->linesize[2]);
      auto y = Napi::Buffer<uint8_t>::Copy(env, frame->data[0], frame->linesize[0] * height);
      auto u = Napi::Buffer<uint8_t>::Copy(env, frame->data[1], frame->linesize[1] * height / 2);
      auto v = Napi::Buffer<uint8_t>::Copy(env, frame->data[2], frame->linesize[2] * height / 2);

      // auto v = Napi::Buffer<uint8_t>::New(env, (uint8_t*)frame->data[2], (size_t)frame->linesize[2] * height / 2, [](Napi::Env, uint8_t*) {});

      auto yuv_buffer = Napi::Object::New(env);

      yuv_buffer.Set(Napi::String::New(env, "frameId"), frame_id);
      yuv_buffer.Set(Napi::String::New(env, "width"), jswidth);
      yuv_buffer.Set(Napi::String::New(env, "height"), jsheight);

      auto y_obj = Napi::Object::New(env);
      auto u_obj = Napi::Object::New(env);
      auto v_obj = Napi::Object::New(env);

      y_obj.Set(Napi::String::New(env, "bytes"), y);
      y_obj.Set(Napi::String::New(env, "stride"), ystride);

      u_obj.Set(Napi::String::New(env, "bytes"), u);
      u_obj.Set(Napi::String::New(env, "stride"), ustride);

      v_obj.Set(Napi::String::New(env, "bytes"), v);
      v_obj.Set(Napi::String::New(env, "stride"), vstride);

      yuv_buffer.Set(Napi::String::New(env, "y"), y_obj);
      yuv_buffer.Set(Napi::String::New(env, "u"), u_obj);
      yuv_buffer.Set(Napi::String::New(env, "v"), v_obj);

      args = { Napi::String::New(env, "yuv"), yuv_buffer };
      cond_.notify_one();
    });

    lock.lock();
    cond_.wait(lock);
  }
}

Napi::Object Init(Napi::Env env, Napi::Object exports) {
  try {
    ff_init();
  } catch (const exception& e) {
    throw Napi::TypeError::New(env, e.what());
  }

  DecoderObject::Init(env, exports);
  PlayBackObject::Init(env, exports);
  return exports;
}

NODE_API_MODULE(ff_binding, Init)
