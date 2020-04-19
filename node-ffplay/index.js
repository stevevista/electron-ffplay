const validABIs = [
  64, 67, 72, 79
]

let abi = +process.versions.modules;
let selAbi = 0
for (let i = 0; i < validABIs.length; i++) {
  if (validABIs[i] == abi) {
    selAbi = validABIs[i];
    break;
  } else if (validABIs[i] > abi) {
    if ( i > 0) {
      selAbi = validABIs[i - 1];
    }
    break;
  }
}

const suffix = process.platform + '-' + process.arch

if (selAbi === 0) {
  throw Error(`no valid abi native match for node-${suffix}: ${process.versions.node}`)
}

const binding = require(`./build/lib/Release/ff_binding-${suffix}.abi-${selAbi}`);
const {EventEmitter} = require('events');
const {inherits} = require('util');
const {PlayBack, Decoder} = binding

inherits(PlayBack, EventEmitter)

PlayBack.prototype.command = function (...args) {
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

module.exports = {
  PlayBack,
  Decoder
}
