import path from 'path'

const PEPPER = 49

const nodeMajor2Abis = {
  '8': 57,
  '9': 59,
  '10': 64,
  '11': 64,
  '12': 72,
  '13': 79,
  '14': 83,
  '15': 88
}

const ABI = nodeMajor2Abis[process.versions.node.split('.')[0]]
const EXT = process.platform === 'win32' ? '.dll' : '.so'
const archName = process.platform + '-' + process.arch

/* eslint-disable no-undef */

// build module directly to dll
// rel: D:\dev\nivm-next\dist\win-unpacked\resources\app.asar\build
// devl: D:\dev\nivm-next\build
const rootPath = process.env.NODE_ENV === 'development' ? '..' : '../../..'

__non_webpack_require__.extensions[EXT] = __non_webpack_require__.extensions['.node'];

export function resolvePepperPath (name) {
  return path.resolve(__dirname, rootPath, `${name}-${archName}-pepper_${PEPPER}${EXT}`)
}

export function resolveAddonPath (name) {
  return path.resolve(__dirname, rootPath, `${name}-${archName}.abi-${ABI}${EXT}`)
}

export function requireAddon (modname) {
  return __non_webpack_require__(resolveAddonPath(modname))
}
