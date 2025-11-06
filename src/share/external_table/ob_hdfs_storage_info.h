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

#ifndef OBDEV_SRC_SHARE_EXTERNAL_TABLE_OB_HDFS_STORAGE_INFO_H
#define OBDEV_SRC_SHARE_EXTERNAL_TABLE_OB_HDFS_STORAGE_INFO_H

#include "lib/restore/ob_storage_info.h"

namespace oceanbase
{
namespace share
{
// The limit on the maximum length of single config in hdfs is 128
static constexpr int64_t OB_MAX_HDFS_SINGLE_CONF_LENGTH = 128;
// The limit on the maximum length of other configs in hdfs is 1024
static constexpr int64_t OB_MAX_HDFS_CONFS_LENGTH = 1024;

const int64_t OB_MAX_HDFS_BACKUP_EXTENSION_LENGTH = 1536;

const char *const KRB5CONF = "krb5conf=";
const char *const PRINCIPAL = "principal=";
const char *const KEYTAB = "keytab=";
const char *const TICKET_CACHE_PATH = "ticiket_cache_path=";
const char *const HDFS_CONFIGS = "configs=";
const char *const HADOOP_USERNAME = "username=";

class ObHDFSStorageInfo : public common::ObObjectStorageInfo
{

public:
  ObHDFSStorageInfo() {
    hdfs_extension_[0] = '\0';
  }
  virtual ~ObHDFSStorageInfo();

  bool operator ==(const ObHDFSStorageInfo &external_storage_info) const;

  // Override methods
  virtual int assign(const ObObjectStorageInfo &storage_info) override;
  virtual void reset() override;
  // virtual int get_storage_info_str(char *storage_info, const int64_t info_len) const override;

  virtual int validate_arguments() const override;
  // the following two functions are designed for ObDeviceManager, which manages all devices by a device_map_
  virtual int64_t get_device_map_key_len() const override;
  virtual int get_device_map_key_str(char *key_str, const int64_t len) const override;

private:
  virtual int parse_storage_info_(const char *storage_info, bool &has_needed_extension) override;
  virtual int get_info_str_(char *storage_info, const int64_t info_len) const override;
  virtual int append_extension_str_(char *storage_info, const int64_t info_len) const override;

public:
  char hdfs_extension_[OB_MAX_HDFS_BACKUP_EXTENSION_LENGTH];
};
} // namespace sql
} // namespace oceanbase

#endif /* OBDEV_SRC_SHARE_EXTERNAL_TABLE_OB_HDFS_STORAGE_INFO_H */
