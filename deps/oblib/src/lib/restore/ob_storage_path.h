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

#include "lib/alloc/alloc_assist.h"
#include "lib/ob_errno.h"
#include "lib/oblog/ob_log_module.h"
#include "lib/string/ob_string.h"

#ifndef OCEANBASE_COMMON_STORAGE_PATH_H_
#define OCEANBASE_COMMON_STORAGE_PATH_H_

namespace oceanbase
{
namespace common
{

static const char *const BACKUP_INFO_TENANT_ID = "tenant_id";
static const char *const BACKUP_BASE_DATA = "base_data";
static const char *const BACKUP_ALL_TENANT_ID_LIST = "all_tenant_id_list";

class ObStoragePath
{
public:
  ObStoragePath();
  virtual ~ObStoragePath();
  int init(const common::ObString &uri);
  int join(const common::ObString &path);
  int join_simple_object_definition(uint64_t object_id);

  int64_t to_string(char *buf, const int64_t buf_len) const;

  const char *get_string() const { return path_; }
  int64_t get_length() const { return cur_pos_; }
  common::ObString get_obstring() const { return common::ObString(cur_pos_, path_); }
  void reset() { cur_pos_ = 0; }
  bool is_valid() const { return cur_pos_ > 0; }

private:
  int trim_right_delim();
  int trim_left_delim(const common::ObString &path, int64_t &delim_pos);
  char path_[common::OB_MAX_URI_LENGTH];
  int64_t cur_pos_;
};


class ObStoragePathUtil
{
public:
  //example: oss://runiu1/ob1.haipeng.zhp/all_tenant_id_list
  //The data_version on the path of the logical backup task will only be the latest, and the data_version of the macro data on the physical backup task is the data_version of the full backup task
  //The path on the physical backup task macro meta is consistent with the logical backup task, and both are the latest version
  //Logical backup tasks will back up every time without multiplexing; physical backup tasks will multiplex macroblocks according to conditions.
  //path example "file:///mnt/test_nfs_runiu/ob1.haipeng.zhp/2/1"
  //marco data dir path example:
  //"oss://runiu1/ob1.haipeng.zhp/base_data_3/1001/1100611139453834/3"
  //"oss://runiu1/ob1.haipeng.zhp/base_data_3/1001/1100611139453834/3/1100611139453834"
  //macro data full path, for backup, example:
  // "oss://runiu1/ob1.haipeng.zhp/base_data_5/1001/1100611139453836/1152921522055151616/1100611139453836/3_0"
  //macro data full path, for restore
  //backup_info file path "oss://runiu1/ob1.haipeng.zhp/tenant_id/1001/backup_info"
  //table definition example: "oss://runiu1/ob1.haipeng.zhp/3/1001/1100611139453822/inc_table_2_definition"
  //sstable_meta example: "oss://runiu1/ob1.haipeng.zhp/2/1001/1100611139453799/1152921513465217024/sstable_meta"
  //table_keys example: "oss://runiu1/ob1.haipeng.zhp/2/1001/1100611139453799/1152921513465217024/table_keys"
  //partition_meta example: "oss://runiu1/ob1.haipeng.zhp/2/1001/1100611139453805/1/partition_meta"
  //part_list example: "oss://runiu1/ob1.haipeng.zhp/3/1/1099511677788/0/1099511677788/part_list"
};

}//common
}//oceanbase

#endif /* OCEANBASE_COMMON_STORAGE_PATH_H_ */
