<template>
  <div>
    <v-dialog v-model="dialog" :persistent="modal" :width="width">
      <v-card>
        <v-card-title
          class="headline lighten-2"
          primary-title
        >
          {{ title }}
        </v-card-title>
        <v-card-text>
          <v-container>
            <v-row v-if="text1Show">
              <v-text-field v-model="text1" required @keydown.enter="doOK"></v-text-field>
            </v-row>

            <v-row v-if="usernameShow">
              <v-text-field label="Username*" v-model="username" required
                  :error-messages="usernameError"></v-text-field>
            </v-row>
            <v-row v-if="passwordShow">
                <v-text-field label="Password*" v-model="password" type="password" required @keydown.enter="doOK"></v-text-field>
            </v-row>
          </v-container>
        </v-card-text>
        <v-card-actions>
          <v-spacer></v-spacer>
          <v-btn color="blue darken-1" text @click="dialog = false">{{ $t('common.close') }}</v-btn>
          <v-btn color="blue darken-1" text @click="doOK">{{ okText }}</v-btn>
        </v-card-actions>
      </v-card>
    </v-dialog>
  </div>
</template>

<script>
export default {
  data: () => ({
    dialog: false,
    modal: true,
    width: 600,
    title: '',
    okText: 'OK',
    text1: '',
    text1Show: false,
    username: '',
    usernameError: '',
    usernameShow: false,
    password: '',
    passwordShow: false
  }),

  watch: {
    dialog (v) {
      if (!v) {
        if (this.callback) {
          this.callback(this._result);
          this.callback = null
        }
      }
    }
  },

  methods: {
    open (options = {}, callback = () => {}) {
      this.callback = callback
      this._result = null
      this.width = options.width || 600
      this.modal = options.modal !== undefined ? options.modal : true
      this.title = options.title || ''
      this.okText = options.okText || this.$t('common.ok')

      if (typeof options.text1 === 'object') {
        this.text1 = options.text1.value || ''
        this.text1Show = true
      } else {
        this.text1Show = false
      }

      if (typeof options.username === 'object') {
        this.username = options.username.value || ''
        this.usernameError = ''
        this.usernameShow = true
      } else {
        this.usernameShow = false
      }

      if (typeof options.password === 'object') {
        this.password = options.password.value || ''
        this.passwordShow = true
      } else {
        this.passwordShow = false
      }

      this.dialog = true
    },

    doOK () {
      const result = {}

      if (this.text1Show) {
        result.text1 = this.text1
      }
      if (this.usernameShow) {
        result.username = this.username
      }
      if (this.passwordShow) {
        result.password = this.password
      }

      this._result = result

      const handleCallbackResult = (out) => {
        if (typeof out !== 'object') {
          this.callback = null
          this.dialog = false
          return
        }

        if (typeof out.then === 'function') {
          out.then(r => handleCallbackResult(r))
          return
        }

        if (out.username && out.username.error) {
          this.usernameError = out.username.error
        }
      };

      const out = this.callback(this._result)
      handleCallbackResult(out)
    }
  }
}
</script>
