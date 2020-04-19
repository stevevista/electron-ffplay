<template>
  <v-sheet height="100%">
        <v-tabs>
          <v-tab>{{ $t('video.playControl') }}</v-tab>
          <v-tab>{{ $t('video.snapshot') }}</v-tab>
          <v-tab>AI</v-tab>
          <v-tab-item>
            <v-list dense>
              <v-list-item>
                <v-list-item-content>
                  <v-list-item-title>
                    <span class="text-right" style="width: 180px;display:inline-block">{{ $t('video.settings.auto_next') }}</span><span class="pr-4"/>
                    <span style="width: 50px;display:inline-block">
                      <v-switch
                        hide-details
                        :input-value="autoPlayNext"
                        @change="setAutoPlayNext"
                      ></v-switch>
                    </span>
                  </v-list-item-title>
                </v-list-item-content>
              </v-list-item>
            </v-list>
          </v-tab-item>

          <v-tab-item>
            <v-list dense>
              <v-list-item>
                <v-list-item-content>
                  <v-list-item-title>
                    <span class="text-right" style="width: 180px;display:inline-block">{{ $t('video.settings.snapshot_format') }}</span><span class="pr-4"/>
                    <v-btn-toggle :value="snapshotFormat" @change="setSnapshotFormat">
                      <v-btn text small value="png">png</v-btn>
                      <v-btn text small value="jpeg">jpeg</v-btn>
                    </v-btn-toggle>
                  </v-list-item-title>
                </v-list-item-content>
              </v-list-item>
              <v-list-item>
                <v-list-item-content>
                  <v-list-item-title>
                    <span class="text-right" style="width: 180px;display:inline-block">连拍模式</span><span class="pr-4"/>
                    <v-btn-toggle>
                      <v-btn text small value="png">时间间隔</v-btn>
                      <v-btn text small value="jpeg">帧号间隔</v-btn>
                    </v-btn-toggle>
                  </v-list-item-title>
                </v-list-item-content>
              </v-list-item>
              <v-list-item>
                <v-list-item-content>
                  <v-list-item-title>
                    <span class="text-right" style="width: 180px;display:inline-block">连拍间隔</span><span class="pr-4"/>
                    <span style="width: 100%;display:inline-block">
                      <v-slider
                        label="23"
                        height="20"
                        color="orange"
                        min="1"
                        max="100"
                        dense
                        hide-details
                      ></v-slider>
                    </span>
                  </v-list-item-title>
                </v-list-item-content>
              </v-list-item>
              <v-list-item>
                <v-list-item-content>
                  <v-list-item-title>
                    <span class="text-right" style="width: 180px;display:inline-block">帧号间隔</span><span class="pr-4"/>
                    <span style="width: 100%;display:inline-block">
                      <v-slider
                        label="23"
                        height="20"
                        color="orange"
                        min="1"
                        max="100"
                        dense
                        hide-details
                      ></v-slider>
                    </span>
                  </v-list-item-title>
                </v-list-item-content>
              </v-list-item>
            </v-list>
          </v-tab-item>

          <v-tab-item>
            <v-list dense>
              <v-list-item>
                <v-list-item-content>
                  <v-list-item-title>
                    <span class="text-right" style="width: 180px;display:inline-block">{{ $t('ai.switch') }}</span><span class="pr-4"/>
                    <v-btn-toggle>
                      <v-btn text small value="pedestrian">{{ $t('ai.pedestrian') }}</v-btn>
                      <v-btn text small value="face">{{ $t('ai.face') }}</v-btn>
                      <v-btn text small value="landmark">{{ $t('ai.landmark') }}</v-btn>
                      <v-btn text small value="vehicle">{{ $t('ai.vehicle') }}</v-btn>
                      <v-btn text small value="plate">{{ $t('ai.plate') }}</v-btn>
                    </v-btn-toggle>
                  </v-list-item-title>
                </v-list-item-content>
              </v-list-item>
            </v-list>
          </v-tab-item>
        </v-tabs>
  </v-sheet>
</template>

<script>
import { mapState, mapMutations } from 'vuex'
export default {
  data: () => ({
  }),
  computed: {
    ...mapState('video', ['autoPlayNext', 'snapshotFormat'])
  },
  methods: {
    ...mapMutations('video', ['setAutoPlayNext', 'setSnapshotFormat'])
  }
}
</script>
