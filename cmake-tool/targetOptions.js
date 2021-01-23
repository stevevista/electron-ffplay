"use strict";

let environment = require("./environment");
let _ = require("lodash");

function TargetOptions(options) {
    this.options = options || {};
}

Object.defineProperties(TargetOptions.prototype, {
    arch: {
        get: function () {
            return this.options.arch || environment.arch;
        }
    },
    isX86: {
        get: function () {
            return this.arch === "ia32";
        }
    },
    isX64: {
        get: function () {
            return this.arch === "x64";
        }
    },
    isArm: {
        get: function () {
            return this.arch === "arm";
        }
    }
});

module.exports = TargetOptions;