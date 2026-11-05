/*
 * Copyright (c) 2026 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <string>

#include <gtest/gtest.h>

#include "lib/utility/ob_platform_io.h"

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <process.h>
#include <windows.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace oceanbase
{
namespace common
{
namespace
{

const int64_t MAX_PATH_FOR_TEST = 260;

std::string temp_root()
{
  std::string root = ::testing::TempDir();
  while (!root.empty() && ('/' == root[root.size() - 1] || '\\' == root[root.size() - 1])) {
    root.erase(root.size() - 1);
  }
  return root;
}

int process_id()
{
#ifdef _WIN32
  return static_cast<int>(::_getpid());
#else
  return static_cast<int>(::getpid());
#endif
}

int close_fd(int fd)
{
#ifdef _WIN32
  return ::_close(fd);
#else
  return ::close(fd);
#endif
}

int make_dir(const std::string &path)
{
#ifdef _WIN32
  return ::_mkdir(path.c_str());
#else
  return ::mkdir(path.c_str(), 0755);
#endif
}

int remove_dir(const std::string &path)
{
#ifdef _WIN32
  return ::_rmdir(path.c_str());
#else
  return ::rmdir(path.c_str());
#endif
}

// Removes a test file, including names that are longer than MAX_PATH on
// Windows where the ANSI CRT call would fail. Test paths are ASCII on purpose.
bool remove_file(const std::string &path)
{
#ifdef _WIN32
  std::wstring wide;
  const int wide_len = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.c_str(), -1,
                                             NULL, 0);
  if (wide_len <= 1) {
    return false;
  }
  wide.resize(static_cast<size_t>(wide_len - 1));
  if (0 == ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.c_str(), -1,
                                 &wide[0], wide_len)) {
    return false;
  }
  for (size_t i = 0; i < wide.size(); ++i) {
    if (L'/' == wide[i]) {
      wide[i] = L'\\';
    }
  }
  const bool has_prefix = wide.size() >= 4 && L'\\' == wide[0] && L'\\' == wide[1]
                          && (L'?' == wide[2] || L'.' == wide[2]) && L'\\' == wide[3];
  if (!has_prefix && wide.size() + 1 > static_cast<size_t>(MAX_PATH_FOR_TEST)) {
    wide = L"\\\\?\\" + wide;
  }
  return FALSE != ::DeleteFileW(wide.c_str());
#else
  return 0 == ::remove(path.c_str());
#endif
}

class TestPlatformIo : public ::testing::Test
{
protected:
  void SetUp() override
  {
    root_ = temp_root() + "/seekdb_platform_io_" + std::to_string(process_id());
    const int mkdir_ret = make_dir(root_);
    if (0 != mkdir_ret && EEXIST != errno) {
      ADD_FAILURE() << "failed to create test dir " << root_ << " errno=" << errno;
    }
  }

  void TearDown() override
  {
    remove_dir(root_);
  }

  std::string path_of(const std::string &name) const
  {
    return root_ + "/" + name;
  }

  std::string root_;
};

TEST_F(TestPlatformIo, OpenPreadPwriteRoundTrip)
{
  // Non-ASCII UTF-8 name: on Windows this only works when the name is opened
  // through the wide-character path, i.e. not through the ANSI code page.
  const std::string file = path_of("seekdb_\xE4\xB8\xAD\xE6\x96\x87_\xC3\xA9.bin");
  int fd = ob_open_utf8(file.c_str(), O_CREAT | O_TRUNC | O_RDWR, 0644);
  ASSERT_GE(fd, 0) << "open failed errno=" << errno;

  const char payload[] = "0123456789";
  ASSERT_EQ(10, ob_pwrite_fd(fd, payload, 10, 0));
  ASSERT_EQ(0, close_fd(fd));

  fd = ob_open_utf8(file.c_str(), O_RDONLY, 0);
  ASSERT_GE(fd, 0) << "reopen failed errno=" << errno;

  char buffer[16] = {0};
  EXPECT_EQ(10, ob_pread_fd(fd, buffer, 10, 0));
  EXPECT_EQ(0, ::memcmp(buffer, payload, 10));

  ::memset(buffer, 0, sizeof(buffer));
  EXPECT_EQ(2, ob_pread_fd(fd, buffer, 2, 3));
  EXPECT_EQ(0, ::memcmp(buffer, "34", 2));

  // A read that starts before EOF but crosses it returns the available prefix.
  ::memset(buffer, 0, sizeof(buffer));
  EXPECT_EQ(3, ob_pread_fd(fd, buffer, 5, 7));
  EXPECT_EQ(0, ::memcmp(buffer, "789", 3));

  // Reads at and beyond EOF report a successful zero-byte transfer.
  EXPECT_EQ(0, ob_pread_fd(fd, buffer, 1, 10));
  EXPECT_EQ(0, ob_pread_fd(fd, buffer, 1, 11));
  EXPECT_EQ(0, close_fd(fd));

  EXPECT_TRUE(remove_file(file));
}

TEST_F(TestPlatformIo, OpenExistingFileWithExclusiveFlag)
{
  const std::string file = path_of("exclusive.bin");
  int fd = ob_open_utf8(file.c_str(), O_CREAT | O_TRUNC | O_RDWR, 0644);
  ASSERT_GE(fd, 0) << "open failed errno=" << errno;
  ASSERT_EQ(0, close_fd(fd));

  errno = 0;
  EXPECT_EQ(-1, ob_open_utf8(file.c_str(), O_CREAT | O_EXCL | O_RDWR, 0644));
  EXPECT_EQ(EEXIST, errno);
  EXPECT_TRUE(remove_file(file));
}

TEST_F(TestPlatformIo, OpenMissingFile)
{
  const std::string file = path_of("does_not_exist.bin");
  errno = 0;
  EXPECT_EQ(-1, ob_open_utf8(file.c_str(), O_RDONLY, 0));
  EXPECT_EQ(ENOENT, errno);
}

TEST_F(TestPlatformIo, PositionedIoOnInvalidFd)
{
  char buffer[8] = {0};
  errno = 0;
  EXPECT_EQ(-1, ob_pread_fd(-1, buffer, sizeof(buffer), 0));
  EXPECT_EQ(EBADF, errno);

  errno = 0;
  EXPECT_EQ(-1, ob_pwrite_fd(-1, buffer, sizeof(buffer), 0));
  EXPECT_EQ(EBADF, errno);
}

TEST_F(TestPlatformIo, OpenAndReadFileLongerThanMaxPath)
{
  // The OS temp directory is short enough that two long intermediate directory
  // components can still be created with plain platform calls; only the final
  // file name pushes the full path beyond MAX_PATH, which is what the platform
  // layer has to handle.
  const std::string first_dir = path_of(std::string(100, 'a'));
  const std::string second_dir = first_dir + "/" + std::string(100, 'b');
  ASSERT_EQ(0, make_dir(first_dir)) << "mkdir failed errno=" << errno;
  ASSERT_EQ(0, make_dir(second_dir)) << "mkdir failed errno=" << errno;

  const std::string file = second_dir + "/" + std::string(60, 'c') + ".bin";
  ASSERT_GT(file.size(), static_cast<size_t>(MAX_PATH_FOR_TEST));

  int fd = ob_open_utf8(file.c_str(), O_CREAT | O_TRUNC | O_RDWR, 0644);
  ASSERT_GE(fd, 0) << "long path open failed errno=" << errno;
  const char payload[] = "long-path";
  EXPECT_EQ(9, ob_pwrite_fd(fd, payload, 9, 0));
  ASSERT_EQ(0, close_fd(fd));

  fd = ob_open_utf8(file.c_str(), O_RDONLY, 0);
  ASSERT_GE(fd, 0) << "long path reopen failed errno=" << errno;
  char buffer[16] = {0};
  EXPECT_EQ(9, ob_pread_fd(fd, buffer, sizeof(buffer), 0));
  EXPECT_EQ(0, ::memcmp(buffer, payload, 9));
  EXPECT_EQ(0, close_fd(fd));

  EXPECT_TRUE(remove_file(file));
  EXPECT_EQ(0, remove_dir(second_dir));
  EXPECT_EQ(0, remove_dir(first_dir));
}

TEST_F(TestPlatformIo, RejectEmptyName)
{
  errno = 0;
  EXPECT_EQ(-1, ob_open_utf8("", O_RDONLY, 0));
  EXPECT_EQ(EINVAL, errno);
}

#ifdef _WIN32
TEST_F(TestPlatformIo, Win32ErrorMapping)
{
  EXPECT_EQ(ENOENT, ob_win32_error_to_errno(ERROR_FILE_NOT_FOUND));
  EXPECT_EQ(ENOENT, ob_win32_error_to_errno(ERROR_PATH_NOT_FOUND));
  EXPECT_EQ(EACCES, ob_win32_error_to_errno(ERROR_ACCESS_DENIED));
  EXPECT_EQ(EEXIST, ob_win32_error_to_errno(ERROR_ALREADY_EXISTS));
  EXPECT_EQ(ENOSPC, ob_win32_error_to_errno(ERROR_DISK_FULL));
  EXPECT_EQ(EBADF, ob_win32_error_to_errno(ERROR_INVALID_HANDLE));
  EXPECT_EQ(ENOMEM, ob_win32_error_to_errno(ERROR_NOT_ENOUGH_MEMORY));
  EXPECT_EQ(EINTR, ob_win32_error_to_errno(ERROR_OPERATION_ABORTED));
  EXPECT_EQ(EINVAL, ob_win32_error_to_errno(ERROR_INVALID_PARAMETER));
  EXPECT_EQ(EIO, ob_win32_error_to_errno(ERROR_CRC));
}
#endif

} // namespace
} // namespace common
} // namespace oceanbase

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
