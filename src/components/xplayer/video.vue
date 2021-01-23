<template>
  <div ref="player" class="player">
    <canvas id="video"/>
    <canvas id="labels"/>
  </div>
</template>

<script>
/* eslint-disable no-useless-escape */
import {mapState} from 'vuex'
import {remote} from 'electron'
import fs from 'fs'
import {PlayBack} from '../../ffplay'
import YUVCanvas from './yuv-canvas'
import {DetectionCanvas} from './detection-canvas'
import {DecodeSync} from './test_decode'
import path from 'path'

let inDNNProcessing = false

const rtpsPatern = /^rtsp:\/\/(.*)/
const audioPattern = /Audio\: (.*)/;
const videoPattern = /Video\: (.*)/;

function PlayOverFFmpeg (core, url, transport) {
  const rtspMath = url.match(rtpsPatern)

  const vargs = []

  if (rtspMath) {
    // vargs.push('-analyzeduration')
    // vargs.push('10000000')
    vargs.push('-stimeout')
    vargs.push('5000000')
    if (transport === 'tcp') {
      vargs.push('-rtsp_transport')
      vargs.push('tcp')
    }
  }

  vargs.push(url)

  let ff 
  if (process.env.NODE_ENV === 'development' && path.basename(url) === 'foreman_352x288_30fps.h264') {
    ff = DecodeSync(vargs)
  } else {
    ff = new PlayBack(...vargs)
  }

  let _authRequire = false

  ff.on('status', v => {
    console.log('----> STATUS ', v);
    core._onStatus(v);
  });

  ff.on('end', () => {
    console.log('----> END ');
    core._onEnd(_authRequire);
  });

  ff.on('error', e => {
    if (e.message.indexOf('401 Unauthorized') >= 0) {
      _authRequire = true
      ff.quit('error')
    }
    core._onError(e);
  });

  ff.on('yuv', frame => {
    frame.cropLeft = 0;
    frame.cropTop = 0;
    frame.cropWidth = frame.width;
    frame.cropHeight = frame.height;

    core._renderFrame(frame);
  })

  ff.on('meta', ({duration, width, height, info}) => {
    let codecp = {}
    const lines = info
    if (duration === 0) {
      duration = Infinity
    }
    codecp.duration = duration
    codecp.window_size = [width, height]

    lines.split('\n').forEach(line => {
      // console.log(line)
      let audio = line.match(audioPattern);
      if (audio) {
        audio = audio[1].split(', ');
        codecp.audio = audio[0];
        codecp.audio_details = audio;
        return
      }

      let video = line.match(videoPattern)
      if (video) {
        video = video[1].split(', ');
        codecp.video = video[0];
        codecp.video_details = video;
      }
    })

    core._onMeta(codecp);
  })

  ff.on('statics', ({fps}) => {
    core.fps = fps
  })

  ff.on('time', time => {
    core._onTime(time);
  })

  ff.on('log', (level, msg) => {
    console.log(msg);
  })

  ff.on('detection', ret => {
    core._onDetection(ret);
  })

  return ff
}

export default {
  props: {
    transport: {default: 'udp', type: String},
    src: {type: String},
    inscreenCanvas: {type: Object},
    infoCrops: {type: Array},
    showDetection: {default: true, type: Boolean},
    dnnEngine: {type: Object}
  },

  data () {
    return {
      development: process.env.NODE_ENV === 'development',
      inferenceOptions: {},
      ff: null,
      mediaSize: null,
      codecp: {},
      faces: [],
      fps: null,
      // status
      isLoading: false,
      paused: true,
      ended: true,
      volume: 1.0,
      muted: false,
      currentTime: 0,
      durationTime: Infinity,
      speedRate: 1.0
    }
  },

  computed: {
    ...mapState('video', ['inferenceFeatures']),

    isPlaying () {
      return !this.paused && !this.ended
    },
    isLive () {
      return this.src && this.src.indexOf('rtsp') === 0
    }
  },

  watch: {
    src (v) {
      this.restart();
    },
    showDetection (v) {
      this.updateVideoInference()
    },
    dnnEngine (v) {
      if (v) {
        this.updateVideoInference()
      }
    },
    inferenceFeatures (v) {
      this.updateVideoInference()
    }
  },

  methods: {
    _onStatus (status) {
      if (status === 'paused') {
        this.paused = true
      } else if (status === 'resumed') {
        this.paused = false
      } else if (status === 'rewind_end') {
        this.speedRate = 1.0
      }
    },

    _onError (e) {
      this.$emit('notice', e.message);
    },

    _onEnd (authRequire) {
      this.ff = null
      this.isLoading = false
      this.ended = true
      this.paused = true

      if (authRequire) {
        this.$emit('auth-require')
      }
    },

    _onMeta (codecp) {
      this.codecp = codecp
      this.durationTime = codecp.duration
      console.log(codecp)

      this.isLoading = false
      this._syncVolume()
    },

    _syncVolume () {
      if (this.ff) {
        this.ff.volume(this.volume)
        if (this.muted) {
          this.ff.toogle_mute()
        }
      }
    },

    _onTime (time) {
      if (this._dragging) {
        return
      }
      this.currentTime = time
    },

    _renderFrame (frame) {
      // console.log(frame)
      if (this.inscreenCanvas) {
        this.inscreenCanvas.drawFrame(frame);
      } else {
        this.yuvCanvas.drawFrame(frame);
      }

      // use local AI engine
      // for test only
      if (this.showDetection && this.dnnEngine) {
        if (!inDNNProcessing) {
          inDNNProcessing = true
          const {frameId} = frame
          this.dnnEngine.predict({
            width: frame.width,
            height: frame.height,
            y: frame.y && frame.y.bytes,
            u: frame.u && frame.u.bytes,
            v: frame.v && frame.v.bytes,
            y_stride: frame.y && frame.y.stride,
            uv_stride: frame.u && frame.u.stride,
            ...this.inferenceOptions
          }).then(objects => {
            this._onDetection({
              version: 999,
              frameId,
              type: 999,
              objects
            })
          }).catch(err => {
            console.log(err)
          }).finally(() => {
            inDNNProcessing = false
          })
        }
      }
    },

    _onDetection (ret) {
      this.labelYuvCanvas.clear();

      if (!this.showDetection || !this.ff) {
        return
      }
      // console.log(ret)
      if (ret.objects) {
        const faces = []
        for (const det of ret.objects) {
          if (det.type === 0 && faces.length < this.infoCrops.length) {
            this.yuvCanvas.cropImage(this.infoCrops[faces.length], det.rect)
            faces.push({
              age: det.age,
              male: det.male,
              emotion: det.emotions && det.emotions.length > 0 ? det.emotions[0] : {}
            })
          }
          this.labelYuvCanvas.drawDetection(det)
        }
        if (faces.length) {
          this.faces = faces
        }
      }
    },

    quit (reason) {
      if (this.ff) {
        this.ff.quit(reason)
        this.ff = null
      }
    },

    restart () {
      if (this.ff) {
        this.ff.once('end', () => {
          this.ff = null
          this.play();
        })
        this.quit('restart')
      } else {
        this.play();
      }
    },

    play () {
      if (!this.src) {
        return
      }

      if (!this.ff) {
        let url = decodeURIComponent(this.src)
        this.durationTime = Infinity
        this.codecp = {}
        this.fps = null
        this.speedRate = 1.0

        this.ff = PlayOverFFmpeg(this, url, this.transport)

        this.paused = false
        this.isLoading = true
        this.ended = false
      } else {
        this.toggle();
      }
    },

    toggle () {
      if (this.ff) {
        this.ff.toogle_pause();
      }
    },

    clickPlay () {
      if (this.isLive) {
        return
      }
      if (this.ended) {
        this.play();
      } else {
        this.toggle();
      }
    },

    toggleMute () {
      if (this.ff) {
        this.ff.toogle_mute()
      }
      this.muted = !this.muted
    },

    setVolume (v) {
      if (this.ff) {
        this.ff.volume(v)
      }
      this.volume = v
    },

    playNextFrame () {
      if (this.ff) {
        this.ff.send('next_frame')
      }
    },

    playPrevFrame () {
      if (this.ff) {
        this.ff.send('prev_frame')
      }
    },

    speed (rate) {
      if (this.ff) {
        this.ff.speed(rate)
        this.speedRate = rate
      }
    },

    resize () {
      this.rescaleCanvasWindowSize();
    },

    seek (time) {
      time = Math.max(time, 0);
      if (this.durationTime) {
        time = Math.min(time, this.durationTime);
      }

      this.seekTo(time)
    },

    seekTo (v) {
      if (this.ff) {
        this.ff.seek_to(v)
        this.currentTime = v
      }
    },

    screenshot () {
      this.mainCanvas.toBlob(async blob => {
        let buffer = await blob.arrayBuffer();
        let data = new Uint8Array(buffer);

        const result = await remote.dialog.showSaveDialog(
          remote.getCurrentWindow(), {
            defaultPath: 'screenshot',
            filters: [{
              name: 'image/png',
              extensions: ['png']
            }]
          })

        if (!result || result.canceled) {
          return
        }

        fs.writeFile(result.filePath, data, () => {})
      });
    },

    rescaleCanvasWindowSize () {
      if (this.mainCanvas.width === 0 || this.mainCanvas.height === 0) {
        return
      }
      const {width, height} = this.container.getBoundingClientRect()
      const wRatio = width / this.mainCanvas.width
      const hRatio = height / this.mainCanvas.height
      const ratio = Math.min(wRatio, hRatio)
      this.ratio = ratio

      const w = parseInt(ratio * this.mainCanvas.width) + 'px'
      const h = parseInt(ratio * this.mainCanvas.height) + 'px'
      this.mainCanvas.style.width = w
      this.mainCanvas.style.height = h
      this.labelCanvas.style.width = w
      this.labelCanvas.style.height = h
    },

    changeWindowSize (width, height) {
      this.labelCanvas.width = width;
      this.labelCanvas.height = height;
      this.rescaleCanvasWindowSize()
    },

    updateVideoInference () {
      this.inferenceOptions = {
        faces: this.inferenceFeatures.indexOf('faces') >= 0,
        vehicles: this.inferenceFeatures.indexOf('vehicles') >= 0,
        personid: false,
        landmarks: this.inferenceFeatures.indexOf('landmarks') >= 0,
        faceid: this.inferenceFeatures.indexOf('faceid') >= 0,
        age: this.inferenceFeatures.indexOf('age') >= 0,
        emotions: this.inferenceFeatures.indexOf('emotions') >= 0,
        yolo: this.inferenceFeatures.indexOf('yolo') >= 0
      }

      if (!this.showDetection) {
        this.labelYuvCanvas.clear()
      }
    }
  },

  mounted () {
    const container = this.$refs.player;
    this.container = container
    this.mainCanvas = container.querySelector('#video')
    this.labelCanvas = container.querySelector('#labels')

    this.yuvCanvas = new YUVCanvas(this.mainCanvas, this)
    this.labelYuvCanvas = new DetectionCanvas(this.labelCanvas)

    this.restart();
  },

  beforeDestroy () {
    this.quit();
  }
}
</script>

<style lang="scss" scoped>
.player {
  position: relative;
  overflow: hidden;
  user-select: none;
  line-height: 1;
  height: 100%;
  background: #000;
  font-size: 0;
  display: flex;
  justify-content: center;
  align-items: center;

  * {
    box-sizing: content-box;
  }

  canvas {
    flex-shrink: 0;
  }
  #labels {
    position: absolute;
  }
}
</style>
