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

#define USING_LOG_PREFIX SHARE

#include "ob_tablet_autoincrement_service.h"
#include "storage/ob_storage_rpc.h"
#include "share/rc/ob_module_provider.h"
#include "logservice/ob_log_service.h"
#include "storage/ob_tablet_autoinc_seq_rpc_handler.h"

namespace oceanbase
{
namespace share
{

int ObTabletAutoincMgr::init(const common::ObTabletID &tablet_id, const int64_t cache_size)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("tablet autoinc mgr init twice", K_(is_inited), K(tablet_id));
  } else {
    tablet_id_ = tablet_id;
    cache_size_ = cache_size;
    is_inited_ = true;
  }
  return ret;
}

int ObTabletAutoincMgr::set_interval(const ObTabletAutoincParam &param, ObTabletCacheInterval &interval)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("tablet autoinc mgr is not inited", K(ret));
  } else if (next_value_ + interval.cache_size_ - 1 > curr_node_.cache_end_) {
    if (prefetch_node_.is_valid()) {
      curr_node_.cache_start_ = prefetch_node_.cache_start_;
      curr_node_.cache_end_ = prefetch_node_.cache_end_;
      prefetch_node_.reset();
    } else {
      ret = OB_SIZE_OVERFLOW;
    }
  }

  if (OB_SUCC(ret)) {
    if (next_value_ < curr_node_.cache_start_) {
      next_value_ = curr_node_.cache_start_;
    }
    const uint64_t start = next_value_;
    const uint64_t end = MIN(curr_node_.cache_end_, start + interval.cache_size_ - 1);
    next_value_ = end + 1;
    interval.set(start, end);
  }
  return ret;
}

int ObTabletAutoincMgr::fetch_interval(const ObTabletAutoincParam &param, ObTabletCacheInterval &interval)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("tablet autoinc mgr is not inited", K(ret));
  } else {
    const int64_t TRY_LOCK_INTERVAL = 1000L; // 1ms
    while (true) {
      if (OB_SUCCESS != mutex_.trylock()) {
        ob_usleep<common::ObWaitEventIds::STORAGE_AUTOINC_FETCH_CONFLICT_SLEEP>(TRY_LOCK_INTERVAL);
        THIS_WORKER.sched_run();
      } else {
        break;
      }
    }
    last_refresh_ts_ = ObTimeUtility::current_time();
    // TODO(shuangcan): may need to optimize the lock performance here
    if (OB_SUCC(set_interval(param, interval))) {
      if (prefetch_condition()) {
        if (OB_FAIL(fetch_new_range(param, tablet_id_, prefetch_node_))) {
          LOG_WARN("failed to prefetch tablet node", K(param), K(ret));
        }
      }
    } else if (OB_SIZE_OVERFLOW == ret) {
      if (OB_FAIL(fetch_new_range(param, tablet_id_, curr_node_))) {
        LOG_WARN("failed to fetch tablet node", K(param), K(ret));
      } else if (OB_FAIL(set_interval(param, interval))) {
        LOG_WARN("failed to alloc cache handle", K(param), K(ret));
      }
    }
    mutex_.unlock();
  }
  return ret;
}

int ObTabletAutoincMgr::fetch_interval_without_cache(const ObTabletAutoincParam &param, ObTabletCacheInterval &interval)
{
  int ret = OB_SUCCESS;
  lib::ObMutexGuard guard(mutex_);
  ObTabletCacheNode node;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("tablet autoinc mgr is not inited", K(ret));
  } else if (OB_FAIL(fetch_new_range(param, tablet_id_, node))) {
    LOG_WARN("failed to fetch tablet node", K(param), K(ret));
  } else {
    interval.set(node.cache_start_, node.cache_end_);
  }
  return ret;
}

int ObTabletAutoincMgr::fetch_new_range(const ObTabletAutoincParam &param,
                                        const common::ObTabletID &tablet_id,
                                        ObTabletCacheNode &node)
{
  int ret = OB_SUCCESS;
  share::ObLocationService *location_service = nullptr;
  ObAddr leader_addr;
  bool is_cache_hit = false;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("tablet auto increment service is not inited", K(ret), K(param), K(tablet_id));
  } else if (OB_ISNULL(location_service = GCTX.location_service_)) {
    ret = OB_ERR_SYS;
    LOG_WARN("location_cache is null", K(ret), KP(location_service));
  } else {
    obcall::ObFetchTabletSeqArg arg;
    obcall::ObFetchTabletSeqRes res;
    arg.cache_size_ = MAX(cache_size_, param.auto_increment_cache_size_); // TODO(shuangcan): confirm this
    
    arg.tablet_id_ = tablet_id;
    // arg.ls_id_ will be filled by location_service->get

    bool finish = false;
    for (int64_t retry_times = 0; OB_SUCC(ret) && !finish; retry_times++) {
      const int64_t rpc_timeout = THIS_WORKER.is_timeout_ts_valid() ? THIS_WORKER.get_timeout_remain() : OB_DEFAULT_RPC_TIMEOUT;
      if (OB_FAIL(location_service->get(tablet_id, 0/*expire_renew_time*/, is_cache_hit, arg.ls_id_))) {
        LOG_WARN("fail to get log stream id", K(ret), K(tablet_id));
      } else if (OB_FAIL(location_service->get_leader(GCONF.cluster_id,
                                                      arg.ls_id_,
                                                      false,/*force_renew*/
                                                      leader_addr))) {
        LOG_WARN("get leader failed", K(ret), K(arg.ls_id_));
      } else if (OB_FAIL(ObTabletAutoincSeqRpcHandler::get_instance().fetch_tablet_autoinc_seq_cache(arg, res))) {
        LOG_WARN("fail to fetch autoinc cache for tablets", K(ret), K(retry_times), K(arg), K(rpc_timeout));
      }
      if (OB_SUCC(ret)) {
        finish = true;
      }
      if (OB_FAIL(ret)) {
        if (is_retryable(ret)) {
          // overwrite ret
          if (OB_UNLIKELY(rpc_timeout <= 0)) {
            ret = OB_TIMEOUT;
            LOG_WARN("timeout", K(ret), K(rpc_timeout));
          } else if (OB_FAIL(THIS_WORKER.check_status())) {
            LOG_WARN("failed to check status", K(ret));
          } else {
            res.reset();
            ob_usleep<common::ObWaitEventIds::STORAGE_AUTOINC_FETCH_RETRY_SLEEP>(RETRY_INTERVAL);
          }
        }
      }
    }

    if (OB_SUCC(ret)) {
      node.cache_start_ = res.cache_interval_.start_;
      node.cache_end_ = res.cache_interval_.end_;
      if (node.cache_end_ == 0) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to get autoinc cache", K(ret));
      } else {
        LOG_INFO("fetch new range success", K(tablet_id), K(node));
      }
    }
  }


  return ret;
}

ObTabletAutoincrementService::ObTabletAutoincrementService()
  : is_inited_(false), node_allocator_(), tablet_autoinc_mgr_map_(), init_node_mutexs_()
{
}

ObTabletAutoincrementService::~ObTabletAutoincrementService()
{
}

int ObTabletAutoincrementService::acquire_mgr(const common::ObTabletID &tablet_id,
    const int64_t init_cache_size,
    ObTabletAutoincMgr *&autoinc_mgr)
{
  int ret = OB_SUCCESS;
  ObTabletAutoincKey key;
  
  key.tablet_id_ = tablet_id;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("tablet auto increment service is not inited", K(ret), K(key));
  } else if (OB_UNLIKELY(!key.is_valid() || nullptr != autoinc_mgr)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(key));
  } else if (OB_FAIL(tablet_autoinc_mgr_map_.get(key, autoinc_mgr))) {
    if (OB_ENTRY_NOT_EXIST != ret) {
      LOG_WARN("get from map failed", K(ret));
    } else {
      lib::ObMutex &mutex = init_node_mutexs_[key.tablet_id_.id() % INIT_NODE_MUTEX_NUM];
      lib::ObMutexGuard guard(mutex);
      if (OB_ENTRY_NOT_EXIST == (ret = tablet_autoinc_mgr_map_.get(key, autoinc_mgr))) {
        if (OB_FAIL(tablet_autoinc_mgr_map_.alloc_value(autoinc_mgr))) {
          LOG_WARN("failed to alloc table mgr", K(ret));
        } else if (OB_FAIL(autoinc_mgr->init(key.tablet_id_, init_cache_size))) {
          LOG_WARN("fail to init tablet autoinc mgr", K(ret), K(key));
        } else if (OB_FAIL(tablet_autoinc_mgr_map_.insert_and_get(key, autoinc_mgr))) {
          LOG_WARN("failed to create table node", K(ret));
        }
        if (OB_FAIL(ret) && autoinc_mgr != nullptr) {
          tablet_autoinc_mgr_map_.free_value(autoinc_mgr);
          autoinc_mgr = nullptr;
        }
      }
    }
  }
  return ret;
}

void ObTabletAutoincrementService::release_mgr(ObTabletAutoincMgr *autoinc_mgr)
{
  tablet_autoinc_mgr_map_.revert(autoinc_mgr);
  return;
}

int ObTabletAutoincrementService::get_autoinc_seq(const common::ObTabletID &tablet_id, uint64_t &autoinc_seq, const int64_t auto_increment_cache_size)
{
  ACTIVE_SESSION_FLAG_SETTER_GUARD(in_sequence_load);
  int ret = OB_SUCCESS;
  ObTabletAutoincParam param;
  
  ObTabletAutoincMgr *autoinc_mgr = nullptr;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("tablet auto increment service is not inited", K(ret));
  } else if (OB_FAIL(acquire_mgr(tablet_id, auto_increment_cache_size, autoinc_mgr))) {
    LOG_WARN("failed to acquire mgr", K(ret));
  } else {
    ObTabletCacheInterval interval(tablet_id, 1/*cache size*/);
    if (OB_ISNULL(autoinc_mgr)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("autoinc mgr is unexpected null", K(ret));
    } else if (OB_FAIL(autoinc_mgr->fetch_interval(param, interval))) {
      LOG_WARN("fail to fetch interval", K(ret), K(param));
    } else if (OB_FAIL(interval.next_value(autoinc_seq))) {
      LOG_WARN("fail to get next value", K(ret));
    }
  }
  if (nullptr != autoinc_mgr) {
    release_mgr(autoinc_mgr);
  }
  return ret;
}

ObTabletAutoincrementService &ObTabletAutoincrementService::get_instance()
{
  static ObTabletAutoincrementService autoinc_service;
  return autoinc_service;
}

int ObTabletAutoincrementService::init()
{
  int ret = OB_SUCCESS;
  lib::ObMemAttr attr("AutoincMgr");
  SET_USE_500(attr);
  if (OB_FAIL(node_allocator_.init(sizeof(ObTabletAutoincMgr), ObModIds::OB_AUTOINCREMENT))) {
    LOG_WARN("failed to init table node allocator", K(ret));
  } else if (OB_FAIL(tablet_autoinc_mgr_map_.init(attr))) {
    LOG_WARN("failed to init table node map", K(ret));
  } else {
    for (int64_t i = 0; i < INIT_NODE_MUTEX_NUM; ++i) {
      init_node_mutexs_[i].set_latch_id(common::ObLatchIds::TABLET_AUTO_INCREMENT_SERVICE_LOCK);
    }
    is_inited_ = true;
  }
  return ret;
}

void ObTabletAutoincrementService::destroy()
{
  tablet_autoinc_mgr_map_.destroy();
  node_allocator_.destroy();
}

int ObTabletAutoincrementService::get_tablet_cache_interval(ObTabletCacheInterval &interval)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("tablet auto increment service is not inited", K(ret));
  } else {
    const int64_t auto_increment_cache_size = MAX(interval.cache_size_, 10000); //TODO(shuangcan): fix me
    ObTabletAutoincParam param;
    
    param.auto_increment_cache_size_ = auto_increment_cache_size;
    ObTabletAutoincMgr *autoinc_mgr = nullptr;
    if (OB_FAIL(acquire_mgr(interval.tablet_id_, auto_increment_cache_size, autoinc_mgr))) {
      LOG_WARN("failed to acquire mgr", K(ret));
    } else if (OB_ISNULL(autoinc_mgr)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("autoinc mgr is unexpected null", K(ret));
    } else if (OB_FAIL(autoinc_mgr->fetch_interval_without_cache(param, interval))) {
      LOG_WARN("fail to fetch interval", K(ret), K(param));
    }
    if (nullptr != autoinc_mgr) {
      release_mgr(autoinc_mgr);
    }
  }

  return ret;
}

int ObTabletAutoincrementService::clear_tablet_autoinc_seq_cache(const common::ObIArray<common::ObTabletID> &tablet_ids,
    const int64_t abs_timeout_us)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("tablet auto increment service is not inited", K(ret));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < tablet_ids.count(); i++) {
    ObTabletAutoincKey key;
    
    key.tablet_id_ = tablet_ids.at(i);
    lib::ObMutex &mutex = init_node_mutexs_[key.tablet_id_.id() % INIT_NODE_MUTEX_NUM];
    lib::ObMutexGuardWithTimeout guard(mutex, abs_timeout_us);
    if (OB_FAIL(guard.get_ret())) {
      LOG_WARN("failed to lock", K(ret));
    } else if (OB_FAIL(tablet_autoinc_mgr_map_.del(key))) {
      if (OB_ENTRY_NOT_EXIST == ret) {
        ret = OB_SUCCESS;
      } else {
        LOG_WARN("failed to del tablet autoinc", K(ret), K(key));
      }
    }
  }
  return ret;
}

int ObTabletAutoincCacheCleaner::add_single_table(const schema::ObSimpleTableSchemaV2 &table_schema)
{
  int ret = OB_SUCCESS;
  if (table_schema.is_table_with_hidden_pk_column() || table_schema.is_aux_lob_meta_table()) {
    
    ObArray<ObTabletID> tablet_ids;
    if (OB_UNLIKELY(false)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("tenant id mismatch", K(ret));
    } else if (OB_FAIL(table_schema.get_tablet_ids(tablet_ids))) {
      LOG_WARN("failed to get tablet ids", K(ret));
    } else if (OB_FAIL(append(tablet_ids_, tablet_ids))) {
      LOG_WARN("failed to append tablet ids", K(ret));
    }
  }
  return ret;
}

// add user table and its related tables that use tablet autoinc, e.g., lob meta table
int ObTabletAutoincCacheCleaner::add_table(schema::ObSchemaGetterGuard &schema_guard, const schema::ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(add_single_table(table_schema))) {
    LOG_WARN("failed to add single table", K(ret));
  }

  if (OB_SUCC(ret)) {
    const uint64_t lob_meta_tid = table_schema.get_aux_lob_meta_tid();
    if (OB_INVALID_ID != lob_meta_tid) {
      const ObTableSchema *lob_meta_table_schema = nullptr;
      if (OB_FAIL(schema_guard.get_table_schema( lob_meta_tid, lob_meta_table_schema))) {
        LOG_WARN("failed to get aux table schema", K(ret), K(lob_meta_tid));
      } else if (OB_ISNULL(lob_meta_table_schema)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("invalid table schema", K(ret), K(lob_meta_tid));
      } else if (OB_FAIL(add_single_table(*lob_meta_table_schema))) {
        LOG_WARN("failed to add single table", K(ret));
      }
    }
  }
  return ret;
}

int ObTabletAutoincCacheCleaner::add_database(const schema::ObDatabaseSchema &database_schema)
{
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard schema_guard;
  
  const uint64_t database_id = database_schema.get_database_id();
  ObArray<const ObSimpleTableSchemaV2 *> table_schemas;
  ObMultiVersionSchemaService &schema_service = ObMultiVersionSchemaService::get_instance();
  if (OB_FAIL(schema_service.get_tenant_schema_guard(schema_guard))) {
    LOG_WARN("fail to get schema guard", KR(ret));
  } else if (OB_FAIL(schema_guard.get_table_schemas_in_database(database_id,
                                                                table_schemas))) {
    LOG_WARN("fail to get table ids in database", K(1UL), K(database_id), K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < table_schemas.count(); i++) {
      const ObSimpleTableSchemaV2 *table_schema = table_schemas.at(i);
      if (OB_ISNULL(table_schema)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("table schema should not be null", K(ret));
      } else if (OB_FAIL(add_single_table(*table_schema))) {
        LOG_WARN("fail to lock_table", KR(ret), KPC(table_schema));
      }
    }
  }
  return ret;
}

int ObTabletAutoincCacheCleaner::commit(const int64_t timeout_us)
{
  int ret = OB_SUCCESS;
  ObTimeGuard time_guard("ObTabletAutoincCacheCleaner", 1 * 1000 * 1000);
  ObTabletAutoincrementService &tablet_autoinc_service = share::ObTabletAutoincrementService::get_instance();
  uint64_t data_version = 0;
  common::ObZone zone;
  common::ObSEArray<common::ObAddr, 8> server_list;
  ObUnitInfoGetter ui_getter;
  obcall::ObClearTabletAutoincSeqCacheArg arg;
  const ObLSID unused_ls_id = SYS_LS;
  int64_t abs_timeout_us = ObTimeUtility::current_time() + timeout_us;
  const ObTimeoutCtx &ctx = ObTimeoutCtx::get_ctx();
  if (THIS_WORKER.is_timeout_ts_valid()) {
    abs_timeout_us = std::min(abs_timeout_us, THIS_WORKER.get_timeout_ts());
  }
  if (ctx.is_timeout_set()) {
    abs_timeout_us = std::min(abs_timeout_us, ctx.get_abs_timeout());
  }
  if (ctx.is_trx_timeout_set()) {
    abs_timeout_us = std::min(abs_timeout_us, ObTimeUtility::current_time() + ctx.get_trx_timeout_us());
  }

  if (OB_ISNULL(GCTX.sql_proxy_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("sql_proxy in GCTX is null", K(ret), K(GCTX.sql_proxy_));
  } else if (OB_FAIL(ui_getter.init(*GCTX.sql_proxy_, &GCONF))) {
    LOG_WARN("init unit info getter failed", K(ret));
  } else if (OB_FAIL(ui_getter.get_tenant_servers(server_list))) {
    LOG_WARN("get tenant servers failed", K(ret));
  } else if (OB_FAIL(arg.init(tablet_ids_, unused_ls_id))) {
    LOG_WARN("failed to init clear tablet autoinc arg", K(ret));
  } else {
    // seekdb: all servers are local, call handler directly.
    if (OB_FAIL(tablet_autoinc_service.clear_tablet_autoinc_seq_cache(tablet_ids_, abs_timeout_us))) {
      LOG_WARN("failed to clear tablet autoinc", K(ret));
    }
  }
  return ret;
}

}
}
