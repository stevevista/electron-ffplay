<template>
  <div class="xplayer-info-panel" v-show="show">
    <div class="xplayer-info-panel-close" @click="$emit('close')">[x]</div>
    <div class="xplayer-info-panel-item">
      <span class="xplayer-info-panel-item-title">Url</span>
      <span class="xplayer-info-panel-item-data">{{ url }}</span>
    </div>
    <div class="xplayer-info-panel-item" v-show="size">
      <span class="xplayer-info-panel-item-title">Size</span>
      <span class="xplayer-info-panel-item-data">{{ sizeDisplay }}</span>
    </div>
    <div class="xplayer-info-panel-item">
      <span class="xplayer-info-panel-item-title">Resolution</span>
      <span class="xplayer-info-panel-item-data">{{ width }} x {{ height }}</span>
    </div>
    <div class="xplayer-info-panel-item" v-show="fps">
      <span class="xplayer-info-panel-item-title">fps</span>
      <span class="xplayer-info-panel-item-data">{{ fps }}</span>
    </div>
    <div class="xplayer-info-panel-item">
            <span class="xplayer-info-panel-item-title">Duration</span>
            <span class="xplayer-info-panel-item-data">{{ codecp.duration }}</span>
    </div>
    <div class="xplayer-info-panel-item">
            <span class="xplayer-info-panel-item-title">Video</span>
            <span class="xplayer-info-panel-item-data">{{ codecp.video }}</span>
    </div>
    <div class="xplayer-info-panel-item">
      <span class="xplayer-info-panel-item-title">Audio</span>
      <span class="xplayer-info-panel-item-data">{{ codecp.audio }}</span>
    </div>

    <div :class="`xplayer-info-panel-item xplayer-info-panel-item-crop${i-1}`" v-show="objects[i-1]" v-for="i in 5" :key="i">
      <span><canvas style="transform: scaleY(-1); height: 70px; width: 70px"/></span>
      <span v-if="objects[i-1]">
        <div>
                <span class="xplayer-info-panel-item-title">Age</span>
                <span class="xplayer-info-panel-item-data">{{ parseInt(objects[i-1].age) }}</span>
        </div>
        <div>
                <span class="xplayer-info-panel-item-title">{{ objects[i-1].male > 0.6 ? 'Male' : 'Female' }}</span>
                <span class="xplayer-info-panel-item-data">{{ objects[i-1].male > 0.6 ? objects[i-1].male.toFixed(2) : (1 - objects[i-1].male).toFixed(2) }}</span>
        </div>
        <div>
                <span class="xplayer-info-panel-item-title">{{ objects[i-1].emotion.emotion }}</span>
                <span class="xplayer-info-panel-item-data">{{ objects[i-1].emotion.confidence.toFixed(2) }}</span>
        </div>
      </span>
    </div>
  </div>
</template>

<script>
function filesize (bytes) {
  const options = {}

  options.calculate = function () {
    const type = ['K', 'B']
    const magnitude = (Math.log(bytes) / Math.log(1024)) | 0
    const result = (bytes / Math.pow(1024, magnitude))
    const fixed = result.toFixed(2)

    const suffix = magnitude
      ? (type[0] + 'MGTPEZY')[magnitude - 1] + type[1]
      : ((fixed | 0) === 1 ? 'Byte' : 'Bytes')

    return {
      suffix,
      result,
      fixed
    }
  }

  options.to = function (unit) {
    let position = ['B', 'K', 'M', 'G', 'T'].indexOf(typeof unit === 'string' ? unit[0].toUpperCase() : 'B')
    var result = bytes

    if (position === -1 || position === 0) return result.toFixed(2)
    for (; position > 0; position--) result /= 1024
    return result.toFixed(2)
  }

  options.human = function () {
    var output = options.calculate()
    return output.fixed + ' ' + output.suffix
  }

  return options;
}

export default {
  props: {
    show: {
      type: Boolean,
      default: false
    },
    url: {
      type: String,
      default: ''
    },
    size: {
      type: Number
    },
    fps: {
      type: Number
    },
    codecp: {
      type: Object,
      default: function () { return {} }
    },
    objects: {
      type: Array,
      default: function () { return [] }
    }
  },

  computed: {
    width () {
      return this.codecp.window_size && this.codecp.window_size[0]
    },
    height () {
      return this.codecp.window_size && this.codecp.window_size[1]
    },
    sizeDisplay () {
      return filesize(this.size).human();
    }
  },

  methods: {
  }
}
</script>

<style lang="scss" scoped>
.xplayer-info-panel {
    position: absolute;
    top: 10px;
    left: 10px;
    width: 400px;
    background: rgba(28, 28, 28, 0.6);
    padding: 10px;
    color: #fff;
    font-size: 12px;
    border-radius: 2px;

    .xplayer-info-panel-close {
        cursor: pointer;
        position: absolute;
        right: 10px;
        top: 10px;
    }

    .xplayer-info-panel-item {
        & > span {
            display: inline-block;
            vertical-align: middle;
            line-height: 15px;
            white-space: nowrap;
            text-overflow: ellipsis;
            overflow: hidden;
        }
    }

    .xplayer-info-panel-item-title {
        width: 100px;
        text-align: right;
        margin-right: 10px;
    }
    
    .xplayer-info-panel-item-data {
        width: 260px;
    }
}
</style>
