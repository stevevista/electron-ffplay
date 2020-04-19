<template>
  <div
    ref="playList"
    class="playList"
  >
    <div class="my-arrow" v-show="isShowArrow">
      <v-icon @click.stop="hideList">mdi-arrow-right-circle</v-icon>
    </div>
    <div class="content-container" v-show="!isHidenList" @contextmenu.stop="contextmenu(-1)">
      <v-list
        dense
        two-line
        subheader
        color="rgba(255, 0, 0, 0)"
      >
        <v-list-item
          v-for="(item, index) in playlist"
          :key="index"
          :value="index"
          :input-value="index===playIndex"
          color="success"
          @dblclick="$emit('play-item', index)"
          @contextmenu.stop="contextmenu(index)"
          @click="">
            <v-list-item-content>
              <v-list-item-title>{{ item.name }}</v-list-item-title>
              <v-list-item-subtitle>{{ item.url }}</v-list-item-subtitle>
            </v-list-item-content>
        </v-list-item>
      </v-list>
    </div>
  </div>
</template>

<script>
import { remote } from 'electron';
const { Menu } = remote;

export default {
  data () {
    return {
      isHidenList: true
    };
  },
  props: {
    isShowArrow: {
      type: Boolean,
      default: false
    },
    playlist: {
      type: Array,
      default: []
    },
    playIndex: {
      type: Number,
      default: null
    }
  },
  methods: {
    hideList () {
      this.isHidenList = !this.isHidenList;
    },
    contextmenu (index) {
      let contextMenuTemplate = [
        {
          label: this.$t('common.play'),
          enabled: index >= 0,
          click: () => {
            this.$emit('play-item', index)
          }
        },
        {
          type: 'separator'
        },
        {
          label: this.$t('video.play_list.remove'),
          enabled: this.playlist.length > 1,
          click: () => {
            this.$emit('remove-item', index)
          }
        },
        {
          type: 'separator'
        },
        {
          label: this.$t('video.play_list.add'),
          click: () => {
            this.$emit('add-items')
          }
        },
        {
          label: this.$t('video.play_list.removeAll'),
          enabled: this.playlist.length > 0,
          click: () => {
            this.$emit('remove-all-items')
          }
        }
      ]
      let m = Menu.buildFromTemplate(contextMenuTemplate);
      Menu.setApplicationMenu(m);

      m.popup({ 
        window: remote.getCurrentWindow()
      });
    }
  }
}
</script>

<style scoped lang="scss">
.playList {
  position: absolute;
  top: 0;
  right: 0;
  height: 100%;
  background-color: rgba(0, 0, 0, 0.7);
  .my-arrow {
    background-color: rgba(0, 0, 0, 0.7);
  }
  border-left: 1px solid #2f2f31;
  .content-container {
    height: 100%;
    overflow: hidden;
    position: relative;
    width: 300px;
  }
  .my-arrow {
    position: absolute;
    top: 50%;
    left: -31px;
    transform: translateY(-50%);
    width: 30px;
    height: 66px;
    line-height: 66px;
    text-align: center;
    border: 1px solid #303031;
    border-right: none;
    cursor: pointer;
    &:hover {
      border: 1px solid #45b000;
      border-right: none;
      > span {
        color: #45b000;
      }
    }
    > span {
      width: 100%;
      line-height: 66px;
    }
  }
  .top {
    padding: 15px 15px 5px;
    display: flex;
    flex-direction: row;
    justify-content: space-between;
    width: 100%;
    > span {
      font-size: 15px;
    }
    .my-icon {
      display: flex;
      flex-direction: row;
      align-items: center;
      font-size: 15px;
      position: relative;
      > span {
        cursor: pointer;
        &:hover {
          color: #1bb017;
        }
      }
      > span + span {
        margin-left: 10px;
      }
      > .delete {
        font-size: 14px;
      }
    }
  }
  .file {
    flex: 1;
    position: absolute;
    top: 50%;
    left: 50%;
    transform: translate(-50%, -50%);
    .no-file {
      display: flex;
      flex-direction: column;
      align-items: center;
      justify-content: space-between;
      > span {
        font-size: 50px;
        margin-bottom: 10px;
      }
      margin-bottom: 30px;
    }
    .open-file {
      position: relative;
      width: 150px;
      height: 40px;
      border-radius: 40px;
      display: flex;
      flex-direction: row;
      align-items: center;
      justify-content: space-around;
      padding: 5px;
      .my-file {
        z-index: -1;
        position: absolute;
        top: 45px;
        left: 0;
        width: 100%;
        border-radius: 5px;
        &:before {
          content: "";
          height: 0;
          width: 0;
          position: absolute;
          top: -10px;
          left: 23px;
          border: 5px solid transparent;
          border-bottom-color: #252527;
        }
        > li {
          width: 100%;
          height: 40px;
          padding: 10px 15px;
          color: #878788;
          border-radius: 5px;
          cursor: pointer;
          &:hover {
            color: #5dee00;
          }
        }
      }
      > div {
        cursor: pointer;
        span:nth-child(1) {
          padding-right: 10px;
        }
        &:hover {
          color: #5dee00;
        }
      }
      > span {
        font-size: 20px;
        border-left: 1px solid #818181;
        padding-left: 10px;
        cursor: pointer;
        &:hover {
          color: #5dee00;
        }
      }
    }
  }
}

.list-content {
  overflow: auto;
}
.top {
  max-height: 40px;
  transition: width 1s;
  overflow: hidden;
}

.extend-menu {
  position: absolute;
  right: 5px;
  top: 40px;
  z-index: 5;
  width: 100px;
  color: #878788;
  padding: 3px 0;
  border-radius: 5px;
  > .line {
    border-bottom: 1px solid #878788;
  }
  &:after {
    content: "";
    position: absolute;
    left: 80%;
    top: -10px;
    height: 0;
    width: 0;
    border: 5px solid transparent;
    border-bottom-color: greenyellow;
  }
  > li {
    height: 30px;
    text-align: center;
    line-height: 30px;
    font-size: 12px;
    cursor: pointer;
    &:hover {
      color: #1bb017;
      > span {
        color: #1bb017;
      }
    }
    > span {
      font-size: 10px;
      padding: 0;
      margin-left: -10px;
    }
  }
}
</style>
