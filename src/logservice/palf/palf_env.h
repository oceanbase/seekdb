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

#ifndef OCEANBASE_LOGSERVICE_PALF_ENV_
#define OCEANBASE_LOGSERVICE_PALF_ENV_
#include <stdint.h>
#include "rpc/frame/ob_req_transport.h"
#include "share/allocator/ob_tenant_mutil_allocator.h"
#include "lib/function/ob_function.h"
#include "palf_env_impl.h"
namespace oceanbase
{
namespace commom
{
class ObAddr;
class ObIOManager;
}

namespace rpc
{
namespace frame
{
class ObReqTransport;
}
}

namespace obrpc
{
class ObBatchRpc;
}
namespace share
{
class ObLocalDevice;
class ObResourceManager;
}
namespace palf
{
class PalfRoleChangeCb;
class PalfHandle;
class PalfDiskOptions;
class ILogBlockPool;

class PalfEnv
{
  friend class LogRequestHandler;
public:
  // static interface
  // create the palf env with the specified "base_dir".
  // store a pointer to a heap-allocated(may be allocate by a specified allocator) in "palf_env",
  // and return OB_SUCCESS on success.
  // store a NULL pointer ) in "palf_env", and return errno on fail.
  // caller should used destroy_palf_env to delete "palf_env" when it is no longer used.
  static int create_palf_env(const PalfOptions &options,
                             const char *base_dir,
                             const common::ObAddr &self,
                             rpc::frame::ObReqTransport *transport,
                             obrpc::ObBatchRpc *batch_rpc,
                             common::ObILogAllocator *alloc_mgr,
                             ILogBlockPool *log_block_pool,
                             PalfMonitorCb *monitor,
                             share::ObLocalDevice *log_local_device,
                             share::ObResourceManager *resource_manager,
                             common::ObIOManager *io_manager,
                             PalfEnv *&palf_env);
  // static interface
  // destroy the palf env, and set "palf_env" to NULL.
  static void destroy_palf_env(PalfEnv *&palf_env);

public:
  PalfEnv();
  ~PalfEnv();
  // Migration scenario destination end replica creation interface
  // @param [in] id, the identifier of the log stream to be created
  // @param [in] access_mode，palf access mode
  // @param [in] palf_base_info, the log start information of palf
  // @param [out] handle, the generated palf_handle object after successful creation
  int create(const int64_t id,
             const AccessMode &access_mode,
             const PalfBaseInfo &palf_base_info,
             PalfHandle &handle);
  // Open a Paxos Replica corresponding to an id, and return the file handle
  int open(int64_t id, PalfHandle &handle);
  // Close a handle
  void close(PalfHandle &handle);
  // Delete the Paxos Replica corresponding to the id, which will also delete the physical file;
  int remove(int64_t id);

  // @brief get palf disk usage
  // @param [out] used_size_byte
  // @param [out] total_size_byte, if in shrinking status, total_size_byte is the value after shrinking.
  // NB: total_size_byte may be smaller than used_size_byte.
  int get_disk_usage(int64_t &used_size_byte, int64_t &total_size_byte);

  // @brief get stable disk usage
  // @param [out] used_size_byte
  // @param [out] total_size_byte, if in shrinking status, total_size_byte is the value before shrinking.
  int get_stable_disk_usage(int64_t &used_size_byte, int64_t &total_size_byte);

  // @brief update options
  // @param [in] options
  int update_options(const PalfOptions &options);
  // @brief get current options
  // @param [out] options
  int get_options(PalfOptions &options);
  // @brief check the disk space used to palf whether is enough
  bool check_disk_space_enough();
  // for failure detector
  // @brief get last io worker start time
  // @param [out] last working time
  // last_working_time will be set as current time when a io task begins,
  // and will be reset as OB_INVALID_TIMESTAMP when an io task ends, atomically.
  int get_io_start_time(int64_t &last_working_time);
  // @brief iterate each PalfHandle of PalfEnv and execute 'func'
  int for_each(const ObFunction<int(const PalfHandle&)> &func);
  // just for LogRpc
  palf::IPalfEnvImpl *get_palf_env_impl() { return &palf_env_impl_; }
  // should be removed in version 4.2.0.0
  int update_replayable_point(const SCN &replayable_scn);
  int start();
private:
  void stop_();
  void wait_();
  void destroy_();
private:
  // the implmention of PalfEnv
  palf::PalfEnvImpl palf_env_impl_;
};
} // end namespace palf
} // end namespace oceanbase
#endif
