{
  "targets": [
    {
      "target_name": "fastscan",
      "sources": [
        "native/src/addon.c",
        "native/src/scanner.c",
        "native/src/mmap_reader.c",
        "native/src/fastscan.c"
      ],
      "include_dirs": [
        "native/include"
      ],
      "cflags": [
        "-O3", 
        "-msse2",
        "-pthread",
        "-Wall"
      ],
      "ldflags": [
        "-pthread"
      ],
      "conditions": [
        [
          "OS=='linux'",
          {
            "defines": [ "_GNU_SOURCE" ]
          }
        ],
        [
          "OS=='mac'",
          {
            "xcode_settings": {
              "GCC_ENABLE_CPP_EXCEPTIONS": "YES",
              "CLANG_CXX_LIBRARY": "libc++",
              "MACOSX_DEPLOYMENT_TARGET": "10.7",
              "OTHER_CFLAGS": [ "-O3", "-Wall" ]
            }
          }
        ],
        [
          "OS=='win'",
          {
            "msvs_settings": {
              "VCCLCompilerTool": {
                "ExceptionHandling": 1,
                "Optimization": "3"
              }
            }
          }
        ]
      ],
      "defines": [ "NAPI_DISABLE_CPP_EXCEPTIONS" ]
    }
  ]
}
