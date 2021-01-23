const {app} = require('electron')
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
  
app.allowRendererProcessReuse = false;

const ABI = nodeMajor2Abis[process.versions.node.split('.')[0]]
console.log(ABI)
app.quit()
