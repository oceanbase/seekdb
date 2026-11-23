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

#ifndef OCEANBASE_LOGSERVICE_WAL_ADAPTER_ENV_
#define OCEANBASE_LOGSERVICE_WAL_ADAPTER_ENV_

#include <optional>
#include "lib/lock/ob_tc_rwlock.h"
#include "handle_impl.h"

namespace oceanbase
{
namespace wal
{
class Handle;

class Env
{
public:
  static int create_wal_env(int64_t disk_soft_target,
                            const char *base_dir,
                            Env *&wal_env);
  static void destroy_wal_env(Env *&wal_env);

  Env();
  ~Env();
  int create(const BaseInfo &wal_base_info, Handle &handle);
  int open(Handle &handle);
  void close(Handle &handle);
  int remove();
  int get_disk_usage(int64_t &used_size_byte, int64_t &total_size_byte);
  int update_disk_soft_target(int64_t disk_soft_target);
  int get_disk_soft_target(int64_t &disk_soft_target);
  int get_io_start_time(int64_t &last_working_time);
  int start();

private:
  typedef common::RWLock RWLock;
  typedef RWLock::RLockGuard RLockGuard;
  typedef RWLock::WLockGuard WLockGuard;
  int wrap_stream_(::rust::Box<wal_rs::Stream> stream, HandleImplPtr &handle);
  void stop_();
  void destroy_();

private:
  RWLock lock_;
  std::optional<::rust::Box<wal_rs::Env>> rust_env_;
  HandleImplPtr wal_handle_;
  bool is_inited_;
  DISALLOW_COPY_AND_ASSIGN(Env);
};

} // namespace wal
} // namespace oceanbase
#endif
