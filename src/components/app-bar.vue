<template>
  <div class="app-bar">
    <v-system-bar window>
      <div class="left">
        <img :src="iconSrc" style="width:26px; height:28px; padding-top:6px; padding-right:6px; padding-bottom:2px;"/>
      </div>
      <span class="font-italic font-weight-medium">{{ title }}</span>
      <v-spacer></v-spacer>
      <div class="right">
        <v-icon @click="minimizeWindow" class="ma-0 pa-0">mdi-window-minimize</v-icon>
        <v-icon v-if="isMaximized" @click="maximizeWindow" class="ma-0 pa-0">mdi-window-restore</v-icon>
        <v-icon v-else @click="maximizeWindow" class="ma-0 pa-0">mdi-window-maximize</v-icon>
        <v-icon class="ma-0 pa-0 close" @click="close">mdi-window-close</v-icon>
      </div>
    </v-system-bar>
  </div>
</template>

<script>
/* eslint-disable camelcase */
import icon from '../../resources/icons/256x256.png';
import {remote} from 'electron'

export default {
  props: {
    title: {
      type: String,
      default: 'live app'
    }
  },

  data () {
    return {
      isMaximized: false,
      iconSrc: 'data:image/gif;base64,R0lGODlhAQABAAD/ACwAAAAAAQABAAACADs='
    }
  },
  mounted () {
    // const iconSrc = 'data:image/x-icon;base64,' + Buffer.from(icon).toString('base64');
    this.iconSrc = icon
    const win = remote.getCurrentWindow()
    if (win) {
      win.addListener('maximize', () => { this.isMaximized = true; });
      win.addListener('unmaximize', () => { this.isMaximized = false; });
    }
  },
  beforeDestroy () {
    const win = remote.getCurrentWindow()
    if (win) {
      win.removeAllListeners('maximize');
      win.removeAllListeners('unmaximize');
    }
  },
  methods: {
    close () {
      window.close()
    },

    maximizeWindow () {
      const win = remote.getCurrentWindow()
      if (win) {
        if (win.isMaximized()) {
          win.restore()
        } else {
          win.maximize()
        }
      }
    },

    minimizeWindow () {
      const win = remote.getCurrentWindow()
      if (win) {
        win.minimize()
      }
    }
  }
}
</script>

<style lang="scss" scoped>
.v-system-bar {
  padding: 0 0 0 8px
}
</style>

<style lang="scss" scoped>
.app-bar {
  -webkit-app-region: drag;
  .left {
    -webkit-app-region: none;
  }
  .right {
    -webkit-app-region: none;
    .v-icon {
      height: 36px;
      width: 46px;
      cursor: pointer;
    }
    .v-icon.theme--dark {
      &:hover {
        background-color: #757575
      }
    }
    .v-icon.theme--light {
      &:hover {
        background-color: #E0E0E0
      }
    }
    .v-icon.close {
      &:hover {
        background-color: #f00
      }
    }
  }
}
</style>
