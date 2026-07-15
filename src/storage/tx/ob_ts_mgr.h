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

#ifndef OCEANBASE_TRANSACTION_OB_TS_MGR_
#define OCEANBASE_TRANSACTION_OB_TS_MGR_

#include <stdint.h>
#include "lib/utility/ob_print_utils.h"
#include "lib/lock/ob_drw_lock.h"
#include "lib/hash/ob_link_hashmap.h"
#include "lib/atomic/atomic128.h"
#include "lib/net/ob_addr.h"
#include "lib/queue/ob_link_queue.h"
#include "lib/container/ob_iarray.h"
#include "share/ob_errno.h"
#include "share/ob_thread_pool.h"
#include "lib/lock/ob_qsync_lock.h"
#include "ob_gts_source.h"
#include "ob_gts_define.h"

#define REFRESH_GTS_INTERVEL_US  (500 * 1000)

namespace oceanbase
{
namespace share
{
class ObLocationService;
namespace schema
{
class ObSchemaGetterGuard;
class ObMultiVersionSchemaService;
}
}
namespace obcall
{
}
namespace rpc
{
namespace frame
{
class ObReqTransport;
}
}
namespace share
{
class SCN;
}
namespace transaction
{
class ObIGlobalTimestampService;

class ObTsCbTask : public common::ObLink
{
public:
  ObTsCbTask() {}
  virtual ~ObTsCbTask() {}
  virtual int gts_callback_interrupted(const int errcode, const share::ObLSID ls_id) = 0;
  virtual int get_gts_callback(const MonotonicTs srr, const share::SCN &gts, const MonotonicTs receive_gts_ts) = 0;
  virtual int gts_elapse_callback(const MonotonicTs srr, const share::SCN &gts) = 0;
  virtual MonotonicTs get_stc() const = 0;
  virtual uint64_t hash() const = 0;
  
  VIRTUAL_TO_STRING_KV("", "");
};

class ObITsMgr
{
public:
  virtual int update_gts(const int64_t gts, bool &update) = 0;
  virtual int get_gts(const MonotonicTs stc,
                      ObTsCbTask *task,
                      share::SCN &scn,
                      MonotonicTs &receive_gts_ts) = 0;
  virtual int get_gts_sync(const MonotonicTs stc,
                           const int64_t timeout_us,
                           share::SCN &scn,
                           MonotonicTs &receive_gts_ts) = 0;

  virtual int get_gts(ObTsCbTask *task, share::SCN &scn) = 0;
  virtual int get_ts_sync(const int64_t timeout_ts,
      share::SCN &scn, bool &is_external_consistent) = 0;
  virtual int get_ts_sync(const int64_t timeout_ts, share::SCN &scn) = 0;
  virtual int wait_gts_elapse(const share::SCN &scn, ObTsCbTask *task,
                              bool &need_wait) = 0;
  virtual int wait_gts_elapse(const share::SCN &scn) = 0;
  virtual bool is_external_consistent() = 0;
  virtual int interrupt_gts_callbacks() = 0;
  virtual int interrupt_gts_callback_for_ls_offline(const share::ObLSID ls_id) = 0;
public:
  VIRTUAL_TO_STRING_KV("", "");
};

class ObTsMgr;
class ObTsMgr : public share::ObThreadPool, public ObITsMgr
{
public:
  ObTsMgr() { reset(); }
  ~ObTsMgr() { destroy(); }
  int init(const common::ObAddr &server,
           share::schema::ObMultiVersionSchemaService &schema_service,
           share::ObLocationService &location_service);
  void reset();
  int start();
  void stop();
  void wait();
  void destroy();
  void run1();

  int handle_gts_err_response(const ObGtsErrResponse &msg);
  int handle_gts_result(const int64_t queue_index, const int ts_type);
  int update_gts(const MonotonicTs srr, const int64_t gts, const int ts_type, bool &update);
  int interrupt_gts_callback_for_ls_offline(const share::ObLSID ls_id);
public:
  int update_gts(const int64_t gts, bool &update);
  // According to stc get the appropriate gts value, if the conditions are not met, need to register gts task, wait for asynchronous callback
  int get_gts(const MonotonicTs stc,
              ObTsCbTask *task,
              share::SCN &scn,
              MonotonicTs &receive_gts_ts);
  /** 
   * The synchronous interface corresponding to `get_gts`, used for synchronously obtaining an appropriate GTS timestamp, with a timeout parameter to avoid long waits.
   * Compared to the original synchronous interface `get_ts_sync`, this interface has better performance.
   * @param[in] stc: The point in time to obtain the GTS, generally current time
   * @param[in] timeout_us: Timeout duration, unit us
   * @param[out] scn: The result of the obtained GTS timestamp
   * @param[out] receive_gts_ts: The point in time when the GTS response was received
   */
  int get_gts_sync(const MonotonicTs stc,
                   const int64_t timeout_us,
                   share::SCN &scn,
                   MonotonicTs &receive_gts_ts);
  //Only get the latest value from local gts cache, but it may fail, failure handling logic as follows:
  //1. If task == NULL, it means the caller does not need asynchronous callbacks, directly return the error, and let the caller handle it
  //2. If task != NULL, need to register asynchronous callback task
  int get_gts(ObTsCbTask *task, share::SCN &scn);
  int get_ts_sync(const int64_t timeout_us,
      share::SCN &scn, bool &is_external_consistent);
  int get_ts_sync(const int64_t timeout_us, share::SCN &scn);
  int wait_gts_elapse(const share::SCN &scn, ObTsCbTask *task,
      bool &need_wait);
  int wait_gts_elapse(const share::SCN &scn);
  bool is_external_consistent() { return true; }
  int refresh_gts_location();
  int interrupt_gts_callbacks();
public:
  TO_STRING_KV("ts_source", "GTS");
public:
  // get current tenant GTS as archive start snapshot (retry up to 10s).
  // relocated from share::ObBackupUtils::get_backup_scn (module-boundary: share must not depend on storage/tx)
  static int get_backup_scn(const uint64_t &tenant_id, share::SCN &scn);
  static ObTsMgr &get_instance();
private:
private:
  static ObTsMgr* &get_instance_inner();
private:
  bool is_inited_;
  bool is_running_;
  common::ObAddr server_;
  ObGtsSource ts_source_;
};

#define OB_TS_MGR (::oceanbase::transaction::ObTsMgr::get_instance())

}
}//end of namespace oceanbase

namespace oceanbase
{
namespace transaction
{
// demoted from share::ObShareUtil(GTS query convenience wrapper, uses OB_TS_MGR.get_gts plus waiting internally)
int get_tenant_gts(share::SCN &gts_scn);
}
}

#endif //OCEANBASE_TRANSACTION_OB_TS_MGR_
