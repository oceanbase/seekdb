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

#ifndef OCEANBASE_LOGSERVICE_WAL_ADAPTER_ENV_IMPL_
#define OCEANBASE_LOGSERVICE_WAL_ADAPTER_ENV_IMPL_
#include "lib/file/file_directory_utils.h"
#include "lib/lock/ob_tc_rwlock.h"
#include "lib/utility/ob_macro_utils.h"
#include "share/storage/local_store.h"
#include "share/log/types/options.h"
#include "directory_utils.h"
#include "handle_impl.h"

namespace oceanbase
{
namespace wal
{
class EnvImpl
{
public:
  EnvImpl();
  ~EnvImpl();
  int init(const Options &options, share::LocalStore &store, const char *base_dir,
      const common::ObAddr &self, MonitorCallback *monitor);
  int start();
  void stop();
  void wait();
  void destroy();

  int create_wal_handle_impl(const BaseInfo &wal_base_info, HandleImpl *&wal_handle_impl);
  int remove_wal_handle_impl();
  int get_wal_handle_impl(HandleImpl *&wal_handle_impl);

  int get_disk_usage(int64_t &used_size_byte, int64_t &total_usable_size_byte);
  int update_options(const Options &options);
  int get_options(Options &options);
  int get_io_start_time(int64_t &last_working_time);
  int create_directory(const char *base_dir);
  int remove_directory(const char *base_dir);
  TO_STRING_KV(K_(self), K_(log_dir), K_(disk_options));

private:
  int reload_wal_handle_impl_();
  struct RemoveStaleIncompleteWalFunctor : public DirectoryFunctor
  {
    explicit RemoveStaleIncompleteWalFunctor(EnvImpl *wal_env_impl);
    ~RemoveStaleIncompleteWalFunctor();
    int func(const char *entry_name) override;
    EnvImpl *wal_env_impl_;
  };
  int create_wal_handle_impl_(const BaseInfo &wal_base_info, HandleImpl *&wal_handle_impl);
  int get_total_used_disk_space_(int64_t &total_used_disk_space,
      int64_t &total_unrecyclable_disk_space);
  int get_disk_usage_(int64_t &used_size_byte, int64_t &unrecyclable_disk_space);
  int get_disk_usage_(int64_t &used_size_byte);
  int remove_wal_handle_impl_();
  int move_incomplete_wal_into_tmp_dir_();
  int check_tmp_log_dir_exist_(bool &exist) const;
  int remove_stale_incomplete_wal_();
  int remove_directory_while_exist_(const char *log_dir);

private:
  typedef common::RWLock RWLock;
  typedef RWLock::RLockGuard RLockGuard;
  typedef RWLock::WLockGuard WLockGuard;
  RWLock wal_meta_lock_;
  share::LocalStore *store_;
  MonitorCallback *monitor_;
  DiskOptions disk_options_;
  char log_dir_[common::MAX_PATH_SIZE];
  char tmp_log_dir_[common::MAX_PATH_SIZE];
  common::ObAddr self_;
  HandleImpl *wal_handle_;
  bool is_inited_;
  bool is_running_;
  DISALLOW_COPY_AND_ASSIGN(EnvImpl);
};

} // namespace wal
} // namespace oceanbase
#endif
