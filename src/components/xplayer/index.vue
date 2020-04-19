<template>
  <div class="ff-player" ref="container" v-show="!inPlayingMiniCanvas" 
    @contextmenu.stop="contextmenu" 
    @fullscreenchange="fullscreenchange">
    <div class="ff-player-video-wrap" @click="clickPlay">
      <FFVideo
        ref="video"
        :src="videoSrc"
        :transport="streamingTransport"
        :inscreenCanvas="inPlayingMiniCanvas"
        :infoCrops="infoCrops"
        :showDetection="showDetection"
        :dnnEngine="dnnEngine"
        @notice="notice"
        v-on:auth-require="onAuthRequired"
        />
      <div class="player-bezel">
        <span class="player-bezel-play-icon" v-show="!video.isPlaying && url">
          <span :class="`mdi mdi-${video.ended ? 'replay' : 'play'} mdi-light mdi-36px`"></span>
        </span>
        <Loading v-show="video.isLoading"/>
      </div>
      
    </div>
    <FFController
      :video="video"
      :streams="streams"
      :isFullScreen="isFullScreen"
      :showOpen="showOpen"
      :showPlay="showPlay"
      :showDetection="showDetection"
      v-on:open-files="openFiles"
      v-on:open-stream="openStreaming"
      v-on:open-dir="openDir"
      v-on:toggle-info="toggleInfoPanel"
      v-on:toggle-fullscreen="toggleFullScreen"
      v-on:toggle-in-screen="toggleInScreenPlay"
      v-on:toggle-show-detection="showDetection=!showDetection"
      @notice="notice"/>

    <PlayList 
      v-show="showPlayList"
      :is-show-arrow="isShowArrow"
      :playlist="playlist"
      :playIndex="playIndex"
      v-on:play-item="playSelectedItem"
      v-on:remove-item="removeListItem"
      v-on:remove-all-items="removeAllItems"
      v-on:add-items="addNewItems"/>

    <InfoPanel 
      :show="showInfoPanel"
      @close="toggleInfoPanel"
      :url="url"
      :fps="video.fps"
      :size="video.mediaSize"
      :codecp="video.codecp"
      :objects="video.faces"/>

    <div class="player-notice"></div>
    <div class="player-speed" v-show="video.speedRate!==1.0"> X {{ video.speedRate }}</div>

    <v-bottom-sheet v-model="showSettings" content-class="opacity-sheet" overlay-opacity="0">
      <PlayerSettings/>
    </v-bottom-sheet>
  </div>
</template>

<script>
import fs from 'fs'
import path from 'path'
import { remote } from 'electron';
import FFVideo from './video';
import FFController from './controller';
import Loading from './loading'
import {mapState} from 'vuex'
import InfoPanel from './info-panel';
import PlayerSettings from './settings'
import PlayList from './play-list';

const { Menu } = remote;

const validMediaExtensions = ['mp4', 'webm', 'ogg', 'mkv', 'avi', 'mov', 'asf', 'wmv', 'navi', '3gp', 'flv', 'f4v', 'rmvb', 'hddvd', 'rm', 'rmvb', 'mp3', 'h264']

function getScrollPosition () {
  return {
    left: window.pageXOffset || document.documentElement.scrollLeft || document.body.scrollLeft || 0,
    top: window.pageYOffset || document.documentElement.scrollTop || document.body.scrollTop || 0
  };
}

function setScrollPosition ({ left = 0, top = 0 }) {
  window.scrollTo(left, top);
}

function isValidMediaFile (url) {
  if (/^(rtsp|http|udp|tcp):\/\//.test(url)) {
    return {url, name: url, stream: true, valid: true}
  }
  const ext = path.extname(url).toLowerCase().substring(1);
  const valid = validMediaExtensions.indexOf(ext) >= 0;
  return {url, name: path.basename(url), stream: false, valid}
}

const rtpsPatern = /^rtsp:\/\/(.*)/

export default {
  components: {
    FFVideo,
    FFController,
    Loading,
    InfoPanel,
    PlayerSettings,
    PlayList
  },

  props: {
    src: null,
    showOpen: {default: false, type: Boolean},
    showPlay: {default: false, type: Boolean},
    showPlayList: {default: false, type: Boolean},
    disableAutoHide: {default: false, type: Boolean},
    inscreenCanvas: {type: Object},
    dnnEngine: {type: Object}
  },

  data () {
    return {
      development: process.env.NODE_ENV === 'development',
      video: {ended: true},
      isFullScreen: false,
      isMenuPopping: false,
      url: null,
      username: null,
      password: null,
      authUsed: false,
      urn: null,
      inPlayingMiniCanvas: null,
      showDetection: true,
      showInfoPanel: false,
      showSettings: false,
      autohide: true,
      isShowArrow: false,
      playlist: [],
      playIndex: 0,
      mediaUrl: 'rtsp://', // 'rtsp://10.21.68.111:8554/mpeg4ESVideoTest' // for test
      infoCrops: []
    }
  },

  computed: {
    ...mapState('video', ['autoPlayNext', 'streamingTransport']),
    videoSrc () {
      if (!this.url || !this.authUsed || !this.username || !this.password) {
        return this.url
      }
      const rtspMath = this.url.match(rtpsPatern)
      if (!rtspMath) {
        return this.url
      }
      return `rtsp://${encodeURIComponent(this.username)}:${encodeURIComponent(this.password)}@${rtspMath[1]}`
    },
    streams () {
      if (!this.src) {
        return null
      }
      if (!this.src.streams) {
        return [0, 'default', this.src.url]
      }
      const out = []
      for (let i = 0; i < this.src.streams.length; i++) {
        const s = this.src.streams[i]
        if (s[1] === this.url) {
          out.unshift([i + 1, s[0], s[1]])
        } else {
          out.push([i + 1, s[0], s[1]])
        }
      }
      return out
    }
  },

  watch: {
    'video.paused': function (v) {
      this.setControllerAutoHide();
    },
    'video.ended': function (v) {
      if (v) {
        this.playEnded();
      }
    },
    src: {
      immediate: true,
      handler: function (newVal) {
        this.authUsed = false
        if (newVal) {
          this.url = newVal.url;
          this.urn = newVal.urn;
          this.username = newVal.username;
          this.password = newVal.password;
        } else {
          this.url = null;
          this.urn = null;
          this.username = null;
          this.password = null;
        }
      }
    }
  },

  methods: {
    openAsFilelist (lists, append) {
      let playlist = append ? [...this.playlist] : []
      const prevCount = playlist.length
      lists = lists.filter(url => {
        return playlist.find(e => e.url === url) === undefined
      })
      lists = lists.map(url => isValidMediaFile(url)).filter(url => url.valid)
      const addlist = lists.map(url => {
        if (url.stream) {
          return {...url}
        }
        const stat = fs.statSync(url.url)
        let size
        if (stat && stat.dev) {
          size = stat.size
        }
        return {...url, size}
      })

      playlist = playlist.concat(addlist);

      this.playlist = playlist
      if (this.playlist.length > 0 && prevCount === 0) {
        this.video.mediaSize = this.playlist[0].size
        this.url = this.playlist[0].url
        this.playIndex = 0
      }
    },
    async openFiles (append) {
      const result = await remote.dialog.showOpenDialog(
        remote.getCurrentWindow(), {
          defaultPath: '',
          properties: ['openFile', 'multiSelections'],
          filters: [{
            name: 'video',
            extensions: validMediaExtensions
          }]
        })

      if (!result || result.canceled) {
        return
      }

      this.openAsFilelist(result.filePaths, append)
    },

    async openDir (append) {
      const result = await remote.dialog.showOpenDialog(
        remote.getCurrentWindow(), {
          defaultPath: '',
          properties: ['openDirectory']
        })

      if (!result || result.canceled) {
        return
      }

      const dir = result.filePaths[0]
      let fielist = await fs.promises.readdir(dir)
      fielist = fielist.map(name => path.join(dir, name))
      this.openAsFilelist(fielist, append)
    },

    openStreaming () {
      this.$emit('openDialog', {
        modal: false,
        title: 'Media URL (HTTP/RTSP/...)',
        text1: {
          value: this.mediaUrl
        }
      }, ret => {
        if (ret && ret.text1) {
          const url = this.mediaUrl = ret.text1
          this.url = url
          this.playIndex = 0
          this.video.mediaSize = null
        }
      })
    },

    contextmenu (e) {
      const contextMenuTemplate = []

      const openItem = {
        label: this.$t('common.open'),
        submenu: [
          {
            label: this.$t('common.openFile'),
            click: () => {
              this.openFiles();
            }
          },
          {
            label: this.$t('video.openDir'),
            click: () => {
              this.openDir();
            }
          },
          {
            label: this.$t('video.openUrl'),
            click: () => {
              this.openStreaming();
            }
          }
        ]
      };

      const playItem = {
        label: !this.video.paused ? this.$t('common.suspend') : this.$t('common.play'),
        enabled: !!this.url,
        click: () => {
          this.video.toggle()
        }
      };

      const speedItem = {
        label: this.$t('video.speed'),
        enabled: !this.video.ended,
        submenu: [
          ...[1, 2, 4, 8, 16]
            .map(n => ({
              label: n + '',
              type: 'checkbox',
              checked: this.video.speedRate === n,
              click: () => {
                this.video.speed(n)
              }
            })),
          {type: 'separator'},
          ...[2, 4, 8, 16]
            .map(n => ({
              label: '1/' + n,
              type: 'checkbox',
              checked: this.video.speedRate === 1 / n,
              click: () => {
                this.video.speed(1 / n)
              }
            })),
          {type: 'separator'},
          {
            label: 'rewind',
            type: 'checkbox',
            checked: this.video.speedRate === -1,
            click: () => {
              this.video.speed(-1)
            }
          }
        ]
      };

      if (this.showOpen) {
        contextMenuTemplate.push(openItem)
        contextMenuTemplate.push({type: 'separator'})
      }

      if (this.showPlay) {
        contextMenuTemplate.push(playItem)
        contextMenuTemplate.push(speedItem)
        contextMenuTemplate.push({type: 'separator'})
      }

      contextMenuTemplate.push({
        label: this.$t('common.fileInfo'),
        type: 'checkbox',
        checked: this.showInfoPanel,
        click: () => {
          this.toggleInfoPanel();
        }
      });

      contextMenuTemplate.push({
        label: this.$t('video.autoHideControl'),
        type: 'checkbox',
        checked: this.autohide,
        click: () => {
          this.autohide = !this.autohide;
          localStorage.setItem('settings.video.autoHide', this.autohide)
        }
      });

      contextMenuTemplate.push({type: 'separator'})
      contextMenuTemplate.push({
        label: this.$t('common.setting'),
        click: () => {
          this.showSettings = true;
        }
      })

      let m = Menu.buildFromTemplate(contextMenuTemplate);
      Menu.setApplicationMenu(m);

      this.isMenuPopping = true;
      m.popup({ 
        window: remote.getCurrentWindow(),
        callback: () => {
          setTimeout(() => {
            this.isMenuPopping = false
          }, 300)
        }});
    },

    clickPlay () {
      if (this.isMenuPopping) {
        return
      }
      this.video.clickPlay();
    },

    cancelWebFullScale () {
      this.container.classList.remove('player-fulled');
      document.body.classList.remove('player-web-fullscreen-fix');
      this.video.resize();
      setScrollPosition(this.lastScrollPosition);
    },

    isWebFullScaled () {
      return this.container.classList.contains('player-fulled');
    },

    toggleInfoPanel () {
      this.showInfoPanel = !this.showInfoPanel
    },

    toggleFullScreen () {
      if (this.isFullScreen) {
        document.webkitCancelFullScreen();
      } else {
        const anotherTypeOn = this.isWebFullScaled();
        if (!anotherTypeOn) {
          this.lastScrollPosition = getScrollPosition();
        }

        this.container.requestFullscreen();

        if (anotherTypeOn) {
          this.cancelWebFullScale();
        }
      }
    },

    toggleInScreenPlay () {
      this.inPlayingMiniCanvas = this.inscreenCanvas
      if (this.inscreenCanvas) {
        this.inscreenCanvas.show(this.video.mainCanvas.width / this.video.mainCanvas.height, () => {
          this.inPlayingMiniCanvas = null
        })

        if (this.video.paused) {
          this.video.playNextFrame()
        }
      }
    },

    fullscreenchange () {
      this.video.resize();
      if (document.fullscreenElement || document.mozFullScreenElement || document.webkitFullscreenElement || document.msFullscreenElement) {
        this.isFullScreen = true
      } else {
        setScrollPosition(this.lastScrollPosition);
        this.isFullScreen = false
      }
    },

    setControllerAutoHide () {
      this.showController();
      clearTimeout(this.autoHideTimer);
      this.autoHideTimer = setTimeout(() => {
        if (this.autohide && !this.showSettings && !this.disableAutoHide && !this.video.paused && !this.inPlayingMiniCanvas) {
          this.hideController();
        }
      }, 3000);
    },

    showController () {
      if (this.isWebFullScaled()) {
        this.cancelWebFullScale()
      }
      this.container.classList.remove('player-hide-controller');
      this.isShowArrow = true;
    },

    hideController () {
      this.isShowArrow = false;
      this.container.classList.add('player-hide-controller');
      if (!this.isFullScreen) {
        this.lastScrollPosition = getScrollPosition();
        this.container.classList.add('player-fulled');
        document.body.classList.add('player-web-fullscreen-fix');
        this.video.resize();
      }
    },

    playSelectedItem (index) {
      if (!this.video.ended) {
        const item = this.playlist[index]
        this.url = item.url
        this.video.mediaSize = item.size
        this.playIndex = index
        this.onCurrentPlayEnd = () => {}
      } else {
        this.playListItem(index)
      }
    },

    playListItem (index) {
      if (this.playIndex === index) {
        if (this.video.paused) {
          this.video.toggle()
        }
        return
      }
      const item = this.playlist[index]
      this.url = item.url
      this.video.mediaSize = item.size
      this.playIndex = index
    },
    removeListItem (index) {
      this.playlist.splice(index, 1)
      if (this.playIndex === index) {
        let nextIndex = index
        if (nextIndex >= this.playlist.length) {
          nextIndex = 0
        }
        // invalid playIndex
        this.playIndex = this.playlist.length
        this.playListItem(nextIndex)
      }
    },
    removeAllItems () {
      this.playlist = []
      this.playIndex = -1
      this.url = ''
    },

    async addNewItems () {
      this.openFiles(true)
    },

    playEnded () {
      if (typeof this.onCurrentPlayEnd === 'function') {
        this.onCurrentPlayEnd()
        this.onCurrentPlayEnd = null
        return
      }
      // play next
      let index = this.playIndex
      if (index < this.playlist.length - 1 && this.autoPlayNext) {
        index++;
        this.playListItem(index)
      }
    },

    notice (text, time = 2000, opacity = 0.8) {
      this.noticeEl.innerHTML = text;
      this.noticeEl.style.opacity = opacity;
      if (this.noticeTime) {
        clearTimeout(this.noticeTime);
      }
      if (time > 0) {
        this.noticeTime = setTimeout(() => {
          this.noticeEl.style.opacity = 0;
        }, time);
      }
    },

    onAuthRequired () {
      if (!this.authUsed && this.username && this.password) {
        // try auth 
        this.authUsed = true;
        return
      }

      // ask user to provide password
      this.$emit('openDialog', {
        modal: true,
        width: 400,
        title: this.$t('common.authorization'),
        username: {
          value: this.username
        },
        password: {
          value: this.password
        }
      }, ret => {
        if (ret && ret.username) {
          const {username, password} = ret
          this.username = username
          this.password = password
          this.authUsed = true;
          this.$emit('auth-updated', this.src, username, password);
        }
      })
    },

    installHotKeys () {
      document.addEventListener('keydown', (e) => {
        if (this.focus) {
          const tag = document.activeElement.tagName.toUpperCase();
          const editable = document.activeElement.getAttribute('contenteditable');
          if (tag !== 'INPUT' && tag !== 'TEXTAREA' && editable !== '' && editable !== 'true') {
            const event = e || window.event;
            let percentage;
            switch (event.keyCode) {
              case 32:
                event.preventDefault();
                this.video.toggle();
                break;
              case 37:
                event.preventDefault();
                this.video.seek(this.video.currentTime - 5);
                this.setControllerAutoHide();
                break;
              case 39:
                event.preventDefault();
                this.video.seek(this.video.currentTime + 5);
                this.setControllerAutoHide();
                break;
              case 38:
                event.preventDefault();
                percentage = this.video.volume + 0.1;
                this.video.setVolume(percentage);
                break;
              case 40:
                event.preventDefault();
                percentage = this.video.volume - 0.1;
                this.video.setVolume(percentage);
                break;
            }
          }
        }
      });
    }
  },

  mounted () {
    this.video = this.$refs.video
    this.container = this.$refs.container

    this.container.addEventListener('mousemove', () => {
      this.setControllerAutoHide();
    });
    this.container.addEventListener('click', () => {
      this.setControllerAutoHide();
    });

    document.addEventListener(
      'click',
      () => {
        this.focus = false;
      },
      true
    );
    this.container.addEventListener(
      'click',
      () => {
        this.focus = true;
      },
      true
    );

    this.noticeEl = this.container.querySelector('.player-notice');

    const v = localStorage.getItem('settings.video.autoHide')
    this.autohide = v === null || v === 'true'

    this.installHotKeys();

    // init volume
    this.video.setVolume(+(localStorage.getItem('settings.video.volume') || 1.0));

    // crops for object detections
    const crops0 = this.container.querySelector('.xplayer-info-panel-item-crop0 canvas');
    const crops1 = this.container.querySelector('.xplayer-info-panel-item-crop1 canvas');
    const crops2 = this.container.querySelector('.xplayer-info-panel-item-crop2 canvas');
    const crops3 = this.container.querySelector('.xplayer-info-panel-item-crop3 canvas');
    const crops4 = this.container.querySelector('.xplayer-info-panel-item-crop4 canvas');

    this.infoCrops = [crops0, crops1, crops2, crops3, crops4];
  },

  beforeDestroy () {
    clearTimeout(this.autoHideTimer);
  }
}
</script>

<style lang="scss">
.opacity-sheet {
  opacity: 0.9;
  height: 400px;
}
</style>

<style lang="scss">
.ff-player {
  flex: 1;
  overflow: hidden;
  height: 100%;
  line-height: 1;
  position: relative;
  user-select: none;

  * {
    box-sizing: content-box;
  }

  .ff-player-video-wrap {
    height: 100%;
    position: relative;
    font-size: 0;

    .player-bezel {
      position: absolute;
      left: 0;
      right: 0;
      top: 0;
      bottom: 0;
      font-size: 22px;
      color: #fff;
      pointer-events: none;
      .player-bezel-play-icon {
        position: absolute;
        top: 50%;
        left: 50%;
        margin: -30px 0 0 -30px;
        height: 60px;
        width: 60px;
        padding: 13px;
        box-sizing: border-box;
        background: rgba(255, 255, 255, .1);
        border-radius: 50%;
        pointer-events: none;
      }
    }
  }

  &.player-hide-controller {
    cursor: none;
    .ff-player-controller {
      opacity: 0;
      transform: translateY(100%);
    }
  }
  &.player-show-controller {
    .ff-player-controller {
      opacity: 1;
    }
  }
  &.player-fulled {
    position: fixed;
    z-index: 100000;
    left: 0;
    top: 0;
    width: 100%;
    height: 100%;
  }
  &:-webkit-full-screen {
    width: 100%;
    height: 100%;
    background: #000;
    position: fixed;
    z-index: 100000;
    left: 0;
    top: 0;
    margin: 0;
    padding: 0;
    transform: translate(0, 0);
  }

  .player-speed {
    opacity: 0.8;
    position: absolute;
    top: 2px;
    right: 2px;
    font-size: 14px;
    border-radius: 2px;
    padding: 7px 20px;
    transition: all .3s ease-in-out;
    overflow: hidden;
    color: #fff;
    pointer-events: none;
  }

  .player-notice {
    opacity: 0;
    position: absolute;
    bottom: 60px;
    left: 20px;
    font-size: 14px;
    border-radius: 2px;
    background: rgba(28, 28, 28, 0.9);
    padding: 7px 20px;
    transition: all .3s ease-in-out;
    overflow: hidden;
    color: #fff;
    pointer-events: none;
  }
}

// To hide scroll bar, apply this class to <body>
.player-web-fullscreen-fix {
    position: fixed;
    top: 0;
    left: 0;
    margin: 0;
    padding: 0;
}
</style>
