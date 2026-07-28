/*
 * Copyright (c) 2025 OceanBase.
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

#include <gtest/gtest.h>
#include <thread>

#define private public
#include "logservice/ob_server_log_block_mgr.h"
#undef private

#include <gtest/gtest.h>

namespace oceanbase
{
using namespace logservice;
using namespace common;
using namespace palf;
namespace unittest
{
static const char *LOG_STREAM_DIR = "log_stream";
static const char *LOG_META_DIR = "meta";
class TestServerLogBlockMgr : public ::testing::Test
{
public:
  static void SetUpTestCase();

  static void TearDownTestCase();
  TestServerLogBlockMgr()
  {}
  virtual ~TestServerLogBlockMgr()
  {}

  static int create_log_stream_dir(const char *tenant_dir);

  static int remove_log_stream_dir(const char *tenant_dir);

  int create_new_blocks_at(const palf::FileDesc &fd,
                           const palf::block_id_t start_block_id, const int64_t block_cnt)
  {
    int ret = OB_SUCCESS;
    char block_path[OB_MAX_FILE_NAME_LENGTH] = {'\0'};
    for (int i = 0; i < block_cnt && OB_SUCC(ret); i++) {
      char create_block_path[OB_MAX_FILE_NAME_LENGTH] = {'\0'};
      int64_t pos = 0;
      bool result = false;
      const palf::block_id_t block_id = start_block_id + i;
      databuff_printf(block_path, OB_MAX_FILE_NAME_LENGTH, pos, "%s/%s/%s/%lu",
                      tenant_string_, LOG_STREAM_DIR, LOG_META_DIR, block_id);
      if (OB_FAIL(block_id_to_string(block_id, create_block_path, OB_MAX_FILE_NAME_LENGTH))) {
        CLOG_LOG(ERROR, "block_id_to_string failed", K(ret));
      } else if (OB_FAIL(log_block_mgr_.create_block_at(fd, create_block_path,
                                                     ObServerLogBlockMgr::BLOCK_SIZE))) {
        CLOG_LOG(ERROR, "create_block_at failed", K(ret), K(fd),
                 K(start_block_id), K(block_id));
      } else if (OB_FAIL(FileDirectoryUtils::is_exists(block_path, result))) {
        CLOG_LOG(ERROR, "is_exists failed", K(ret), K(block_path), K(result), K(i),
                 K(start_block_id));
      } else if (false == result) {
        ret = OB_ERR_UNEXPECTED;
        CLOG_LOG(ERROR, "file not exist, unexpected error", K(ret), K(block_path),
                 K(start_block_id), K(i));
      } else {
      }
    }
    return ret;
  }

  int delete_blocks_at(const palf::FileDesc &fd,
                       const palf::block_id_t start_block_id, const int64_t block_cnt)
  {
    int ret = OB_SUCCESS;
    char block_path[OB_MAX_FILE_NAME_LENGTH] = {'\0'};
    for (int i = 0; i < block_cnt && OB_SUCC(ret); i++) {
      char remove_block_path[OB_MAX_FILE_NAME_LENGTH] = {'\0'};
      int64_t pos = 0;
      bool result = false;
      const palf::block_id_t block_id = start_block_id + i;
      databuff_printf(block_path, OB_MAX_FILE_NAME_LENGTH, pos, "%s/%s/%s/%lu",
                      tenant_string_, LOG_STREAM_DIR, LOG_META_DIR, block_id);
      if (OB_FAIL(block_id_to_string(block_id, remove_block_path, OB_MAX_FILE_NAME_LENGTH))) {
        CLOG_LOG(ERROR, "block_id_to_string failed", K(ret));
      } else if (OB_FAIL(log_block_mgr_.remove_block_at(fd, remove_block_path))) {
        CLOG_LOG(ERROR, "delete_new_block_at failed", K(ret), K(fd),
                 K(start_block_id), K(i));
      } else if (OB_FAIL(FileDirectoryUtils::is_exists(block_path, result))) {
        CLOG_LOG(ERROR, "is_exists failed", K(ret), K(block_path), K(result), K(i),
                 K(start_block_id));
      } else if (true == result) {
        ret = OB_ERR_UNEXPECTED;
        CLOG_LOG(ERROR, "file exist, unexpected error", K(ret), K(block_path),
                 K(start_block_id), K(start_block_id), K(i));
      } else {
      }
    }
    return ret;
  }

public:
  virtual void SetUp();
  virtual void TearDown();
  static const char *log_disk_base_path_;
  static const char *tenant_string_;
  static int log_stream_fd_;
  ObServerLogBlockMgr log_block_mgr_;
};

const char *TestServerLogBlockMgr::log_disk_base_path_ = "clog_disk/clog";
const char *TestServerLogBlockMgr::tenant_string_ = "clog_disk/clog/sys";
int TestServerLogBlockMgr::log_stream_fd_ = -1;

void TestServerLogBlockMgr::SetUpTestCase()
{
  bool result = false;
  int ret = OB_SUCCESS;
  if (OB_FAIL(FileDirectoryUtils::create_directory(tenant_string_))) {
    CLOG_LOG(ERROR, "FileDirectoryUtils create_directory failed", K(ret),
             K(tenant_string_));
  } else if (OB_FAIL(create_log_stream_dir(tenant_string_))) {
    CLOG_LOG(ERROR, "create log stream directory failed", K(ret));
  } else {
    CLOG_LOG(INFO, "SetUpTestSuite success", K(log_disk_base_path_));
  }
}

void TestServerLogBlockMgr::TearDownTestCase()
{
  remove_log_stream_dir(tenant_string_);
  system("rm -rf clog_disk");
}

int TestServerLogBlockMgr::create_log_stream_dir(const char *tenant_dir)
{
  int ret = OB_SUCCESS;
  char ls_path[OB_MAX_FILE_NAME_LENGTH] = {'\0'};
  int fd = -1;
  snprintf(ls_path, OB_MAX_FILE_NAME_LENGTH, "%s/%s", tenant_dir, LOG_STREAM_DIR);
  if (-1 == ::mkdir(ls_path, ObServerLogBlockMgr::CREATE_DIR_MODE)) {
    ret = palf::convert_sys_errno();
    CLOG_LOG(ERROR, "::mkdir failed", K(ret));
  } else if (FALSE_IT(snprintf(ls_path, OB_MAX_FILE_NAME_LENGTH, "%s/%s/%s", tenant_dir,
                               LOG_STREAM_DIR, LOG_META_DIR))
             || -1 == ::mkdir(ls_path, ObServerLogBlockMgr::CREATE_DIR_MODE)) {
    ret = palf::convert_sys_errno();
    CLOG_LOG(ERROR, "::mkdir failed", K(ret));
  } else if (-1 == (fd = ::open(ls_path, ObServerLogBlockMgr::OPEN_DIR_FLAG))) {
    ret = palf::convert_sys_errno();
    CLOG_LOG(ERROR, "::open failed", K(ret));
  } else {
    log_stream_fd_ = fd;
  }
  return ret;
}

int TestServerLogBlockMgr::remove_log_stream_dir(const char *tenant_dir)
{
  int ret = OB_SUCCESS;
  char stream_path[OB_MAX_FILE_NAME_LENGTH] = {'\0'};
  snprintf(stream_path, OB_MAX_FILE_NAME_LENGTH, "%s/%s", tenant_dir, LOG_STREAM_DIR);
  if (-1 == FileDirectoryUtils::delete_directory_rec(stream_path)) {
    ret = palf::convert_sys_errno();
    CLOG_LOG(ERROR, "::rmdir failed", K(ret), K(stream_path));
  } else if (-1 == ::close(log_stream_fd_)) {
    ret = palf::convert_sys_errno();
    CLOG_LOG(ERROR, "::close failed", K(ret), K(stream_path));
  }
  return ret;
}

void TestServerLogBlockMgr::SetUp()
{
  ASSERT_EQ(OB_SUCCESS, log_block_mgr_.init(log_disk_base_path_));
  log_block_mgr_.get_runtime_log_disk_size_func_ = [](int64_t &out) -> int
  {
    out = 0;
    return OB_SUCCESS;
  };
}

void TestServerLogBlockMgr::TearDown()
{
  log_block_mgr_.destroy();
}

using namespace palf;
TEST_F(TestServerLogBlockMgr, basic_func)
{
  EXPECT_EQ(OB_SUCCESS, create_new_blocks_at(log_stream_fd_, 0, 10));
  int64_t in_use_size_byte;
  EXPECT_EQ(OB_SUCCESS, log_block_mgr_.get_disk_usage(in_use_size_byte));
  EXPECT_EQ(10*ObServerLogBlockMgr::BLOCK_SIZE, in_use_size_byte);
  EXPECT_EQ(OB_SUCCESS, delete_blocks_at(log_stream_fd_, 0, 10));
  EXPECT_EQ(OB_SUCCESS, log_block_mgr_.get_disk_usage(in_use_size_byte));
  EXPECT_EQ(0, in_use_size_byte);
}

TEST_F(TestServerLogBlockMgr, restart_for_empty_log_disk)
{
  log_block_mgr_.destroy();
  int64_t in_use_size_byte;
  EXPECT_EQ(OB_SUCCESS, log_block_mgr_.init(log_disk_base_path_));
  EXPECT_EQ(OB_SUCCESS, log_block_mgr_.get_disk_usage(in_use_size_byte));
  EXPECT_EQ(0, in_use_size_byte);
}

TEST_F(TestServerLogBlockMgr, allocate_blocks_in_log_stream)
{
  EXPECT_EQ(OB_SUCCESS, create_new_blocks_at(log_stream_fd_, 0, 10));
  EXPECT_EQ(OB_SUCCESS, delete_blocks_at(log_stream_fd_, 0, 3));
}

TEST_F(TestServerLogBlockMgr, restart_for_non_empty_log_disk)
{
  log_block_mgr_.destroy();
  EXPECT_EQ(OB_SUCCESS, log_block_mgr_.init(log_disk_base_path_));
  EXPECT_EQ(OB_SUCCESS, delete_blocks_at(log_stream_fd_, 3, 7));
}

TEST_F(TestServerLogBlockMgr, unexpected_root_directory_and_tmp_file)
{
  system("mkdir clog_disk/clog/tenant_0111");
  system("mkdir clog_disk/clog/tenant_0111/log");
  system("touch clog_disk/clog/tenant_0111/log/0");
  system("touch clog_disk/clog/tenant_0111/log/1");
  system("touch clog_disk/clog/sys/log_stream/meta/10000.tmp");
#ifdef __APPLE__
  // macOS doesn't have fallocate, use dd instead
  system("dd if=/dev/zero of=clog_disk/clog/sys/log_stream/meta/10000.tmp bs=67108863 count=1 2>/dev/null");
#else
  system("fallocate -l 67108863 clog_disk/clog/sys/log_stream/meta/10000.tmp ");
#endif
  log_block_mgr_.destroy();
  bool result = false;
  EXPECT_EQ(OB_ERR_UNEXPECTED, log_block_mgr_.init(log_disk_base_path_));
  EXPECT_EQ(OB_SUCCESS, FileDirectoryUtils::is_exists("clog_disk/clog/sys/log_stream/meta/10000.tmp",result));
  EXPECT_EQ(false, result);
  system("rm -rf clog_disk/clog/tenant_0111");
  EXPECT_EQ(OB_SUCCESS, log_block_mgr_.init(log_disk_base_path_));
}

TEST_F(TestServerLogBlockMgr, check_dir_is_empty)
{
  system("mkdir clog_disk/test");
  bool result = false;
  EXPECT_EQ(OB_SUCCESS, ObServerLogBlockMgr::check_clog_directory_is_empty("clog_disk/test", result));
  EXPECT_EQ(true, result);
  system("mkdir clog_disk/test/sys");
  EXPECT_EQ(OB_SUCCESS, ObServerLogBlockMgr::check_clog_directory_is_empty("clog_disk/test", result));
  EXPECT_EQ(false, result);
}

class DummyBlockPool : public palf::ILogBlockPool {
public:
  virtual int create_block_at(const palf::FileDesc &dir_fd,
                              const char *block_path,
                              const int64_t block_size)
  {
    if (-1 == ::openat(dir_fd, block_path, palf::LOG_WRITE_FLAG | O_CREAT, 0644)) {
      return OB_IO_ERROR;
    }
    return OB_SUCCESS;
  }
  virtual int remove_block_at(const palf::FileDesc &dir_fd,
                              const char *block_path)
  {
    if (-1 == ::unlinkat(dir_fd, block_path, 0)) {
      return OB_IO_ERROR;
    }
    return OB_SUCCESS;
  }
};

TEST_F(TestServerLogBlockMgr, basic_func_test)
{
  const char *test_path = "clog_disk/basic_func_test";
  const char *file_path_obs= "1.tmp";
  const char *file_path = "clog_disk/basic_func_test/1.tmp";
  char cmd_mkdir[OB_MAX_FILE_NAME_LENGTH] = {'\0'};
  char cmd_touch[OB_MAX_FILE_NAME_LENGTH] = {'\0'};
  char cmd_alloc_file[OB_MAX_FILE_NAME_LENGTH] = {'\0'};
  snprintf(cmd_mkdir, OB_MAX_FILE_NAME_LENGTH, "mkdir %s", test_path);
  snprintf(cmd_touch, OB_MAX_FILE_NAME_LENGTH, "touch %s", file_path);
#ifdef __APPLE__
  // macOS doesn't have fallocate, use dd instead
  snprintf(cmd_alloc_file, OB_MAX_FILE_NAME_LENGTH, "dd if=/dev/zero of=%s bs=%lu count=1 2>/dev/null", file_path, PALF_PHY_BLOCK_SIZE);
#else
  snprintf(cmd_alloc_file, OB_MAX_FILE_NAME_LENGTH, "fallocate -l %lu %s", PALF_PHY_BLOCK_SIZE, file_path);
#endif
  system(cmd_mkdir);
  system(cmd_touch);
  int dir_fd = ::open(test_path, O_DIRECTORY | O_RDONLY);
  bool result = false;
  EXPECT_EQ(OB_SUCCESS, is_block_used_for_palf(dir_fd, file_path_obs, result));
  EXPECT_EQ(false, result);
  system(cmd_alloc_file);
  EXPECT_EQ(OB_SUCCESS, is_block_used_for_palf(dir_fd, file_path_obs, result));
  EXPECT_EQ(true, result);
  DummyBlockPool block_pool;
  EXPECT_EQ(OB_SUCCESS, remove_tmp_file_or_directory_at(test_path, &block_pool));
  EXPECT_EQ(OB_SUCCESS, FileDirectoryUtils::is_empty_directory(test_path, result));
  EXPECT_EQ(true, result);
  EXPECT_EQ(OB_SUCCESS, remove_directory_rec("clog_disk", &block_pool));
  EXPECT_EQ(OB_SUCCESS, FileDirectoryUtils::is_exists("clog_disk", result));
  EXPECT_EQ(false, result);
}

} // namespace unittest
} // namespace oceanbase

int main(int argc, char **argv)
{
  system("rm -f test_server_log_block_mgr.log");
  system("rm -rf clog_disk");
  system("rm -rf clog_disk/clog");
  system("mkdir clog_disk");
  system("mkdir clog_disk/clog");
  OB_LOGGER.set_file_name("test_server_log_block_mgr.log", true);
  OB_LOGGER.set_log_level("INFO");
  PALF_LOG(INFO, "begin unittest::test_server_log_block_mgr");
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
