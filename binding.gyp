{
  "targets": [
    {
      "target_name": "fastscan",
      "sources": [
        "native/src/addon.c",
        "native/src/scanner.c",
        "native/src/mmap_reader.c",
        "native/src/fastscan.c",
        "native/src/thread_pool.c"
      ],
      "include_dirs": [
        "native/include"
      ],
      "cflags": [
        "-O3",
        "-pthread",
        "-Wall"
      ],
      "ldflags": [
        "-pthread"
      ],
      "conditions": [
        [
          "target_arch=='x64' or target_arch=='ia32'",
          {
            "cflags": [
              "-mavx2",
              "-msse2"
            ]
          }
        ],
        [
          "target_arch=='arm64'",
          {
            "defines": [
              "__ARM_NEON"
            ]
          }
        ],
        [
          "OS=='linux'",
          {
            "defines": [
              "_GNU_SOURCE"
            ]
          }
        ],
        [
          "OS=='mac'",
          {
            "xcode_settings": {
              "MACOSX_DEPLOYMENT_TARGET": "10.15",
              "OTHER_CFLAGS": [
                "-O3",
                "-Wall"
              ]
            }
          }
        ],
        [
          "OS=='win'",
          {
            "defines": [
              "WIN32_LEAN_AND_MEAN",
              "_CRT_SECURE_NO_WARNINGS"
            ],
            "msvs_settings": {
              "VCCLCompilerTool": {
                "Optimization": "3",
                "FavorSizeOrSpeed": "1",
                "InlineFunctionExpansion": "2",
                "AdditionalOptions": [
                  "/O2",
                  "/arch:AVX2"
                ]
              }
            }
          }
        ]
      ],
      "defines": [
        "NAPI_DISABLE_CPP_EXCEPTIONS"
      ]
    }
  ]
}
