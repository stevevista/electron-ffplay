/*
Copyright (c) 2014-2016 Brion Vibber <brion@pobox.com>

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
MPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/
/* eslint-disable*/
"use strict";

const shaders = {
	vertex: `
precision lowp float;

attribute vec2 aPosition;
attribute vec2 aLumaPosition;
attribute vec2 aChromaPosition;
varying vec2 vLumaPosition;
varying vec2 vChromaPosition;

void main() {
	gl_Position = vec4(aPosition, 0, 1);
	vLumaPosition = aLumaPosition;
	vChromaPosition = aChromaPosition;
}
	`,
	// inspired by https://github.com/mbebenita/Broadway/blob/master/Player/canvas.js
	fragment: `
precision lowp float;

uniform sampler2D uTextureY;
uniform sampler2D uTextureCb;
uniform sampler2D uTextureCr;
varying vec2 vLumaPosition;
varying vec2 vChromaPosition;

void main() {
	// Y, Cb, and Cr planes are uploaded as LUMINANCE textures.
	float fY = texture2D(uTextureY, vLumaPosition).x;
	float fCb = texture2D(uTextureCb, vChromaPosition).x;
	float fCr = texture2D(uTextureCr, vChromaPosition).x;
	
	// Premultipy the Y...
	float fYmul = fY * 1.1643828125;
	
	// And convert that to RGB!
	gl_FragColor = vec4(fYmul + 1.59602734375 * fCr - 0.87078515625, fYmul - 0.39176171875 * fCb - 0.81296875 * fCr + 0.52959375, fYmul + 2.017234375 * fCb - 1.081390625, 1);
}
	`,
	vertexStripe: `
precision lowp float;

attribute vec2 aPosition;
attribute vec2 aTexturePosition;
varying vec2 vTexturePosition;

void main() {
	gl_Position = vec4(aPosition, 0, 1);
	vTexturePosition = aTexturePosition;
}
`,
	// extra 'stripe' texture fiddling to work around IE 11's poor performance on gl.LUMINANCE and gl.ALPHA textures
	// Y, Cb, and Cr planes are mapped into a pseudo-RGBA texture
	// so we can upload them without expanding the bytes on IE 11
	// which doesn't allow LUMINANCE or ALPHA textures
	// The stripe textures mark which channel to keep for each pixel.
	// Each texture extraction will contain the relevant value in one
	// channel only.
	fragmentStripe: `
precision lowp float;

uniform sampler2D uStripe;
uniform sampler2D uTexture;
varying vec2 vTexturePosition;

void main() {
	float fLuminance = dot(texture2D(uStripe, vTexturePosition), texture2D(uTexture, vTexturePosition));
	gl_FragColor = vec4(fLuminance, fLuminance, fLuminance, 1);
}
`
};

	/**
	 * Warning: canvas must not have been used for 2d drawing prior!
	 *
	 * @param {HTMLCanvasElement} canvas - HTML canvas element to attach to
	 * @constructor
	 */
function WebGLFrameSink(canvas, owner) {
		var self = this;
		const options = {
				// Don't trigger discrete GPU in multi-GPU systems
				preferLowPowerToHighPerformance: true,
				powerPreference: 'low-power',
				// Don't try to use software GL rendering!
				failIfMajorPerformanceCaveat: true,
				// In case we need to capture the resulting output.
				preserveDrawingBuffer: true
		};
	
		const gl = canvas.getContext('webgl2', options);

		if (gl === null) {
			throw new Error('WebGL unavailable');
		}

		function compileShader(type, source) {
			var shader = gl.createShader(type);
			gl.shaderSource(shader, source);
			gl.compileShader(shader);

			if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
				var err = gl.getShaderInfoLog(shader);
				gl.deleteShader(shader);
				throw new Error('GL shader compilation for ' + type + ' failed: ' + err);
			}

			return shader;
		}


		var program,
			unpackProgram,
			err;

		// In the world of GL there are no rectangles.
		// There are only triangles.
		// THERE IS NO SPOON.
		var rectangle = new Float32Array([
			// First triangle (top left, clockwise)
			-1.0, -1.0,
			+1.0, -1.0,
			-1.0, +1.0,

			// Second triangle (bottom right, clockwise)
			-1.0, +1.0,
			+1.0, -1.0,
			+1.0, +1.0
		]);

		var textures = {};
		var framebuffers = {};
		var stripes = {};
		var buf, positionLocation, unpackPositionLocation;
		var unpackTexturePositionBuffer, unpackTexturePositionLocation;
		var stripeLocation, unpackTextureLocation;
		var lumaPositionBuffer, lumaPositionLocation;
		var chromaPositionBuffer, chromaPositionLocation;

		function createOrReuseTexture(name) {
			if (!textures[name]) {
				textures[name] = gl.createTexture();
			}
			return textures[name];
		}

		function uploadTexture(name, width, height, data) {
			var texture = createOrReuseTexture(name);
			gl.activeTexture(gl.TEXTURE0);

			if (WebGLFrameSink.stripe) {
				var uploadTemp = !textures[name + '_temp'];
				var tempTexture = createOrReuseTexture(name + '_temp');
				gl.bindTexture(gl.TEXTURE_2D, tempTexture);
				if (uploadTemp) {
					// new texture
					gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
					gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
					gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
					gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
					gl.texImage2D(
						gl.TEXTURE_2D,
						0, // mip level
						gl.RGBA, // internal format
						width / 4,
						height,
						0, // border
						gl.RGBA, // format
						gl.UNSIGNED_BYTE, // type
						data // data!
					);
				} else {
					// update texture
					gl.texSubImage2D(
						gl.TEXTURE_2D,
						0, // mip level
						0, // x offset
						0, // y offset
						width / 4,
						height,
						gl.RGBA, // format
						gl.UNSIGNED_BYTE, // type
						data // data!
					);
				}

				var stripeTexture = textures[name + '_stripe'];
				var uploadStripe = !stripeTexture;
				if (uploadStripe) {
					stripeTexture = createOrReuseTexture(name + '_stripe');
				}
				gl.bindTexture(gl.TEXTURE_2D, stripeTexture);
				if (uploadStripe) {
					gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
					gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
					gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
					gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
					gl.texImage2D(
						gl.TEXTURE_2D,
						0, // mip level
						gl.RGBA, // internal format
						width,
						1,
						0, // border
						gl.RGBA, // format
						gl.UNSIGNED_BYTE, //type
						buildStripe(width, 1) // data!
					);
				}

			} else {
				gl.bindTexture(gl.TEXTURE_2D, texture);
				gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
				gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
				gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
				gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
				gl.texImage2D(
					gl.TEXTURE_2D,
					0, // mip level
					gl.LUMINANCE, // internal format
					width,
					height,
					0, // border
					gl.LUMINANCE, // format
					gl.UNSIGNED_BYTE, //type
					data // data!
				);
			}
		}

		function unpackTexture(name, width, height) {
			var texture = textures[name];

			// Upload to a temporary RGBA texture, then unpack it.
			// This is faster than CPU-side swizzling in ANGLE on Windows.
			gl.useProgram(unpackProgram);

			var fb = framebuffers[name];
			if (!fb) {
				// Create a framebuffer and an empty target size
				gl.activeTexture(gl.TEXTURE0);
				gl.bindTexture(gl.TEXTURE_2D, texture);
				gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
				gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
				gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
				gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
				gl.texImage2D(
					gl.TEXTURE_2D,
					0, // mip level
					gl.RGBA, // internal format
					width,
					height,
					0, // border
					gl.RGBA, // format
					gl.UNSIGNED_BYTE, //type
					null // data!
				);

				fb = framebuffers[name] = gl.createFramebuffer();
			}

			gl.bindFramebuffer(gl.FRAMEBUFFER, fb);
			gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, texture, 0);

			var tempTexture = textures[name + '_temp'];
			gl.activeTexture(gl.TEXTURE1);
			gl.bindTexture(gl.TEXTURE_2D, tempTexture);
			gl.uniform1i(unpackTextureLocation, 1);

			var stripeTexture = textures[name + '_stripe'];
			gl.activeTexture(gl.TEXTURE2);
			gl.bindTexture(gl.TEXTURE_2D, stripeTexture);
			gl.uniform1i(stripeLocation, 2);

			// Rectangle geometry
			gl.bindBuffer(gl.ARRAY_BUFFER, buf);
			gl.enableVertexAttribArray(positionLocation);
			gl.vertexAttribPointer(positionLocation, 2, gl.FLOAT, false, 0, 0);

			// Set up the texture geometry...
			gl.bindBuffer(gl.ARRAY_BUFFER, unpackTexturePositionBuffer);
			gl.enableVertexAttribArray(unpackTexturePositionLocation);
			gl.vertexAttribPointer(unpackTexturePositionLocation, 2, gl.FLOAT, false, 0, 0);

			// Draw into the target texture...
			gl.viewport(0, 0, width, height);

			gl.drawArrays(gl.TRIANGLES, 0, rectangle.length / 2);

			gl.bindFramebuffer(gl.FRAMEBUFFER, null);

		}

		function attachTexture(name, register, index) {
			gl.activeTexture(register);
			gl.bindTexture(gl.TEXTURE_2D, textures[name]);
			gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
			gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
			gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
			gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);

			gl.uniform1i(gl.getUniformLocation(program, name), index);
		}

		function buildStripe(width) {
			if (stripes[width]) {
				return stripes[width];
			}
			var len = width,
				out = new Uint32Array(len);
			for (var i = 0; i < len; i += 4) {
				out[i    ] = 0x000000ff;
				out[i + 1] = 0x0000ff00;
				out[i + 2] = 0x00ff0000;
				out[i + 3] = 0xff000000;
			}
			return stripes[width] = new Uint8Array(out.buffer);
		}

		function initProgram(vertexShaderSource, fragmentShaderSource) {
			var vertexShader = compileShader(gl.VERTEX_SHADER, vertexShaderSource);
			var fragmentShader = compileShader(gl.FRAGMENT_SHADER, fragmentShaderSource);

			var program = gl.createProgram();
			gl.attachShader(program, vertexShader);
			gl.attachShader(program, fragmentShader);

			gl.linkProgram(program);
			if (!gl.getProgramParameter(program, gl.LINK_STATUS)) {
				var err = gl.getProgramInfoLog(program);
				gl.deleteProgram(program);
				throw new Error('GL program linking failed: ' + err);
			}

			return program;
		}

		function init() {
			if (WebGLFrameSink.stripe) {
				unpackProgram = initProgram(shaders.vertexStripe, shaders.fragmentStripe);
				unpackPositionLocation = gl.getAttribLocation(unpackProgram, 'aPosition');

				unpackTexturePositionBuffer = gl.createBuffer();
				var textureRectangle = new Float32Array([
					0, 0,
					1, 0,
					0, 1,
					0, 1,
					1, 0,
					1, 1
				]);
				gl.bindBuffer(gl.ARRAY_BUFFER, unpackTexturePositionBuffer);
				gl.bufferData(gl.ARRAY_BUFFER, textureRectangle, gl.STATIC_DRAW);

				unpackTexturePositionLocation = gl.getAttribLocation(unpackProgram, 'aTexturePosition');
				stripeLocation = gl.getUniformLocation(unpackProgram, 'uStripe');
				unpackTextureLocation = gl.getUniformLocation(unpackProgram, 'uTexture');
			}
			program = initProgram(shaders.vertex, shaders.fragment);

			buf = gl.createBuffer();
			gl.bindBuffer(gl.ARRAY_BUFFER, buf);
			gl.bufferData(gl.ARRAY_BUFFER, rectangle, gl.STATIC_DRAW);

			positionLocation = gl.getAttribLocation(program, 'aPosition');
			lumaPositionBuffer = gl.createBuffer();
			lumaPositionLocation = gl.getAttribLocation(program, 'aLumaPosition');
			chromaPositionBuffer = gl.createBuffer();
			chromaPositionLocation = gl.getAttribLocation(program, 'aChromaPosition');
		}

		/**
		 * Actually draw a frame.
		 * @param {YUVFrame} buffer - YUV frame buffer object
		 */
		self.drawFrame = function(buffer) {
			const {width, height, cropLeft, cropTop, cropWidth, cropHeight} = buffer;

			var formatUpdate = (!program || canvas.width !== width || canvas.height !== height);
			if (formatUpdate) {
				// Keep the canvas at the right size...
				canvas.width = buffer.width;
				canvas.height = buffer.height;
				if (owner) {
					owner.changeWindowSize(width, height);
				}
				self.clear();
			}

			if (!program) {
				init();
			}

			if (formatUpdate) {
				// clear cache
				textures = {};
				framebuffers = {};
				stripes = {};

				var setupTexturePosition = function(buffer, location, texWidth) {
					// Warning: assumes that the stride for Cb and Cr is the same size in output pixels
					var textureX0 = cropLeft / texWidth;
					var textureX1 = (cropLeft + cropWidth) / texWidth;
					var textureY0 = (cropTop + cropHeight) / height;
					var textureY1 = cropTop / height;
					var textureRectangle = new Float32Array([
						textureX0, textureY0,
						textureX1, textureY0,
						textureX0, textureY1,
						textureX0, textureY1,
						textureX1, textureY0,
						textureX1, textureY1
					]);

					gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
					gl.bufferData(gl.ARRAY_BUFFER, textureRectangle, gl.STATIC_DRAW);
				};
				setupTexturePosition(
					lumaPositionBuffer,
					lumaPositionLocation,
					buffer.y.stride);
				setupTexturePosition(
					chromaPositionBuffer,
					chromaPositionLocation,
					buffer.u.stride * 2);
			}

			// Create or update the textures...
			uploadTexture('uTextureY', buffer.y.stride, buffer.height, buffer.y.bytes);
			uploadTexture('uTextureCb', buffer.u.stride, buffer.height / 2, buffer.u.bytes);
			uploadTexture('uTextureCr', buffer.v.stride, buffer.height / 2, buffer.v.bytes);

			if (WebGLFrameSink.stripe) {
				// Unpack the textures after upload to avoid blocking on GPU
				unpackTexture('uTextureY', buffer.y.stride, buffer.height);
				unpackTexture('uTextureCb', buffer.u.stride, buffer.height / 2);
				unpackTexture('uTextureCr', buffer.v.stride, buffer.height / 2);
			}

			// Set up the rectangle and draw it
			gl.useProgram(program);
			gl.viewport(0, 0, canvas.width, canvas.height);

			attachTexture('uTextureY', gl.TEXTURE0, 0);
			attachTexture('uTextureCb', gl.TEXTURE1, 1);
			attachTexture('uTextureCr', gl.TEXTURE2, 2);

			// Set up geometry
			gl.bindBuffer(gl.ARRAY_BUFFER, buf);
			gl.enableVertexAttribArray(positionLocation);
			gl.vertexAttribPointer(positionLocation, 2, gl.FLOAT, false, 0, 0);

			// Set up the texture geometry...
			gl.bindBuffer(gl.ARRAY_BUFFER, lumaPositionBuffer);
			gl.enableVertexAttribArray(lumaPositionLocation);
			gl.vertexAttribPointer(lumaPositionLocation, 2, gl.FLOAT, false, 0, 0);

			gl.bindBuffer(gl.ARRAY_BUFFER, chromaPositionBuffer);
			gl.enableVertexAttribArray(chromaPositionLocation);
			gl.vertexAttribPointer(chromaPositionLocation, 2, gl.FLOAT, false, 0, 0);

			// Aaaaand draw stuff.
			gl.drawArrays(gl.TRIANGLES, 0, rectangle.length / 2);
		};

		self.clear = function() {
			gl.viewport(0, 0, canvas.width, canvas.height);
			gl.clearColor(0.0, 0.0, 0.0, 0.0);
			gl.clear(gl.COLOR_BUFFER_BIT);
		};

		self.cropImage = function (cropCanvas, rect) {
			const width = rect[2] - rect[0];
			const height = rect[3] - rect[1]
			const y = canvas.height - rect[3]
			
			const ctx = cropCanvas.getContext('2d')
			const img = ctx.createImageData(width, height)

			gl.readPixels(rect[0], y, width, height, gl.RGBA, gl.UNSIGNED_BYTE, img.data);
			// console.log(img.data); // Uint8Array
			cropCanvas.width = width;
			cropCanvas.height = height;

			ctx.putImageData(img, 0, 0)
		}

		self.clear();

		return self;
	}

	// For Windows; luminance and alpha textures are ssllooww to upload,
	// so we pack into RGBA and unpack in the shaders.
	//
	// This seems to affect all browsers on Windows, probably due to fun
	// mismatches between GL and D3D.
	WebGLFrameSink.stripe = (function() {
		if (navigator.userAgent.indexOf('Windows') !== -1) {
			return true;
		}
		return false;
	})();

module.exports = WebGLFrameSink;

