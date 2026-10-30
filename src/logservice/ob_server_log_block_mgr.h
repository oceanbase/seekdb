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

#ifndef OCEANBASE_LOGSERVICE_OB_SERVER_LOG_BLOCK_MGR_
#define OCEANBASE_LOGSERVICE_OB_SERVER_LOG_BLOCK_MGR_

#include "rust/storage_io.h"
#include <fcntl.h>                          // O_RDWR
#include "lib/lock/ob_spin_lock.h"          // ObSpinLock
#include "lib/container/ob_se_array.h"      // ObSEArray
#include "lib/utility/ob_macro_utils.h"     // DISALLOW_COPY_AND_ASSIGN
#include "lib/utility/ob_print_utils.h"     // TOS_TRING_KV
#include "lib/ob_define.h"                  // OB_MAX_FILE_NAME_LENGTH
#include "lib/lock/ob_tc_rwlock.h"          // ObTCRWLock
#include "lib/lock/ob_spin_lock.h"          // ObSpinLock
#include "share/log/palf/log_define.h"      // block_id_t
#include "share/startup_result.h"
#include "share/local_io_config.h"
#include "palf/log_block_pool_interface.h"  // ObIServerLogBlockPool
#include "palf/log_io_utils.h"              // ObBaseDirFunctor

namespace oceanbase
{
namespace logservice
{
class ObLogService;
class ObServerLogBlockMgr : public palf::ILogBlockPool
{
public:
  static int check_clog_directory_is_empty(const char *clog_dir, bool &result);
private:
  static const int64_t BLOCK_SIZE = palf::PALF_PHY_BLOCK_SIZE;

private:
  typedef common::RWLock RWLock;
  typedef RWLock::RLockGuard RLockGuard;
  typedef RWLock::WLockGuard WLockGuard;

public:
  typedef int (*GetLogDiskInfoInConfig)(int64_t &log_disk_size,
                                        int64_t &log_disk_percentage,
                                        int64_t &total_log_disk_size);
  explicit ObServerLogBlockMgr(share::StartupResult &startup_result);
  ~ObServerLogBlockMgr();
  // @brief initialize ObServerLogBlockMgr, reload myself if has reserved.
  // @param[in] the path of log disk(ie: like **/store/clog), and make sure this path has existed.
  // @retval
  //   OB_SUCCESS
  //   OB_IO_ERROR
  int init(const char *log_disk_base_path,
           GetLogDiskInfoInConfig get_log_disk_info_in_config,
           const share::LocalIOConfig &io_config);
  void bind_log_service(ObLogService &log_service)
  {
    log_service_ = &log_service;
  }
  void unbind_log_service(ObLogService &log_service)
  {
    if (log_service_ == &log_service) {
      log_service_ = nullptr;
    }
  }
  int start(const int64_t log_disk_size);
  void stop() { stopped_.store(true); }
  void destroy();
  int get_disk_usage(int64_t &in_use_size_byte) override final;
  int set_capacity(int64_t bytes) override final;
  bool can_reinitialize() const { return !is_inited_ && owner_->named_owner_replaceable(); }
  const storage_io::NamedOwnerHandle &physical_owner() const { return *owner_; }
  int rename_block_at(const file_io::DirectoryHandle &directory, const char *old_name, const char *new_name) override final;
  int create_directory_at(const file_io::DirectoryHandle &directory, const char *name, uint32_t mode) override final;
  int rename_directory_at(const file_io::DirectoryHandle &directory, const char *old_name, const char *new_name) override final;
  int remove_directory_at(const file_io::DirectoryHandle &directory, const char *name) override final;
  int64_t get_log_disk_size();

  // @brief allocate a new block, and move it to the specified directory with the specified
  // name.
  // @param[in] the file description of directory.
  // @param[in] the name of this block.
  // @param[in] specified block size.
  int create_and_publish_block_at(const file_io::DirectoryHandle &directory,
                      const char *temporary_name, const char *dest_block_path,
                      const int64_t block_size) override final;

  // @brief recycle a block, and move it to myself directory.
  // @param[in] the directory description of source directory.
  // @param[in] the name of this block.
  int remove_block_at(const file_io::DirectoryHandle &directory,
                      const char *src_block_path, bool required = false) override final;

  // @brief before 'update_server_log_disk_size' in ObServerRuntimeController, need update it.
  // @param[in] current and requested local runtime log-disk sizes.
  // @param[out] the log-disk size accepted by the log service.
  // @param[in] ObLogService*
  //   OB_SUCCESS
  //   OB_MACHINE_RESOURCE_NOT_ENOUGH
  int update_log_disk_size(const int64_t old_log_disk_size,
                           const int64_t new_log_disk_size,
                           int64_t &allowed_log_disk_size,
                           ObLogService *log_service);
  TO_STRING_KV(K_(is_inited));

private:
  int do_load_(const char *log_disk_path);

  bool check_space_is_enough_(const int64_t log_disk_size) const;
  int get_runtime_log_disk_size_(int64_t &log_disk_size) const;
private:
  int64_t get_in_use_size_();
  int get_has_allocated_blocks_cnt_in_(const char *log_disk_path,
                                       int64_t &has_allocated_block_cnt);
  int remove_tmp_file_or_directory_for_runtime_(const char *log_disk_path);
private:
  int scan_runtime_dir_(const file_io::DirectoryHandle &directory, const char *runtime_dir, int64_t &has_allocated_block_cnt);
  int scan_ls_dir_(const file_io::DirectoryHandle &directory, const char *ls_dir, int64_t &has_allocated_block_cnt);
private:
  share::StartupResult &startup_result_;
  share::LocalIOConfig io_config_;
  std::atomic<bool> stopped_{false};
  GetLogDiskInfoInConfig get_log_disk_info_in_config_func_;
  ObLogService *log_service_;
  // NB: in progress of expanding, the free size byte calcuated by BLOCK_SIZE * (max_block_id_ - min_block_id_) may be greater than
  //     curr_total_size_, if we calcuated log disk in use by curr_total_size_ - 'free size byte', the resule may be negative.
  rust::Box<storage_io::NamedOwnerHandle> owner_;
  // Before start ObServerLogBlockMgr, not support log disk resize.
  bool is_started_;
  bool is_inited_;

private:
  DISALLOW_COPY_AND_ASSIGN(ObServerLogBlockMgr);
};

} // namespace logservice
} // namespace oceanbase
#endif
