# Simple video player using electron & ffplay (as node addon)

![avatar](./ffplay.png)

# License
* [FFMpeg] (https://ffmpeg.org/legal.html)
* [cmake-js] (https://github.com/cmake-js/cmake-js)
* [cmake-node-module] (https://github.com/mapbox/cmake-node-module/tree/master)

# Requirements
- Windows 10 x64 (not tested on other platforms)
- CMake 3.9 or above
- Visual Studio 2017/2019
- nodejs 12.x.x or above

- https://github.com/ShiftMediaProject/SDL/releases/download/release-2.0.14/libsdl_release-2.0.14_msvc15.zip
- https://github.com/BtbN/FFmpeg-Builds/releases/download/autobuild-2021-01-14-13-18/ffmpeg-n4.3.1-29-g89daac5fe2-win64-lgpl-shared-4.3.zip

uncompress libsdl2*.zip & ffmpeg*.zip to electron-ffplay/.third-party/prebuilt

put .third_party/prebuilt/bin[/x64]/*.dll in electron-ffplay/

# build
```bash
# get electron-ffplay source
cd electron-ffplay

# install dependencies
npm i

# build addon
npm run configure:ffplay
npm run build:ffplay

# run 
npm run dev

``` 
