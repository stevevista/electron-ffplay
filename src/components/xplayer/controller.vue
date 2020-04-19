<template>
  <div class="ff-player-controller">
    <!-- left -->
    <div class="player-icons player-icons-left">
      <div class="player-play-streams" v-show="showOpen">
        <button class="player-icon player-play-streams-sub" style="top: -60px;" :aria-label="$t('video.openUrl')" data-balloon-pos="right" @click="$emit('open-stream')">
          <span class="player-icon-content"><span class="mdi mdi-link-variant mdi-light mdi-24px"></span></span>
        </button>
        <button class="player-icon player-play-streams-sub" style="top: -30px;" :aria-label="$t('video.openDir')" data-balloon-pos="right" @click="$emit('open-dir')">
          <span class="player-icon-content"><span class="mdi mdi-playlist-play mdi-light mdi-24px"></span></span>
        </button>
        <button class="player-icon" @click="$emit('open-files')">
          <span class="player-icon-content"><span class="mdi mdi-folder-open mdi-light mdi-24px"></span></span>
        </button>
      </div>
      <div class="player-play-streams" v-if="streams && streams.length > 1">
        <button 
              v-for="(s,index) in streams"
              :key="index"
              v-show="index > 0"
              class="player-icon player-play-streams-sub" :style="`top: -${index*30}px;`" :aria-label="s[1]" data-balloon-pos="right" @click="url=s[2]">
                <span class="player-icon-content"><span :class="`mdi mdi-numeric-${s[0]}-box mdi-light mdi-24px`"></span></span>
        </button>
        <button class="player-icon" :aria-label="streams[0][1]" data-balloon-pos="right">
              <span class="player-icon-content"><span :class="`mdi mdi-numeric-${streams[0][0]}-box mdi-light mdi-24px`"></span></span>
        </button>
      </div>
      <control-button
          :icon="video.ended ? 'replay' : (video.paused ? 'play' : 'pause')"
          :disabled="!video.src"
          v-show="showPlay"
          @click="clickPlay"/>
      <control-button
          icon="arrow-left-bold-box"
          @click="playPrevFrame"
          :tooltip="$t('video.prev_frame')"
          :disabled="!showPlay || !video.src || video.ended"
          v-show="development && showPlay && !video.isLive"/>
      <control-button
          icon="arrow-right-bold-box"
          @click="playNextFrame"
          :tooltip="$t('video.next_frame')"
          :disabled="!showPlay || !video.src || video.ended"
          v-show="development && showPlay && !video.isLive"/>
      <div class="player-volume" ref="volumeButton">
        <control-button
            :icon="volumeIcon"
            @click="clickVolumne"/>
        <div class="player-volume-bar-wrap" data-balloon-pos="up">
            <div class="player-volume-bar">
              <div class="player-volume-bar-inner" :style="`width: ${volumePercentage*100}%`">
                <span class="player-thumb" :style="`background: #b7daff`"></span>
              </div>
            </div>
          </div>
        </div>
        <span class="player-time">
          {{ secondToTime(video.currentTime) }} / {{ secondToTime(video.durationTime) }}
      </span>
    </div>
    <!-- right -->
    <div class="player-icons player-icons-right">
      <control-button
          icon="information-variant"
          @click="$emit('toggle-info')"/>
      <control-button
          :disabled="video.ended"
          icon="camera"
          @click="screenshot"
          :tooltip="$t('video.snapshot')"/>
      <control-button
          icon="face-recognition"
          @click="$emit('toggle-show-detection')"
          :inactive="!showDetection"
          :tooltip="$t('video.detection')"/>
      <control-button
          :icon="isFullScreen ? 'fullscreen-exit' : 'crop-free'"
          @click="$emit('toggle-fullscreen')"
          :tooltip="$t('video.Full_screen')"/>
      <control-button
            :disabled="video.ended || isFullScreen"
            icon="vector-difference-ba"
            @click="$emit('toggle-in-screen')"
            :tooltip="$t('video.in_screen')"/>
    </div>
    <!-- play bar -->
    <div class="player-bar-wrap" ref="playedBarWrap">
      <div class="player-bar-time hidden">00:00</div>
      <div class="player-bar-preview"></div>
      <div class="player-bar">
        <div class="player-played" :style="`width: ${playedPercentage*100}%`">
          <span class="player-thumb" :style="`background: #b7daff`"></span>
        </div>
      </div>
    </div>
  </div>
</template>

<script>
/* eslint-disable no-template-curly-in-string */

/**
  * optimize control play progress

  * optimize get element's view position,for float dialog video player
  */
function getBoundingClientRectViewLeft (element) {
  const scrollTop = window.scrollY || window.pageYOffset || document.body.scrollTop + ((document.documentElement && document.documentElement.scrollTop) || 0);

  if (element.getBoundingClientRect) {
    if (typeof getBoundingClientRectViewLeft.offset !== 'number') {
      let temp = document.createElement('div');
      temp.style.cssText = 'position:absolute;top:0;left:0;';
      document.body.appendChild(temp);
      getBoundingClientRectViewLeft.offset = -temp.getBoundingClientRect().top - scrollTop;
      document.body.removeChild(temp);
      temp = null;
    }
    const rect = element.getBoundingClientRect();
    const offset = getBoundingClientRectViewLeft.offset;

    return rect.left + offset;
  } else {
    // not support getBoundingClientRect
    return getElementViewLeft(element);
  }
}

/**
 * control play progress
 */
// get element's view position
function getElementViewLeft (element) {
  let actualLeft = element.offsetLeft;
  let current = element.offsetParent;
  const elementScrollLeft = document.body.scrollLeft + document.documentElement.scrollLeft;
  if (!document.fullscreenElement && !document.mozFullScreenElement && !document.webkitFullscreenElement) {
    while (current !== null) {
      actualLeft += current.offsetLeft;
      current = current.offsetParent;
    }
  } else {
    while (current !== null && current !== element) {
      actualLeft += current.offsetLeft;
      current = current.offsetParent;
    }
  }
  return actualLeft - elementScrollLeft;
}

const ControlButton = {
  template: ' <button class="player-icon" @click="!disabled && $emit(\'click\')" :style="style" :aria-label="tooltip" data-balloon-pos="up">' +
            '  <span :class="`mdi mdi-${icon} mdi-light mdi-24px`"></span>' +
            '</button>',
  props: {
    icon: {type: String, default: ''},
    disabled: {type: Boolean, default: false},
    inactive: {type: Boolean, default: false},
    tooltip: {type: String, default: null}
  },
  computed: {
    style () {
      if (this.inactive) return 'opacity: 0.4';
      if (this.disabled) return 'opacity: 0.4';
      return '';
    }
  }
};

export default {
  props: {
    video: {required: true, type: Object},
    streams: {type: Array},
    isFullScreen: {default: false, type: Boolean},
    showOpen: {default: false, type: Boolean},
    showPlay: {default: false, type: Boolean},
    showDetection: {default: false, type: Boolean}
  },
  components: {
    ControlButton
  },
  data () {
    return {
      development: process.env.NODE_ENV === 'development'
    }
  },
  computed: {
    playedPercentage () {
      return this.video.durationTime ? this.video.currentTime / this.video.durationTime : 0;
    },
    volumePercentage () {
      return this.video.muted ? 0 : this.video.volume
    },
    volumeIcon () {
      if (this.video.muted) {
        return 'volume-mute';
      } else if (this.video.volume >= 0.95) {
        return 'volume-high';
      } else if (this.video.volume > 0.5) {
        return 'volume-medium';
      } else {
        return 'volume-low';
      }
    }
  },
  watch: {
  },
  mounted () {
    this.volumeButton = this.$refs.volumeButton;
    this.volumeBarEl = this.volumeButton.querySelector('.player-volume-bar');
    this.volumeBarWrapEl = this.volumeButton.querySelector('.player-volume-bar-wrap');
    this.playedBarWrapEl = this.$refs.playedBarWrap;
    this.playedBarTimeEl = this.playedBarWrapEl.querySelector('.player-bar-time');

    this.initPlayedBar();
    this.initVolumeButton();
  },
  methods: {
    clickPlay () {
      this.video.clickPlay()
    },
    playPrevFrame () {
      this.video.playPrevFrame()
    },
    playNextFrame () {
      this.video.playNextFrame()
    },
    clickVolumne () {
      this.video.toggleMute()
    },
    screenshot () {
      this.video.screenshot()
    },

    volumeChange (percentage, nostorage) {
      percentage = parseFloat(percentage);
      if (!isNaN(percentage)) {
        percentage = Math.max(percentage, 0);
        percentage = Math.min(percentage, 1);
        const formatPercentage = `${(percentage * 100).toFixed(0)}%`;
        this.volumeBarWrapEl.dataset.balloon = formatPercentage;
        if (!nostorage) {
          localStorage.setItem('settings.video.volume', percentage);
        }
        this.$emit('notice', `${this.$t('video.Volume')} ${(percentage * 100).toFixed(0)}%`);
        this.video.setVolume(percentage);
      }
    },

    initPlayedBar () {
      const thumbMove = (e) => {
        let percentage = ((e.clientX || e.changedTouches[0].clientX) - getBoundingClientRectViewLeft(this.playedBarWrapEl)) / this.playedBarWrapEl.clientWidth;
        percentage = Math.max(percentage, 0);
        percentage = Math.min(percentage, 1);
        this.currentTime = percentage * this.video.durationTime
      };

      const thumbUp = (e) => {
        document.removeEventListener('mouseup', thumbUp);
        document.removeEventListener('mousemove', thumbMove);
        let percentage = ((e.clientX || e.changedTouches[0].clientX) - getBoundingClientRectViewLeft(this.playedBarWrapEl)) / this.playedBarWrapEl.clientWidth;
        percentage = Math.max(percentage, 0);
        percentage = Math.min(percentage, 1);
        this.video.seek(percentage * this.video.durationTime);
        this._dragging = false;
      };

      this.playedBarWrapEl.addEventListener('mousedown', () => {
        if (this.video.ended) {
          return
        }
        this._dragging = true;
        document.addEventListener('mousemove', thumbMove);
        document.addEventListener('mouseup', thumbUp);
      });

      this.playedBarWrapEl.addEventListener('mousemove', (e) => {
        if (this.video.durationTime) {
          const px = this.playedBarWrapEl.getBoundingClientRect().left;
          const tx = (e.clientX || e.changedTouches[0].clientX) - px;
          if (tx < 0 || tx > this.playedBarWrapEl.offsetWidth) {
            return;
          }
          const time = this.video.durationTime * (tx / this.playedBarWrapEl.offsetWidth);
          this.playedBarTimeEl.style.left = `${tx - (time >= 3600 ? 25 : 20)}px`;
          this.playedBarTimeEl.innerText = this.secondToTime(time);
          this.playedBarTimeEl.classList.remove('hidden');
        }
      });

      this.playedBarWrapEl.addEventListener('mouseenter', () => {
        if (this.video.durationTime) {
          this.playedBarTimeEl.classList.remove('hidden');
        }
      });

      this.playedBarWrapEl.addEventListener('mouseleave', () => {
        if (this.video.durationTime) {
          this.playedBarTimeEl.classList.add('hidden');
        }
      });
    },

    initVolumeButton () {
      const vWidth = 35;

      const volumeMove = (event) => {
        const e = event || window.event;
        const percentage = ((e.clientX || e.changedTouches[0].clientX) - getBoundingClientRectViewLeft(this.volumeBarEl) - 5.5) / vWidth;
        this.volumeChange(percentage);
      };
      const volumeUp = () => {
        document.removeEventListener('mouseup', volumeUp);
        document.removeEventListener('mousemove', volumeMove);
        this.volumeButton.classList.remove('player-volume-active');
      };

      this.volumeBarWrapEl.addEventListener('click', (event) => {
        const e = event || window.event;
        const percentage = ((e.clientX || e.changedTouches[0].clientX) - getBoundingClientRectViewLeft(this.volumeBarEl) - 5.5) / vWidth;
        this.volumeChange(percentage);
      });
      this.volumeBarWrapEl.addEventListener('mousedown', () => {
        document.addEventListener('mousemove', volumeMove);
        document.addEventListener('mouseup', volumeUp);
        this.volumeButton.classList.add('player-volume-active');
      });
    },
    /**
     * Parse second to time string
     *
     * @param {Number} second
     * @return {String} 00:00 or 00:00:00
     */
    secondToTime (second) {
      second = second || 0;
      if (second === 0 || second === Infinity || second.toString() === 'NaN') {
        return '00:00';
      }
      const add0 = (num) => (num < 10 ? '0' + num : '' + num);
      const hour = Math.floor(second / 3600);
      const min = Math.floor((second - hour * 3600) / 60);
      const sec = Math.floor(second - hour * 3600 - min * 60);
      return (hour > 0 ? [hour, min, sec] : [min, sec]).map(add0).join(':');
    }
  }
}
</script>

<style lang="scss" scoped>
.ff-player-controller {
  position: absolute;
  bottom: 0;
  left: 0;
  right: 0;
  height: 41px;
  padding: 0 20px;
  user-select: none;
  transition: all 0.3s ease;

  .player-icons {
    height: 38px;
    position: absolute;
    bottom: 0;
    &.player-icons-left {
        .player-icon {
          padding: 0px;
        }
    }
    &.player-icons-right {
      right: 20px;
      .player-icon {
        padding: 0px;
      }
    }
    .player-time {
        line-height: 38px;
        color: #eee;
        text-shadow: 0 0 2px rgba(0, 0, 0, .5);
        vertical-align: middle;
        font-size: 13px;
        cursor: default;
    }
    .player-icon {
            width: 40px;
            height: 100%;
            border: none;
            background-color: transparent;
            outline: none;
            cursor: pointer;
            vertical-align: middle;
            box-sizing: border-box;
            display: inline-block;
            .player-icon-content {
                transition: all .2s ease-in-out;
                opacity: .8;
            }
            &:hover {
                .player-icon-content {
                    opacity: 1;
                }
            }
        &.player-setting-icon {
          padding-top: 8.5px;
        }
      }
      .player-play-streams {
        display: inline-block;
        height: 100%;
        position: relative;
        &:hover {
          .player-play-streams-sub {
            display: block;
          }
        }
        .player-play-streams-sub {
          position: absolute;
          z-index: 1;
          display: none;
        }
      }
      .player-volume {
            position: relative;
            display: inline-block;
            cursor: pointer;
            height: 100%;
            &:hover {
                .player-volume-bar-wrap .player-volume-bar {
                    width: 45px;
                }
                .player-volume-bar-wrap .player-volume-bar .player-volume-bar-inner .player-thumb {
                    transform: scale(1);
                }
            }
            &.player-volume-active {
                .player-volume-bar-wrap .player-volume-bar {
                    width: 45px;
                }
                .player-volume-bar-wrap .player-volume-bar .player-volume-bar-inner .player-thumb {
                    transform: scale(1);
                }
            }
            .player-volume-bar-wrap {
                display: inline-block;
                margin: 0 10px 0 -5px;
                vertical-align: middle;
                height: 100%;
                .player-volume-bar {
                    position: relative;
                    top: 17px;
                    width: 0;
                    height: 3px;
                    background: #aaa;
                    transition: all 0.3s ease-in-out;
                    .player-volume-bar-inner {
                      background: #b7daff;
                      position: absolute;
                        bottom: 0;
                        left: 0;
                        height: 100%;
                        transition: all 0.1s ease;
                        will-change: width;
                        .player-thumb {
                            position: absolute;
                            top: 0;
                            right: 5px;
                            margin-top: -4px;
                            margin-right: -10px;
                            height: 11px;
                            width: 11px;
                            border-radius: 50%;
                            cursor: pointer;
                            transition: all .3s ease-in-out;
                            transform: scale(0);
                        }
                    }
                }
            }
      }
    }

    .player-bar-wrap {
        padding: 15px 0;
        cursor: pointer;
        position: absolute;
        bottom: 33px;
        width: calc(100% - 40px);
        height: 3px;
        &:hover {
            .player-bar .player-played .player-thumb {
                transform: scale(1);
            }
            .player-highlight {
                display: block;
                width: 8px;
                transform: translateX(-4px);
                top: 4px;
                height: 40%;
            }
        }
        .player-highlight {
            z-index: 12;
            position: absolute;
            top: 5px;
            width: 6px;
            height: 20%;
            border-radius: 6px;
            background-color: #fff;
            text-align: center;
            transform: translateX(-3px);
            transition: all .2s ease-in-out;
            &:hover {
                .player-highlight-text {
                    display: block;
                }
                &~.player-bar-preview {
                    opacity: 0;
                }
                &~.player-bar-time {
                    opacity: 0;
                }
            }
            .player-highlight-text {
                display: none;
                position: absolute;
                left: 50%;
                top: -24px;
                padding: 5px 8px;
                background-color: rgba(0, 0, 0, .62);
                color: #fff;
                border-radius: 4px;
                font-size: 12px;
                white-space: nowrap;
                transform: translateX(-50%);
            }
        }
        .player-bar-preview {
            position: absolute;
            background: #fff;
            pointer-events: none;
            display: none;
            background-size: 16000px 100%;
        }
        .player-bar-preview-canvas {
            position: absolute;
            width: 100%;
            height: 100%;
            z-index: 1;
            pointer-events: none;
        }
        .player-bar-time {
            &.hidden {
                opacity: 0;
            }
            position: absolute;
            left: 0px;
            top: -20px;
            border-radius: 4px;
            padding: 5px 7px;
            background-color: rgba(0, 0, 0, 0.62);
            color: #fff;
            font-size: 12px;
            text-align: center;
            opacity: 1;
            transition: opacity .1s ease-in-out;
            word-wrap: normal;
            word-break: normal;
            z-index: 2;
            pointer-events: none;
        }
        .player-bar {
            position: relative;
            height: 3px;
            width: 100%;
            background: rgba(255, 255, 255, .2);
            cursor: pointer;
            .player-played {
              background: #b7daff;
              position: absolute;
                left: 0;
                top: 0;
                bottom: 0;
                height: 3px;
                will-change: width;
                .player-thumb {
                    position: absolute;
                    top: 0;
                    right: 5px;
                    margin-top: -4px;
                    margin-right: -10px;
                    height: 11px;
                    width: 11px;
                    border-radius: 50%;
                    cursor: pointer;
                    transition: all .3s ease-in-out;
                    transform: scale(0);
                }
            }
        }
    }
  }
</style>
