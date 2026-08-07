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
#include "common/ob_timeout_ctx.h"
#include "share/rc/ob_server_runtime.h"
#include "storage/ob_tablet_autoinc_seq_service.h"
#include "storage/ls/ob_ls.h"
#include "storage/tablet/ob_tablet_autoincrement_state.h"
#include "storage/tablet/ob_tablet_fork_mds_helper.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "share/schema/ob_schema_guard_wrapper.h"

namespace oceanbase
{
namespace share
{

namespace
{

int get_system_tablet_handle(
    const common::ObTabletID &tablet_id,
    storage::ObTabletHandle &tablet_handle)
{
  int ret = OB_SUCCESS;
  storage::ObLSService *ls_service =
      ::oceanbase::share::server_service<::oceanbase::storage::ObLSService>();
  storage::ObLS *ls = nullptr;
  if (OB_ISNULL(ls_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls service is null", K(ret));
  } else if (OB_FAIL(ls_service->get_ls(ls))) {
    LOG_WARN("get ls failed", K(ret));
  } else if (OB_ISNULL(ls)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls is null", K(ret));
  } else if (OB_FAIL(ls->get_tablet(tablet_id, tablet_handle))) {
    LOG_WARN("failed to get tablet", K(ret), K(tablet_id));
  }
  return ret;
}

} // namespace

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
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("tablet auto increment service is not inited", K(ret), K(param), K(tablet_id));
  } else {
    ObTabletAutoincInterval interval;
    const uint64_t range_size = MAX(cache_size_, param.auto_increment_cache_size_);
    bool finish = false;
    for (int64_t retry_times = 0; OB_SUCC(ret) && !finish; retry_times++) {
      const int64_t timeout = THIS_WORKER.is_timeout_ts_valid()
          ? THIS_WORKER.get_timeout_remain() : OB_DEFAULT_RPC_TIMEOUT;
      if (OB_FAIL(storage::ObTabletAutoincSeqService::get_instance().fetch_tablet_autoinc_seq_cache(
          tablet_id, range_size, interval))) {
        LOG_WARN("fail to fetch local autoinc cache for tablet",
            K(ret), K(tablet_id), K(range_size), K(retry_times), K(timeout));
      } else {
        finish = true;
      }
      if (OB_FAIL(ret) && is_retryable(ret)) {
        // Overwrite a retryable error when the request is still runnable so that
        // the next loop can retry the local log submission.
        if (OB_UNLIKELY(timeout <= 0)) {
          ret = OB_TIMEOUT;
          LOG_WARN("timeout while fetching local autoinc cache", K(ret), K(timeout));
        } else if (OB_FAIL(THIS_WORKER.check_status())) {
          LOG_WARN("failed to check status", K(ret));
        } else {
          interval.reset();
          ob_usleep<common::ObWaitEventIds::STORAGE_AUTOINC_FETCH_RETRY_SLEEP>(RETRY_INTERVAL);
        }
      }
    }

    if (OB_SUCC(ret)) {
      node.cache_start_ = interval.start_;
      node.cache_end_ = interval.end_;
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
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("tablet autoincrement service init twice", K(ret));
  } else if (OB_FAIL(node_allocator_.init(sizeof(ObTabletAutoincMgr), ObModIds::OB_AUTOINCREMENT))) {
    LOG_WARN("failed to init table node allocator", K(ret));
  } else if (OB_FAIL(tablet_autoinc_mgr_map_.init(attr))) {
    LOG_WARN("failed to init table node map", K(ret));
  } else if (OB_FAIL(storage::ObTabletAutoincSeqService::get_instance().init())) {
    LOG_WARN("failed to init local tablet autoinc sequence service", K(ret));
  } else {
    for (int64_t i = 0; i < INIT_NODE_MUTEX_NUM; ++i) {
      init_node_mutexs_[i].set_latch_id(common::ObLatchIds::TABLET_AUTO_INCREMENT_SERVICE_LOCK);
    }
    is_inited_ = true;
  }
  if (OB_FAIL(ret)) {
    tablet_autoinc_mgr_map_.destroy();
    node_allocator_.destroy();
  }
  return ret;
}

void ObTabletAutoincrementService::destroy()
{
  storage::ObTabletAutoincSeqService::get_instance().destroy();
  tablet_autoinc_mgr_map_.destroy();
  node_allocator_.destroy();
  is_inited_ = false;
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

int ObTabletAutoincrementService::copy_sequences_for_fork(
    const common::ObIArray<common::ObTabletID> &source_tablet_ids,
    const common::ObIArray<common::ObTabletID> &destination_tablet_ids,
    common::ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  obcall::ObBatchSetTabletAutoincSeqArg arg;
  common::ObArenaAllocator allocator("ForkAutoinc");

  if (OB_UNLIKELY(source_tablet_ids.empty()
      || source_tablet_ids.count() != destination_tablet_ids.count())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid tablet pairs for sequence copy", K(ret),
        K(source_tablet_ids.count()), K(destination_tablet_ids.count()));
  } else {
    arg.is_tablet_creating_ = true;
  }

  for (int64_t i = 0; OB_SUCC(ret) && i < source_tablet_ids.count(); ++i) {
    allocator.reuse();
    const common::ObTabletID &source_tablet_id = source_tablet_ids.at(i);
    const common::ObTabletID &destination_tablet_id = destination_tablet_ids.at(i);
    storage::ObTabletHandle tablet_handle;
    storage::ObTabletAutoincSeq autoinc_seq;
    ObTabletAutoincSeqCopyParam param;
    param.src_tablet_id_ = source_tablet_id;
    param.dest_tablet_id_ = destination_tablet_id;
    param.ret_code_ = OB_SUCCESS;

    if (OB_UNLIKELY(!source_tablet_id.is_valid()
        || !destination_tablet_id.is_valid())) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid tablet pair for sequence copy", K(ret),
          K(source_tablet_id), K(destination_tablet_id), K(i));
    } else if (OB_FAIL(get_system_tablet_handle(source_tablet_id, tablet_handle))) {
      LOG_WARN("failed to get source tablet", K(ret), K(source_tablet_id));
    } else if (OB_ISNULL(tablet_handle.get_obj())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("tablet handle is null", K(ret), K(source_tablet_id));
    } else if (OB_FAIL(tablet_handle.get_obj()->get_autoinc_seq(
                   autoinc_seq, allocator))) {
      LOG_WARN("failed to get autoincrement sequence", K(ret),
          K(source_tablet_id));
    } else if (OB_FAIL(autoinc_seq.get_autoinc_seq_value(param.autoinc_seq_))) {
      LOG_WARN("failed to get autoincrement sequence value", K(ret),
          K(source_tablet_id));
    } else if (OB_FAIL(arg.autoinc_params_.push_back(param))) {
      LOG_WARN("failed to append autoincrement sequence", K(ret), K(param));
    }
  }

  if (OB_SUCC(ret)) {
    storage::ObTabletForkMdsArg fork_mds_arg;
    if (OB_FAIL(fork_mds_arg.set_autoinc_seq_arg(arg))) {
      LOG_WARN("failed to set autoincrement fork arg", K(ret), K(arg));
    } else if (OB_FAIL(storage::ObTabletForkMdsHelper::register_mds(
                   fork_mds_arg, false /*need_flush_redo*/, trans))) {
      LOG_WARN("failed to register fork MDS for autoincrement sequences",
          K(ret));
    } else {
      LOG_INFO("registered fork MDS for autoincrement sequences",
          K(arg.autoinc_params_.count()));
    }
  }
  return ret;
}

int ObTabletAutoincrementService::read_migration_sequences(
    const common::ObIArray<ObTabletAutoincSeqCopyParam> &request_params,
    common::ObIArray<ObTabletAutoincSeqCopyParam> &result_params)
{
  int ret = OB_SUCCESS;
  result_params.reuse();
  if (OB_UNLIKELY(request_params.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid sequence migration read", K(ret), K(request_params.count()));
  } else if (OB_FAIL(result_params.assign(request_params))) {
    LOG_WARN("failed to copy sequence migration request", K(ret));
  } else if (OB_FAIL(storage::ObTabletAutoincSeqService::get_instance()
                         .batch_get_tablet_autoinc_seq(result_params))) {
    LOG_WARN("failed to read migration sequences", K(ret));
  }
  return ret;
}

int ObTabletAutoincrementService::write_migration_sequences(
    const common::ObIArray<ObTabletAutoincSeqCopyParam> &request_params,
    common::ObIArray<ObTabletAutoincSeqCopyParam> &result_params)
{
  int ret = OB_SUCCESS;
  result_params.reuse();
  if (OB_UNLIKELY(request_params.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid sequence migration write", K(ret), K(request_params.count()));
  } else if (OB_FAIL(result_params.assign(request_params))) {
    LOG_WARN("failed to copy sequence migration request", K(ret));
  } else if (OB_FAIL(storage::ObTabletAutoincSeqService::get_instance()
                         .batch_set_tablet_autoinc_seq(result_params))) {
    LOG_WARN("failed to write migration sequences", K(ret));
  }
  return ret;
}

int ObTabletAutoincrementService::collect_single_table_cache_invalidation_(
    const schema::ObSimpleTableSchemaV2 &table_schema,
    common::ObIArray<common::ObTabletID> &cache_tablet_ids)
{
  int ret = OB_SUCCESS;
  if (table_schema.is_table_with_hidden_pk_column() || table_schema.is_aux_lob_meta_table()) {
    ObArray<ObTabletID> tablet_ids;
    if (OB_FAIL(table_schema.get_tablet_ids(tablet_ids))) {
      LOG_WARN("failed to get tablet ids", K(ret));
    } else if (OB_FAIL(append(cache_tablet_ids, tablet_ids))) {
      LOG_WARN("failed to append tablet ids", K(ret));
    }
  }
  return ret;
}

int ObTabletAutoincrementService::collect_table_cache_invalidation(
    schema::ObSchemaGetterGuard &schema_guard,
    const schema::ObTableSchema &table_schema,
    common::ObIArray<common::ObTabletID> &cache_tablet_ids)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(collect_single_table_cache_invalidation_(
          table_schema, cache_tablet_ids))) {
    LOG_WARN("failed to add single table", K(ret));
  }

  if (OB_SUCC(ret)) {
    const uint64_t lob_meta_tid = table_schema.get_aux_lob_meta_tid();
    if (OB_INVALID_ID != lob_meta_tid) {
      const ObTableSchema *lob_meta_table_schema = nullptr;
      if (OB_FAIL(schema_guard.get_table_schema(lob_meta_tid, lob_meta_table_schema))) {
        LOG_WARN("failed to get aux table schema", K(ret), K(lob_meta_tid));
      } else if (OB_ISNULL(lob_meta_table_schema)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("invalid table schema", K(ret), K(lob_meta_tid));
      } else if (OB_FAIL(collect_single_table_cache_invalidation_(
                     *lob_meta_table_schema, cache_tablet_ids))) {
        LOG_WARN("failed to add single table", K(ret));
      }
    }
  }
  return ret;
}

int ObTabletAutoincrementService::collect_table_cache_invalidation(
    schema::ObSchemaGuardWrapper &schema_guard,
    const schema::ObTableSchema &table_schema,
    common::ObIArray<common::ObTabletID> &cache_tablet_ids)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(collect_single_table_cache_invalidation_(
          table_schema, cache_tablet_ids))) {
    LOG_WARN("failed to add single table", K(ret));
  }

  if (OB_SUCC(ret)) {
    const uint64_t lob_meta_tid = table_schema.get_aux_lob_meta_tid();
    if (OB_INVALID_ID != lob_meta_tid) {
      const ObTableSchema *lob_meta_table_schema = nullptr;
      if (OB_FAIL(schema_guard.get_table_schema(
              lob_meta_tid, lob_meta_table_schema))) {
        LOG_WARN("failed to get aux table schema", K(ret), K(lob_meta_tid));
      } else if (OB_ISNULL(lob_meta_table_schema)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("invalid table schema", K(ret), K(lob_meta_tid));
      } else if (OB_FAIL(collect_single_table_cache_invalidation_(
                     *lob_meta_table_schema, cache_tablet_ids))) {
        LOG_WARN("failed to add single table", K(ret));
      }
    }
  }
  return ret;
}

int ObTabletAutoincrementService::collect_database_cache_invalidation(
    const schema::ObDatabaseSchema &database_schema,
    common::ObIArray<common::ObTabletID> &cache_tablet_ids)
{
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard schema_guard;
  const uint64_t database_id = database_schema.get_database_id();
  ObArray<const ObSimpleTableSchemaV2 *> table_schemas;
  ObMultiVersionSchemaService &schema_service = ObMultiVersionSchemaService::get_instance();
  if (OB_FAIL(schema_service.get_runtime_schema_guard(schema_guard))) {
    LOG_WARN("fail to get schema guard", KR(ret));
  } else if (OB_FAIL(schema_guard.get_table_schemas_in_database(
                 database_id, table_schemas))) {
    LOG_WARN("fail to get table ids in database", K(1UL), K(database_id), K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < table_schemas.count(); i++) {
      const ObSimpleTableSchemaV2 *table_schema = table_schemas.at(i);
      if (OB_ISNULL(table_schema)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("table schema should not be null", K(ret));
      } else if (OB_FAIL(collect_single_table_cache_invalidation_(
                     *table_schema, cache_tablet_ids))) {
        LOG_WARN("failed to collect table cache invalidation", KR(ret),
            KPC(table_schema));
      }
    }
  }
  return ret;
}

int ObTabletAutoincrementService::invalidate_caches(
    const common::ObIArray<common::ObTabletID> &cache_tablet_ids)
{
  int ret = OB_SUCCESS;
  static const int64_t DEFAULT_TIMEOUT_US = 1 * 1000 * 1000;
  ObTimeGuard time_guard("TabletAutoincCacheInvalidation", DEFAULT_TIMEOUT_US);
  int64_t abs_timeout_us = ObTimeUtility::current_time() + DEFAULT_TIMEOUT_US;
  const ObTimeoutCtx &ctx = ObTimeoutCtx::get_ctx();
  if (THIS_WORKER.is_timeout_ts_valid()) {
    abs_timeout_us = std::min(abs_timeout_us, THIS_WORKER.get_timeout_ts());
  }
  if (ctx.is_timeout_set()) {
    abs_timeout_us = std::min(abs_timeout_us, ctx.get_abs_timeout());
  }
  if (ctx.is_trx_timeout_set()) {
    abs_timeout_us = std::min(
        abs_timeout_us, ObTimeUtility::current_time() + ctx.get_trx_timeout_us());
  }
  if (OB_FAIL(clear_tablet_autoinc_seq_cache(
          cache_tablet_ids, abs_timeout_us))) {
    LOG_WARN("failed to clear tablet autoincrement cache", K(ret),
        K(cache_tablet_ids));
  }
  return ret;
}

}
}
