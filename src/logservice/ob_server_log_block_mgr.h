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

#include <fcntl.h>                          // O_RDWR
#include "lib/lock/ob_spin_lock.h"          // ObSpinLock
#include "lib/container/ob_se_array.h"      // ObSEArray
#include "lib/utility/ob_macro_utils.h"     // DISALLOW_COPY_AND_ASSIGN
#include "lib/utility/ob_print_utils.h"     // TOS_TRING_KV
#include "lib/ob_define.h"                  // OB_MAX_FILE_NAME_LENGTH
#include "lib/lock/ob_tc_rwlock.h"          // ObTCRWLock
#include "lib/lock/ob_spin_lock.h"          // ObSpinLock
#include "lib/function/ob_function.h"       // ObFunction
#include "palf/log_define.h"                // block_id_t
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
  static const int64_t GB = 1024 * 1024 * 1024ll;
  static const int64_t MB = 1024 * 1024ll;
  static const int64_t KB = 1024ll;
  static const int64_t BLOCK_SIZE = palf::PALF_PHY_BLOCK_SIZE;
  static const int64_t LOG_POOL_META_SERIALIZE_SIZE = 4 * KB;
  static const int64_t SLEEP_TS_US = 1 * 1000;
  static const int CREATE_FILE_FLAG = O_RDWR | O_CREAT | O_EXCL | O_SYNC | O_DIRECT;
  static const int OPEN_FILE_FLAG = O_RDWR | O_SYNC | O_DIRECT;
  static const int OPEN_DIR_FLAG = O_DIRECTORY | O_RDONLY;
  static const int CREATE_DIR_MODE =
      S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IROTH;
  static const int CREATE_FILE_MODE = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
  static const int64_t RUN_INTERVAL = 1 * 1000 * 1000;
  static const int64_t PRINT_INTERVAL = 3 * 1000 * 1000;

private:
  typedef common::RWLock RWLock;
  typedef RWLock::RLockGuard RLockGuard;
  typedef RWLock::WLockGuard WLockGuard;

public:
  ObServerLogBlockMgr();
  ~ObServerLogBlockMgr();
  // @brief initialize ObServerLogBlockMgr, reload myself if has reserved.
  // @param[in] the path of log disk(ie: like **/store/clog), and make sure this path has existed.
  // @retval
  //   OB_SUCCESS
  //   OB_IO_ERROR
  int init(const char *log_disk_base_path);
  int start(const int64_t log_disk_size);
  void destroy();
  int get_disk_usage(int64_t &in_use_size_byte);
  int64_t get_log_disk_size();

  // @brief allocate a new block, and move it to the specified directory with the specified
  // name.
  // @param[in] the file description of directory.
  // @param[in] the name of this block.
  // @param[in] specified block size.
  int create_block_at(const palf::FileDesc &dest_dir_fd,
                      const char *dest_block_path,
                      const int64_t block_size) override final;

  // @brief recycle a block, and move it to myself directory.
  // @param[in] the directory description of source directory.
  // @param[in] the name of this block.
  int remove_block_at(const palf::FileDesc &src_dir_fd,
                      const char *src_block_path) override final;

  // @brief before 'update_tenant_log_disk_size' in ObMultiTenant, need update it.
  // @param[in] the log disk size used by tenant.
  // @param[in] the log disk size need by tenant.
  // @param[in] the log disk size allowed by tenant
  // @param[in] ObLogService*
  //   OB_SUCCESS
  //   OB_MACHINE_RESOURCE_NOT_ENOUGH
  int update_tenant(const int64_t old_log_disk_size,
                    const int64_t new_log_disk_size,
                    int64_t &allowed_log_disk_size,
                    ObLogService *log_service);

  int force_update_tenant_log_disk(const uint64_t tenant_id,
                                   const int64_t new_log_disk_size);
  TO_STRING_KV(K_(is_inited));

private:
  int do_load_(const char *log_disk_path);
  int scan_log_disk_dir_(const char *log_disk_path, int64_t &has_allocated_block_cnt);

  bool check_space_is_enough_(const int64_t log_disk_size) const;
  int get_all_tenants_log_disk_size_(int64_t &log_disk_size) const;
private:
  int64_t get_in_use_size_();
  int allocate_block_at_(const palf::FileDesc &dir_fd, const char *block_path, const int64_t block_size);
  int free_block_at_(const palf::FileDesc &dir_fd, const char *block_path);
  int get_has_allocated_blocks_cnt_in_(const char *log_disk_path,
                                       int64_t &has_allocated_block_cnt);
  int remove_tmp_file_or_directory_for_tenant_(const char *log_disk_path);
private:
  int unlinkat_until_success_(const palf::FileDesc &src_dir_fd, const char *block_path,
                              const int flag);
  int fsync_until_success_(const palf::FileDesc &src_fd);
  int scan_tenant_dir_(const char *tenant_dir, int64_t &has_allocated_block_cnt);
  int scan_ls_dir_(const char *tenant_dir, int64_t &has_allocated_block_cnt);
private:
  typedef common::ObFunction<int(int64_t&)> GetTenantsLogDiskSize;
  GetTenantsLogDiskSize get_tenants_log_disk_size_func_;
  // NB: in progress of expanding, the free size byte calcuated by BLOCK_SIZE * (max_block_id_ - min_block_id_) may be greater than
  //     curr_total_size_, if we calcuated log disk in use by curr_total_size_ - 'free size byte', the resule may be negative.
  int64_t block_cnt_in_use_;
  // Before start ObServerLogBlockMgr, not support log disk resize.
  bool is_started_;
  bool is_inited_;

private:
  DISALLOW_COPY_AND_ASSIGN(ObServerLogBlockMgr);
};

} // namespace logservice
} // namespace oceanbase
#endif
