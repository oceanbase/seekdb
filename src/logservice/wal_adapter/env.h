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
#include <stdint.h>
#include "logservice/ob_log_allocator.h"
#include "lib/function/ob_function.h"
#include "env_impl.h"
namespace oceanbase
{
namespace commom
{
class ObAddr;
class ObIOManager;
}

namespace share
{
class ObLocalDevice;
}
namespace wal
{
class Handle;
class DiskOptions;

class Env
{
public:
  // static interface
  // create the wal env with the specified "base_dir".
  // store a pointer to a heap-allocated(may be allocate by a specified allocator) in "wal_env",
  // and return OB_SUCCESS on success.
  // store a NULL pointer ) in "wal_env", and return errno on fail.
  // caller should used destroy_wal_env to delete "wal_env" when it is no longer used.
  static int create_wal_env(const Options &options,
                             share::LocalStore &store,
                             const char *base_dir,
                             const common::ObAddr &self,
                             MonitorCallback *monitor,
                             Env *&wal_env);
  // static interface
  // destroy the wal env, and set "wal_env" to NULL.
  static void destroy_wal_env(Env *&wal_env);

public:
  Env();
  ~Env();
  // Create the unique log stream.
  // @param [in] wal_base_info, the log start information of wal
  // @param [out] handle, the generated wal_handle object after successful creation
  int create(const BaseInfo &wal_base_info,
             Handle &handle);
  // Open the unique log stream and return its handle.
  int open(Handle &handle);
  // Close a handle
  void close(Handle &handle);
  // Delete the unique log stream and its physical files.
  int remove();

  // @brief get wal disk usage
  // @param [out] used_size_byte
  // @param [out] total_size_byte, the configured soft target for checkpoint and recycling.
  int get_disk_usage(int64_t &used_size_byte, int64_t &total_size_byte);

  // @brief update options
  // @param [in] options
  int update_options(const Options &options);
  // @brief get current options
  // @param [out] options
  int get_options(Options &options);
  // for failure detector
  // @brief get last io worker start time
  // @param [out] last working time
  // last_working_time will be set as current time when a io task begins,
  // and will be reset as OB_INVALID_TIMESTAMP when an io task ends, atomically.
  int get_io_start_time(int64_t &last_working_time);
  // @brief iterate each Handle of Env and execute 'func'
  int start();
private:
  void stop_();
  void wait_();
  void destroy_();
private:
  // the implmention of Env
  wal::EnvImpl wal_env_impl_;
};
} // end namespace wal
} // end namespace oceanbase
#endif
