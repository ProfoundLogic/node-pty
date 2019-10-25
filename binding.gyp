{
  'targets': [{
    'target_name': 'pty',
      'cflags!': [ '-fno-exceptions' ],
      'cflags_cc!': [ '-fno-exceptions' ],
      'xcode_settings': { 'GCC_ENABLE_CPP_EXCEPTIONS': 'YES',
        'CLANG_CXX_LIBRARY': 'libc++',
        'MACOSX_DEPLOYMENT_TARGET': '10.7',
      },
      'msvs_settings': {
        'VCCLCompilerTool': { 'ExceptionHandling': 1 },
      },
      'cflags!': [ '-fno-exceptions' ],
      'cflags_cc!': [ '-fno-exceptions' ],
      'xcode_settings': { 'GCC_ENABLE_CPP_EXCEPTIONS': 'YES',
        'CLANG_CXX_LIBRARY': 'libc++',
        'MACOSX_DEPLOYMENT_TARGET': '10.7',
      },
      'msvs_settings': {
        'VCCLCompilerTool': { 'ExceptionHandling': 1 },
      },
    'include_dirs' : [
      '<!@(node -p "require(\'node-addon-api\').include")',
    ],
    'conditions': [
      ['OS=="win"', {
        # "I disabled those warnings because of winpty" - @peters (GH-40)
        'msvs_disabled_warnings': [ 4506, 4530 ],
        'include_dirs' : [
          'deps/winpty/src/include',
        ],
        'dependencies' : [
          'deps/winpty/src/winpty.gyp:winpty-agent',
          'deps/winpty/src/winpty.gyp:winpty',
          "<!(node -p \"require('node-addon-api').gyp\")",
        ],
        'sources' : [
          'src/win/pty.cc',
          'src/win/path_util.cc'
        ],
        'libraries': [
          'shlwapi.lib'
        ],
      }, { # OS!="win"
        'sources': [
          'src/unix/pty.cc'
        ],
        'libraries': [
          '-lutil',
          '-L/usr/lib',
          '-L/usr/local/lib'
        ],
      }],
      # http://www.gnu.org/software/gnulib/manual/html_node/forkpty.html
      #   One some systems (at least including Cygwin, Interix,
      #   OSF/1 4 and 5, and Mac OS X) linking with -lutil is not required.
      ['OS=="mac" or OS=="solaris"', {
        'libraries!': [
          '-lutil'
        ]
      }],
      ['OS=="mac"', {
        # The cflags+ option is for node-addon-api 
        "cflags+":  ["-fvisibility=hidden"],
        "xcode_settings": {
          "OTHER_CPLUSPLUSFLAGS": [
            "-std=c++11",
            "-stdlib=libc++"
          ],
          "OTHER_LDFLAGS": [
            "-stdlib=libc++"
          ],
          "MACOSX_DEPLOYMENT_TARGET":"10.7",
          # For node-addon-api
          'GCC_SYMBOLS_PRIVATE_EXTERN': 'YES', # -fvisibility=hidden
        }
      }]
    ]
  }]
}
