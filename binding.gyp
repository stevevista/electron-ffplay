{
    'targets': [
        {
            'target_name': 'node-ffplay-darwin-x64.abi-72.dylib',
            'cflags': ['-fexceptions', '-DHAVE_NANOSLEEP', '-lSDL2', '-pthread','-w','-Wdiscarded-qualifiers','-fpermissive','-Wsign-compare'],
            'cflags_cc': ['-fexceptions','-DHAVE_NANOSLEEP', '-lSDL2', '-pthread','-w','-Wdiscarded-qualifiers','-fpermissive','-Wsign-compare'],
            'sources': ['node-ffplay/src/player.cc', 'node-ffplay/src/player.h', 'node-ffplay/src/wrap.cc'],
            'include_dirs': ['<!@(node -p \'require("node-addon-api").include\')'],
            'libraries': [],
            'dependencies': [
                '<!(node -p \'require("node-addon-api").gyp\')'
            ],
            'defines': [
                'NAPI_CPP_EXCEPTIONS'
            ],
            'conditions': [
                ['OS=="mac"', {
                    'xcode_settings': {
                        'GCC_ENABLE_CPP_EXCEPTIONS': 'YES',
                        'MACOSX_DEPLOYMENT_TARGET': '12.0',
                        'OTHER_CFLAGS': ['-fpermissive','-w','-Wdiscarded-qualifiers']
                    },
                    'libraries': [
                        '/usr/local/lib/libSDL2.dylib',
                        '/usr/local/lib/libSDL2_image.dylib',
                        '/usr/local/lib/libavcodec.dylib',
                        '/usr/local/lib/libavformat.dylib',
                        '/usr/local/lib/libavfilter.dylib',
                        '/usr/local/lib/libavutil.dylib'
                    ],
                    'include_dirs': [
                        '/usr/local/include/SDL2',
                        '/usr/local/libavcodec',
                        '/usr/local/libavdevice',
                        '/usr/local/libavfilter',
                        '/usr/local/libavformat',

                    ]
                }],

                ['OS=="linux" and target_arch=="x64"', {
                    'libraries': [
                        '/usr/lib/x86_64-linux-gnu/libSDL2-2.0.so',
                        '/usr/lib/x86_64-linux-gnu/libSDL2_image-2.0.so'
                    ],
                    'include_dirs': [
                        '/usr/include/SDL2'
                    ]
                }],

                ['OS=="linux" and target_arch=="ia32"', {
                    'libraries': [
                        '/usr/lib/libSDL2-2.0.so',
                        '/usr/lib/libSDL2_image-2.0.so'
                    ],
                    'include_dirs': [
                        '/usr/include/SDL2'
                    ]
                }]
            ]
        }
    ]
}
