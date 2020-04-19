<template>
  <span class="xplayer-loading-icon">
    <svg height="100%" version="1.1" viewBox="0 0 22 22">
            <svg x="7" y="1">
                <circle class="xplayer-loading-dot xplayer-loading-dot-0" cx="4" cy="4" r="2"></circle>
            </svg>
            <svg x="11" y="3">
                <circle class="xplayer-loading-dot xplayer-loading-dot-1" cx="4" cy="4" r="2"></circle>
            </svg>
            <svg x="13" y="7">
                <circle class="xplayer-loading-dot xplayer-loading-dot-2" cx="4" cy="4" r="2"></circle>
            </svg>
            <svg x="11" y="11">
                <circle class="xplayer-loading-dot xplayer-loading-dot-3" cx="4" cy="4" r="2"></circle>
            </svg>
            <svg x="7" y="13">
                <circle class="xplayer-loading-dot xplayer-loading-dot-4" cx="4" cy="4" r="2"></circle>
            </svg>
            <svg x="3" y="11">
                <circle class="xplayer-loading-dot xplayer-loading-dot-5" cx="4" cy="4" r="2"></circle>
            </svg>
            <svg x="1" y="7">
                <circle class="xplayer-loading-dot xplayer-loading-dot-6" cx="4" cy="4" r="2"></circle>
            </svg>
            <svg x="3" y="3">
                <circle class="xplayer-loading-dot xplayer-loading-dot-7" cx="4" cy="4" r="2"></circle>
            </svg>
    </svg>
  </span>
</template>

<style lang="scss" scoped>
.xplayer-loading-icon {
  position: absolute;
  top: 50%;
  left: 50%;
  margin: -18px 0 0 -18px;
  height: 36px;
  width: 36px;
  pointer-events: none;

  svg {
        width: 100%;
        height: 100%;
        
        path,
        circle {
            fill: #fff;
        }
  }

  .xplayer-loading-dot {
            animation: xplayer-loading-dot-fade .8s ease infinite;
            opacity: 0;
            transform-origin: 4px 4px;
            @for $i from 7 through 1 {
                &.xplayer-loading-dot-#{$i} {
                    animation-delay: .1s * $i;
                }
            }
  }
        @keyframes xplayer-loading-dot-fade {
            0% {
                opacity: .7;
                transform: scale(1.2, 1.2)
            }
            50% {
                opacity: .25;
                transform: scale(.9, .9)
            }
            to {
                opacity: .25;
                transform: scale(.85, .85)
            }
        }
}
</style>
