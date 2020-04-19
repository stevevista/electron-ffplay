const streamingTransport = localStorage.getItem('settings.video.streamingTransport') || 'udp';

let autoPlayNext = localStorage.getItem('settings.video.autoPlayNext');
autoPlayNext = autoPlayNext === null || autoPlayNext === 'true'

const snapshotFormat = localStorage.getItem('settings.video.snapshotFormat') || 'png';

const state = {
  currentVideo: null,
  streamingTransport,
  autoPlayNext,
  snapshotFormat,
  usePcInference: false,
  inferenceFeatures: ['faces', 'age', 'emotions', 'vehicles']
}

const getters = {
  currentVideo (state) {
    return state.currentVideo
  }
}

const mutations = {
  setStreamingTransport (state, v) {
    state.streamingTransport = v
  },
  setAutoPlayNext (state, v) {
    state.autoPlayNext = v
  },
  setSnapshotFormat (state, v) {
    state.snapshotFormat = v
  },
  saveAllSettings (state) {
    localStorage.setItem('settings.video.streamingTransport', state.streamingTransport);
    localStorage.setItem('settings.video.autoPlayNext', state.autoPlayNext);
  },
  setPcInference (state, v) {
    state.usePcInference = v
  },
  setInferenceFeatures (state, v) {
    state.inferenceFeatures = v
  }
}

export default {
  namespaced: true,
  state,
  getters,
  mutations
}
