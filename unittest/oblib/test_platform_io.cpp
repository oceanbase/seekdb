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
#include <sys/stat.h>

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

TEST_F(TestPlatformIo, DirectoryMetadataRoundTrip)
{
  // Non-ASCII directory names must survive create/scan/rename/remove on the
  // platform layer on every platform.
  const std::string dir = path_of("seekdb_\xE7\x9B\xAE\xE5\xBD\x95\xE6\xB5\x8B\xE8\xAF\x95");
  ASSERT_EQ(0, ob_mkdir_utf8(dir.c_str(), 0755)) << "mkdir failed errno=" << errno;

  ObPlatformFileStat dir_stat;
  ASSERT_EQ(0, ob_stat_utf8(dir.c_str(), dir_stat)) << "stat dir failed errno=" << errno;
  EXPECT_TRUE(ob_stat_is_dir(dir_stat.mode));

  const std::string file = dir + "/plain.bin";
  const int fd = ob_open_utf8(file.c_str(), O_CREAT | O_TRUNC | O_RDWR, 0644);
  ASSERT_GE(fd, 0) << "create in dir failed errno=" << errno;
  const char payload[] = "dir-entry";
  ASSERT_EQ(9, ob_pwrite_fd(fd, payload, 9, 0));
  ASSERT_EQ(0, close_fd(fd));

  ObPlatformFileStat file_stat;
  ASSERT_EQ(0, ob_stat_utf8(file.c_str(), file_stat)) << "stat file failed errno=" << errno;
  EXPECT_FALSE(S_ISDIR(file_stat.mode));
  EXPECT_EQ(9, file_stat.size);

  // Directory scan reports the entry names.
  ObPlatformDir *handle = ob_opendir_utf8(dir.c_str());
  ASSERT_NE(nullptr, handle) << "opendir failed errno=" << errno;
  bool saw_file = false;
  char entry_name[256] = {0};
  int read_ret = 0;
  while (1 == (read_ret = ob_readdir_utf8(handle, entry_name, sizeof(entry_name)))) {
    if (0 == ::strcmp(entry_name, "plain.bin")) {
      saw_file = true;
    }
  }
  EXPECT_EQ(0, read_ret) << "readdir failed errno=" << errno;
  EXPECT_TRUE(saw_file);
  EXPECT_EQ(0, ob_closedir_utf8(handle));

  // rename + unlink + rmdir on UTF-8 names.
  const std::string renamed = dir + "/renamed_\xC3\xA9.bin";
  EXPECT_EQ(0, ob_rename_utf8(file.c_str(), renamed.c_str())) << "rename failed errno=" << errno;
  EXPECT_EQ(-1, ob_stat_utf8(file.c_str(), file_stat));
  EXPECT_EQ(0, ob_stat_utf8(renamed.c_str(), file_stat));
  EXPECT_EQ(0, ob_unlink_utf8(renamed.c_str())) << "unlink failed errno=" << errno;
  EXPECT_EQ(0, ob_rmdir_utf8(dir.c_str())) << "rmdir failed errno=" << errno;
}

TEST_F(TestPlatformIo, AccessAndAbsolutePath)
{
  const std::string file = path_of("access.bin");
  const int fd = ob_open_utf8(file.c_str(), O_CREAT | O_TRUNC | O_RDWR, 0644);
  ASSERT_GE(fd, 0);
  ASSERT_EQ(0, close_fd(fd));

  EXPECT_EQ(0, ob_access_utf8(file.c_str(), ObPathPermission::EXISTS));
  EXPECT_EQ(0, ob_access_utf8(file.c_str(), ObPathPermission::READ));
  EXPECT_EQ(0, ob_access_utf8(file.c_str(), ObPathPermission::WRITE));
  errno = 0;
  EXPECT_EQ(-1, ob_access_utf8(path_of("missing.bin").c_str(), ObPathPermission::EXISTS));
  EXPECT_EQ(ENOENT, errno);

  char absolute[4096] = {0};
  EXPECT_EQ(0, ob_absolute_path_utf8(file.c_str(), absolute, sizeof(absolute)));
  EXPECT_TRUE(absolute[0] != '\0');
#ifdef _WIN32
  EXPECT_TRUE(ob_is_absolute_path_utf8(absolute));
#else
  EXPECT_TRUE(ob_is_absolute_path_utf8(absolute));
  EXPECT_FALSE(ob_is_absolute_path_utf8("relative/path"));
#endif

  int64_t total_space = 0;
  int64_t free_space = 0;
  EXPECT_EQ(0, ob_get_disk_space_utf8(root_.c_str(), total_space, free_space));
  EXPECT_GT(total_space, 0);
  EXPECT_GT(free_space, 0);

  EXPECT_EQ(0, ob_fsync_dir_utf8(root_.c_str())) << "fsync dir failed errno=" << errno;
  EXPECT_EQ(0, ob_unlink_utf8(file.c_str()));
}

TEST_F(TestPlatformIo, DirectoryEntryKindRoundTrip)
{
  const std::string sub_dir = path_of("entry_sub_dir");
  const std::string file_path = path_of("entry_file");
  ASSERT_EQ(0, make_dir(sub_dir)) << "errno=" << errno;
  const int fd = ob_open_utf8(file_path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
  ASSERT_GE(fd, 0) << "errno=" << errno;
  ASSERT_EQ(0, close_fd(fd));

  ObPlatformDir *dir = ob_opendir_utf8(root_.c_str());
  ASSERT_NE(nullptr, dir) << "errno=" << errno;
  int seen = 0;
  int read_ret = 0;
  ObPlatformDirEntry entry;
  while (1 == (read_ret = ob_readdir_entry_utf8(dir, entry))) {
    const bool is_sub_dir = 0 == ::strcmp(entry.name, "entry_sub_dir");
    const bool is_file = 0 == ::strcmp(entry.name, "entry_file");
    if (is_sub_dir || is_file) {
      // Filesystems may report DT_UNKNOWN (macOS APFS does); callers then stat
      // the entry, so resolve the expected kind the same way here.
      ObPlatformDirEntryType expected = entry.type;
      if (OB_DIR_ENTRY_UNKNOWN == expected) {
        ObPlatformFileStat stat;
        const std::string full_path = path_of(entry.name);
        ASSERT_EQ(0, ob_stat_utf8(full_path.c_str(), stat)) << "errno=" << errno;
        expected = ob_stat_is_dir(stat.mode) ? OB_DIR_ENTRY_DIR : OB_DIR_ENTRY_FILE;
      }
      if (is_sub_dir) {
        EXPECT_EQ(OB_DIR_ENTRY_DIR, expected);
      } else {
        EXPECT_EQ(OB_DIR_ENTRY_FILE, expected);
      }
      ++seen;
    }
  }
  EXPECT_EQ(0, read_ret) << "errno=" << errno;
  EXPECT_EQ(2, seen);
  EXPECT_EQ(0, ob_closedir_utf8(dir));

  EXPECT_TRUE(remove_file(file_path));
  EXPECT_EQ(0, remove_dir(sub_dir));
}

TEST_F(TestPlatformIo, ReaddirNameOnlyCompatibility)
{
  const std::string file_path = path_of("readdir_file");
  const int fd = ob_open_utf8(file_path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
  ASSERT_GE(fd, 0) << "errno=" << errno;
  ASSERT_EQ(0, close_fd(fd));

  ObPlatformDir *dir = ob_opendir_utf8(root_.c_str());
  ASSERT_NE(nullptr, dir) << "errno=" << errno;
  bool found = false;
  char name[OB_PLATFORM_DIR_ENTRY_NAME_SIZE];
  int read_ret = 0;
  while (1 == (read_ret = ob_readdir_utf8(dir, name, sizeof(name)))) {
    if (0 == ::strcmp(name, "readdir_file")) {
      found = true;
    }
  }
  EXPECT_EQ(0, read_ret) << "errno=" << errno;
  EXPECT_TRUE(found);
  EXPECT_EQ(0, ob_closedir_utf8(dir));
  EXPECT_TRUE(remove_file(file_path));
}

TEST_F(TestPlatformIo, ExecutablePathAndAlias)
{
  char exe_path[4096];
  ASSERT_EQ(0, ob_get_executable_path_utf8(exe_path, sizeof(exe_path))) << "errno=" << errno;
  ASSERT_LT(0, static_cast<int64_t>(::strlen(exe_path)));
  ObPlatformFileStat exe_stat;
  ASSERT_EQ(0, ob_stat_utf8(exe_path, exe_stat)) << "errno=" << errno;
  EXPECT_GT(exe_stat.size, 0);

  const std::string alias = path_of("seekdb_alias");
  ASSERT_EQ(0, ob_create_executable_alias_utf8(alias.c_str())) << "errno=" << errno;
#ifdef _WIN32
  const std::string alias_file = alias + ".exe";
  ObPlatformFileStat alias_stat;
  ASSERT_EQ(0, ob_stat_utf8(alias_file.c_str(), alias_stat)) << "errno=" << errno;
  EXPECT_EQ(exe_stat.size, alias_stat.size);
  EXPECT_TRUE(remove_file(alias_file));
#else
  ObPlatformFileStat link_stat;
  ASSERT_EQ(0, ob_stat_utf8(alias.c_str(), link_stat, false)) << "errno=" << errno;
  EXPECT_TRUE(ob_stat_is_link(link_stat.mode));
  EXPECT_EQ(0, ::unlink(alias.c_str()));
#endif
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
