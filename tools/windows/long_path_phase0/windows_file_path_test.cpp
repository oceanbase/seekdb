// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
#include "lib/file/windows_file_path.h"
#include "lib/alloc/ob_iallocator.h"
#include "lib/ob_errno.h"
#include "path_fixture.h"
#include <cstdlib>
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#include <winioctl.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace oceanbase::common;
void require(bool value, const char *message)
{
  if (!value) { throw std::runtime_error(message); }
}
class TestAllocator : public ObIAllocator
{
public:
  int fail_after = -1;
  int live = 0;
  void *alloc(int64_t size) override
  {
    if (fail_after == 0) { return nullptr; }
    if (fail_after > 0) { --fail_after; }
    void *p = std::malloc(size);
    if (p != nullptr) { ++live; }
    return p;
  }
  void *alloc(int64_t size, const ObMemAttr &) override { return alloc(size); }
  void free(void *p) override { if (p != nullptr) { --live; std::free(p); } }
};
std::string encode(const std::wstring &value)
{
  const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
      value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
  require(size > 0, "encode size");
  std::string result(size, '\0');
  require(WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
      static_cast<int>(value.size()), result.data(), size, nullptr, nullptr) == size, "encode");
  return result;
}
std::wstring directory(const std::wstring &root, int64_t size, bool unicode)
{
  const auto value = seekdb_phase0::directory_at_length(
      std::u16string(root.begin(), root.end()), size, unicode);
  return std::wstring(value.begin(), value.end());
}
void check(WindowsFilePath &path, int ret, const char *stage)
{
  if (ret != OB_SUCCESS) {
    throw std::runtime_error(std::string(stage) + " ret=" + std::to_string(ret) +
        " win32=" + std::to_string(path.win32_error()));
  }
}

void write_fixture(TestAllocator &allocator, const std::wstring &file)
{
  WindowsFilePath path(allocator);
  check(path, path.assign(encode(file).c_str()), "fixture path");
  int fd = -1;
  check(path, path.open(_O_CREAT | _O_EXCL | _O_RDWR | _O_NOINHERIT, _S_IREAD | _S_IWRITE, fd), "fixture open");
  const int written = _write(fd, "saved", 5);
  const int closed = _close(fd);
  require(written == 5 && closed == 0, "fixture write/close");
}
void file_operations(const std::wstring &root)
{
  TestAllocator allocator;
  DWORD handles_before = 0;
  require(GetProcessHandleCount(GetCurrentProcess(), &handles_before), "initial handles");
  {
    WindowsFilePath path(allocator), parent(allocator), child(allocator);
    const auto life = root + L"\\life";
    for (bool unicode : {false, true}) {
      for (int64_t length : {100, 259, 260, 261, 280, 600, 1200, 2048, 4096}) {
        const auto prefix = life + L"\\" + std::to_wstring(length) + (unicode ? L"u" : L"a");
        const auto file = directory(prefix, length, unicode);
        const auto dir = file.substr(0, file.find_last_of(L'\\'));
        check(parent, parent.assign(encode(dir).c_str()), "parent normalize");
        check(parent, parent.create_directory(true), "parent create");
        HANDLE directory_handle = CreateFileW(parent.wide(), FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        require(directory_handle != INVALID_HANDLE_VALUE, "open directory handle");
        const std::string relative = encode(file.substr(file.find_last_of(L'\\') + 1));
        for (const char *invalid : {"", "C:\\absolute", "\\rooted", "bad\xff"}) {
          require(child.assign_at(directory_handle, invalid) == OB_INVALID_ARGUMENT &&
              child.wide() == nullptr, "invalid relative path must clear output");
        }
        if (length == 4096) {
          require(child.assign_at(directory_handle, (relative + "x").c_str()) == OB_SIZE_OVERFLOW &&
              child.wide() == nullptr, "directory child length overflow");
        }
        const int resolved = child.assign_at(directory_handle, relative.c_str());
        const BOOL directory_closed = CloseHandle(directory_handle);
        check(child, resolved, "resolve directory handle child");
        require(directory_closed && encode(file) == child.utf8(), "directory handle resolution");
        require(child.assign_at(INVALID_HANDLE_VALUE, relative.c_str()) == OB_IO_ERROR &&
            child.win32_error() == ERROR_INVALID_HANDLE && child.wide() == nullptr,
            "invalid handle must clear path");
        std::cout << "NATIVE_DIRECTORY_HANDLE_PASS units=" << length << " unicode=" << unicode << '\n';
        check(path, path.assign(encode(file).c_str()), "file normalize");
        int64_t total = 0, available = 0;
        check(parent, parent.get_disk_space(total, available), "disk space");
        require(total > 0 && available > 0 && available <= total, "disk capacity");
        int fd = -1;
        check(path, path.open(_O_CREAT | _O_EXCL | _O_RDWR | _O_NOINHERIT, _S_IREAD | _S_IWRITE, fd), "file open");
        require(child.assign_at(reinterpret_cast<HANDLE>(_get_osfhandle(fd)), "child") == OB_IO_ERROR &&
            child.win32_error() == ERROR_DIRECTORY && child.wide() == nullptr,
            "regular file must not act as directory");
        const unsigned char bytes[] = {0, 10, 13, 26, 255};
        unsigned char readback[sizeof(bytes)] = {};
        require(_write(fd, bytes, sizeof(bytes)) == sizeof(bytes), "binary write");
        require(_commit(fd) == 0 && _lseeki64(fd, 0, SEEK_SET) == 0, "commit/seek");
        require(_read(fd, readback, sizeof(readback)) == sizeof(readback) &&
            memcmp(bytes, readback, sizeof(bytes)) == 0, "binary readback");
        WIN32_FILE_ATTRIBUTE_DATA info = {};
        check(path, path.get_info(info), "file info");
        require(info.nFileSizeHigh == 0 && info.nFileSizeLow == sizeof(bytes), "file size");
        bool allowed = false;
        check(path, path.check_mode(4, allowed), "read access");
        require(allowed, "read access false");
        HANDLE independent = CreateFileW(path.wide(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, 0, nullptr);
        require(independent != INVALID_HANDLE_VALUE, "independent open");
        BY_HANDLE_FILE_INFORMATION a = {}, b = {};
        const BOOL identity_ok = GetFileInformationByHandle(reinterpret_cast<HANDLE>(_get_osfhandle(fd)), &a) &&
            GetFileInformationByHandle(independent, &b);
        CloseHandle(independent);
        require(identity_ok && a.dwVolumeSerialNumber == b.dwVolumeSerialNumber &&
            a.nFileIndexHigh == b.nFileIndexHigh && a.nFileIndexLow == b.nFileIndexLow, "file identity");
        require(path.delete_file() == OB_IO_ERROR && path.win32_error() == ERROR_SHARING_VIOLATION,
            "open file deletion classified as missing");
        require(_close(fd) == 0, "close fd");
        {
          WindowsDirectoryIterator iterator(allocator);
          require(iterator.open(path) == OB_FILE_NOT_EXIST, "enumerated a file");
          check(parent, iterator.open(parent), "enumerate parent");
          DWORD attributes = 0;
          require(iterator.next(child, attributes) == OB_SUCCESS, "enumeration child");
          require(encode(file) == child.utf8() && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0,
              "enumeration path or attributes");
          require(iterator.next(child, attributes) == OB_ITER_END, "enumeration end");
        }
        check(path, path.delete_file(), "delete closed file");
        require(path.get_info(info) == OB_FILE_NOT_EXIST && path.win32_error() == ERROR_FILE_NOT_FOUND,
            "missing file query");
        require(path.delete_file() == OB_FILE_NOT_EXIST, "missing file delete");
        {
          WindowsDirectoryIterator empty(allocator);
          check(parent, empty.open(parent), "empty open");
          DWORD attributes = 0;
          require(empty.next(child, attributes) == OB_ITER_END, "empty directory");
        }
        std::cout << "NATIVE_FILE_PASS units=" << length << " unicode=" << unicode << '\n';
      }
    }
    const auto temp = root + L"\\cleanup";
    check(parent, parent.assign(encode(temp + L"\\keep\\nested.tmp").c_str()), "temp root");
    check(parent, parent.create_directory(true), "temp create");
    write_fixture(allocator, temp + L"\\keep\\nested.tmp\\remove");
    write_fixture(allocator, temp + L"\\keep\\survive");
    write_fixture(allocator, temp + L"\\leaf.tmp");
    check(path, path.assign(encode(temp).c_str()), "cleanup root");
    check(path, path.remove_tree(true), "temporary cleanup");
    require(GetFileAttributesW((temp + L"\\keep\\survive").c_str()) != INVALID_FILE_ATTRIBUTES, "deleted survivor");
    require(GetFileAttributesW((temp + L"\\keep\\nested.tmp").c_str()) == INVALID_FILE_ATTRIBUTES, "tmp dir remains");
    require(GetFileAttributesW((temp + L"\\leaf.tmp").c_str()) == INVALID_FILE_ATTRIBUTES, "tmp file remains");
    const int retained = allocator.live;
    for (int fail = 0; fail < 15; ++fail) {
      allocator.fail_after = fail;
      const int ret = path.remove_tree(true);
      require(ret == OB_ALLOCATE_MEMORY_FAILED, "traversal allocation error");
      require(allocator.live == retained, "traversal failure leaked memory");
    }
    allocator.fail_after = -1;
    check(path, path.remove_tree(false), "cleanup tree");
    require(GetFileAttributesW(temp.c_str()) == INVALID_FILE_ATTRIBUTES, "tree remains");
    std::cout << "NATIVE_TREE_PASS allocation_failures=15\n";

    const auto sparse = root + L"\\large-sparse";
    check(path, path.assign(encode(sparse).c_str()), "sparse path");
    int fd = -1;
    check(path, path.open(_O_CREAT | _O_EXCL | _O_RDWR, _S_IREAD | _S_IWRITE, fd), "sparse open");
    DWORD returned = 0;
    require(DeviceIoControl(reinterpret_cast<HANDLE>(_get_osfhandle(fd)), FSCTL_SET_SPARSE,
        nullptr, 0, nullptr, 0, &returned, nullptr), "mark sparse");
    LARGE_INTEGER sparse_size = {};
    sparse_size.QuadPart = (1LL << 32) + 17;
    const HANDLE sparse_handle = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
    require(SetFilePointerEx(sparse_handle, sparse_size, nullptr, FILE_BEGIN), "sparse seek");
    require(SetEndOfFile(sparse_handle), "sparse resize");
    require(_close(fd) == 0, "sparse close");
    WIN32_FILE_ATTRIBUTE_DATA info = {};
    check(path, path.get_info(info), "sparse info");
    require(info.nFileSizeHigh == 1 && info.nFileSizeLow == 17, "64 bit file size");
    require(SetFileAttributesW(path.wide(), FILE_ATTRIBUTE_READONLY), "readonly fixture");
    require(path.delete_file() == OB_IO_ERROR && path.win32_error() == ERROR_ACCESS_DENIED, "readonly delete");
    require(SetFileAttributesW(path.wide(), FILE_ATTRIBUTE_NORMAL), "restore attributes");
    check(path, path.delete_file(), "sparse delete");
    std::cout << "NATIVE_SPARSE_PASS bytes=4294967313\n";

    const auto deep_root = root + L"\\deep";
    std::wstring deep = deep_root;
    while (deep.size() + 2 <= 4096) { deep += L"\\a"; }
    if (deep.size() < 4096) { deep += L"a"; }
    check(path, path.assign(encode(deep).c_str()), "deep path");
    std::cout << "NATIVE_DEEP_STAGE create_start\n";
    check(path, path.create_directory(true), "deep create");
    std::cout << "NATIVE_DEEP_STAGE create_done\n";
    check(path, path.assign(encode(deep_root).c_str()), "deep removal root");
    std::cout << "NATIVE_DEEP_STAGE remove_start\n";
    check(path, path.remove_tree(false), "iterative deep removal");
    std::cout << "NATIVE_DEEP_STAGE remove_done\n";
    require(GetFileAttributesW(deep_root.c_str()) == INVALID_FILE_ATTRIBUTES, "deep remains");
    std::cout << "NATIVE_DEEP_PASS units=4096\n";

    const auto outside = root + L"\\outside";
    const auto links = root + L"\\links";
    check(parent, parent.assign(encode(outside).c_str()), "outside");
    check(parent, parent.create_directory(true), "outside mkdir");
    write_fixture(allocator, outside + L"\\survive.tmp");
    check(path, path.assign(encode(links).c_str()), "link root");
    check(path, path.create_directory(true), "link root mkdir");
    require(CreateSymbolicLinkW((links + L"\\alias.tmp").c_str(), outside.c_str(),
        SYMBOLIC_LINK_FLAG_DIRECTORY | 2), "directory link fixture");
    check(path, path.remove_tree(false), "link tree removal");
    require(GetFileAttributesW((outside + L"\\survive.tmp").c_str()) != INVALID_FILE_ATTRIBUTES,
        "traversed outside directory link");
    check(parent, parent.remove_tree(false), "outside fixture cleanup");
    std::cout << "NATIVE_REPARSE_PASS\n";
  }
  require(allocator.live == 0, "file operations allocator leak");
  DWORD handles_after = 0;
  require(GetProcessHandleCount(GetCurrentProcess(), &handles_after), "final handles");
  require(handles_after == handles_before, "search/file handle leak");
  std::cout << "NATIVE_RESOURCE_PASS handles=" << handles_after << '\n';
}
void test(const std::wstring &root)
{
  TestAllocator allocator;
  {
    WindowsFilePath path(allocator);
    for (bool unicode : {false, true}) {
      for (int64_t length : {100, 259, 260, 261, 280, 600, 1200, 2048, 4096}) {
        const auto value = directory(root, length, unicode);
        const auto input = encode(value);
        check(path, path.assign(input.c_str()), "normalize");
        require(path.length() == length && input == path.utf8(), "length/UTF8");
        require(std::wstring(path.wide()) == L"\\\\?\\" + value, "extended spelling");
        check(path, path.create_directory(true), "recursive create");
        check(path, path.create_directory(false), "existing directory");
        const DWORD attributes = GetFileAttributesW(path.wide());
        require(attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0,
            "actual directory");
        check(path, path.assign(encode(L"\\\\?\\" + value).c_str()), "already extended");
        require(input == path.utf8(), "no duplicate prefix");
        check(path, path.assign(path.utf8()), "normalize owned input");
        require(input == path.utf8(), "owned input changed");
        std::cout << "NATIVE_DIRECTORY_PASS units=" << length << " unicode=" << unicode << '\n';
      }
    }
    auto reject = [&](const std::string &input, int expected, int64_t limit = 4096) {
      const int ret = path.assign(input.data(), input.size(), limit);
      require(ret == expected, "negative classification");
      require(path.wide() == nullptr && path.utf8() == nullptr && allocator.live == 0,
          "rejected path retains resources");
      require(path.create_directory(true) == OB_NOT_INIT, "rejected path performed mkdir");
      std::cout << "NATIVE_REJECT_PASS code=" << ret << '\n';
    };
    for (const std::string input : {"", "C:\\NUL.txt", "C:\\COM1 .txt", "C:\\data.", "C:\\data ",
        "C:\\data:stream", "C:\\a*", "\\\\server\\share\\db", "\\\\.\\pipe\\db", "\\\\?\\relative",
        "C:\\\xc0\xaf"}) { reject(input, OB_INVALID_ARGUMENT); }
    reject(std::string("C:\\ab\0cd", 8), OB_INVALID_ARGUMENT);
    reject(encode(L"C:\\LPT\u00b2.txt"), OB_INVALID_ARGUMENT);
    reject("C:\\" + std::string(256, 'x'), OB_SIZE_OVERFLOW);
    reject(encode(directory(root, 2049, true)), OB_SIZE_OVERFLOW, 2048);
    reject(encode(directory(root, 4097, true)), OB_SIZE_OVERFLOW);
    check(path, path.assign(encode(directory(root, 2048, true)).c_str(),
        encode(directory(root, 2048, true)).size(), WindowsFilePath::BASE_PATH_UNITS), "base limit");
    for (int fail = 0; fail < 3; ++fail) {
      allocator.fail_after = fail;
      reject(encode(root + L"\\allocation-failure"), OB_ALLOCATE_MEMORY_FAILED);
    }
    allocator.fail_after = -1;
    require(GetFileAttributesW((root + L"\\allocation-failure").c_str()) == INVALID_FILE_ATTRIBUTES,
        "allocation failure created directory");
    const auto file = root + L"\\collision";
    HANDLE h = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    require(h != INVALID_HANDLE_VALUE, "create collision file");
    CloseHandle(h);
    check(path, path.assign(encode(file).c_str()), "collision path");
    require(path.create_directory(true) == OB_ENTRY_EXIST, "file accepted as directory");
    require(path.win32_error() == ERROR_ALREADY_EXISTS, "collision diagnostic");
    check(path, path.assign(encode(root + L"\\no-parent\\leaf").c_str()), "missing parent path");
    require(path.create_directory(false) == OB_IO_ERROR && path.win32_error() == ERROR_PATH_NOT_FOUND,
        "missing parent diagnostic");
    check(path, path.assign(".\\tools\\..\\tools"), "relative path");
    const auto expected = encode(std::filesystem::current_path().wstring() + L"\\tools");
    require(expected == path.utf8(), "relative startup context");
    std::cout << "NATIVE_ERROR_AND_RELATIVE_PASS\n";
  }
  require(allocator.live == 0, "allocator leak");
}
} // namespace

int main(int argc, char **argv)
{
  try {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    const bool cleanup_only = argc == 3 && std::string(argv[2]) == "--cleanup";
    require(argc == 2 || cleanup_only, "requires exclusive ASCII test root");
    const std::string arg(argv[1]);
    require(arg.rfind("C:\\s\\seek533-native-path-", 0) == 0 &&
        arg.find("..") == std::string::npos && arg.find('\\', 5) == std::string::npos,
        "test root guard");
    const std::wstring root(arg.begin(), arg.end());
    if (cleanup_only) {
      TestAllocator allocator;
      WindowsFilePath path(allocator);
      check(path, path.assign(arg.c_str()), "cleanup path");
      std::cout << "NATIVE_CLEANUP_START\n";
      check(path, path.remove_tree(false), "cleanup failed fixture");
      require(!std::filesystem::exists(root), "cleanup root remains");
      std::cout << "NATIVE_CLEANUP_PASS\n";
      return 0;
    }
    require(std::filesystem::is_empty(root), "test root must be empty");
    const auto cwd = std::filesystem::current_path();
    test(root);
    file_operations(root);
    require(cwd == std::filesystem::current_path(), "cwd changed");
    TestAllocator cleanup_allocator;
    WindowsFilePath cleanup(cleanup_allocator);
    check(cleanup, cleanup.assign(encode(root).c_str()), "final cleanup path");
    check(cleanup, cleanup.remove_tree(false), "final cleanup");
    require(!std::filesystem::exists(root), "cleanup");
    std::cout << "NATIVE_PATH_COMPONENT_PASS\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
