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

#ifndef OB_FILE_SYSTEM_ROUTER_H_
#define OB_FILE_SYSTEM_ROUTER_H_

#include "common/ob_zone.h"
#include "lib/container/ob_se_array.h"
#include "lib/lock/ob_tc_rwlock.h"
#include "lib/ob_define.h"
#include "lib/utility/ob_macro_utils.h"
#include "share/ob_zone_info.h"
#include "storage/blocksstable/ob_log_file_spec.h"

namespace oceanbase {
namespace storage {

class ObFileSystemRouter final
{
public:
  static ObFileSystemRouter & get_instance();
  int init(const char *data_dir, const char *redo_dir);

  OB_INLINE const char* get_data_dir() const { return data_dir_; }
  OB_INLINE const char* get_slog_dir() const { return slog_dir_; }
  OB_INLINE const char* get_clog_dir() const { return clog_dir_; }
  int get_tenant_clog_dir(
      const uint64_t tenant_id,
      char (&tenant_clog_dir)[common::MAX_PATH_SIZE]);

  // only work in local file system
  OB_INLINE const char* get_sstable_dir() const { return sstable_dir_; }

  OB_INLINE int64_t get_svr_seq() const { return svr_seq_; }
  OB_INLINE void set_svr_seq(const int64_t svr_seq) { svr_seq_ = svr_seq; }

  OB_INLINE bool is_single_zone_deployment_on() const { return false; }

  OB_INLINE const blocksstable::ObLogFileSpec &get_clog_file_spec() const { return clog_file_spec_; }
  OB_INLINE const blocksstable::ObLogFileSpec &get_slog_file_spec() const { return slog_file_spec_; }

private:
  ObFileSystemRouter();
  virtual ~ObFileSystemRouter() = default;

  void reset();
  int init_shm_file_path();
  int init_local_dirs(const char* data_dir, const char* redo_dir);

private:
  char data_dir_[common::MAX_PATH_SIZE];
  char slog_dir_[common::MAX_PATH_SIZE];
  char clog_dir_[common::MAX_PATH_SIZE];
  char sstable_dir_[common::MAX_PATH_SIZE];

  blocksstable::ObLogFileSpec clog_file_spec_;
  blocksstable::ObLogFileSpec slog_file_spec_;
  int64_t svr_seq_;
  bool is_inited_;

  DISALLOW_COPY_AND_ASSIGN(ObFileSystemRouter);
};
#define OB_FILE_SYSTEM_ROUTER (::oceanbase::storage::ObFileSystemRouter::get_instance())
} // namespace storage
} // namespace oceanbase
#endif /* OB_FILE_SYSTEM_ROUTER_H_ */
