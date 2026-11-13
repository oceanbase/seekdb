{
  "targets": [
    {
      "target_name": "seekdb",
      "sources": [ "seekdb.cpp" ],
      "include_dirs": [
        "<!@(node -p \"require('node-addon-api').include\")",
        "../../../src/include"
      ],
      "libraries": [
        "-lseekdb"
      ],
      "library_dirs": [
        "../../../build_release/src/include"
      ],
      "conditions": [
        ["OS!='win'", {
          "ldflags": [
            "<!@(node -e \"const path = require('path'); const moduleRoot = process.cwd(); const projectRoot = path.resolve(moduleRoot, '../../../'); const libPath = path.resolve(projectRoot, 'build_release/src/include'); console.log('-Wl,-rpath,' + libPath);\")"
          ]
        }]
      ],
      "cflags!": [ "-fno-exceptions" ],
      "cflags_cc!": [ "-fno-exceptions" ],
      "defines": [ "NAPI_DISABLE_CPP_EXCEPTIONS" ],
      "xcode_settings": {
        "GCC_ENABLE_CPP_EXCEPTIONS": "YES",
        "CLANG_CXX_LIBRARY": "libc++",
        "MACOSX_DEPLOYMENT_TARGET": "10.7",
        "OTHER_LDFLAGS": [
          "<!@(node -e \"const path = require('path'); const moduleRoot = process.cwd(); const projectRoot = path.resolve(moduleRoot, '../../../'); const libPath = path.resolve(projectRoot, 'build_release/src/include'); console.log('-Wl,-rpath,' + libPath);\")"
        ]
      },
      "msvs_settings": {
        "VCCLCompilerTool": { "ExceptionHandling": 1 }
      }
    }
  ]
}
