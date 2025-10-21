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

#ifndef OCEANBASE_SENSITIVE_TEST_SHARED_STORAGE_CLEAN_RESIDUAL_DATA_H_
#define OCEANBASE_SENSITIVE_TEST_SHARED_STORAGE_CLEAN_RESIDUAL_DATA_H_

#include <gtest/gtest.h>
#include "share/backup/ob_backup_struct.h"              // ObBackupStorageInfo
#include "share/backup/ob_backup_io_adapter.h"          // ObBackupIoAdapter
#include "share/backup/ob_backup_path.h"                // ObBackupPath
#include "share/object_storage/ob_device_config_mgr.h"  // ObDeviceConfigMgr

namespace oceanbase {
namespace storage {
// `TestDelOp` is used to perform deletion operations on files while listing files.
class TestDelOp : public ObBaseDirEntryOperator
{
public:
  TestDelOp(const share::ObBackupPath &path, const share::ObBackupStorageInfo *storage_info)
      : path_(path), storage_info_(storage_info)
  {}
  virtual ~TestDelOp()
  {}
  int func(const dirent *entry) override;

private:
  const share::ObBackupPath path_;
  const share::ObBackupStorageInfo *storage_info_;
};

int TestDelOp::func(const dirent *entry)
{
  int ret = OB_SUCCESS;
  common::ObBackupIoAdapter io_adapter;
  share::ObBackupPath tmp_path;
  if (OB_ISNULL(entry)) {
    ret = OB_INVALID_ARGUMENT;
    OB_LOG(WARN, "invalid list entry, entry is null", KR(ret));
  } else if (OB_ISNULL(entry->d_name)) {
    ret = OB_INVALID_ARGUMENT;
    OB_LOG(WARN, "invalid list entry, d_name is null", KR(ret));
  } else if (OB_FAIL(tmp_path.init(path_.get_obstr()))) {
    OB_LOG(WARN, "failed to init tmp_path", KR(ret), K_(path));
  } else if (OB_FAIL(tmp_path.join(entry->d_name, share::ObBackupFileSuffix::NONE))) {
    OB_LOG(WARN, "failed to join file name", KR(ret), KCSTRING(entry->d_name));
  } else if (OB_FAIL(io_adapter.del_file(tmp_path.get_ptr(), storage_info_))) {
    // File does not exist should be considered successful
    if (OB_OBJECT_NOT_EXIST == ret) {
      OB_LOG(INFO, "object is not exist", KR(ret), K(tmp_path));
      ret = OB_SUCCESS;
    } else {
      OB_LOG(WARN, "failed to delete file", KR(ret), K(tmp_path));
    }
  } else {
    OB_LOG(INFO, "success to delete file", K(tmp_path));
  }
  return ret;
}

class ResidualDataCleaner
{
public:
  ResidualDataCleaner() : is_inited_(false), path_(), storage_info_()
  {}
  virtual ~ResidualDataCleaner()
  {}
  // Directly retrieve the device_config from the mock environment
  // in the test and perform initialization.
  int init_in_mock_env();
  int clean_residual_data();

private:
  int init(const char *path, const char *endpoint, const char *access_info, const char *extension);
  bool is_inited_;
  share::ObBackupPath path_;
  share::ObBackupStorageInfo storage_info_;
};

int ResidualDataCleaner::init(const char *path, const char *endpoint, const char *access_info, const char *extension)
{
  int ret = OB_SUCCESS;
  common::ObStorageType type = common::ObStorageType::OB_STORAGE_MAX_TYPE;
  if (OB_ISNULL(path) || OB_ISNULL(endpoint) || OB_ISNULL(access_info) || OB_ISNULL(extension)) {
    ret = OB_INVALID_ARGUMENT;
    OB_LOG(WARN, "invalid initialization argument", KR(ret), KP(path), KP(endpoint), KP(access_info), KP(extension));
  } else if (OB_FAIL(path_.init(path))) {
    OB_LOG(WARN, "failed to set path when initiating residual data cleaner", KR(ret), KCSTRING(path));
  } else if (OB_FAIL(get_storage_type_from_path(path, type))) {
    OB_LOG(WARN, "failed to get storage type when initiating residual data cleaner", KR(ret), KCSTRING(path));
  } else if (OB_FAIL(storage_info_.set(type, endpoint, access_info, extension))) {
    OB_LOG(WARN, "failed to set storage info when initiating residual data cleaner", KR(ret), KCSTRING(path), KCSTRING(endpoint));
  } else {
    is_inited_ = true;
    OB_LOG(INFO, "success to initiate residual data cleaner", KCSTRING(path), KCSTRING(endpoint));
  }
  return ret;
};

int ResidualDataCleaner::init_in_mock_env()
{
  int ret = OB_SUCCESS;
  share::ObDeviceConfig device_config;
  if (OB_FAIL(share::ObDeviceConfigMgr::get_instance().get_device_config(
          share::ObStorageUsedType::TYPE::USED_TYPE_DATA, device_config))) {
    OB_LOG(WARN, "failed to get device config when initiating residual data cleaner", KR(ret));
  } else if (OB_FAIL(init(
                 device_config.path_, device_config.endpoint_, device_config.access_info_, device_config.extension_))) {
    OB_LOG(WARN, "failed to init residual data cleaner", KR(ret));
  }
  return ret;
};

int ResidualDataCleaner::clean_residual_data()
{
  int ret = OB_SUCCESS;
  common::ObBackupIoAdapter io_adapter;
  TestDelOp del_op(path_, &storage_info_);
  if (OB_FAIL(io_adapter.list_files(path_.get_obstr(), &storage_info_, del_op))) {
    if (OB_DIR_NOT_EXIST == ret) {
      OB_LOG(INFO, "dir is not exist when cleaning residual data", KR(ret));
      ret = OB_SUCCESS;
    } else {
      OB_LOG(WARN, "failed to del dir when cleaning residual data", KR(ret));
    }
  } else {
    OB_LOG(INFO, "success to clean residual data");
  }
  return ret;
};

class ResidualDataCleanerHelper
{
public:
  ResidualDataCleanerHelper()
  {}
  virtual ~ResidualDataCleanerHelper()
  {}
  static int clean_in_mock_env();
};

int ResidualDataCleanerHelper::clean_in_mock_env()
{
  int ret = OB_SUCCESS;
  const int failed_count = ::testing::UnitTest::GetInstance()->failed_test_count();
  const ::testing::TestCase *test_case = ::testing::UnitTest::GetInstance()->current_test_case();
  // When all test cases are passed, clean residual data
   if (OB_ISNULL(test_case)) {
      ret = OB_ERR_UNEXPECTED;
      OB_LOG(WARN, "test case is null when try to clean residual data", KR(ret));
    } else if (failed_count == 0) {
      int ret = OB_SUCCESS;
      ResidualDataCleaner cleaner;
      if (OB_FAIL(cleaner.init_in_mock_env())) {
        OB_LOG(WARN, "failed to init residual data cleaner", KR(ret));
      } else if (OB_FAIL(cleaner.clean_residual_data())) {
        OB_LOG(WARN, "failed to clean residual data", KR(ret), K(test_case->name()));
      } else {
        OB_LOG(INFO, "success to clean residual data after test", K(test_case->name()));
      }
  }
  return ret;
};


}  // namespace storage
}  // namespace oceanbase

#endif  // OCEANBASE_SENSITIVE_TEST_SHARED_STORAGE_CLEAN_RESIDUAL_DATA_H_
