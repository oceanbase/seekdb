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
          "link_settings": {
            "libraries": [
              "-Wl,-rpath,$ORIGIN"
            ]
          },
          "product_dir": '<(pack_dir)'
        }]
      ]
    }
  ]
}
