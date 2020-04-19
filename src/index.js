import '@mdi/font/css/materialdesignicons.css'
import 'vuetify/dist/vuetify.min.css'
import 'balloon-css';
import Vue from 'vue'
import Vuetify from 'vuetify'

import store from './store'
import i18n from './i18n'
import App from './components/app'
import zhHans from 'vuetify/es5/locale/zh-Hans'
import electron from 'electron'

/* eslint-disable no-new */
Vue.config.productionTip = false
Vue.prototype.$electron = electron

Vue.use(Vuetify)

// load default settings
const darkStr = localStorage.getItem('settings.theme.dark') || 'true';
const themeDark = darkStr === 'true';

const localeStr = localStorage.getItem('settings.locale');
if (localeStr) {
  i18n.locale = localeStr;
}

const vm = new Vue({
  components: { App },
  store,
  i18n,
  vuetify: new Vuetify({
    lang: {
      locales: { 'zh-cn': zhHans },
      current: i18n.locale
    },
    theme: {
      dark: themeDark
    }
  }),
  template: '<App/>'
}).$mount('#app')

window.addEventListener('beforeunload', e => {
  // options are restored in app.mounted
  localStorage.setItem('settings.theme.dark', vm.$vuetify.theme.dark);
  localStorage.setItem('settings.locale', vm.$i18n.locale);
  store.commit('video/saveAllSettings')
});
