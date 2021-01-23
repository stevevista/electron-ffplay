/* eslint-disable*/
import {Decoder} from '../../ffplay'
import {EventEmitter} from 'events';
import fs from 'fs'

let frameSize = [
		11727,
		4582,
		1456,
		664,
		776,
		3797,
		1434,
		703,
		677,
		4446,
		1476,
		702,
		743,
		5640,
		1655,
		1037,
		830,
		4764,
		1016,
		697,
		485,
		4279,
		957,
		451,
		541,
		3538,
		707,
		508,
		298,
		2636,
		430,
		421,
		3896,
		1081,
		622,
		571,
		4058,
		1022,
		611,
		477,
		3526,
		917,
		499,
		505,
		3473,
		1204,
		561,
		625,
		4322,
		1211,
		819,
		511,
		4564,
		1222,
		593,
		760,
		3768,
		1167,
		670,
		538,
		3671,
		1015,
		589,
		645,
		4105,
		1265,
		653,
		710,
		3837,
		1332,
		628,
		712,
		3769,
		1162,
		760,
		630,
		3636,
		969,
		897,
		4692,
		1448,
		848,
		729,
		4932,
		1589,
		800,
		836,
		3728,
		1570,
		696,
		1156,
		5333,
		2190,
		1128,
		1240,
		4944,
		1119,
		760,
		530,
		3421,
		648,
		427,
		440,
		3225,
		683,
		440,
		324,
		2844,
		534,
		322,
		379,
		2770,
		667,
		414,
		329,
		2568,
		702,
		363,
		397,
		3351,
		929,
		438,
		456,
		3965,
		1184,
		566,
		740,
		5103,
		1449,
		662,
		622,
		4300,
		1167,
		614,
		590,
		3681,
		1348,
		675,
		851,
		4469,
		1874,
		1072,
		1003,
		4192,
		1828,
		974,
		869,
		3521,
		1534,
		829,
		720,
		4906,
		2117,
		993,
		1649,
		6121,
		2386,
		1728,
		1229,
		3624,
		1129,
		603,
		575,
		2698,
		764,
		515,
		461,
		3283,
		1213,
		600,
		936,
		5888,
		2247,
		1412,
		1203,
		3826,
		991,
		3312,
		1011,
		2710,
		2465,
		2398,
		2363,
		2847,
		2830,
		2723,
		4034,
		1224,
		2328,
		2556,
		2747,
		2931,
		3095,
		2934,
		3007,
		6070,
		2441,
		1198,
		1103,
		1816,
		2331,
		2820,
		2778,
		2397,
		2459,
		2464,
		2581,
		2380,
		2184,
		2476,
		3283,
		4155,
		4041,
		4012,
		3768,
		15280,
		841,
		628,
		11795,
		1044,
		550,
		494,
		5967,
		827,
		522,
		404,
		8338,
		653,
		371,
		327,
		9169,
		964,
		393,
		440,
		6349,
		513,
		313,
		487,
		7677,
		1069,
		505,
		520,
		7883,
		901,
		619,
		544,
		7344,
		807,
		502,
		314,
		33123,
		5625,
		467,
		278,
		165,
		4590,
		604,
		419,
		67,
		5746,
		266,
		214,
		451,
		5018,
		507,
		208,
		283,
		5388,
		845,
		317,
		139,
		5015,
		387,
		132,
		212,
		5159,
		458,
		174,
		158,
		5264,
		228,
		160,
		224,
		4882,
		615,
		307,
		368,
		3745,
		413,
		380,
		351,
		2650,
		617,
		285,
		168,
		964,
		811,
		445,
		324,
		312
	];

const delay = (ms) => new Promise((resolve) => setTimeout(resolve, ms))

async function loopPacket (fd, ff) {
	let decoer = new Decoder('h264')

	for (const size of frameSize) {
		const buffer = await readFile(fd, size)

		// decode (buffer[, size, offset, type, callback])
		// type = 0 padding YUV [width, height, ystride, ustride, vstride, Y, U, V]
		// type = 1 compat YUV [width, height, data]
		// type = 2 RGBA [width, height, data]
		let result = decoer.decode(buffer, size)
		// console.log('--------->', result)
		if (result) {
			let frame = {
				width: result[0],
				height: result[1],
				format: {
					cropLeft: 0,
					cropTop: 0,
					cropWidth: result[0],
					cropHeight: result[1]
				},
				y: { bytes: result[5], stride: result[2] },
				u: { bytes: result[6], stride: result[3] },
				v: { bytes: result[7], stride: result[4] }
			}

			ff.emit('yuv', frame)
			await delay(50);
		}
	}

	ff.emit('end')
}

function openFile (url) {
	return new Promise((resolve, reject) => {
		fs.open(url, 'r', (err, fd) => {
			if (err) reject(err)
			else resolve(fd)
		})
	})
}

function readFile (fd, size) {
	let buffer = Buffer.alloc(size)
	return new Promise((resolve, reject) => {
		fs.read(fd, buffer, 0, size, null, (err) => {
			if (err) reject(err)
			else resolve(buffer)
		});
	})
}

export function DecodeSync (vargs) {
	let url = vargs[vargs.length - 1] // 'C:\\Users\\rjzhou\\Documents\\foreman_352x288_30fps.h264'
	let emit = new EventEmitter()

	openFile(url).then(fd => loopPacket(fd, emit))

  emit.quit = () => {}
  emit.toogle_pause = () => {}
  emit.toogle_mute = () => {}
  emit.volume_up = () => {}
  emit.volume_down = () => {}
  emit.volume = (v) => {}
  emit.seek = (v) => {}
	emit.seek_to = (v) => {}
	
	return emit
}

export async function DecodeInPlayer (ff, url) {
	ff.open_video_decode('h264')

	const fd = await openFile(url)

	for (const size of frameSize) {
		const buffer = await readFile(fd, size)
		ff.send_video_data(buffer)
		await delay(50);
	}
	ff.quit()
}
