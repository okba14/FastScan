{
  "targets": [
    {
      "target_name": "fastscan",
      "sources": [
        "native/src/addon.c",
        "native/src/scanner.c",
        "native/src/mmap_reader.c",
        "native/src/parser.c",
        "native/src/fastscan.c"
      ],
      "include_dirs": [
        "native/include"
      ],

      'cflags': [
        '-O3', 
        '-pthread'
      ],
      'ldflags': [
        '-pthread'
      ],
      "conditions": [
        [
          "OS=='linux'",
          {
            "sources": [ "native/src/syscall_linux.c" ]
          }
        ],
        [
          "OS!='linux'",
          {
            "sources": [ "native/src/syscall_stub.c" ]
          }
        ]
      ],
      "cflags!": [ "-fno-exceptions" ],
      "cflags_cc!": [ "-fno-exceptions" ],
      "defines": [ "NAPI_DISABLE_CPP_EXCEPTIONS" ],
      "xcode_settings": {
        "GCC_ENABLE_CPP_EXCEPTIONS": "YES",
        "CLANG_CXX_LIBRARY": "libc++",
        "MACOSX_DEPLOYMENT_TARGET": "10.7"
      },
      "msvs_settings": {
        "VCCLCompilerTool": {
          "ExceptionHandling": 1
        }
      }
    }
  ]
}
