import {requireAddon} from './import-dll'

const binding = requireAddon('node-ffplay');
const {EventEmitter} = require('events');
const {inherits} = require('util');
const {PlayBack, Decoder} = binding

inherits(PlayBack, EventEmitter)

PlayBack.prototype.command = function (...args) {
  console.log(args)
  this.send(...args)
}

PlayBack.prototype.quit = function (reason) {
  this.send('quit')
}

PlayBack.prototype.toogle_pause = function () {
  this.send('pause')
}

PlayBack.prototype.toogle_mute = function () {
  this.send('mute')
}

PlayBack.prototype.volume_up = function () {
  this.send('volume', 1)
}

PlayBack.prototype.volume_down = function () {
  this.send('volume', -1)
}

PlayBack.prototype.volume = function (v) {
  this.send('volume', 2, v)
}

PlayBack.prototype.seek = function (...args) {
  this.send('seek', ...args)
}

PlayBack.prototype.seek_to = function (v) {
  this.send('seek', 0, v)
}

PlayBack.prototype.speed = function (v) {
  this.send('speed', 0, v)
}

export {
  PlayBack,
  Decoder
}
