<template>
  <div id="app">
    <v-app>
      <app-bar :title="`${title} - ${version}`"/>
      <XVideo @openDialog="openDialog" show-open show-play show-play-list/>
      <DialogUtils ref="dialogUtils"/>
    </v-app>
  </div>
</template>

<script>
import AppBar from './app-bar';
import pkg from '../../package.json'
import XVideo from './xplayer';
import DialogUtils from './dialogs'

export default {
  components: { AppBar, XVideo, DialogUtils },

  data: () => ({
    title: pkg.name,
    version: pkg.version,
    inscreenCanvas: null
  }),

  watch: {
    '$i18n.locale' (v) {
      this.$vuetify.lang.current = v
    }
  },

  methods: {
    openDialog (optinos, callback) {
      this.$refs.dialogUtils.open(optinos, callback)
    }
  }
}
</script>

<style lang="scss">
html {
  overflow: hidden;
}

/* slim scrollbar style */
::-webkit-scrollbar {
  width: 5px;
}

::-webkit-scrollbar-thumb {
  background: #ddd;
  -webkit-border-radius: 8px;
  border-radius: 8px;
}


* {
    margin: 0;
    padding: 0;
    box-sizing: border-box;
}

body {
    position: absolute;
    top: 0;
    left: 0;
    width: 100%;
    height: 100%;
     -webkit-user-select:none;
    -moz-user-select:none;
    -ms-user-select:none;
    user-select:none;
}
</style>

<style lang="scss" scoped>
#app {
  height: 100vh;
  overflow: hidden;
}
</style>
