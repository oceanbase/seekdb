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
#ifndef OCEANBASE_LOGSERVICE_OB_LOG_RESTORE_NET_DRIVER_H_
#define OCEANBASE_LOGSERVICE_OB_LOG_RESTORE_NET_DRIVER_H_

#include "lib/container/ob_iarray.h"     // Array
#include "lib/ob_errno.h"
#include "lib/utility/ob_macro_utils.h"
#include "lib/compress/ob_compress_util.h" // ObCompressorType
#include "common/ob_region.h"  // ObRegion
#include "logservice/logfetcher/ob_log_fetcher_ls_ctx_additional_info_factory.h"
#include "logservice/logfetcher/ob_log_fetcher_err_handler.h"
#include "logservice/logfetcher/ob_log_fetcher_ls_ctx_default_factory.h"
#include "share/backup/ob_log_restore_struct.h"   // ObRestoreSourceServiceAttr
#include "ob_log_restore_driver_base.h"
#include "ob_restore_log_function.h"  // ObRestoreLogFunction
#include "storage/ls/ob_ls.h"         // ObLS
namespace oceanbase
{
namespace share
{
struct ObLSID;
struct SCN;
}

namespace storage
{
class ObLSService;
}

namespace logfetcher
{
class ObLogFetcher;
}

namespace logservice
{
class ObLogService;
// The driver for standby based on net service, and its functions includes:
// 1. fetcher and proxy management;
// 2. schedule ls, add ls leader to log fetcher and remove it if it changes to follower
// 3. scan_ls, if the log restore source is not service, stop fetcher
// 4. set the max restore upper limit
class ObLogRestoreNetDriver : public ObLogRestoreDriverBase
{
public:
  ObLogRestoreNetDriver();
  ~ObLogRestoreNetDriver();
public:
  int init(const uint64_t tenant_id,
      storage::ObLSService *ls_svr,
      ObLogService *log_service);
  void destroy();
  int start();
  void stop();
  void wait();

  // Schedule all ls leader to do restore
  // Init Log Fetcher and Add LS into Log Fetcher
  int do_schedule(share::ObRestoreSourceServiceAttr &source);

  // Set source without scheduling (non-blocking)
  // The background thread will call do_schedule() later
  int set_source(share::ObRestoreSourceServiceAttr &source);

  // Check if source is set and valid
  bool has_source() const;

  // Get the saved source (for converting to ObLogRestoreSourceItem)
  int get_saved_source(share::ObRestoreSourceServiceAttr &source) const;

  // Schedule using the source that was set via set_source()
  // This is called by the background thread
  int do_schedule_with_saved_source();

  // Remove LS if it is not leader or log_restore_source is not service
  // Destroy Fetcher if No LS exist in Fetcher
  int scan_ls(const share::ObLogRestoreSourceType &type);

  // Remove All LS and destroy Fetcher
  // Only happend when tenant role not match or log_restore_source is empty
  void clean_resource();

  // set the max scn can be restored
  int set_restore_log_upper_limit();

  int set_compressor_type(const common::ObCompressorType &compressor_type);

private:
  // TODO How does LogFetcher distinguish LogRestoreSource changes, e.g., from cluster 1's tenant A to cluster 2's tenant B
  // LogFetcher needs to provide interface to distinguish different cluster_id, tenant_id
private:
  int copy_source_(const share::ObRestoreSourceServiceAttr &source);
  int refresh_fetcher_if_needed_(const share::ObRestoreSourceServiceAttr &source);
  int init_fetcher_if_needed_(const int64_t cluster_id, const uint64_t tenant_id);
  void delete_fetcher_if_needed_with_lock_();
  void update_config_();
  int64_t get_rpc_timeout_sec_();
  // update standby_fetch_log_specified_region
  void update_standby_preferred_upstream_log_region_();

  int do_fetch_log_(storage::ObLS &ls);
  int check_need_schedule_(storage::ObLS &ls,
      int64_t &proposal_id,
      bool &need_schedule);
  int add_ls_();
  int remove_stale_ls_();

  int add_ls_if_needed_with_lock_(const share::ObLSID &id, const int64_t proposal_id);
  // Check if fetcher is stale based on source addr list (only ip_list is supported now)
  bool is_fetcher_stale_(const share::ObRestoreSourceServiceAttr &source);
  int stop_fetcher_safely_();
  // check ls empty before destory fetcher
  void destroy_fetcher_();
  // destroy fetcher forcedly, without check
  void destroy_fetcher_forcedly_();

  int check_ls_stale_(const share::ObLSID &id, const int64_t proposal_id, bool &is_stale);
  int get_ls_count_in_fetcher_(int64_t &count);

private:
  class LogErrHandler : public logfetcher::IObLogErrHandler
  {
    public:
      LogErrHandler() : inited_(false), ls_svr_(NULL) {}
      virtual ~LogErrHandler() { destroy(); }
      int init(storage::ObLSService *ls_svr);
      void destroy();
      virtual void handle_error(const int err_no, const char *fmt, ...) override {}
      virtual void handle_error(const share::ObLSID &ls_id,
          const ErrType &err_type,
          share::ObTaskId &trace_id,
          const palf::LSN &lsn,
          const int err_no,
          const char *fmt, ...) override;
    private:
      bool inited_;
      storage::ObLSService *ls_svr_;
  };

  class LogFetcherLSCtxAddInfoFactory : public logfetcher::ObILogFetcherLSCtxAddInfoFactory
  {
    const char *ALLOC_LABEL = "LSCtxAddInfo";
    public:
      virtual ~LogFetcherLSCtxAddInfoFactory() {}
      virtual void destroy() override {}
      virtual int alloc(const char *str, logfetcher::ObILogFetcherLSCtxAddInfo *&ptr) override;
      virtual void free(logfetcher::ObILogFetcherLSCtxAddInfo *ptr) override;
  };
  class LogFetcherLSCtxAddInfo : public logfetcher::ObILogFetcherLSCtxAddInfo
  {

   virtual int init(
       const logservice::TenantLSID &ls_id,
       const int64_t start_commit_version) { return 0;}

  /// get tps info of current LS
  virtual double get_tps() { return 0;}

  /// get dispatch progress and dispatch info of current LS
  virtual int get_dispatch_progress(
      const share::ObLSID &ls_id,
      int64_t &progress,
      logfetcher::PartTransDispatchInfo &dispatch_info);
  };
private:
  typedef common::RWLock RWLock;
  typedef RWLock::RLockGuard RLockGuard;
  typedef RWLock::WLockGuard WLockGuard;
  mutable RWLock lock_;
  bool stop_flag_;     // flag marks tenant threads stop
  share::ObRestoreSourceServiceAttr source_;

  // components for log fetcher
  ObRestoreLogFunction restore_function_;
  LogErrHandler error_handler_;
  logfetcher::ObLogFetcherLSCtxDefaultFactory ls_ctx_factory_;
  LogFetcherLSCtxAddInfoFactory ls_ctx_add_info_factory_;
  logfetcher::ObLogFetcherConfig cfg_;

  // LS resource in Fetcher can be freed only before fetcher stops,
  // so in the tenant drop scene, remove all ls from Fetcher and set fetcher stop.
  logfetcher::ObLogFetcher *fetcher_;

private:
  DISALLOW_COPY_AND_ASSIGN(ObLogRestoreNetDriver);
};
} // namespace logservice
} // namespace oceanbase
#endif /* OCEANBASE_LOGSERVICE_OB_LOG_RESTORE_NET_DRIVER_H_ */
