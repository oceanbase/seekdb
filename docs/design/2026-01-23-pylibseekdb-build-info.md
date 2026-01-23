# [Design] Proposal for pylibseekdb Build Information

| Item          | Description                                                     |
|---------------|-----------------------------------------------------------------|
| Author        | @wrn                                                            |
| Date          | 2026-01-23                                                      |
| Related Issue | [embed] print building information                              |
| Status        | Draft                                                           |

## 1. Background

### 1.1 Problem Statement

Currently, `pylibseekdb` users have no way to determine the exact build information of the library they are using, such as:
- Commit ID (Git revision)
- Version number
- Build date and time
- Build branch
- Build flags

This information is critical for:
- **Debugging**: Reproducing issues with the exact same build
- **Compatibility**: Ensuring version compatibility between client and server
- **Audit**: Tracking which version is deployed in production

### 1.2 Existing Implementation Reference

OceanBase observer already has a mature version printing mechanism via `ObCommandLineParser::print_version()`:

```cpp
// src/observer/ob_command_line_parser.cpp:384-401
void ObCommandLineParser::print_version()
{
  MPRINT("%s (%s %s %s)\n", exe_name.ptr(), OB_OCEANBASE_NAME, OB_SEEKDB_NAME, PACKAGE_VERSION);
  MPRINT("REVISION: %s", build_version());
  MPRINT("BUILD_BRANCH: %s", build_branch());
  MPRINT("BUILD_TIME: %s %s", build_date(), build_time());
  MPRINT("BUILD_FLAGS: %s%s", build_flags(), extra_flags);
  MPRINT("BUILD_INFO: %s\n", build_info());
  MPRINT("%s", COPYRIGHT);
}
```

Output example:
```
observer (OceanBase seekdb 1.1.0.0)
REVISION: 1-e81a3ead2dada566ef953261313597f1bd19a648
BUILD_BRANCH: feature/issue-83
BUILD_TIME: Jan 20 2026 14:30:25
BUILD_FLAGS: RelWithDebInfo
BUILD_INFO:
```

## 2. Design Goals

1. **Consistency**: Follow the same version information format as `observer -V`
2. **Accessibility**: Provide multiple ways for Python users to access build info
3. **Non-intrusive**: Reuse existing version infrastructure, no code duplication
4. **Pythonic**: Follow Python conventions (e.g., `__version__`, `version_info`)

## 3. Detailed Design

### 3.1 API Design

We will expose build information through the `pylibseekdb` module in two ways:

#### 3.1.1 Module Attributes (for programmatic access)

```python
import libseekdb_python as seekdb

# Simple version string (already exists, enhanced)
print(seekdb.__version__)
# Output: "1.1.0.0"

# Structured build info dictionary
print(seekdb.build_info)
# Output:
# {
#     'version': '1.1.0.0',
#     'revision': '1-e81a3ead2dada566ef953261313597f1bd19a648',
#     'branch': 'feature/issue-83',
#     'build_date': 'Jan 20 2026',
#     'build_time': '14:30:25',
#     'build_flags': 'RelWithDebInfo',
#     'product': 'OceanBase seekdb'
# }
```

#### 3.1.2 Function Interface (for formatted output, similar to `observer -V`)

```python
import libseekdb_python as seekdb

# Print formatted version info (similar to observer -V)
seekdb.print_version()
# Output:
# pylibseekdb (OceanBase seekdb 1.1.0.0)
# REVISION: 1-e81a3ead2dada566ef953261313597f1bd19a648
# BUILD_BRANCH: feature/issue-83
# BUILD_TIME: Jan 20 2026 14:30:25
# BUILD_FLAGS: RelWithDebInfo

# Get version string (without printing)
version_str = seekdb.get_version_string()
```

### 3.2 Implementation Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                     Python Layer                                 │
│  libseekdb_python.so                                            │
│  ├── __version__        (str)                                   │
│  ├── build_info         (dict)                                  │
│  ├── print_version()    (function)                              │
│  └── get_version_string() (function)                            │
└─────────────────────────────────────────────────────────────────┘
                              │
                              │ pybind11 bindings
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                     C++ Layer                                    │
│  src/observer/embed/python/ob_embed_impl.cpp                    │
│  └── Uses existing version functions                            │
└─────────────────────────────────────────────────────────────────┘
                              │
                              │ function calls
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                 Existing Version Infrastructure                  │
│  src/share/ob_version.cpp.in → ob_version.cpp                  │
│  ├── build_version()    - "BUILD_NUMBER-GIT_REVISION"          │
│  ├── build_branch()     - Git branch name                       │
│  ├── build_date()       - __DATE__                              │
│  ├── build_time()       - __TIME__                              │
│  ├── build_flags()      - CMAKE_BUILD_TYPE                      │
│  └── build_info()       - Additional build info                 │
│                                                                  │
│  deps/oblib/src/lib/ob_define.h                                 │
│  ├── OB_OCEANBASE_NAME  - "OceanBase"                           │
│  ├── OB_SEEKDB_NAME     - "seekdb"                              │
│  └── PACKAGE_VERSION    - CMake PROJECT_VERSION                 │
└─────────────────────────────────────────────────────────────────┘
```

### 3.3 Code Changes

#### 3.3.1 Modify `src/observer/embed/python/ob_embed_impl.cpp`

Add the following to the `PYBIND11_MODULE` block:

```cpp
#include "lib/ob_define.h"

// External version functions from ob_version.cpp
extern const char* build_version();
extern const char* build_branch();
extern const char* build_date();
extern const char* build_time();
extern const char* build_flags();
extern const char* build_info();

PYBIND11_MODULE(PYTHON_MODEL_NAME, m) {
    m.doc() = "OceanBase seekdb";

    // Existing __version__ attribute (keep as-is or enhance)
    char embed_version_str[oceanbase::common::OB_SERVER_VERSION_LENGTH];
    oceanbase::common::VersionUtil::print_version_str(
        embed_version_str, sizeof(embed_version_str), DATA_CURRENT_VERSION);
    m.attr("__version__") = PACKAGE_VERSION;  // Use simple version string

    // NEW: Build info dictionary
    pybind11::dict build_info_dict;
    build_info_dict["version"] = PACKAGE_VERSION;
    build_info_dict["revision"] = build_version();
    build_info_dict["branch"] = build_branch();
    build_info_dict["build_date"] = build_date();
    build_info_dict["build_time"] = build_time();
    build_info_dict["build_flags"] = build_flags();
    build_info_dict["product"] = std::string(OB_OCEANBASE_NAME) + " " + OB_SEEKDB_NAME;
    m.attr("build_info") = build_info_dict;

    // NEW: print_version() function
    m.def("print_version", []() {
        printf("pylibseekdb (%s %s %s)\n",
               OB_OCEANBASE_NAME, OB_SEEKDB_NAME, PACKAGE_VERSION);
        printf("REVISION: %s\n", build_version());
        printf("BUILD_BRANCH: %s\n", build_branch());
        printf("BUILD_TIME: %s %s\n", build_date(), build_time());
        printf("BUILD_FLAGS: %s\n", build_flags());
    }, "Print version information similar to 'observer -V'");

    // NEW: get_version_string() function
    m.def("get_version_string", []() -> std::string {
        char buf[1024];
        snprintf(buf, sizeof(buf),
            "pylibseekdb (%s %s %s)\n"
            "REVISION: %s\n"
            "BUILD_BRANCH: %s\n"
            "BUILD_TIME: %s %s\n"
            "BUILD_FLAGS: %s",
            OB_OCEANBASE_NAME, OB_SEEKDB_NAME, PACKAGE_VERSION,
            build_version(),
            build_branch(),
            build_date(), build_time(),
            build_flags());
        return std::string(buf);
    }, "Get version information as a string");

    // ... existing code ...
}
```

#### 3.3.2 CMakeLists.txt Changes

Ensure `PACKAGE_VERSION` is properly defined for the Python module. In `src/observer/embed/CMakeLists.txt`, add:

```cmake
target_compile_definitions(${libname} PRIVATE
  PYTHON_MODEL_NAME=${libname}
  PACKAGE_VERSION="${PROJECT_VERSION}"
)
```

### 3.4 Alternative Designs Considered

| Approach | Pros | Cons | Decision |
|----------|------|------|----------|
| **A: Module attributes + functions** | Pythonic, flexible, both programmatic and human-readable access | Slightly more code | ✅ **Selected** |
| **B: Only `__version__` attribute** | Minimal change | Limited info, not like `observer -V` | ❌ Rejected |
| **C: Separate `version` submodule** | Clean namespace | Over-engineering for simple feature | ❌ Rejected |
| **D: Command-line `-V` flag for module** | Consistent with observer | Python modules don't typically have CLI flags | ❌ Rejected |

## 4. Compatibility

### 4.1 Backward Compatibility

- **`__version__`**: Currently exists but returns data version (e.g., "1.1.0.0"). We will keep this behavior or enhance it to return `PACKAGE_VERSION` which is more meaningful for users.
- **New APIs**: `build_info`, `print_version()`, `get_version_string()` are purely additive and do not break existing code.

### 4.2 Python Version Compatibility

- Requires Python 3.6+ (same as existing pylibseekdb requirement)
- Uses standard pybind11 bindings, no special Python version dependencies

## 5. Testing Plan

### 5.1 Unit Tests

Add tests in `src/observer/embed/python/test_version.py`:

```python
import libseekdb_python as seekdb

def test_version_attribute():
    assert hasattr(seekdb, '__version__')
    assert isinstance(seekdb.__version__, str)
    assert len(seekdb.__version__) > 0

def test_build_info_dict():
    assert hasattr(seekdb, 'build_info')
    info = seekdb.build_info
    assert 'version' in info
    assert 'revision' in info
    assert 'branch' in info
    assert 'build_date' in info
    assert 'build_time' in info
    assert 'build_flags' in info
    assert 'product' in info

def test_print_version():
    # Should not raise
    seekdb.print_version()

def test_get_version_string():
    version_str = seekdb.get_version_string()
    assert 'pylibseekdb' in version_str
    assert 'REVISION:' in version_str
    assert 'BUILD_BRANCH:' in version_str
    assert 'BUILD_TIME:' in version_str
    assert 'BUILD_FLAGS:' in version_str
```

### 5.2 Integration Test

Verify output matches expected format after build:

```bash
python3 -c "import libseekdb_python as seekdb; seekdb.print_version()"
```

Expected output format:
```
pylibseekdb (OceanBase seekdb 1.1.0.0)
REVISION: 1-<git-sha>
BUILD_BRANCH: <branch-name>
BUILD_TIME: <date> <time>
BUILD_FLAGS: <build-type>
```

## 6. Documentation

Update the pylibseekdb usage documentation to include:

```markdown
## Version Information

To check the build information of your pylibseekdb installation:

```python
import libseekdb_python as seekdb

# Quick version check
print(seekdb.__version__)  # "1.1.0.0"

# Detailed build info
print(seekdb.build_info)

# Formatted output (like 'observer -V')
seekdb.print_version()
```
```

## 7. Implementation Checklist

- [ ] Modify `src/observer/embed/python/ob_embed_impl.cpp` to add version APIs
- [ ] Update `src/observer/embed/CMakeLists.txt` if needed for compile definitions
- [ ] Add unit tests for version APIs
- [ ] Update documentation
- [ ] Verify build and test on Linux

## 8. Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Version functions not linked in embed build | Build failure | Ensure `oceanbase_static` lib includes ob_version.o |
| `PACKAGE_VERSION` not defined | Compile error | Add explicit compile definition in CMakeLists.txt |
| printf output not captured by Python | print_version() output may be buffered | Use fflush(stdout) or Python print |

## 9. References

- [ObCommandLineParser::print_version](../src/observer/ob_command_line_parser.cpp#L384-L401)
- [ob_version.cpp.in](../src/share/ob_version.cpp.in)
- [pylibseekdb entry point](../src/observer/embed/python/ob_embed_impl.cpp)
- [pybind11 documentation](https://pybind11.readthedocs.io/)
