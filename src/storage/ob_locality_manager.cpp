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

#include "storage/ob_locality_manager.h"
#include "observer/ob_server_struct.h"

namespace oceanbase
{
using namespace common;
using namespace share;
namespace storage
{
ObLocalityManager::ObLocalityManager()
  : rwlock_(ObLatchIds::SERVER_LOCALITY_MGR_LOCK),
    ssl_invited_nodes_buf_(NULL)
{
  reset();
  ssl_invited_nodes_buf_ = new (std::nothrow) char[common::OB_MAX_CONFIG_VALUE_LEN];
  ssl_invited_nodes_buf_[0]  = '\0';
}

void ObLocalityManager::reset()
{
  is_inited_ = false;
  self_.reset();
  sql_proxy_ = NULL;
  is_loaded_ = false;
}

void ObLocalityManager::destroy()
{
  if (NULL != ssl_invited_nodes_buf_) {
    delete[] ssl_invited_nodes_buf_;
    ssl_invited_nodes_buf_ = NULL;
  }
  if (is_inited_) {
    is_inited_ = false;
    TG_DESTROY(lib::TGDefIDs::LocalityReload);
    refresh_locality_task_queue_.destroy();
    STORAGE_LOG(INFO, "ObLocalityManager destroy finished");
  }
}

int ObLocalityManager::init(const ObAddr &self, ObMySQLProxy *sql_proxy)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    STORAGE_LOG(WARN, "ObLocalityManager init twice", K(ret));
  } else if (!self.is_valid() || OB_ISNULL(sql_proxy)) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "invalid argument", K(ret), K(self), KP(sql_proxy));
  } else if (OB_FAIL(refresh_locality_task_queue_.init(1,
                                                       "LocltyRefTask",
                                                       REFRESH_LOCALITY_TASK_NUM,
                                                       REFRESH_LOCALITY_TASK_NUM))) {
    STORAGE_LOG(WARN, "fail to initialize refresh locality task queue", K(ret));
  } else if (OB_FAIL(reload_locality_task_.init(this))) {
    STORAGE_LOG(WARN, "init reload locality task failed", K(ret));
  } else if (OB_FAIL(TG_START(lib::TGDefIDs::LocalityReload))) {
    STORAGE_LOG(WARN, "fail to initialize locality timer");
  } else {
    self_ = self;
    sql_proxy_ = sql_proxy;
    is_inited_ = true;
  }
  return ret;
}

int ObLocalityManager::start()
{
  int ret = OB_SUCCESS;
  bool repeat = true;
  STORAGE_LOG(INFO, "start locality manager");
  if (OB_UNLIKELY(!is_inited_)) {
    STORAGE_LOG(ERROR, "locality manager not inited, cannot start.");
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(TG_SCHEDULE(lib::TGDefIDs::LocalityReload,
                                 reload_locality_task_,
                                 RELOAD_LOCALITY_INTERVAL,
                                 repeat))) {
    STORAGE_LOG(ERROR, "fail to schedule reload locality task");
  }
  return ret;
}

int ObLocalityManager::stop()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(ERROR, "locality manager not inited, cannot stop.", K(ret));
  } else {
    TG_STOP(lib::TGDefIDs::LocalityReload);
    refresh_locality_task_queue_.stop();
  }
  return ret;
}

int ObLocalityManager::wait()
{
  int ret = OB_SUCCESS;
  TG_WAIT(lib::TGDefIDs::LocalityReload);
  refresh_locality_task_queue_.wait();
  return ret;
}

int ObLocalityManager::is_server_legitimate(const ObAddr& addr, bool& is_valid)
{
  int ret = OB_SUCCESS;
  is_valid = true;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "ObLocalityManager not init", K(ret));
  } else {
    is_valid = true;
  }
  return ret;
}

void ObLocalityManager::set_ssl_invited_nodes(const common::ObString &new_value)
{
  if (OB_LIKELY(NULL != (ssl_invited_nodes_buf_))
      && 0 != new_value.case_compare(ssl_invited_nodes_buf_)) {
    SpinWLockGuard guard(rwlock_);
    if (new_value.empty() || new_value.length() >= common::OB_MAX_CONFIG_VALUE_LEN - 1) {
      ssl_invited_nodes_buf_[0] = '\0';
    } else {
      MEMCPY(ssl_invited_nodes_buf_, new_value.ptr(), new_value.length());
      ssl_invited_nodes_buf_[new_value.length()] =  '\0';
    }
  }
  STORAGE_LOG(INFO, "set_ssl_invited_nodes", K(new_value));
}

int ObLocalityManager::load_region()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "ObLocalityManager not init", K(ret));
  } else {
    //Firstly, check it's need to warn
    check_if_locality_has_been_loaded();
    if (!is_loaded_) {
      is_loaded_ = true;
    }
  }
  return ret;
}

int ObLocalityManager::get_server_zone_type(const common::ObAddr &server,
                                         common::ObZoneType &zone_type) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "ObLocalityManager not init", KR(ret));
  } else {
    zone_type = ObZoneType::ZONE_TYPE_READWRITE;
  }
  return ret;
}

int ObLocalityManager::get_server_region(const common::ObAddr &server,
                                         common::ObRegion &region) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "ObLocalityManager not init", K(ret));
  } else if (!server.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "invalid argument", K(ret), K(server));
  } else {
    region = DEFAULT_REGION_NAME;
  }
  return ret;
}


int ObLocalityManager::get_server_zone(const common::ObAddr &server,
                                       common::ObZone &zone) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "ObLocalityManager not init", K(ret));
  } else if (!server.is_valid() || OB_ISNULL(GCTX.config_)) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "invalid argument", K(ret), K(server), KP(GCTX.config_));
  } else {
    zone = GCTX.config_->zone.str();
  }
  return ret;
}


int ObLocalityManager::get_server_idc(const common::ObAddr &server,
                                      common::ObIDC &idc) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "ObLocalityManager not init", K(ret));
  } else if (!server.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "invalid argument", K(ret), K(server));
  } else {
    // do nothing
  }
  return ret;
}




int ObLocalityManager::is_local_server(const ObAddr &server, bool &is_local)
{
  int ret = OB_SUCCESS;
  is_local = false;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "ObLocalityManager not init", K(ret));
  } else if (server == GCTX.self_addr()) {
    is_local = true;
  }
  return ret;
}

int ObLocalityManager::is_same_zone(const common::ObAddr &server, bool &is_same_zone)
{
  int ret = OB_SUCCESS;
  ObZone self_zone;
  ObZone svr_zone;
  is_same_zone = false;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "ObLocalityManager not init", K(ret));
  } else if (!server.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "invalid argument", K(ret), K(server));
  } else if (OB_FAIL(get_server_zone(self_, self_zone))) {
    STORAGE_LOG(WARN, "fail to get self zone", K(ret), K(self_));
  } else if (OB_FAIL(get_server_zone(server, svr_zone))) {
    STORAGE_LOG(WARN, "fail to get server zone", K(ret), K(server));
  } else if (self_zone == svr_zone) {
    is_same_zone = true;
  }
  return ret;
}

int ObLocalityManager::check_if_locality_has_been_loaded()
{
  int ret = OB_SUCCESS;
  SpinRLockGuard guard(rwlock_);
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "ObLocalityManager not init", K(ret));
  } else if (!is_loaded_) {
    const int64_t start_service_time = GCTX.start_service_time_;
    if (start_service_time > 0) {
      const int64_t now = ObTimeUtility::current_time();
      // When the observer starts and a very long time has passed without cache being refreshed, an error needs to be reported
      if (now - start_service_time > FAIL_TO_LOAD_LOCALITY_CACHE_TIMEOUT) {
        ret = OB_ERR_UNEXPECTED;
        STORAGE_LOG(WARN, "fail to load first cache since service started!",
                    K(ret), K(now), K(start_service_time));
      }
    }
  }
  return ret;
}

int ObLocalityManager::is_local_zone_read_only(bool &is_readonly)
{
  int ret = OB_SUCCESS;
  is_readonly = false;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "ObLocalityManager not init", K(ret));
  }
  return ret;
}

int ObLocalityManager::add_refresh_locality_task()
{
  int ret = OB_SUCCESS;
  ObRefreshLocalityTask task(this);
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "locality is not initialized", K(ret));
  } else if (OB_FAIL(refresh_locality_task_queue_.add_task(task))) {
    if (OB_EAGAIN != ret) {
      STORAGE_LOG(WARN, "add refresh locality task failed", K(ret));
    }
  }
  return ret;
}

ObLocalityManager::ReloadLocalityTask::ReloadLocalityTask()
  : is_inited_(false),
    locality_mgr_(NULL)
{
}

int ObLocalityManager::ReloadLocalityTask::init(ObLocalityManager *locality_mgr)
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
    STORAGE_LOG(WARN, "ReloadLocalityTask init twice", K(ret));
  } else if (OB_ISNULL(locality_mgr)) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "invalid argument", K(ret), KP(locality_mgr));
  } else {
    is_inited_ = true;
    locality_mgr_ = locality_mgr;
  }

  return ret;
}


void ObLocalityManager::ReloadLocalityTask::runTimerTask()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "ReloadLocalityTask not init", K(ret));
  } else if (OB_FAIL(locality_mgr_->add_refresh_locality_task())) {
    STORAGE_LOG(WARN, "runTimer to refresh locality_info fail", K(ret));
  } else {
    STORAGE_LOG(INFO, "runTimer to refresh locality_info", K(ret));
  }
}

ObLocalityManager::ObRefreshLocalityTask::ObRefreshLocalityTask(
    ObLocalityManager *locality_mgr)
    : IObDedupTask(T_REFRESH_LOCALITY),
      locality_mgr_(locality_mgr)
{
}

ObLocalityManager::ObRefreshLocalityTask::~ObRefreshLocalityTask()
{
}

int64_t ObLocalityManager::ObRefreshLocalityTask::hash() const
{
  uint64_t hash_val = 0;
  return static_cast<int64_t>(hash_val);
}

bool ObLocalityManager::ObRefreshLocalityTask::operator ==(const IObDedupTask &other) const
{
  UNUSED(other);
  bool b_ret = true;
  return b_ret;
}

int64_t ObLocalityManager::ObRefreshLocalityTask::get_deep_copy_size() const
{
  return sizeof(*this);
}

IObDedupTask *ObLocalityManager::ObRefreshLocalityTask::deep_copy(
    char *buffer,
    const int64_t buf_size) const
{
  ObRefreshLocalityTask *task = NULL;
  if (OB_UNLIKELY(OB_ISNULL(buffer))
      || OB_UNLIKELY(buf_size < get_deep_copy_size())) {
    STORAGE_LOG_RET(WARN, OB_INVALID_ARGUMENT, "invalid argument", KP(buffer), K(buf_size));
  } else {
    task = new(buffer) ObRefreshLocalityTask(locality_mgr_);
  }
  return task;
}

int ObLocalityManager::ObRefreshLocalityTask::process()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(locality_mgr_)) {
    ret = OB_ERR_UNEXPECTED;
    STORAGE_LOG(WARN, "locality manager is null", K(ret));
  } else if (OB_FAIL(locality_mgr_->load_region())) {
    STORAGE_LOG(WARN, "process refresh locality task fail", K(ret));
  }
  return ret;
}

}// storage
}// oceanbase
