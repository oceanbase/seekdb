{
  "variables": {
    "pack_dir%": ""
  },
  "targets": [
    {
      "target_name": "seekdb",
      "sources": [
        "../../../unittest/include/nodejs_napi/seekdb.cpp"
      ],
      "include_dirs": [
        "<!@(node -p \"require('node-addon-api').include\")",
        "../../../src/include",
        "<(pack_dir)"
      ],
      "conditions": [
        ["OS=='mac'", {
          "cflags!": [ "-fno-exceptions" ],
          "cflags_cc!": [ "-fno-exceptions" ],
          "defines": [ "NAPI_DISABLE_CPP_EXCEPTIONS" ],
          "xcode_settings": {
            "GCC_ENABLE_CPP_EXCEPTIONS": "YES",
            "CLANG_CXX_LIBRARY": "libc++",
            "OTHER_LDFLAGS": [
              "-Wl,-rpath,@loader_path"
            ]
          },
          "library_dirs": [ "<(pack_dir)" ],
          "libraries": [ "-lseekdb" ],
          "link_settings": {
            "libraries": [
              "-Wl,-rpath,@loader_path"
            ]
          },
          "product_dir": '<(pack_dir)'
        }],
        ["OS=='linux'", {
          "cflags!": [ "-fno-exceptions" ],
          "cflags_cc!": [ "-fno-exceptions" ],
          "defines": [ "NAPI_DISABLE_CPP_EXCEPTIONS" ],
          "library_dirs": [ "<(pack_dir)" ],
          "libraries": [ "-lseekdb" ],
          "ldflags": [
            "-Wl,-rpath,<(pack_dir)"
          ],
          "product_dir": '<(pack_dir)'
        }],
        ["OS=='win'", {
          "defines": [ "NAPI_DISABLE_CPP_EXCEPTIONS" ],
          "library_dirs": [ "<(pack_dir)" ],
          "libraries": [ "seekdb.lib" ],
          "product_dir": '<(pack_dir)',
          "msvs_settings": {
            "VCCLCompilerTool": { "ExceptionHandling": 1 },
            "VCLinkerTool": {
              # product_dir points at the unpacked zip tree, where seekdb.lib already
              # exists as the import library we link against. MSVC would default the
              # generated import lib to the same name (OutDir\seekdb.lib), which
              # fails with LNK1149 (output filename matches input filename). Redirect
              # it to a distinct name.
              "ImportLibrary": "<(pack_dir)/seekdb_import.lib"
            }
          }
        }]
      ]
    }
  ]
}
