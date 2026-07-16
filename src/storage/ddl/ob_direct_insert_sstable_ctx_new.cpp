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

#define USING_LOG_PREFIX STORAGE

#include "ob_direct_insert_sstable_ctx_new.h"
#include "storage/ddl/ob_ddl_storage_util.h"
#include "storage/ob_tablet_autoincrement_service.h"
#include "share/rc/ob_module_provider.h"
#include "storage/compaction/ob_schedule_dag_func.h"
#include "storage/ddl/ob_ddl_merge_task.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "storage/ddl/ob_direct_load_mgr_utils.h"

using namespace oceanbase;
using namespace oceanbase::common;
using namespace oceanbase::storage;
using namespace oceanbase::blocksstable;
using namespace oceanbase::share;
using namespace oceanbase::share::schema;
using namespace oceanbase::sql;

int64_t ObTenantDirectLoadMgr::generate_context_id()
{
  return ATOMIC_AAF(&context_id_generator_, 1);
}

ObTenantDirectLoadMgr::ObTenantDirectLoadMgr()
  : is_inited_(false), slice_id_generator_(0), context_id_generator_(0), last_gc_time_(0)
{
}

ObTenantDirectLoadMgr::~ObTenantDirectLoadMgr()
{
  destroy();
}

void ObTenantDirectLoadMgr::destroy()
{
  int ret = OB_SUCCESS;
  common::ObArray<ObTabletDirectLoadMgrKey> tablet_mgr_keys;
  for (TABLET_MGR_MAP::const_iterator iter = tablet_mgr_map_.begin();
        iter != tablet_mgr_map_.end(); ++iter) {
    if (OB_FAIL(tablet_mgr_keys.push_back(iter->first))) {
      LOG_WARN("push back failed", K(ret));
    }
  }
  for (int64_t i = 0; i < tablet_mgr_keys.count(); i++) {
    // overwrite ret
    if (OB_FAIL(remove_tablet_direct_load(tablet_mgr_keys.at(i)))) {
      LOG_WARN("remove tablet mgr failed", K(ret), K(tablet_mgr_keys.at(i)));
    }
  }
  tablet_exec_context_map_.destroy();
  bucket_lock_.destroy();
  allocator_.reset();
  is_inited_ = false;
}

int64_t ObTenantDirectLoadMgr::generate_slice_id()
{
  return ATOMIC_AAF(&slice_id_generator_, 1);
}

int ObTenantDirectLoadMgr::mtl_init(ObTenantDirectLoadMgr *&tenant_direct_load_mgr)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(tenant_direct_load_mgr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected error", K(ret));
  } else if (OB_FAIL(tenant_direct_load_mgr->init())) {
    LOG_WARN("init failed", K(ret));
  }
  return ret;
}

int ObTenantDirectLoadMgr::init()
{
  int ret = OB_SUCCESS;
  
  const int64_t bucket_num = common::hash::cal_next_prime(common::calculate_scaled_value_by_memory(4096L, 100000L));
  const int64_t memory_limit = 1024LL * 1024LL * 1024LL * 10LL; // 10GB
  lib::ObMemAttr attr("TenantDLMgr");
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret));
  } else if (OB_UNLIKELY(!true)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret));
  } else if (OB_FAIL(allocator_.init(OB_MALLOC_MIDDLE_BLOCK_SIZE,
    attr.label_, memory_limit))) {
    LOG_WARN("init alloctor failed", K(ret));
  } else if (OB_FAIL(bucket_lock_.init(bucket_num, ObLatchIds::TENANT_DIRECT_LOAD_MGR_LOCK,
      ObLabel("TenDLBucket")))) {
    LOG_WARN("init bucket lock failed", K(ret), K(bucket_num));
  } else if (OB_FAIL(tablet_mgr_map_.create(bucket_num, attr, attr))) {
    LOG_WARN("create context map failed", K(ret));
  } else if (OB_FAIL(tablet_exec_context_map_.create(bucket_num, attr, attr))) {
    LOG_WARN("create context map failed", K(ret));
  } else {
    allocator_.set_attr(attr);
    slice_id_generator_ = ObTimeUtility::current_time();
    is_inited_ = true;
  }
  return ret;
}

int ObTenantDirectLoadMgr::get_agent_exec_context(
    const int64_t context_id,
    const ObTabletID &tablet_id,
    const ObDirectLoadType &type,
    ObTabletDirectLoadMgrHandle &direct_load_mgr_handle,
    share::SCN &start_scn,
    int64_t &execution_id)
{
  int ret = OB_SUCCESS;
  direct_load_mgr_handle.reset();
  start_scn.reset();
  execution_id = -1;
  ObTabletDirectLoadMgr *tablet_mgr = nullptr;

  ObTabletDirectLoadExecContextId exec_id;
  ObTabletDirectLoadExecContext exec_context;
  exec_id.tablet_id_ = tablet_id;
  exec_id.context_id_ = context_id;

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(context_id <= 0 || !tablet_id.is_valid() || !is_valid_direct_load(type))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(context_id), K(tablet_id), K(type), K(context_id));
  } else if (OB_FAIL(get_tablet_exec_context_with_rlock(exec_id, exec_context))) {
    LOG_WARN("get table execution context failed", K(ret), K(exec_id));
  } else if (is_shared_storage_dempotent_mode(type)) {
    ObTabletDirectLoadMgrKey mgr_key(tablet_id, context_id);
    if (OB_FAIL(get_tablet_mgr(mgr_key, direct_load_mgr_handle))) {
      LOG_WARN("get tablet direct load mgr failed", K(ret), K(mgr_key));
    } else {
      start_scn = direct_load_mgr_handle.get_obj()->get_start_scn();
      execution_id = exec_context.execution_id_;
    }
  } else {
    // shared nothing.
    ObTabletDirectLoadMgrKey mgr_key(tablet_id, type, context_id);
    ObBucketHashRLockGuard guard(bucket_lock_, mgr_key.hash());
    const bool is_full_direct_load_task = is_full_direct_load(type);
    if (OB_FAIL(get_tablet_mgr_no_lock(mgr_key, direct_load_mgr_handle))) {
      if (OB_ENTRY_NOT_EXIST == ret && is_full_direct_load_task) {
        if (OB_FAIL(check_and_process_finished_tablet(tablet_id))) {
          LOG_WARN("check and report checksum if need failed", K(ret), K(tablet_id));
        }
      } else {
        LOG_WARN("get table mgr failed", K(ret), K(tablet_id));
      }
    }
    if (OB_SUCC(ret)) {
        start_scn = exec_context.start_scn_;
        execution_id = exec_context.execution_id_;
    }
  }
  return ret;
}

// 1. Leader create it when start tablet direct load task;
// 2. Follower create it before replaying start log;
// 3. Migrate/Rebuild create tablet/ LS online create it.
int ObTenantDirectLoadMgr::create_tablet_direct_load(
    const int64_t context_id,
    const int64_t execution_id,
    const ObTabletDirectLoadInsertParam &build_param,
    const share::SCN checkpoint_scn,
    const bool only_persisted_ddl_data)
{
  int ret = OB_SUCCESS;
  const common::ObTabletID &tablet_id = build_param.common_param_.tablet_id_;
  ObLSService *ls_service = nullptr;
  ObLS *ls = nullptr;
  ObTabletHandle tablet_handle;
  ObTabletBindingMdsUserData ddl_data;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(!build_param.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(context_id), K(build_param));
  } else if (OB_ISNULL(ls_service = share::g_mp->ls_service())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected err", K(ret));
  } else if (OB_FAIL(ls_service->get_ls(ls))) {
    LOG_WARN("failed to get log stream", K(ret), K(build_param));
  } else if (OB_FAIL(ObDDLUtil::ddl_get_tablet(ls, tablet_id, tablet_handle, ObMDSGetTabletMode::READ_WITHOUT_CHECK))) {
    LOG_WARN("get tablet handle failed", K(ret), K(build_param));
  } else if (!only_persisted_ddl_data && OB_FAIL(tablet_handle.get_obj()->ObITabletMdsInterface::get_ddl_data(share::SCN::max_scn(), ddl_data))) {
    LOG_WARN("failed to get ddl data from tablet", K(ret), K(tablet_id));
  } else if (only_persisted_ddl_data && OB_FAIL((tablet_handle.get_obj()->get_mds_data_from_tablet<mds::DummyKey, ObTabletBindingMdsUserData>(
      mds::DummyKey(),
      share::SCN::max_scn(),
      ObTabletCommon::DEFAULT_GET_TABLET_DURATION_US,
      ReadBindingInfoOp(ddl_data))))) {
    if (OB_SNAPSHOT_DISCARDED == ret) {
      ddl_data.set_default_value();
      ret = OB_SUCCESS;
    } else {
      LOG_WARN("failed to get ddl data from tablet", K(ret), K(tablet_id));
    }
  }

  if (OB_SUCC(ret)) {
    ObTabletHandle lob_tablet_handle;
    ObTabletMemberWrapper<ObTabletTableStore> table_store_wrapper;
    ObTabletMemberWrapper<ObTabletTableStore> lob_store_wrapper;
    ObTabletDirectLoadMgrHandle data_tablet_direct_load_mgr_handle;
    ObTabletDirectLoadMgrHandle lob_tablet_direct_load_mgr_handle;
    data_tablet_direct_load_mgr_handle.reset();
    lob_tablet_direct_load_mgr_handle.reset();
    const ObTabletID &lob_meta_tablet_id = ddl_data.lob_meta_tablet_id_;
    ObTabletDirectLoadMgrKey data_mgr_key(build_param.common_param_.tablet_id_, build_param.common_param_.direct_load_type_, context_id);
    ObTabletDirectLoadMgrKey lob_mgr_key(lob_meta_tablet_id, build_param.common_param_.direct_load_type_, context_id);
    
    ObTabletDirectLoadExecContextId exec_id;
    ObTabletDirectLoadExecContext exec_context;
    exec_id.tablet_id_ = data_mgr_key.tablet_id_;
    exec_id.context_id_ = context_id;
    ObSEArray<uint64_t, 2> dl_mgr_key_hash_array; // data tablet and lob meta.
    ObMultiBucketLockGuard lock_guard(bucket_lock_, true/*is_write_lock*/);
    if (OB_FAIL(dl_mgr_key_hash_array.push_back(data_mgr_key.hash()))) {
      LOG_WARN("push back failed", K(ret));
    } else if (lob_meta_tablet_id.is_valid() &&
        OB_FAIL(dl_mgr_key_hash_array.push_back(lob_mgr_key.hash()))) {
      LOG_WARN("push back failed", K(ret));
    } else if (exec_id.is_valid() && OB_FAIL(dl_mgr_key_hash_array.push_back(exec_id.hash()))) {
      LOG_WARN("push back failed", K(ret), K(exec_id));
    } else if (OB_FAIL(lock_guard.lock_multi_buckets(dl_mgr_key_hash_array))) {
      LOG_WARN("lock multi buckets failed", K(ret));
    } else if (!lob_meta_tablet_id.is_valid() || checkpoint_scn.is_valid_and_not_min()) {
      // has no lob, or recover from checkpoint.
      LOG_DEBUG("do not create lob mgr handle when create data tablet mgr", K(ret), K(lob_meta_tablet_id),
          K(checkpoint_scn), K(build_param));
    } else if (OB_FAIL(ObDDLUtil::ddl_get_tablet(ls, lob_meta_tablet_id,
        lob_tablet_handle, ObMDSGetTabletMode::READ_WITHOUT_CHECK))) {
      LOG_WARN("get tablet handle failed", K(ret), K(lob_meta_tablet_id));
    } else if (OB_FAIL(lob_tablet_handle.get_obj()->fetch_table_store(lob_store_wrapper))) {
      LOG_WARN("fail to fetch table store", K(ret));
    } else if (OB_FAIL(try_create_tablet_direct_load_mgr_nolock(
        nullptr != lob_store_wrapper.get_member()->get_major_sstables().get_boundary_table(false/*first*/),
        allocator_, lob_mgr_key, lob_tablet_direct_load_mgr_handle))) {
      LOG_WARN("try create data tablet direct load mgr failed", K(ret), K(build_param));
    }

    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(tablet_handle.get_obj()->fetch_table_store(table_store_wrapper))) {
      LOG_WARN("fetch table store failed", K(ret));
    } else {
      ObTabletDirectLoadExecContext exec_context;
      exec_context.execution_id_ = execution_id;
      exec_context.start_scn_.reset();
      if (OB_FAIL(try_create_tablet_direct_load_mgr_nolock(
          nullptr != table_store_wrapper.get_member()->get_major_sstables().get_boundary_table(false/*first*/),
          allocator_, data_mgr_key, data_tablet_direct_load_mgr_handle))) {
        // Newly-allocated Lob meta tablet direct load mgr will be cleanuped when tablet gc task works.
        LOG_WARN("try create data tablet direct load mgr failed", K(ret), K(build_param));
      } else if (!checkpoint_scn.is_valid_and_not_min() && OB_FAIL(tablet_exec_context_map_.set_refactored(exec_id, exec_context, true /*overwrite*/))) {
        LOG_WARN("get table execution context failed", K(ret), K(exec_id));
      }
    }

    if (OB_FAIL(ret)) {
    } else if (data_tablet_direct_load_mgr_handle.is_valid()) {
      if (OB_FAIL(data_tablet_direct_load_mgr_handle.get_obj()->update(
          lob_tablet_direct_load_mgr_handle.get_obj(), build_param))) {
        LOG_WARN("init tablet mgr failed", K(ret), K(build_param));
      }
    }

  #ifdef ERRSIM
    if (OB_SUCC(ret) && REACH_COUNT_INTERVAL(200)) {
      ret = OB_E(EventTable::EN_DDL_START_FAIL) OB_SUCCESS;
      FLOG_INFO("ddl inject error to test mem free", K(ret));
    }
  #endif
    if (OB_FAIL(ret)) {
      (void) tablet_exec_context_map_.erase_refactored(exec_id);
      if (is_shared_storage_dempotent_mode(build_param.common_param_.direct_load_type_)) {
        // Shared-storage managers are cleaned up by the foreground path.
        if (data_mgr_key.is_valid()) {
          (void)remove_tablet_direct_load_nolock(data_mgr_key);
        }
        if (lob_mgr_key.is_valid()) {
          (void)remove_tablet_direct_load_nolock(lob_mgr_key);
        }
      }
    }
  }
  return ret;
}

int ObTenantDirectLoadMgr::replay_create_tablet_direct_load(
    const ObTablet *tablet,
    const int64_t execution_id,
    const ObTabletDirectLoadInsertParam &build_param)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(OB_ISNULL(tablet) || execution_id < 0 || !build_param.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(tablet), K(execution_id), K(build_param));
  } else {
    ObTabletMemberWrapper<ObTabletTableStore> table_store_wrapper;
    ObTabletDirectLoadMgrHandle direct_load_mgr_handle;
    ObTabletDirectLoadMgrKey data_mgr_key(build_param.common_param_.tablet_id_, build_param.common_param_.direct_load_type_);
    ObBucketHashWLockGuard guard(bucket_lock_, data_mgr_key.hash());
    if (OB_FAIL(tablet->fetch_table_store(table_store_wrapper))) {
      LOG_WARN("fetch table store failed", K(ret));
    } else if (OB_FAIL(try_create_tablet_direct_load_mgr_nolock(
            nullptr != table_store_wrapper.get_member()->get_major_sstables().get_boundary_table(false/*first*/),
            allocator_, data_mgr_key, direct_load_mgr_handle))) {
      // Newly-allocated Lob meta tablet direct load mgr will be cleanuped when tablet gc task works.
      LOG_WARN("try create data tablet direct load mgr failed", K(ret), K(build_param));
    }
  }
  return ret;
}

int ObTenantDirectLoadMgr::alloc_tablet_direct_load_mgr(
    ObIAllocator &allocator,
    const ObTabletDirectLoadMgrKey &mgr_key,
    ObBaseTabletDirectLoadMgr *&direct_load_mgr)
{
  int ret = OB_SUCCESS;
  direct_load_mgr = nullptr;
  if (OB_UNLIKELY(!mgr_key.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(mgr_key));
  } else {
    switch (mgr_key.direct_load_type_) {
      case DIRECT_LOAD_DDL:
        if (OB_ISNULL(direct_load_mgr = OB_NEWx(ObTabletFullDirectLoadMgr, &allocator))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("alloc full mgr failed", K(ret), K(mgr_key));
        }
        break;
      default:
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unknown type", K(ret), K(mgr_key));
    }
  }
  return ret;
}


int ObTenantDirectLoadMgr::try_create_tablet_direct_load_mgr_nolock(
    const bool major_sstable_exist,
    ObIAllocator &allocator,
    const ObTabletDirectLoadMgrKey &mgr_key,
    ObTabletDirectLoadMgrHandle &direct_load_mgr_handle)
{
  int ret = OB_SUCCESS;
  direct_load_mgr_handle.reset();
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(!mgr_key.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(mgr_key));
  } else {
    ObBaseTabletDirectLoadMgr *direct_load_mgr = nullptr;
    const bool is_full_direct_load_task = is_full_direct_load(mgr_key.direct_load_type_);
    const bool is_shared_storage_ddl = is_shared_storage_dempotent_mode(mgr_key.direct_load_type_);
    if (OB_FAIL(get_tablet_mgr_no_lock(mgr_key, direct_load_mgr_handle))) {
      if (OB_ENTRY_NOT_EXIST == ret) {
        ret = OB_SUCCESS;
      } else {
        LOG_WARN("get refactored failed", K(ret), K(mgr_key));
      }
    } else if (OB_UNLIKELY(is_shared_storage_ddl || !is_full_direct_load_task)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("tablet direct load mgr should be nullptr under ss mode", K(ret), K(mgr_key), K(major_sstable_exist));
    } else if (OB_ISNULL(direct_load_mgr = direct_load_mgr_handle.get_obj())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected err", K(ret), K(mgr_key));
    }
    if (OB_SUCC(ret) && (!major_sstable_exist || is_shared_storage_ddl || !is_full_direct_load_task)) {
      // shared storage mode, create under the new context even if major exists.
      // shared nothing mode, create under the new context and major not exist.
      if (nullptr == direct_load_mgr) {
        if (OB_FAIL(alloc_tablet_direct_load_mgr(allocator, mgr_key, direct_load_mgr))) {
          LOG_WARN("alloc failed", K(ret), K(mgr_key));
        } else if (OB_FAIL(direct_load_mgr_handle.set_obj(direct_load_mgr))) {
          LOG_WARN("set direct load mgr failed", K(ret));
        }
        // cleanup if failed.
        if (OB_FAIL(ret) && nullptr != direct_load_mgr) {
          direct_load_mgr->~ObBaseTabletDirectLoadMgr();
          allocator.free(direct_load_mgr);
          direct_load_mgr = nullptr;
        }
        // ownership of direct_load_mgr has been transferred to direct_load_mgr_handle
        if (OB_FAIL(ret)) {
        } else if (OB_FAIL(tablet_mgr_map_.set_refactored(mgr_key, direct_load_mgr))) {
          LOG_WARN("set tablet mgr failed", K(ret));
        } else {
          direct_load_mgr->inc_ref();
          LOG_INFO("create tablet direct load mgr", K(mgr_key), K(major_sstable_exist));
        }
      }
    }
  }
  return ret;
}

int ObTenantDirectLoadMgr::alloc_execution_context_id(
    int64_t &context_id)
{
  int ret = OB_SUCCESS;
  context_id = generate_context_id();
  return ret;
}

int ObTenantDirectLoadMgr::alloc_slice_id(int64_t &slice_id)
{
  slice_id = generate_slice_id();
  return OB_SUCCESS;
}

int ObTenantDirectLoadMgr::open_tablet_direct_load(
    const ObDirectLoadType &type,
    const ObTabletID &tablet_id,
    const int64_t context_id)
{
  int ret = OB_SUCCESS;
  share::SCN start_scn;
  ObTabletDirectLoadMgrHandle handle;
  ObTabletDirectLoadExecContextId exec_id;
  ObTabletDirectLoadExecContext exec_context;
  exec_id.tablet_id_ = tablet_id;
  exec_id.context_id_ = context_id;
  bool is_mgr_exist = false;
  const bool is_full_direct_load_task = is_full_direct_load(type);
  ObTabletDirectLoadMgrKey mgr_key(tablet_id, type, context_id);
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(!tablet_id.is_valid() || context_id < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(context_id));
  } else if (is_shared_storage_dempotent_mode(type)) {
    ObTabletDirectLoadMgrKey mgr_key(tablet_id, type, context_id);
    if (OB_FAIL(get_tablet_mgr(mgr_key, handle))) {
      LOG_WARN("get table mgr failed", K(ret), K(tablet_id), K(mgr_key));
    } else if (OB_FAIL(handle.get_obj()->open(exec_context.execution_id_, start_scn))) {
      LOG_WARN("update tablet direct load failed", K(ret), K(is_full_direct_load_task), K(tablet_id), K(exec_context));
    }
  } else {
    // shared nothing.
    // FIXME @SUZHI following key does no contain context id, incremental direct load always fail to get.
    if (OB_FAIL(get_tablet_mgr(mgr_key, handle))) {
      if (OB_ENTRY_NOT_EXIST == ret && is_full_direct_load_task) {
        if (OB_FAIL(check_and_process_finished_tablet(tablet_id))) {
          LOG_WARN("check and report checksum if need failed", K(ret), K(tablet_id));
        }
      } else {
        LOG_WARN("get table mgr failed", K(ret), K(tablet_id), K(is_full_direct_load_task));
      }
    } else {
      is_mgr_exist = true;
    }

    if (OB_SUCC(ret)) {
      if (OB_FAIL(get_tablet_exec_context_with_rlock(exec_id, exec_context))) {
     	 LOG_WARN("get table execution context failed", K(ret), K(exec_id));
      }
    }

    if (OB_SUCC(ret) && is_mgr_exist) {
      if (OB_FAIL(handle.get_obj()->open(exec_context.execution_id_, start_scn))) {
        LOG_WARN("update tablet direct load failed", K(ret), K(is_full_direct_load_task), K(tablet_id), K(exec_context));
      }
    }

    if (OB_SUCC(ret)) {
      ObBucketHashWLockGuard guard(bucket_lock_, exec_id.hash());
      exec_context.start_scn_ = start_scn;
      if (OB_FAIL(tablet_exec_context_map_.set_refactored(exec_id, exec_context, true/*overwrite*/))) {
        LOG_WARN("get table execution context failed", K(ret), K(exec_id));
      }
    }
  }
  return ret;
}

int ObTenantDirectLoadMgr::close_tablet_direct_load(
    const int64_t context_id,
    const ObDirectLoadType &type,
    const ObTabletID &tablet_id,
    const bool need_commit,
    const bool emergent_finish,
    const int64_t task_id,
    const int64_t table_id,
    const int64_t execution_id)
{
  int ret = OB_SUCCESS;
  UNUSED(emergent_finish);
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(context_id <= 0
      || !is_valid_direct_load(type)
      || !tablet_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(context_id), K(type), K(tablet_id),
        K(task_id), K(table_id), K(execution_id));
  } else if (is_idem_type(type)) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("unsupported direct load type", K(ret), K(type));
  } else if (is_shared_storage_dempotent_mode(type)) {
    if (OB_FAIL(close_tablet_direct_load_for_ss(context_id, tablet_id, need_commit))) {
      LOG_WARN("close tablet direct load failed", K(ret), K(context_id), K(tablet_id), K(need_commit));
    }
  } else {
    if (OB_FAIL(close_tablet_direct_load_for_sn(context_id, type, tablet_id, need_commit, task_id, table_id, execution_id))) {
      LOG_WARN("close tablet direct load failed", K(ret), K(context_id),
          K(tablet_id), K(need_commit), K(task_id), K(table_id), K(execution_id));
    }
  }
  return ret;
}

int ObTenantDirectLoadMgr::close_tablet_direct_load_for_sn(
    const int64_t context_id,
    const ObDirectLoadType &type,
    const ObTabletID &tablet_id,
    const bool need_commit,
    const int64_t task_id,
    const int64_t table_id,
    const int64_t execution_id)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  ObTabletDirectLoadMgrHandle handle;
  ObTabletDirectLoadExecContextId exec_id;
  exec_id.tablet_id_ = tablet_id;
  exec_id.context_id_ = context_id;
  const bool is_full_direct_load_task = is_full_direct_load(type);
  // FIXME SUZHI, inc direct load mgr key with context id.
  ObTabletDirectLoadMgrKey data_mgr_key(tablet_id, type, context_id);
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(!tablet_id.is_valid() || context_id <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(tablet_id), K(context_id));
  } else if (OB_FAIL(get_tablet_mgr(data_mgr_key, handle))) {
    if (OB_ENTRY_NOT_EXIST == ret && is_full_direct_load_task) {
      if (OB_FAIL(check_and_process_finished_tablet(tablet_id, task_id, table_id, execution_id))) {
        LOG_WARN("check and report checksum if need failed", K(ret), K(tablet_id), K(task_id), K(execution_id));
      }
    } else {
      LOG_WARN("get table mgr failed", K(ret), K(tablet_id));
    }
  } else if (need_commit) {
    ObTabletDirectLoadExecContext exec_context;
    if (OB_FAIL(get_tablet_exec_context_with_rlock(exec_id, exec_context))) {
      LOG_WARN("get exec context failed", K(ret), K(exec_id));
    } else if (OB_ISNULL(handle.get_obj())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("handle is invalid", K(ret));
    } else if (need_commit && handle.get_obj()->get_sqc_build_ctx().build_param_.is_replay_) {
      ret = OB_TASK_EXPIRED;
      LOG_WARN("failed to commit, since tablet direct load mgr is build for replay, some info may be invalid, need retry the whole task", K(ret));
    } else if (OB_FAIL(handle.get_obj()->close(exec_context.execution_id_, exec_context.start_scn_))) {
      LOG_WARN("close failed", K(ret));
    } else {

    }
  }

  if (OB_SUCC(ret)) {
    if (is_full_direct_load_task) {
    // For full direct load, the ObTabletDirectLoadMgr will be removed from MTL when,
    // 1. the direct load task abort indicated by `need_commit = false`, and we do not care about
    //    the error code triggered by the not found ObTabletDirectLoadMgr after.
    // 2. the direct load task commit and all ddl kvs persist successfully.
    // But how to notify the follower to remove it, with write commit failed log or tablet gc task ??
      if (nullptr != handle.get_full_obj()) {
        IGNORE_RETURN handle.get_full_obj()->cleanup_slice_writer(context_id); // remove slice writer of current context
      }
    } else {
      // For incremental direct load, the ObTabletDirectLoadMgr will be removed immediately
      ObTabletID lob_meta_tablet_id = handle.get_obj()->get_lob_meta_tablet_id();
      ObTabletDirectLoadMgrKey lob_meta_mgr_key(lob_meta_tablet_id, type, context_id);
      if (lob_meta_mgr_key.is_valid() && OB_FAIL(remove_tablet_direct_load(lob_meta_mgr_key))) {
        LOG_ERROR("fail to remove lob meta tablet direct load", K(ret), K(lob_meta_mgr_key));
      } else if (OB_FAIL(remove_tablet_direct_load(data_mgr_key))) {
        LOG_WARN("fail to remove tablet direct load", K(ret), K(data_mgr_key));
      }
    }
  }

  ObBucketHashWLockGuard guard(bucket_lock_, exec_id.hash());
  if (OB_TMP_FAIL(tablet_exec_context_map_.erase_refactored(exec_id))) {
    LOG_WARN("erase refactored failed", K(ret), K(tmp_ret), K(exec_id));
  }

  LOG_INFO("erase execution context", K(ret), K(exec_id), K(tablet_id));
  return ret;
}

int ObTenantDirectLoadMgr::close_tablet_direct_load_for_ss(
    const int64_t context_id,
    const ObTabletID &tablet_id,
    const bool need_commit)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  ObTabletID lob_meta_tablet_id;
  ObTabletDirectLoadMgrHandle handle;
  ObTabletDirectLoadMgrKey data_mgr_key(tablet_id, context_id);
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(!tablet_id.is_valid() || context_id <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(tablet_id), K(context_id));
  } else if (OB_FAIL(get_tablet_mgr(data_mgr_key, handle))) {
    LOG_WARN("get table mgr failed", K(ret), K(tablet_id));
  } else if (need_commit &&
      OB_FAIL(handle.get_obj()->close(1/*placeholder*/, SCN::min_scn()/*placeholder*/))) {
    LOG_WARN("close to generate major sstable failed", K(ret));
  } 
  
  if (OB_LIKELY(handle.is_valid())) {
    // No matter succ or fail above, dec ref is necessary.
    ObTabletDirectLoadExecContextId exec_id;
    exec_id.tablet_id_ = tablet_id;
    exec_id.context_id_ = context_id;
    const ObTabletID &lob_meta_tablet_id = handle.get_obj()->get_lob_meta_tablet_id();
    ObTabletDirectLoadMgrKey lob_mgr_key(lob_meta_tablet_id, context_id);
    ObSEArray<uint64_t, 3> dl_mgr_key_hash_array; // data tablet and lob meta.
    ObMultiBucketLockGuard lock_guard(bucket_lock_, true/*is_write_lock*/);
    if (OB_TMP_FAIL(dl_mgr_key_hash_array.push_back(data_mgr_key.hash()))) {
      LOG_WARN("push back failed", K(tmp_ret), K(data_mgr_key));
    } else if (lob_meta_tablet_id.is_valid() &&
        OB_TMP_FAIL(dl_mgr_key_hash_array.push_back(lob_mgr_key.hash()))) {
      LOG_WARN("push back failed", K(tmp_ret), K(lob_mgr_key));
    } else if (OB_TMP_FAIL(dl_mgr_key_hash_array.push_back(exec_id.hash()))) {
      LOG_WARN("push back failed", K(tmp_ret));
    } else if (OB_TMP_FAIL(lock_guard.lock_multi_buckets(dl_mgr_key_hash_array))) {
      LOG_WARN("lock multi buckets failed", K(tmp_ret));
    } else {
      if (OB_TMP_FAIL(remove_tablet_direct_load_nolock(data_mgr_key))) {
        ret = OB_SUCC(ret) ? tmp_ret : ret;
        LOG_WARN("remove direct load mgr failed", K(ret), K(tmp_ret), K(data_mgr_key));
      }
      if (lob_meta_tablet_id.is_valid() &&
          OB_TMP_FAIL(remove_tablet_direct_load_nolock(lob_mgr_key))) { // override tmp ret.
        ret = OB_SUCC(ret) ? tmp_ret : ret;
        LOG_WARN("remove direct load mgr failed", K(ret), K(tmp_ret), K(lob_mgr_key));
      }
      if (OB_TMP_FAIL(tablet_exec_context_map_.erase_refactored(exec_id))) { // override tmp ret.
        ret = OB_SUCC(ret) ? tmp_ret : ret;
        LOG_WARN("erase refactored failed", K(ret), K(tmp_ret), K(exec_id));
      }
    }
    ret = OB_SUCC(ret) ? tmp_ret : ret;
  }
  
  return ret;
}

// Other utils function.

int ObTenantDirectLoadMgr::get_tablet_cache_interval(
    const int64_t context_id,
    const ObTabletID &tablet_id,
    ObTabletCacheInterval &interval)
{
  int ret = OB_SUCCESS;
  ObTabletAutoincrementService &autoinc_service = ObTabletAutoincrementService::get_instance();
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(context_id < 0 || !tablet_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(context_id), K(tablet_id));
  } else {
    ObTabletDirectLoadExecContext exec_context;
    ObTabletDirectLoadExecContextId exec_id;
    exec_id.tablet_id_ = tablet_id;
    exec_id.context_id_ = context_id;
    ObBucketHashWLockGuard guard(bucket_lock_, exec_id.hash());
    if (OB_FAIL(autoinc_service.get_tablet_cache_interval(interval))) {
      LOG_WARN("failed to get tablet cache intervals", K(ret));
    } else if (OB_FAIL(tablet_exec_context_map_.get_refactored(exec_id, exec_context))) {
      LOG_WARN("get tablet execution context failed", K(ret));
    } else {
      interval.task_id_ = exec_context.seq_interval_task_id_++;
      if (OB_FAIL(tablet_exec_context_map_.set_refactored(exec_id, exec_context, true/*overwrite*/))) {
        LOG_WARN("set tablet execution context map", K(ret));
      }
    }
  }

  return ret;
}

int ObTenantDirectLoadMgr::check_and_process_finished_tablet(
    const ObTabletID &tablet_id,
    const int64_t task_id,
    const int64_t table_id,
    const int64_t execution_id)
{
  int ret = OB_SUCCESS;
  ObLS *ls = nullptr;
  ObTabletHandle tablet_handle;
  ObSSTableMetaHandle sst_meta_hdl;
  
  uint64_t data_format_version = 0;
  int64_t snapshot_version = 0;
  share::ObDDLTaskStatus unused_task_status = share::ObDDLTaskStatus::PREPARE;
  const ObSSTable *first_major_sstable = nullptr;
  ObTabletMemberWrapper<ObTabletTableStore> table_store_wrapper;
  const int64_t max_wait_timeout_us = 30L * 1000L * 1000L; // 30s
  ObTimeGuard tg("ddl_retry_tablet", max_wait_timeout_us);
  if (OB_UNLIKELY(!tablet_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(tablet_id));
  } else if (OB_FAIL(share::g_mp->ls_service()->get_ls(ls))) {
    LOG_WARN("failed to get log stream", K(ret));
  }
  while (OB_SUCC(ret)) {
    if (OB_FAIL(THIS_WORKER.check_status())) {
      LOG_WARN("check status failed", K(ret), K(tablet_id));
    } else if (tg.get_diff() > max_wait_timeout_us) {
      ret = OB_NEED_RETRY;
      LOG_WARN("process finished tablet timeout, need retry", K(ret), K(tablet_id), K(tg));
    } else if (OB_FAIL(ObDDLUtil::ddl_get_tablet(ls,
        tablet_id, tablet_handle, ObMDSGetTabletMode::READ_WITHOUT_CHECK))) {
      LOG_WARN("get tablet handle failed", K(ret), K(tablet_id));
    } else if (OB_UNLIKELY(nullptr == tablet_handle.get_obj())) {
      ret = OB_ERR_SYS;
      LOG_WARN("tablet handle is null", K(ret), K(tablet_id));
    } else if (task_id <= 0 || common::OB_INVALID_ID == table_id || execution_id < 0
      || tablet_handle.get_obj()->get_tablet_meta().ddl_execution_id_ > execution_id) {
      // no need to report checkksum.
      LOG_INFO("no need to report checksum", K(ret), K(task_id), K(table_id), K(execution_id),
        "tablet_meta", tablet_handle.get_obj()->get_tablet_meta());
      break;
    } else if (OB_FAIL(tablet_handle.get_obj()->fetch_table_store(table_store_wrapper))) {
      LOG_WARN("fail to fetch table store", K(ret));
    } else if (FALSE_IT(first_major_sstable = static_cast<ObSSTable *>(
          table_store_wrapper.get_member()->get_major_sstables().get_boundary_table(false/*first*/)))) {
    } else if (nullptr == first_major_sstable) {
      LOG_INFO("major not exist, retry later", K(ret), K(tablet_id), K(tg));
      usleep(100L * 1000L); // 100ms
    } else if (OB_FAIL(ObTabletDDLUtil::check_and_get_major_sstable(
        tablet_id, first_major_sstable, table_store_wrapper))) {
      LOG_WARN("check if major sstable exist failed", K(ret), K(tablet_id));
    } else if (OB_FAIL(first_major_sstable->get_meta(sst_meta_hdl))) {
      LOG_WARN("fail to get sstable meta handle", K(ret));
    } else if (OB_FAIL(ObDDLUtil::get_data_information(task_id, data_format_version, snapshot_version, unused_task_status))) {
      LOG_WARN("get ddl cluster version failed", K(ret), K(task_id));
    } else {
      const int64_t *column_checksums = sst_meta_hdl.get_sstable_meta().get_col_checksum();
      int64_t column_count = sst_meta_hdl.get_sstable_meta().get_col_checksum_cnt();
      if (OB_FAIL(ObTabletDDLUtil::report_ddl_checksum(
            tablet_id,
            table_id,
            execution_id,
            task_id,
            column_checksums,
            column_count,
            data_format_version))) {
        LOG_WARN("report ddl column checksum failed", K(ret), K(tablet_id), K(execution_id));
      } else {
        break;
      }
    }
  }
  return ret;
}

int ObTenantDirectLoadMgr::get_tablet_mgr_and_check_major(
    const ObTabletID &tablet_id,
    const bool is_full_direct_load,
    ObTabletDirectLoadMgrHandle &direct_load_mgr_handle,
    bool &is_major_sstable_exist)
{
  int ret = OB_SUCCESS;
  if (!is_full_direct_load) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("inc direct load shouldn't reach here", K(ret));
  } else {
    const ObDirectLoadType key_type = ObDirectLoadType::DIRECT_LOAD_DDL;
    // all caller is full_direct_load, do not need context_id to initial tablet_mgr;
    ret = get_tablet_mgr(ObTabletDirectLoadMgrKey(tablet_id, key_type), direct_load_mgr_handle);
    is_major_sstable_exist = false;
    if (OB_ENTRY_NOT_EXIST == ret) {
      int tmp_ret = OB_SUCCESS;
      ObLS *ls = nullptr;
      ObTabletHandle tablet_handle;
      if (OB_TMP_FAIL(share::g_mp->ls_service()->get_ls(ls))) {
        LOG_WARN("failed to get log stream", K(tmp_ret));
      } else if (OB_TMP_FAIL(ObDDLUtil::ddl_get_tablet(ls, tablet_id, tablet_handle))) {
        LOG_WARN("get tablet handle failed", K(tmp_ret), K(tablet_id));
      } else {
        is_major_sstable_exist = tablet_handle.get_obj()->get_major_table_count() > 0
          || tablet_handle.get_obj()->get_tablet_meta().table_store_flag_.with_major_sstable();
      }
      if (!is_major_sstable_exist) {
        ret = OB_TASK_EXPIRED;
      }
    }
  }
  return ret;
}

int ObTenantDirectLoadMgr::get_tablet_mgr(
    const ObTabletDirectLoadMgrKey &key,
    ObTabletDirectLoadMgrHandle &direct_load_mgr_handle)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(!key.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(key), K(common::lbt()));
  } else {
    ObBucketHashRLockGuard guard(bucket_lock_, key.hash());
    if (OB_FAIL(get_tablet_mgr_no_lock(key, direct_load_mgr_handle))) {
      if (OB_ENTRY_NOT_EXIST != ret) {
        LOG_WARN("get table mgr without lock failed", K(ret), K(key));
      }
    }
  }
  return ret;
}

int ObTenantDirectLoadMgr::get_tablet_mgr_no_lock(
    const ObTabletDirectLoadMgrKey &mgr_key,
    ObTabletDirectLoadMgrHandle &direct_load_mgr_handle)
{
  int ret = OB_SUCCESS;
  ObBaseTabletDirectLoadMgr *tablet_mgr = nullptr;
  if (OB_UNLIKELY(!mgr_key.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(mgr_key));
  } else if (OB_FAIL(tablet_mgr_map_.get_refactored(mgr_key, tablet_mgr))) {
    if (OB_HASH_NOT_EXIST != ret) {
      LOG_WARN("get refactored failed", K(ret), K(mgr_key));
    } else {
      ret = OB_HASH_NOT_EXIST == ret ? OB_ENTRY_NOT_EXIST : ret;
    }
  } else if (OB_FAIL(direct_load_mgr_handle.set_obj(tablet_mgr))) {
    LOG_WARN("set handle failed", K(ret), K(mgr_key));
  } else if (!direct_load_mgr_handle.is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected err", K(ret), K(mgr_key));
  }
  return ret;
}

int ObTenantDirectLoadMgr::get_tablet_exec_context_with_rlock(
    const ObTabletDirectLoadExecContextId &exec_id,
    ObTabletDirectLoadExecContext &exec_context)
{
  int ret = OB_SUCCESS;
  exec_context.reset();
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(!exec_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(exec_id));
  } else {
    ObBucketHashRLockGuard guard(bucket_lock_, exec_id.hash());
    if (OB_FAIL(tablet_exec_context_map_.get_refactored(exec_id, exec_context))) {
      LOG_WARN("get refactored failed", K(ret), K(exec_id));
    }
  }
  return ret;
}

int ObTenantDirectLoadMgr::GetGcCandidateOp::operator() (common::hash::HashMapPair<ObTabletDirectLoadMgrKey, ObBaseTabletDirectLoadMgr *> &kv)
{
  int ret = OB_SUCCESS;
  const ObTabletDirectLoadMgrKey &key = kv.first;
  ObBaseTabletDirectLoadMgr *tablet_direct_load_mgr = kv.second;
  if (1 == tablet_direct_load_mgr->get_ref()) {
    if (is_shared_storage_dempotent_mode(key.direct_load_type_)) {
      // shared storage mgr shoule be freed by the front only.
    } else if (OB_FAIL(candidate_mgrs_.push_back(key))) {
      LOG_WARN("failed to push back", K(ret));
    }
  }
  return ret;
}

int ObTenantDirectLoadMgr::gc_tablet_direct_load()
{
  int ret = OB_SUCCESS;
  if (!tablet_mgr_map_.empty() && ObDDLUtil::reach_time_interval(10 * 1000 * 1000, last_gc_time_)) {
    ObSEArray<ObTabletDirectLoadMgrKey, 8> candidate_mgrs;
    {
      ObBucketTryRLockAllGuard guard(bucket_lock_);
      if (OB_SUCC(guard.get_ret())) {
        GetGcCandidateOp op(candidate_mgrs);
        (void)tablet_mgr_map_.foreach_refactored(op);
      }
    }

    for (int64_t i = 0; i < candidate_mgrs.count(); i++) { // overwrite ret
      const ObTabletDirectLoadMgrKey &mgr_key = candidate_mgrs.at(i);
      ObLSService *ls_svr = share::g_mp->ls_service();
      ObLS *ls = nullptr;
      ObTabletHandle tablet_handle;
      if (OB_ISNULL(ls_svr)) {
        ret = OB_ERR_SYS;
        LOG_WARN("invalid mtl ObLSService", K(ret));
      } else if (OB_FAIL(ls_svr->get_ls(ls))) {
        LOG_WARN("get single log stream failed", K(ret));
      } else if (OB_FAIL(ls->get_tablet(mgr_key.tablet_id_, tablet_handle,
              ObTabletCommon::DEFAULT_GET_TABLET_DURATION_US, ObMDSGetTabletMode::READ_ALL_COMMITED))) {
        LOG_WARN("failed to get tablet", K(ret), K(mgr_key));
      } else if (tablet_handle.get_obj()->get_major_table_count() > 0) {
        (void)remove_tablet_direct_load(mgr_key);
      }
    }
  }
  return ret;
}

int ObTenantDirectLoadMgr::remove_tablet_direct_load(const ObTabletDirectLoadMgrKey &mgr_key)
{
  ObBucketHashWLockGuard guard(bucket_lock_, mgr_key.hash());
  return remove_tablet_direct_load_nolock(mgr_key);
}

int ObTenantDirectLoadMgr::remove_tablet_direct_load_nolock(const ObTabletDirectLoadMgrKey &mgr_key)
{
  int ret = OB_SUCCESS;
#ifdef ERRSIM
  if (OB_SUCC(ret)) {
    ret = OB_E(EventTable::EN_DDL_RETRY_WRITE_SLICE_AFTER_SUCC) OB_SUCCESS; // do not remove mgr.
    LOG_INFO("errsim injected, retry to write slice when major exists", K(ret));
  }
#endif
  if (OB_FAIL(ret)) {
  } else if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(!mgr_key.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(mgr_key));
  } else {
    ObBaseTabletDirectLoadMgr *tablet_direct_load_mgr = nullptr;
    if (OB_FAIL(tablet_mgr_map_.get_refactored(mgr_key, tablet_direct_load_mgr))) {
      ret = OB_HASH_NOT_EXIST == ret ? OB_ENTRY_NOT_EXIST : ret;
      LOG_TRACE("get table mgr failed", K(ret), K(mgr_key), K(common::lbt()));
    } else if (OB_ISNULL(tablet_direct_load_mgr)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected err", K(ret), K(mgr_key));
    } else if (OB_FAIL(tablet_mgr_map_.erase_refactored(mgr_key))) {
      LOG_WARN("erase from map failed", K(ret));
    } else {
      LOG_INFO("remove tablet direct load mgr from MTL", K(ret), K(mgr_key), K(common::lbt()), K(tablet_direct_load_mgr->get_ref()));
      if (0 == tablet_direct_load_mgr->dec_ref()) {
        tablet_direct_load_mgr->~ObBaseTabletDirectLoadMgr();
        allocator_.free(tablet_direct_load_mgr);
      } else {
        // unreachable
      }
    }
  }
  return ret;
}

struct DestroySliceWriterMapFn
{
public:
  DestroySliceWriterMapFn(ObIAllocator *allocator, const int64_t context_id = -1) :allocator_(allocator), context_id_(context_id) {}
  int operator () (hash::HashMapPair<ObTabletDirectLoadBuildCtx::SliceKey, ObDirectLoadSliceWriter *> &entry) {
    int ret = OB_SUCCESS;
    if (nullptr != allocator_) {
      if (nullptr != entry.second && (-1 == context_id_ || entry.first.context_id_ == context_id_)) {
        LOG_INFO("erase a slice writer", K(&entry.second), "slice_id", entry.first, K(context_id_));
        entry.second->~ObDirectLoadSliceWriter();
        allocator_->free(entry.second);
        entry.second = nullptr;
      }
    }
    return ret;
  }

private:
  ObIAllocator *allocator_;
  int64_t context_id_;
};

ObTabletDirectLoadBuildCtx::ObTabletDirectLoadBuildCtx()
  : allocator_(), slice_writer_allocator_(), build_param_(), slice_mgr_map_(), data_block_desc_(), index_builder_(nullptr),
    column_stat_array_(), is_task_end_(false), task_finish_count_(0), task_total_cnt_(0),
    commit_scn_(), schema_allocator_("TDL_schema", OB_MALLOC_NORMAL_BLOCK_SIZE), storage_schema_(nullptr)
{
  column_stat_array_.set_attr(ObMemAttr("TblDL_CSA"));
}

ObTabletDirectLoadBuildCtx::~ObTabletDirectLoadBuildCtx()
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(index_builder_)) {
    index_builder_->~ObSSTableIndexBuilder();
    allocator_.free(index_builder_);
    index_builder_ = nullptr;
  }
  ObTabletObjLoadHelper::free(schema_allocator_, storage_schema_);
  storage_schema_ = nullptr;
  schema_allocator_.reset();
  commit_scn_.reset();
  for (int64_t i = 0; i < column_stat_array_.count(); i++) {
    ObOptColumnStat *col_stat = column_stat_array_.at(i);
    col_stat->~ObOptColumnStat();
    allocator_.free(col_stat);
    col_stat = nullptr;
  }
  column_stat_array_.reset();

  if (!slice_mgr_map_.empty()) {
    DestroySliceWriterMapFn destroy_map_fn(&slice_writer_allocator_);
    slice_mgr_map_.foreach_refactored(destroy_map_fn);
    slice_mgr_map_.destroy();
  }
  allocator_.reset();
  slice_writer_allocator_.reset();
}

bool ObTabletDirectLoadBuildCtx::is_valid() const
{
  return build_param_.is_valid();
}

void ObTabletDirectLoadBuildCtx::reset_slice_ctx_on_demand()
{
  ATOMIC_STORE(&task_finish_count_, 0);
  ATOMIC_STORE(&task_total_cnt_, build_param_.runtime_only_param_.task_cnt_);
}

void ObTabletDirectLoadBuildCtx::cleanup_slice_writer(const int64_t context_id)
{
  if (!slice_mgr_map_.empty()) {
    DestroySliceWriterMapFn destroy_map_fn(&slice_writer_allocator_, context_id);
    slice_mgr_map_.foreach_refactored(destroy_map_fn);
  }
  LOG_INFO("cleanup slice writer of current context", K(context_id), K(build_param_));
}

ObTabletDirectLoadMgr::ObTabletDirectLoadMgr()
  : is_inited_(false), is_schema_item_ready_(false),
    sqc_build_ctx_(),
    column_items_(), lob_column_idxs_(), lob_col_types_(), schema_item_(), dir_id_(0), task_cnt_(0),
    micro_index_clustered_(false), is_no_logging_(false)
{
  column_items_.set_attr(ObMemAttr("DL_schema"));
  lob_column_idxs_.set_attr(ObMemAttr("DL_schema"));
  lob_col_types_.set_attr(ObMemAttr("DL_schema"));
}

ObTabletDirectLoadMgr::~ObTabletDirectLoadMgr()
{
  ObLatchWGuard guard(lock_, ObLatchIds::TABLET_DIRECT_LOAD_MGR_LOCK);
  FLOG_INFO("deconstruct tablet direct load mgr", KP(this), KPC(this), K(lbt()));
  is_inited_ = false;
  column_items_.reset();
  lob_column_idxs_.reset();
  lob_col_types_.reset();
  schema_item_.reset();
  is_schema_item_ready_ = false;
  micro_index_clustered_ = false;
  is_no_logging_ = false;
}

bool ObTabletDirectLoadMgr::is_valid()
{
  return is_inited_ == true && tablet_id_.is_valid()
      && is_valid_direct_load(direct_load_type_);
}

int ObTabletDirectLoadMgr::update(
    ObBaseTabletDirectLoadMgr *lob_tablet_mgr,
    const ObTabletDirectLoadInsertParam &build_param)
{
  int ret = OB_SUCCESS;
  const int64_t bucket_num = 97L; // 97
  const int64_t memory_limit = 1024LL * 1024LL * 1024LL * 10LL; // 10GB
  ObLSService *ls_service = nullptr;
  ObLS *ls = nullptr;
  ObTabletHandle tablet_handle;
  if (OB_UNLIKELY(!build_param.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(build_param));
  } else if (OB_ISNULL(ls_service = share::g_mp->ls_service())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected err", K(ret));
  } else if (OB_FAIL(ls_service->get_ls(ls))) {
    LOG_WARN("failed to get log stream", K(ret), K(build_param));
  } else if (OB_FAIL(ObDDLUtil::ddl_get_tablet(ls,
                                               build_param.common_param_.tablet_id_,
                                               tablet_handle,
                                               ObMDSGetTabletMode::READ_WITHOUT_CHECK))) {
    LOG_WARN("get tablet handle failed", K(ret), K(build_param));
  } else if (OB_FAIL(prepare_storage_schema(tablet_handle))) {
    LOG_WARN("fail to prepare storage schema", K(ret), K(tablet_handle));
  } else if (OB_ISNULL(sqc_build_ctx_.storage_schema_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("null storage schema", K(ret));
  } else if (nullptr != lob_tablet_mgr) {
    // has lob
    ObTabletDirectLoadInsertParam lob_param;
    ObSchemaGetterGuard schema_guard;
    ObTabletBindingMdsUserData ddl_data;
    const ObTableSchema *table_schema = nullptr;
    if (OB_FAIL(lob_param.assign(build_param))) {
      LOG_WARN("assign lob parameter failed", K(ret));
    } else if (OB_FAIL(tablet_handle.get_obj()->ObITabletMdsInterface::get_ddl_data(share::SCN::max_scn(), ddl_data))) {
      LOG_WARN("get ddl data failed", K(ret));
    } else if (OB_FALSE_IT(lob_param.common_param_.tablet_id_ = ddl_data.lob_meta_tablet_id_)) {
    } else if (OB_FAIL(ObMultiVersionSchemaService::get_instance().get_tenant_schema_guard(
      schema_guard, lob_param.runtime_only_param_.schema_version_))) {
      LOG_WARN("get tenant schema failed", K(ret), K(lob_param));
    } else if (OB_FAIL(schema_guard.get_table_schema(
              lob_param.runtime_only_param_.table_id_, table_schema))) {
      LOG_WARN("get table schema failed", K(ret), K(lob_param));
    } else if (OB_ISNULL(table_schema)) {
      ret = OB_TABLE_NOT_EXIST;
      LOG_WARN("table not exist", K(ret), K(lob_param));
    } else {
      lob_param.runtime_only_param_.table_id_ = table_schema->get_aux_lob_meta_tid();
    }

    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(lob_mgr_handle_.set_obj(lob_tablet_mgr))) {
      LOG_WARN("set lob direct load mgr failed", K(ret), K(lob_param));
    } else if (OB_FAIL(lob_mgr_handle_.get_obj()->update(nullptr, lob_param))) {
      LOG_WARN("init lob failed", K(ret), K(lob_param));
    } else {
      LOG_INFO("set lob mgr handle", K(lob_param));
    }
  }

  if (OB_FAIL(ret)) {
  } else if (!build_param.is_replay_ && !sqc_build_ctx_.slice_mgr_map_.created()) {
    // 1. Create slice_mgr_map if the tablet_direct_load_mgr is created firstly.
    // 2. Create slice_mgr_map if the node is switched from follower to leader.
    
    lib::ObMemAttr attr("TabletDLMgr");
    lib::ObMemAttr slice_writer_attr("SliceWriter");
    lib::ObMemAttr slice_writer_map_attr("SliceWriterMap");
    if (OB_FAIL(sqc_build_ctx_.allocator_.init(OB_MALLOC_MIDDLE_BLOCK_SIZE,
      attr.label_, memory_limit))) {
      LOG_WARN("init alloctor failed", K(ret));
    } else if (OB_FAIL(sqc_build_ctx_.slice_writer_allocator_.init(OB_MALLOC_MIDDLE_BLOCK_SIZE,
      slice_writer_attr.label_, memory_limit))) {
      LOG_WARN("init allocator failed", K(ret));
    } else if (OB_FAIL(sqc_build_ctx_.slice_mgr_map_.create(bucket_num,
                                                      slice_writer_map_attr, slice_writer_map_attr))) {
      LOG_WARN("create slice writer map failed", K(ret));
    } else if (OB_FAIL(cond_.init(ObWaitEventIds::DIRECT_LOAD_RESCAN_LOCK_WAIT))) {
      LOG_WARN("init condition failed", K(ret));
    } else {
      sqc_build_ctx_.allocator_.set_attr(attr);
      sqc_build_ctx_.slice_writer_allocator_.set_attr(slice_writer_attr);
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(sqc_build_ctx_.build_param_.assign(build_param))) {
      LOG_WARN("assign build param failed", K(ret));
    } else {
      tablet_id_ = build_param.common_param_.tablet_id_;
      direct_load_type_ = build_param.common_param_.direct_load_type_;
      tenant_data_version_ = build_param.common_param_.data_format_version_;
      micro_index_clustered_ = tablet_handle.get_obj()->get_tablet_meta().micro_index_clustered_;
      is_inited_ = true;
    }
  }
  return ret;
}

int ObTabletDirectLoadMgr::open_sstable_slice(
    const bool is_data_tablet_process_for_lob,
    const blocksstable::ObMacroDataSeq &start_seq,
    const ObDirectLoadSliceInfo &slice_info)
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret), KPC(this));
  } else if (OB_UNLIKELY(!start_seq.is_valid() || !slice_info.is_valid() || !sqc_build_ctx_.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(tablet_id_), K(start_seq), K(slice_info), K(sqc_build_ctx_));
  } else if (is_data_tablet_process_for_lob) {
    if (OB_UNLIKELY(!lob_mgr_handle_.is_valid())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected err", K(ret), KPC(this));
    } else if (OB_FAIL(lob_mgr_handle_.get_obj()->open_sstable_slice(
        false, start_seq, slice_info))) {
      LOG_WARN("open sstable slice for lob failed", K(ret), KPC(this));
    }
  } else if (OB_UNLIKELY(!is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected err", K(ret), KPC(this));
  } else if (OB_FAIL(prepare_schema_item_on_demand(sqc_build_ctx_.build_param_.runtime_only_param_.table_id_,
                                                   sqc_build_ctx_.build_param_.runtime_only_param_.parallel_))) {
    LOG_WARN("prepare table schema item on demand", K(ret), K(sqc_build_ctx_.build_param_));
  } else {
    ObDirectLoadSliceWriter *slice_writer = nullptr;
    if (OB_ISNULL(slice_writer = OB_NEWx(ObDirectLoadSliceWriter, (&sqc_build_ctx_.slice_writer_allocator_)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to new ObDirectLoadSliceWriter", KR(ret));
    } else if (OB_FAIL(slice_writer->init(this, start_seq, slice_info.slice_idx_, slice_info.merge_slice_idx_))) {
      LOG_WARN("init sstable slice writer failed", K(ret), K(start_seq), K(slice_info), KPC(this));
    } else if (OB_FAIL(sqc_build_ctx_.slice_mgr_map_.set_refactored(ObTabletDirectLoadBuildCtx::SliceKey(slice_info.context_id_, slice_info.slice_id_), slice_writer))) {
      LOG_WARN("set refactored failed", K(ret), K(slice_info), KPC(this));
    } else {
      LOG_INFO("add a slice writer", KP(slice_writer), K(slice_info), K(sqc_build_ctx_.slice_mgr_map_.size()),
          KP(sqc_build_ctx_.index_builder_));
    }
    if (OB_FAIL(ret)) {
      if (OB_NOT_NULL(slice_writer)) {
        slice_writer->~ObDirectLoadSliceWriter();
        sqc_build_ctx_.slice_writer_allocator_.free(slice_writer);
        slice_writer = nullptr;
      }
    }
  }
  return ret;
}

int ObTabletDirectLoadMgr::prepare_schema_item_on_demand(const uint64_t table_id,
                                                         const int64_t parallel)
{
  int ret = OB_SUCCESS;
  uint32_t lock_tid = 0;
  const bool is_schema_item_ready = ATOMIC_LOAD(&is_schema_item_ready_);
  if (!is_schema_item_ready) {
    if (OB_FAIL(wrlock(TRY_LOCK_TIMEOUT, lock_tid))) {
      LOG_WARN("failed to wrlock", K(ret), KPC(this));
    } else if (is_schema_item_ready_) {
      // do nothing
    } else if (OB_UNLIKELY(OB_INVALID_ID == table_id)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid arguments", K(ret), K(table_id));
    } else {
      
      ObSchemaGetterGuard schema_guard;
      const ObDataStoreDesc &data_desc = sqc_build_ctx_.data_block_desc_.get_desc();
      const ObTableSchema *table_schema = nullptr;
      const ObTableSchema *data_table_schema = nullptr;
      bool is_vector_data_complement = false;
      if (OB_FAIL(ObMultiVersionSchemaService::get_instance().get_tenant_schema_guard(schema_guard))) {
        LOG_WARN("get tenant schema failed", K(ret), K(table_id));
      } else if (OB_FAIL(schema_guard.get_table_schema( table_id, table_schema))) {
        LOG_WARN("get table schema failed", K(ret), K(table_id));
      } else if (OB_ISNULL(table_schema)) {
        ret = OB_TABLE_NOT_EXIST;
        LOG_WARN("table not exist", K(ret), K(table_id));
      } else if (OB_FAIL(prepare_index_builder_if_need(*table_schema))) {
        LOG_WARN("prepare sstable index builder failed", K(ret), K(sqc_build_ctx_));
      } else if (FALSE_IT(is_vector_data_complement= ObDirectLoadMgrUtil::need_process_vec_index(table_schema->get_index_type()))) {
      } else if (is_vector_data_complement && 
                 OB_FAIL(ObDirectLoadMgrUtil::prepare_schema_item_for_vec_idx_data(schema_guard,
                                                                                   table_schema,
                                                                                   data_table_schema,
                                                                                   sqc_build_ctx_.schema_allocator_,
                                                                                   schema_item_))) {
        LOG_WARN("fail to prepare vector index data", K(ret));
      }
      if (OB_FAIL(ret)) {
      } else {
        schema_item_.is_index_table_ = table_schema->is_index_table();
        schema_item_.rowkey_column_num_ = table_schema->get_rowkey_column_num();
        schema_item_.is_unique_index_ = table_schema->is_unique_index();
        schema_item_.lob_inrow_threshold_ = is_vector_data_complement ?
                                            data_table_schema->get_lob_inrow_threshold() :
                                            table_schema->get_lob_inrow_threshold();

        if (OB_FAIL(column_items_.reserve(data_desc.get_col_desc_array().count()))) {
          LOG_WARN("reserve column schema array failed", K(ret), K(data_desc.get_col_desc_array().count()), K(column_items_));
        } else {
          for (int64_t i = 0; OB_SUCC(ret) && i < data_desc.get_col_desc_array().count(); ++i) {
            const ObColDesc &col_desc = data_desc.get_col_desc_array().at(i);
            const schema::ObColumnSchemaV2 *column_schema = nullptr;
            const schema::ObColumnSchemaV2 *data_column_schema = nullptr;
            ObColumnSchemaItem column_item;
            if (i >= table_schema->get_rowkey_column_num() && i < table_schema->get_rowkey_column_num() + ObMultiVersionRowkeyHelpper::get_extra_rowkey_col_cnt()) {
              // skip multi version column, keep item invalid
              column_item.col_type_ = col_desc.col_type_; // for append_batch
            } else if (OB_ISNULL(column_schema = table_schema->get_column_schema(col_desc.col_id_))) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("column schema is null", K(ret), K(i), K(data_desc.get_col_desc_array()), K(col_desc.col_id_));
            } else if (is_vector_data_complement && OB_ISNULL(data_column_schema = data_table_schema->get_column_schema(col_desc.col_id_))) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("data column schema is null", K(ret), K(i), K(data_desc.get_col_desc_array()), K(col_desc.col_id_));
            } else {
              column_item.is_valid_ = true;
              column_item.col_type_ = column_schema->get_meta_type();
              column_item.col_accuracy_ = column_schema->get_accuracy();
              if (is_vector_data_complement) {
                column_item.column_flags_ = data_column_schema->get_column_flags();
              }
            }
            if (OB_SUCC(ret)) {
              if (OB_FAIL(column_items_.push_back(column_item))) {
                LOG_WARN("push back null column schema failed", K(ret));
              } else if (OB_NOT_NULL(column_schema) && column_schema->get_meta_type().is_lob_storage()) { // not multi version column
                if (OB_FAIL(lob_column_idxs_.push_back(i))) {
                  LOG_WARN("push back lob column idx failed", K(ret), K(i));
                } else if (OB_FAIL(lob_col_types_.push_back(column_schema->get_meta_type()))) {
                  LOG_WARN("push back lob col_type  failed", K(ret), K(i));
                } else if (i < table_schema->get_rowkey_column_num()) {
                  schema_item_.has_lob_rowkey_ = true;
                }
              }
            }
          }
        }
      }
      if (OB_SUCC(ret)) {
        ATOMIC_STORE(&is_schema_item_ready_, true);
      }
    }
  }
  if (0 != lock_tid) {
    unlock(lock_tid);
  }
  return ret;
}

int ObTabletDirectLoadMgr::fill_sstable_slice(
    const ObDirectLoadSliceInfo &slice_info,
    const SCN &start_scn,
    ObIStoreRowIterator *iter,
    int64_t &affected_rows,
    ObInsertMonitor *insert_monitor)
{
  int ret = OB_SUCCESS;
  affected_rows = 0;
  share::SCN commit_scn;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(!slice_info.is_valid() || !start_scn.is_valid_and_not_min()) || !sqc_build_ctx_.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(slice_info), K(start_scn), K(sqc_build_ctx_));
  } else if (OB_UNLIKELY(!is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected err", K(ret), KPC(this));
  } else if (is_full_direct_load(direct_load_type_)) {
    if (sqc_build_ctx_.get_commit_scn().is_valid_and_not_min()) {
      ret = OB_TRANS_COMMITED;
      FLOG_INFO("already committed", K(commit_scn), KPC(this));
    } else if (start_scn != get_start_scn()) {
      ret = OB_TASK_EXPIRED;
      LOG_WARN("task expired", K(ret), "start_scn of current execution", start_scn, "start_scn latest", get_start_scn());
    }
  }
  if (OB_SUCC(ret)) {
    ObDirectLoadSliceWriter *slice_writer = nullptr;
    ObTabletDirectLoadBuildCtx::SliceKey slice_key(slice_info.context_id_, slice_info.slice_id_);
    if (OB_FAIL(sqc_build_ctx_.slice_mgr_map_.get_refactored(slice_key, slice_writer))) {
      LOG_WARN("get refactored failed", K(ret), K(slice_info));
    } else if (OB_ISNULL(slice_writer) || OB_UNLIKELY(!ATOMIC_LOAD(&is_schema_item_ready_))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected err", K(ret), K(slice_info), K(is_schema_item_ready_));
    } else if (OB_FAIL(slice_writer->fill_sstable_slice(start_scn, sqc_build_ctx_.build_param_.runtime_only_param_.table_id_, tablet_id_,
        sqc_build_ctx_.storage_schema_, iter, schema_item_, direct_load_type_, column_items_, dir_id_,
        sqc_build_ctx_.build_param_.runtime_only_param_.parallel_, slice_info.context_id_, affected_rows, insert_monitor))) {
      LOG_WARN("fill sstable slice failed", K(ret), KPC(this));
    }
  }
  if (OB_FAIL(ret) && (OB_TRANS_COMMITED != ret)) {
    // cleanup when failed.
    int tmp_ret = OB_SUCCESS;
    ObDirectLoadSliceWriter *slice_writer = nullptr;
    ObTabletDirectLoadBuildCtx::SliceKey slice_key(slice_info.context_id_, slice_info.slice_id_);
    if (OB_TMP_FAIL(sqc_build_ctx_.slice_mgr_map_.erase_refactored(slice_key, &slice_writer))) {
      LOG_ERROR("erase failed", K(ret), K(tmp_ret), K(slice_info));
    } else {
      LOG_INFO("erase a slice writer", KP(slice_writer), K(slice_key), K(sqc_build_ctx_.slice_mgr_map_.size()));
      slice_writer->~ObDirectLoadSliceWriter();
      sqc_build_ctx_.slice_writer_allocator_.free(slice_writer);
      slice_writer = nullptr;
    }
  }
  return ret;
}

int ObTabletDirectLoadMgr::fill_sstable_slice(
    const ObDirectLoadSliceInfo &slice_info,
    const SCN &start_scn,
    const ObBatchDatumRows &datum_rows,
    ObInsertMonitor *insert_monitor)
{ 
  int ret = OB_SUCCESS;
  share::SCN commit_scn;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(!slice_info.is_valid() || !start_scn.is_valid_and_not_min()) || !sqc_build_ctx_.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(slice_info), K(start_scn), K(sqc_build_ctx_));
  } else if (OB_UNLIKELY(!is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected err", K(ret), KPC(this));
  } else if (is_full_direct_load(direct_load_type_)) {
    if (sqc_build_ctx_.commit_scn_.is_valid_and_not_min()) {
      ret = OB_TRANS_COMMITED;
      FLOG_INFO("already committed", K(commit_scn), KPC(this));
    } else if (start_scn != get_start_scn()) {
      ret = OB_TASK_EXPIRED;
      LOG_WARN("task expired", K(ret), "start_scn of current execution", start_scn, "start_scn latest", get_start_scn());
    }
  }
  if (OB_SUCC(ret)) {
    ObDirectLoadSliceWriter *slice_writer = nullptr;
    ObTabletDirectLoadBuildCtx::SliceKey slice_key(slice_info.context_id_, slice_info.slice_id_);
    if (OB_FAIL(sqc_build_ctx_.slice_mgr_map_.get_refactored(slice_key, slice_writer))) {
      LOG_WARN("get refactored failed", K(ret), K(slice_info));
    } else if (OB_ISNULL(slice_writer) || OB_UNLIKELY(!ATOMIC_LOAD(&is_schema_item_ready_))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected err", K(ret), K(slice_info), K(is_schema_item_ready_));
    } else if (OB_FAIL(slice_writer->fill_sstable_slice(start_scn,
                                                        sqc_build_ctx_.build_param_.runtime_only_param_.table_id_,
                                                        tablet_id_,
                                                        sqc_build_ctx_.storage_schema_,
                                                        datum_rows,
                                                        schema_item_,
                                                        direct_load_type_,
                                                        column_items_,
                                                        dir_id_,
                                                        sqc_build_ctx_.build_param_.runtime_only_param_.parallel_,
                                                        slice_info.context_id_,
                                                        insert_monitor))) {
      LOG_WARN("fill sstable slice failed", K(ret), KPC(this));
    }
  }
  if (OB_FAIL(ret) && (OB_TRANS_COMMITED != ret)) {
    // cleanup when failed.
    int tmp_ret = OB_SUCCESS;
    ObDirectLoadSliceWriter *slice_writer = nullptr;
    ObTabletDirectLoadBuildCtx::SliceKey slice_key(slice_info.context_id_, slice_info.slice_id_);
    if (OB_TMP_FAIL(sqc_build_ctx_.slice_mgr_map_.erase_refactored(slice_key, &slice_writer))) {
      LOG_ERROR("erase failed", K(ret), K(tmp_ret), K(slice_info));
    } else {
      LOG_INFO("erase a slice writer", KP(slice_writer), K(slice_key), K(sqc_build_ctx_.slice_mgr_map_.size()));
      slice_writer->~ObDirectLoadSliceWriter();
      sqc_build_ctx_.slice_writer_allocator_.free(slice_writer);
      slice_writer = nullptr;
    }
  }
  return ret;
}

int ObTabletDirectLoadMgr::fill_lob_sstable_slice(
    ObIAllocator &allocator,
    const ObDirectLoadSliceInfo &slice_info,
    const SCN &start_scn,
    share::ObTabletCacheInterval &pk_interval,
    blocksstable::ObDatumRow &datum_row)
{
  int ret = OB_SUCCESS;
  share::SCN commit_scn;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(!slice_info.is_valid() || !sqc_build_ctx_.is_valid() || !start_scn.is_valid_and_not_min() ||
      !lob_mgr_handle_.is_valid() || !lob_mgr_handle_.get_obj()->get_sqc_build_ctx().is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(slice_info), "lob_direct_load_mgr is valid", lob_mgr_handle_.is_valid(), KPC(this), K(start_scn));
  } else if (is_full_direct_load(direct_load_type_)) {
    if (sqc_build_ctx_.get_commit_scn().is_valid_and_not_min()) {
      ret = OB_TRANS_COMMITED;
      FLOG_INFO("already committed", K(commit_scn), KPC(this));
    } else if (start_scn != get_start_scn()) {
      ret = OB_TASK_EXPIRED;
      LOG_WARN("task expired", K(ret), "start_scn of current execution", start_scn, "start_scn latest", get_start_scn());
    }
  }

  if (OB_SUCC(ret)) {
    ObDirectLoadSliceWriter *slice_writer = nullptr;
    ObTabletDirectLoadBuildCtx::SliceKey slice_key(slice_info.context_id_, slice_info.slice_id_);
    const int64_t trans_version = is_full_direct_load(direct_load_type_) ? table_key_.get_snapshot_version() : INT64_MAX;
    ObBatchSliceWriteInfo info(tablet_id_, trans_version, direct_load_type_);

    if (OB_FAIL(lob_mgr_handle_.get_obj()->get_sqc_build_ctx().slice_mgr_map_.get_refactored(slice_key, slice_writer))) {
      LOG_WARN("get refactored failed", K(ret), K(slice_info), K(sqc_build_ctx_.slice_mgr_map_.size()));
    } else if (OB_ISNULL(slice_writer) || OB_UNLIKELY(!ATOMIC_LOAD(&(lob_mgr_handle_.get_obj()->is_schema_item_ready_)))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected err", K(ret), K(slice_info), K(lob_mgr_handle_.get_obj()->is_schema_item_ready_));
    } else if (OB_FAIL(slice_writer->fill_lob_sstable_slice(lob_mgr_handle_.get_obj()->sqc_build_ctx_.build_param_.runtime_only_param_.table_id_, allocator, sqc_build_ctx_.allocator_,
          start_scn, info, pk_interval, lob_column_idxs_, lob_col_types_, schema_item_, datum_row))) {
        LOG_WARN("fail to fill batch sstable slice", K(ret), K(start_scn), K(tablet_id_), K(pk_interval));
    }
  }
  if (OB_FAIL(ret) && lob_mgr_handle_.is_valid()) {
    // cleanup when failed.
    int tmp_ret = OB_SUCCESS;
    ObDirectLoadSliceWriter *slice_writer = nullptr;
    ObTabletDirectLoadBuildCtx::SliceKey slice_key(slice_info.context_id_, slice_info.slice_id_);
    if (OB_TMP_FAIL(lob_mgr_handle_.get_obj()->get_sqc_build_ctx().slice_mgr_map_.erase_refactored(slice_key, &slice_writer))) {
      LOG_ERROR("erase failed", K(ret), K(tmp_ret), K(slice_info));
    } else {
      LOG_INFO("erase a slice writer", KP(slice_writer), K(slice_key), K(sqc_build_ctx_.slice_mgr_map_.size()));
      slice_writer->~ObDirectLoadSliceWriter();
      lob_mgr_handle_.get_obj()->get_sqc_build_ctx().slice_writer_allocator_.free(slice_writer);
      slice_writer = nullptr;
    }
  }
  return ret;
}

int ObTabletDirectLoadMgr::fill_lob_sstable_slice(
    ObIAllocator &allocator,
    const ObDirectLoadSliceInfo &slice_info,
    const SCN &start_scn,
    share::ObTabletCacheInterval &pk_interval,
    blocksstable::ObBatchDatumRows &datum_rows)
{
  int ret = OB_SUCCESS;
  share::SCN commit_scn;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(!slice_info.is_valid() || !sqc_build_ctx_.is_valid() || !start_scn.is_valid_and_not_min() ||
      !lob_mgr_handle_.is_valid() || !lob_mgr_handle_.get_obj()->get_sqc_build_ctx().is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(slice_info), "lob_direct_load_mgr is valid", lob_mgr_handle_.is_valid(), KPC(this), K(start_scn));
  } else if (is_full_direct_load(direct_load_type_)) {
    if (sqc_build_ctx_.commit_scn_.is_valid_and_not_min()) {
      ret = OB_TRANS_COMMITED;
      FLOG_INFO("already committed", K(commit_scn), KPC(this));
    } else if (start_scn != get_start_scn()) {
      ret = OB_TASK_EXPIRED;
      LOG_WARN("task expired", K(ret), "start_scn of current execution", start_scn, "start_scn latest", get_start_scn());
    }
  }

  if (OB_SUCC(ret)) {
    ObDirectLoadSliceWriter *slice_writer = nullptr;
    ObTabletDirectLoadBuildCtx::SliceKey slice_key(slice_info.context_id_, slice_info.slice_id_);
    const int64_t trans_version = is_full_direct_load(direct_load_type_) ? table_key_.get_snapshot_version() : INT64_MAX;
    ObBatchSliceWriteInfo info(tablet_id_, trans_version, direct_load_type_);

    if (OB_FAIL(lob_mgr_handle_.get_obj()->get_sqc_build_ctx().slice_mgr_map_.get_refactored(slice_key, slice_writer))) {
      LOG_WARN("get refactored failed", K(ret), K(slice_info), K(sqc_build_ctx_.slice_mgr_map_.size()));
    } else if (OB_ISNULL(slice_writer) || OB_UNLIKELY(!ATOMIC_LOAD(&(lob_mgr_handle_.get_obj()->is_schema_item_ready_)))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected err", K(ret), K(slice_info), K(lob_mgr_handle_.get_obj()->is_schema_item_ready_));
    } else if (OB_FAIL(slice_writer->fill_lob_sstable_slice(lob_mgr_handle_.get_obj()->sqc_build_ctx_.build_param_.runtime_only_param_.table_id_, allocator, sqc_build_ctx_.allocator_, 
          start_scn, info, pk_interval, lob_column_idxs_, lob_col_types_, schema_item_, datum_rows))) {
        LOG_WARN("fail to fill batch sstable slice", K(ret), K(start_scn), K(tablet_id_), K(pk_interval));
    }
  }
  if (OB_FAIL(ret) && lob_mgr_handle_.is_valid()) {
    // cleanup when failed.
    int tmp_ret = OB_SUCCESS;
    ObDirectLoadSliceWriter *slice_writer = nullptr;
    ObTabletDirectLoadBuildCtx::SliceKey slice_key(slice_info.context_id_, slice_info.slice_id_);
    if (OB_TMP_FAIL(lob_mgr_handle_.get_obj()->get_sqc_build_ctx().slice_mgr_map_.erase_refactored(slice_key, &slice_writer))) {
      LOG_ERROR("erase failed", K(ret), K(tmp_ret), K(slice_info));
    } else {
      LOG_INFO("erase a slice writer", KP(slice_writer), K(slice_key), K(sqc_build_ctx_.slice_mgr_map_.size()));
      slice_writer->~ObDirectLoadSliceWriter();
      lob_mgr_handle_.get_obj()->get_sqc_build_ctx().slice_writer_allocator_.free(slice_writer);
      slice_writer = nullptr;
    }
  }
  return ret;
}

struct CancelSliceWriterMapFn
{
public:
  CancelSliceWriterMapFn() {}
  int operator () (hash::HashMapPair<ObTabletDirectLoadBuildCtx::SliceKey, ObDirectLoadSliceWriter *> &entry) {
    int ret = OB_SUCCESS;
    if (nullptr != entry.second) {
      LOG_INFO("slice writer cancel", K(&entry.second), "slice_key", entry.first);
      entry.second->cancel();
    }
    return ret;
  }
};

int ObTabletDirectLoadMgr::cancel()
{
  CancelSliceWriterMapFn cancel_map_fn;
  sqc_build_ctx_.slice_mgr_map_.foreach_refactored(cancel_map_fn);
  return OB_SUCCESS;
}

int ObTabletDirectLoadMgr::close_sstable_slice(
    const bool is_data_tablet_process_for_lob,
    const ObDirectLoadSliceInfo &slice_info,
    const share::SCN &start_scn,
    const int64_t execution_id,
    ObInsertMonitor *insert_monitor,
    blocksstable::ObMacroDataSeq &next_seq)
{
  int ret = OB_SUCCESS;
  next_seq.reset();
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(!slice_info.is_valid() || !start_scn.is_valid_and_not_min() || !sqc_build_ctx_.is_valid() || execution_id < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(slice_info), K(start_scn), K(execution_id), K(sqc_build_ctx_));
  } else if (OB_UNLIKELY(!is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected err", K(ret), KPC(this));
  } else if (is_data_tablet_process_for_lob) {
    if (OB_UNLIKELY(!lob_mgr_handle_.is_valid())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected err", K(ret), K(slice_info));
    } else if (OB_FAIL(lob_mgr_handle_.get_obj()->close_sstable_slice(
        false, slice_info, start_scn, execution_id, nullptr/*insert_monitor*/, next_seq))) {
      LOG_WARN("close lob sstable slice failed", K(ret), K(slice_info));
    }
  } else {
    ObDirectLoadSliceWriter *slice_writer = nullptr;

    ObTabletDirectLoadBuildCtx::SliceKey slice_key(slice_info.context_id_, slice_info.slice_id_);
    if (OB_FAIL(sqc_build_ctx_.slice_mgr_map_.get_refactored(slice_key, slice_writer))) {
      ret = OB_HASH_NOT_EXIST == ret ? OB_ENTRY_NOT_EXIST : ret;
      LOG_WARN("get refactored failed", K(ret), K(slice_info));
    } else if (OB_ISNULL(slice_writer)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected err", K(ret), K(slice_info));
    } else if (OB_FAIL(slice_writer->close())) {
      LOG_WARN("close failed", K(ret), K(slice_info));
    } else if (FALSE_IT(next_seq = slice_writer->get_next_block_start_seq())) {
    } else if (!slice_info.is_lob_slice_ && is_ddl_direct_load(direct_load_type_)) {
      int64_t task_finish_count = -1;
      {
        uint32_t lock_tid = 0;
        if (OB_FAIL(rdlock(TRY_LOCK_TIMEOUT, lock_tid))) {
          LOG_WARN("failed to wrlock", K(ret), KPC(this));
        } else if (start_scn == get_start_scn() && slice_info.is_task_finish_) {
          task_finish_count = ATOMIC_AAF(&sqc_build_ctx_.task_finish_count_, 1);
        }
        if (0 != lock_tid) {
          unlock(lock_tid);
        }
      }
      LOG_INFO("inc task finish count", K(tablet_id_), K(execution_id), K(task_finish_count), K(sqc_build_ctx_.task_total_cnt_));
      if (OB_FAIL(ret)) {
      } else if (OB_ISNULL(sqc_build_ctx_.storage_schema_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("invalid tablet handle", K(ret), KP(sqc_build_ctx_.storage_schema_));
      } else {
        if (task_finish_count >= sqc_build_ctx_.task_total_cnt_) {
          if (ObDirectLoadMgrUtil::need_process_vec_index(sqc_build_ctx_.storage_schema_->get_index_type())) {
            if (OB_FAIL(slice_writer->fill_vector_index_data(sqc_build_ctx_.build_param_.common_param_.read_snapshot_,
                                                             sqc_build_ctx_.storage_schema_,
                                                             start_scn,
                                                             schema_item_,
                                                             insert_monitor,
                                                             slice_info.context_id_))) {
              LOG_WARN("fail to fill vector index data", K(ret));
            }
          }
          // for ddl, write commit log when all slices ready.
          if (OB_FAIL(ret)) {
          } else if (OB_FAIL(close(execution_id, start_scn))) {
            LOG_WARN("close sstable slice failed", K(ret), K(sqc_build_ctx_.build_param_));
          }
        }
      }
    }
    if (OB_NOT_NULL(slice_writer)) {
      int tmp_ret = OB_SUCCESS;
      ObTabletDirectLoadBuildCtx::SliceKey slice_key(slice_info.context_id_, slice_info.slice_id_);
      if (OB_TMP_FAIL(sqc_build_ctx_.slice_mgr_map_.erase_refactored(slice_key))) {
        LOG_ERROR("erase failed", K(ret), K(tmp_ret), K(slice_info));
      } else {
        LOG_INFO("erase a slice writer", K(ret), K(slice_key), KP(slice_writer), K(sqc_build_ctx_.slice_mgr_map_.size()));
        slice_writer->~ObDirectLoadSliceWriter();
        sqc_build_ctx_.slice_writer_allocator_.free(slice_writer);
        slice_writer = nullptr;
      }
      ret = OB_SUCC(ret) ? tmp_ret : ret;
    }
  }
  return ret;
}

int ObTabletDirectLoadMgr::prepare_index_builder_if_need(const ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  ObWholeDataStoreDesc index_block_desc;
  if (sqc_build_ctx_.index_builder_ != nullptr) {
    LOG_INFO("index builder is already prepared");
  } else if (OB_FAIL(index_block_desc.init(true/*is ddl*/, table_schema, tablet_id_,
          is_full_direct_load(direct_load_type_) ? compaction::ObMergeType::MAJOR_MERGE : compaction::ObMergeType::MINOR_MERGE,
          is_full_direct_load(direct_load_type_) ? table_key_.get_snapshot_version() : 1L,
          tenant_data_version_, get_micro_index_clustered(), 0/*concurrent_cnt*/,
          is_full_direct_load(direct_load_type_) ? SCN::invalid_scn() : table_key_.get_end_scn()))) {
    LOG_WARN("fail to init data desc", K(ret));
  } else if (FALSE_IT(index_block_desc.get_static_desc().schema_version_ = sqc_build_ctx_.build_param_.runtime_only_param_.schema_version_)) {
    /* set as a fixed schema version */
  } else {
    void *builder_buf = nullptr;

    if (OB_ISNULL(builder_buf = sqc_build_ctx_.allocator_.alloc(sizeof(ObSSTableIndexBuilder)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to alloc memory", K(ret));
    } else if (OB_ISNULL(sqc_build_ctx_.index_builder_ = new (builder_buf) ObSSTableIndexBuilder(true /*use buffer*/))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to new ObSSTableIndexBuilder", K(ret));
    } else if (OB_FAIL(sqc_build_ctx_.index_builder_->init(
            index_block_desc.get_desc(), // index_block_desc is copied in index_builder
            ObSSTableIndexBuilder::DISABLE))) {
      LOG_WARN("failed to init index builder", K(ret), K(index_block_desc));
    } else if (OB_FAIL(sqc_build_ctx_.data_block_desc_.init(true/*is ddl*/, table_schema, tablet_id_,
            is_full_direct_load(direct_load_type_) ? compaction::ObMergeType::MAJOR_MERGE : compaction::ObMergeType::MINOR_MERGE,
            is_full_direct_load(direct_load_type_) ? table_key_.get_snapshot_version() : 1L,
            tenant_data_version_, get_micro_index_clustered(), 0/*concurrent_cnt*/,
            is_full_direct_load(direct_load_type_) ? SCN::invalid_scn() : table_key_.get_end_scn()))) {
      LOG_WARN("fail to init data block desc", K(ret));
    } else {
      sqc_build_ctx_.data_block_desc_.get_static_desc().schema_version_ = sqc_build_ctx_.build_param_.runtime_only_param_.schema_version_;
      sqc_build_ctx_.data_block_desc_.get_desc().sstable_index_builder_ = sqc_build_ctx_.index_builder_; // for build the tail index block in macro block
    }


    if (OB_FAIL(ret)) {
      if (nullptr != sqc_build_ctx_.index_builder_) {
        sqc_build_ctx_.index_builder_->~ObSSTableIndexBuilder();
        sqc_build_ctx_.index_builder_ = nullptr;
      }
      if (nullptr != builder_buf) {
        sqc_build_ctx_.allocator_.free(builder_buf);
        builder_buf = nullptr;
      }
      sqc_build_ctx_.data_block_desc_.reset();
    }
  }
  return ret;
}

int ObTabletDirectLoadMgr::wrlock(const int64_t timeout_us, uint32_t &tid)
{
  int ret = OB_SUCCESS;
  const int64_t abs_timeout_us = timeout_us + ObTimeUtility::current_time();
  if (OB_SUCC(lock_.wrlock(ObLatchIds::TABLET_DIRECT_LOAD_MGR_LOCK, abs_timeout_us))) {
    tid = static_cast<uint32_t>(GETTID());
  }
  if (OB_TIMEOUT == ret) {
    ret = OB_EAGAIN;
  }
  return ret;
}

int ObTabletDirectLoadMgr::rdlock(const int64_t timeout_us, uint32_t &tid)
{
  int ret = OB_SUCCESS;
  const int64_t abs_timeout_us = timeout_us + ObTimeUtility::current_time();
  if (OB_SUCC(lock_.rdlock(ObLatchIds::TABLET_DIRECT_LOAD_MGR_LOCK, abs_timeout_us))) {
    tid = static_cast<uint32_t>(GETTID());
  }
  if (OB_TIMEOUT == ret) {
    ret = OB_EAGAIN;
  }
  return ret;
}

void ObTabletDirectLoadMgr::unlock(const uint32_t tid)
{
  if (OB_SUCCESS != lock_.unlock(&tid)) {
    ob_abort();
  }
}

int ObTabletDirectLoadMgr::prepare_storage_schema(ObTabletHandle &tablet_handle)
{
  int ret = OB_SUCCESS;
  if (nullptr != sqc_build_ctx_.storage_schema_) {
    LOG_INFO("storage schema has been prepared before", K(*sqc_build_ctx_.storage_schema_));
  } else if (OB_UNLIKELY(!tablet_handle.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid tablet handle", K(ret), K(tablet_handle));
  } else if (OB_FAIL(tablet_handle.get_obj()->load_storage_schema(sqc_build_ctx_.schema_allocator_, sqc_build_ctx_.storage_schema_))) {
    LOG_WARN("load storage schema failed", K(ret));
  }
  return ret;
}

ObTabletFullDirectLoadMgr::ObTabletFullDirectLoadMgr()
  : ObTabletDirectLoadMgr(), start_scn_(share::SCN::min_scn()),
    commit_scn_(share::SCN::min_scn()), execution_id_(-1)
{
}

ObTabletFullDirectLoadMgr::~ObTabletFullDirectLoadMgr()
{
}

int ObTabletFullDirectLoadMgr::update(
    ObBaseTabletDirectLoadMgr *lob_tablet_mgr,
    const ObTabletDirectLoadInsertParam &build_param)
{
  int ret = OB_SUCCESS;
  uint32_t lock_tid = 0;
  if (OB_UNLIKELY(!build_param.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(build_param));
  } else if (OB_FAIL(wrlock(TRY_LOCK_TIMEOUT, lock_tid))) {
    LOG_WARN("failed to wrlock", K(ret), K(build_param));
  } else if (OB_FAIL(ObTabletDirectLoadMgr::update(lob_tablet_mgr, build_param))) {
    LOG_WARN("init failed", K(ret), K(build_param));
  } else {
    table_key_.reset();
    table_key_.tablet_id_ = build_param.common_param_.tablet_id_;
    if (OB_ISNULL(sqc_build_ctx_.storage_schema_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("null storage schema", K(ret));
    } else {
      table_key_.table_type_ = ObITable::MAJOR_SSTABLE;
    }
    table_key_.version_range_.snapshot_version_ = build_param.common_param_.read_snapshot_;
  }
  if (0 != lock_tid) {
    unlock(lock_tid);
  }
  LOG_INFO("init tablet direct load mgr finished", K(ret), K(build_param), KPC(this));
  return ret;
}

int ObTabletFullDirectLoadMgr::open(const int64_t current_execution_id, share::SCN &start_scn)
{
  int ret = OB_SUCCESS;
  uint32_t lock_tid = 0;
  ObLSService *ls_service = nullptr;
  ObLS *ls = nullptr;
  ObTabletHandle tablet_handle;
  ObTabletFullDirectLoadMgr *lob_tablet_mgr = nullptr;
  start_scn.reset();
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(!is_valid() || !sqc_build_ctx_.is_valid() || current_execution_id < 0)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected err", K(ret), KPC(this), K(current_execution_id));
  } else if (OB_FAIL(wrlock(TRY_LOCK_TIMEOUT, lock_tid))) {
    LOG_WARN("failed to wrlock", K(ret), KPC(this));
  } else if (lob_mgr_handle_.is_valid()
    && OB_ISNULL(lob_tablet_mgr = lob_mgr_handle_.get_full_obj())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected err", K(ret), KPC(this));
  } else if (OB_ISNULL(ls_service = share::g_mp->ls_service())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("ls service should not be null", K(ret));
  } else if (OB_FAIL(ls_service->get_ls(ls))) {
    LOG_WARN("get ls failed", K(ret));
  } else if (OB_FAIL(ObDDLUtil::ddl_get_tablet(ls, tablet_id_, tablet_handle))) {
    LOG_WARN("fail to get tablet handle", K(ret), K(tablet_id_));
  } else if (OB_UNLIKELY(!tablet_handle.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet handle is invalid", K(ret), K(tablet_handle));
  } else if (OB_FAIL(FILE_MANAGER_INSTANCE_WITH_MTL_SWITCH.alloc_dir(dir_id_))) {
    LOG_WARN("alloc dir id failed", K(ret));
  } else if (current_execution_id < execution_id_
    || current_execution_id < tablet_handle.get_obj()->get_tablet_meta().ddl_execution_id_) {
    ret = OB_TASK_EXPIRED;
    LOG_INFO("receive a old execution id, don't do start", K(ret), K(current_execution_id), K(sqc_build_ctx_),
      "tablet_meta", tablet_handle.get_obj()->get_tablet_meta());
  } else if (get_commit_scn(tablet_handle.get_obj()->get_tablet_meta()).is_valid_and_not_min()) {
    // has already committed.
    start_scn = start_scn_;
    if (!start_scn.is_valid_and_not_min()) {
      start_scn = tablet_handle.get_obj()->get_tablet_meta().ddl_start_scn_;
    }
    if (!start_scn.is_valid_and_not_min()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("start scn must be valid after commit", K(ret), K(start_scn));
    } else {
      sqc_build_ctx_.commit_scn_.atomic_store(get_commit_scn(tablet_handle.get_obj()->get_tablet_meta()));
    }
  } else if (OB_ISNULL(sqc_build_ctx_.storage_schema_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("null storage schema", K(ret), K(sqc_build_ctx_));
  } else {
    ObDDLKvMgrHandle ddl_kv_mgr_handle;
    ObDDLKvMgrHandle lob_kv_mgr_handle;
    ObTabletDirectLoadMgrHandle direct_load_mgr_handle;
    if (OB_FAIL(direct_load_mgr_handle.set_obj(this))) {
      LOG_WARN("set handle failed", K(ret));
    } else if (OB_FAIL(tablet_handle.get_obj()->get_ddl_kv_mgr(ddl_kv_mgr_handle, true/*try_create*/))) {
      LOG_WARN("create ddl kv mgr failed", K(ret));
    } else if (nullptr != lob_tablet_mgr) {
      ObTabletHandle lob_tablet_handle;
      if (OB_FAIL(ObDDLUtil::ddl_get_tablet(ls, lob_tablet_mgr->get_tablet_id(), lob_tablet_handle))) {
        LOG_WARN("get tablet handle failed", K(ret), KPC(lob_tablet_mgr));
      } else if (OB_FAIL(lob_tablet_handle.get_obj()->get_ddl_kv_mgr(lob_kv_mgr_handle, true/*try_create*/))) {
        LOG_WARN("create ddl kv mgr failed", K(ret));
      }
    }
    if (OB_SUCC(ret)) {
      ObDDLRedoLogWriter redo_writer;
      if (OB_FAIL(redo_writer.init(tablet_id_))) {
        LOG_WARN("init redo writer failed", K(ret), K(tablet_id_));
      } else if (OB_FAIL(redo_writer.write_start_log(table_key_,
        current_execution_id, sqc_build_ctx_.build_param_.common_param_.data_format_version_, direct_load_type_,
        ddl_kv_mgr_handle, lob_kv_mgr_handle, direct_load_mgr_handle, lock_tid, start_scn))) {
        LOG_WARN("fail write start log", K(ret), K(table_key_), K(tenant_data_version_), K(sqc_build_ctx_));
      } else if (OB_UNLIKELY(!start_scn.is_valid_and_not_min())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected err", K(ret), K(start_scn));
      } else if (nullptr != lob_tablet_mgr
        && OB_FAIL(lob_tablet_mgr->init_ddl_table_store(start_scn, table_key_.get_snapshot_version(), start_scn))) {
        LOG_WARN("clean up ddl sstable failed", K(ret), K(start_scn), K(table_key_));
      } else if (OB_FAIL(init_ddl_table_store(start_scn, table_key_.get_snapshot_version(), start_scn))) {
        LOG_WARN("clean up ddl sstable failed", K(ret), K(start_scn), K(table_key_));
      }
    }
  }
  if (lock_tid != 0) {
    unlock(lock_tid);
  }
  return ret;
}

int ObTabletFullDirectLoadMgr::close(const int64_t execution_id, const SCN &start_scn)
{
  int ret = OB_SUCCESS;
  SCN commit_scn;
  ObLSService *ls_service = nullptr;
  ObLS *ls = nullptr;
  ObTabletHandle tablet_handle;
  ObTabletHandle new_tablet_handle;
  bool sstable_already_created = false;
  
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(execution_id < 0 || !start_scn.is_valid_and_not_min())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(execution_id), K(start_scn));
  } else if (OB_ISNULL(ls_service = share::g_mp->ls_service())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("ls service should not be null", K(ret));
  } else if (OB_FAIL(ls_service->get_ls(ls))) {
    LOG_WARN("get ls failed", K(ret));
  } else if (OB_FAIL(ObDDLUtil::ddl_get_tablet(ls, tablet_id_, tablet_handle))) {
    LOG_WARN("fail to get tablet handle", K(ret), K(tablet_id_));
  } else if (OB_UNLIKELY(!tablet_handle.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet handle is invalid", K(ret), K(tablet_handle));
  } else {
    uint32_t lock_tid = 0;
    ObDDLRedoLogWriter redo_writer;
#ifdef ERRSIM
    SERVER_EVENT_SYNC_ADD("storage_ddl", "before_ddl_close",
                          "tablet_id", tablet_id_.id(),
                          "execution_id", execution_id,
                          "start_scn", start_scn);
#endif
    if (OB_FAIL(wrlock(TRY_LOCK_TIMEOUT, lock_tid))) {
      LOG_WARN("failed to wrlock", K(ret), KPC(this));
    } else if (FALSE_IT(sstable_already_created = sqc_build_ctx_.is_task_end_)) {
    } else if (sstable_already_created) {
      LOG_INFO("had already closed", K(ret));
    } else if (OB_FAIL(redo_writer.init(tablet_id_))) {
      LOG_WARN("init redo writer failed", K(ret), K(tablet_id_));
    } else {
      ObTabletDirectLoadMgrHandle direct_load_mgr_handle;
      if (OB_FAIL(direct_load_mgr_handle.set_obj(this))) {
        LOG_WARN("set direct load mgr handle failed", K(ret));
      } else if (OB_FAIL(redo_writer.write_commit_log(table_key_, start_scn,
          direct_load_mgr_handle, tablet_handle, commit_scn, lock_tid))) {
        LOG_WARN("fail write ddl commit log", K(ret), K(table_key_), K(sqc_build_ctx_));
      }
    }
    if (0 != lock_tid) {
      unlock(lock_tid);
    }
  }

  bool is_delay_build_major = false;
#ifdef ERRSIM
    is_delay_build_major = 0 != GCONF.errsim_ddl_major_delay_time;
    sqc_build_ctx_.is_task_end_ = is_delay_build_major ? true : sqc_build_ctx_.is_task_end_;  // skip report checksum
#endif
  if (OB_FAIL(ret) || sstable_already_created) {
  } else if (OB_UNLIKELY(!start_scn.is_valid_and_not_min()) || !commit_scn.is_valid_and_not_min()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected err", K(ret), KPC(this));
  } else if (OB_FAIL(commit(*tablet_handle.get_obj(), start_scn, commit_scn,
      sqc_build_ctx_.build_param_.runtime_only_param_.table_id_, sqc_build_ctx_.build_param_.runtime_only_param_.task_id_, false/*is replay*/))) {
    LOG_WARN("failed to do ddl kv commit", K(ret), KPC(this));
  }

  if (OB_FAIL(ret)) {
  } else if (sstable_already_created || is_delay_build_major) {
    LOG_INFO("sstable had already created, skip waiting for major generated and reporting chksum", K(start_scn), K(commit_scn),
        K(sstable_already_created), K(is_delay_build_major));
  } else if (OB_FAIL(schedule_merge_task(start_scn, commit_scn, true/*wait_major_generate*/, false/*is_replay*/))) {
    LOG_WARN("schedule merge task and wait real major generate", K(ret),
        K(sstable_already_created), K(start_scn), K(commit_scn));
  } else if (lob_mgr_handle_.is_valid() &&
      OB_FAIL(lob_mgr_handle_.get_full_obj()->schedule_merge_task(start_scn, commit_scn, true/*wait_major_generate*/, false/*is_replay*/))) {
    LOG_WARN("schedule merge task and wait real major generate for lob failed", K(ret),
        K(sstable_already_created), K(start_scn), K(commit_scn));
  } else if (OB_FAIL(ObDDLUtil::ddl_get_tablet(ls, tablet_id_, new_tablet_handle))) {
    LOG_WARN("fail to get tablet handle", K(ret), K(tablet_id_));
  } else {
    ObSSTableMetaHandle sst_meta_hdl;
    ObSSTable *first_major_sstable = nullptr;
    ObTabletMemberWrapper<ObTabletTableStore> table_store_wrapper;
    if (OB_FAIL(new_tablet_handle.get_obj()->fetch_table_store(table_store_wrapper))) {
      LOG_WARN("fetch table store failed", K(ret));
    } else if (OB_ISNULL(first_major_sstable = static_cast<ObSSTable *>
      (table_store_wrapper.get_member()->get_major_sstables().get_boundary_table(false/*first*/)))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("no major after wait merge success", K(ret), K(tablet_id_));
    } else if (OB_UNLIKELY(first_major_sstable->get_key() != table_key_)) {
      ret = OB_SNAPSHOT_DISCARDED;
      LOG_WARN("ddl major sstable dropped, snapshot holding may have bug",
        K(ret), KPC(first_major_sstable), K(table_key_), K(tablet_id_), K(sqc_build_ctx_.build_param_), K(sqc_build_ctx_.build_param_.runtime_only_param_.task_id_));
    } else if (OB_FAIL(first_major_sstable->get_meta(sst_meta_hdl))) {
      LOG_WARN("fail to get sstable meta handle", K(ret));
    } else {
      const int64_t *column_checksums = sst_meta_hdl.get_sstable_meta().get_col_checksum();
      int64_t column_count = sst_meta_hdl.get_sstable_meta().get_col_checksum_cnt();
    #ifdef ERRSIM
      if (OB_SUCC(ret)) {
        ret = OB_E(EventTable::EN_DDL_RETRY_WRITE_SLICE_AFTER_SUCC) OB_SUCCESS;
        LOG_INFO("errsim injected, retry to write slice when major exists", K(ret));
      }
    #endif
      if (OB_FAIL(ret)) {
      } else {
        for (int64_t retry_cnt = 10; retry_cnt > 0; retry_cnt--) { // overwrite ret
          if (OB_FAIL(ObTabletDDLUtil::report_ddl_checksum(
                  tablet_id_,
                  sqc_build_ctx_.build_param_.runtime_only_param_.table_id_,
                  execution_id,
                  sqc_build_ctx_.build_param_.runtime_only_param_.task_id_,
                  column_checksums,
                  column_count,
                  tenant_data_version_))) {
            LOG_WARN("report ddl column checksum failed", K(ret), K(tablet_id_), K(execution_id), K(sqc_build_ctx_));
          } else {
            break;
          }
          ob_usleep(100L * 1000L);
        }
      }
    }
    if (OB_SUCC(ret)) {
      sqc_build_ctx_.is_task_end_ = true;
    }
  }
  return ret;
}

int ObTabletFullDirectLoadMgr::start_with_checkpoint(
    ObTablet &tablet,
    const share::SCN &start_scn,
    const uint64_t data_format_version,
    const int64_t execution_id,
    const share::SCN &checkpoint_scn)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!checkpoint_scn.is_valid_and_not_min())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(checkpoint_scn));
  } else if (OB_UNLIKELY(!table_key_.is_valid())) {
    ret = OB_ERR_SYS;
    LOG_WARN("the table key not updated", K(ret), KPC(this));
  } else {
    ObITable::TableKey table_key = table_key_;
    ret = start(tablet, table_key, start_scn, data_format_version, execution_id, checkpoint_scn);
  }
  return ret;
}

// For Leader and follower both.
// For replay start log only, migration_create_tablet and online will no call the intrface.
int ObTabletFullDirectLoadMgr::start(
    ObTablet &tablet,
    const ObITable::TableKey &table_key,
    const share::SCN &start_scn,
    const uint64_t data_format_version,
    const int64_t execution_id,
    const share::SCN &checkpoint_scn)
{
  int ret = OB_SUCCESS;
  share::SCN saved_start_scn;
  int64_t saved_snapshot_version = 0;
  ObDDLKvMgrHandle ddl_kv_mgr_handle;
  ObDDLKvMgrHandle lob_kv_mgr_handle;
  ddl_kv_mgr_handle.reset();
  lob_kv_mgr_handle.reset();
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(table_key != table_key_)
    || !start_scn.is_valid_and_not_min()
    || execution_id < 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(table_key), K(table_key_), K(start_scn), K(execution_id));
  } else if (OB_FAIL(tablet.get_ddl_kv_mgr(ddl_kv_mgr_handle, true/*try_create*/))) {
    LOG_WARN("create tablet ddl kv mgr handle failed", K(ret));
  } else if (lob_mgr_handle_.is_valid()) {
    ObLS *ls = nullptr;
    ObTabletHandle lob_tablet_handle;
    if (OB_ISNULL(share::g_mp->ls_service())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected err", K(ret));
    } else if (OB_FAIL(share::g_mp->ls_service()->get_ls(ls))) {
      LOG_WARN("get ls failed", K(ret));
    } else if (OB_FAIL(ObDDLUtil::ddl_get_tablet(ls, lob_mgr_handle_.get_obj()->get_tablet_id(), lob_tablet_handle))) {
      LOG_WARN("get tablet failed", K(ret));
    } else if (OB_FAIL(lob_tablet_handle.get_obj()->get_ddl_kv_mgr(lob_kv_mgr_handle, true/*try_create*/))) {
      LOG_WARN("create tablet ddl kv mgr handle failed", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    ObLS *ls = nullptr;
    if (OB_FAIL(share::g_mp->ls_service()->get_ls(ls))) {
      LOG_WARN("get ls failed", K(ret));
    } else if (OB_ISNULL(ls->get_ddl_log_handler())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("ls or ddl log handler is null", K(ret), KPC(ls));
    } else if (OB_FAIL(ls->get_ddl_log_handler()->add_tablet(tablet_id_))) {
      LOG_WARN("add tablet id failed", K(ret), K(tablet_id_));
    } else if (lob_kv_mgr_handle.is_valid() && OB_FAIL(ls->get_ddl_log_handler()->add_tablet(lob_mgr_handle_.get_obj()->get_tablet_id()))) {
      LOG_WARN("add lob tablet id failed", K(ret), "lob_tablet_id", lob_mgr_handle_.get_obj()->get_tablet_id());
    }
  }
  if (OB_SUCC(ret)) {
    uint32_t lock_tid = 0;
    if (OB_FAIL(wrlock(TRY_LOCK_TIMEOUT, lock_tid))) {
      LOG_WARN("failed to wrlock", K(ret), KPC(this));
    } else if (OB_FAIL(start_nolock(table_key, start_scn, data_format_version, execution_id, checkpoint_scn, 
        ddl_kv_mgr_handle, lob_kv_mgr_handle))) {
      LOG_WARN("failed to ddl start", K(ret));
    } else {
      // save variables under lock
      saved_start_scn = start_scn_;
      saved_snapshot_version = table_key_.get_snapshot_version();
      const SCN ddl_commit_scn = get_commit_scn(tablet.get_tablet_meta());
      commit_scn_.atomic_store(ddl_commit_scn);
      if (lob_mgr_handle_.is_valid()) {
        lob_mgr_handle_.get_full_obj()->set_commit_scn_nolock(ddl_commit_scn);
      }
    }
    if (0 != lock_tid) {
      unlock(lock_tid);
    }
  }
  if (OB_SUCC(ret) && !checkpoint_scn.is_valid_and_not_min()) {
    // remove ddl sstable if exists and flush ddl start log ts and snapshot version into tablet meta.
    // persist lob meta tablet before data tablet is necessary, to avoid start-loss for lob meta tablet when recovered from checkpoint.
    if (lob_mgr_handle_.is_valid() &&
      OB_FAIL(lob_mgr_handle_.get_full_obj()->init_ddl_table_store(saved_start_scn, saved_snapshot_version, saved_start_scn))) {
      LOG_WARN("clean up ddl sstable failed", K(ret));
    } else if (OB_FAIL(init_ddl_table_store(saved_start_scn, saved_snapshot_version, saved_start_scn))) {
      LOG_WARN("clean up ddl sstable failed", K(ret), K(tablet_id_));
    }
  }
  FLOG_INFO("start full direct load mgr finished", K(ret), K(start_scn), K(execution_id), KPC(this));
  return ret;
}

int ObTabletFullDirectLoadMgr::start_nolock(
    const ObITable::TableKey &table_key,
    const share::SCN &start_scn,
    const uint64_t data_format_version,
    const int64_t execution_id,
    const SCN &checkpoint_scn,
    ObDDLKvMgrHandle &ddl_kv_mgr_handle,
    ObDDLKvMgrHandle &lob_kv_mgr_handle)
{
  int ret = OB_SUCCESS;
  bool is_brand_new = false;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret), K(is_inited_));
  } else if (OB_UNLIKELY(!table_key.is_valid() || !start_scn.is_valid_and_not_min() || data_format_version < 0 || execution_id < 0
      || (checkpoint_scn.is_valid_and_not_min() && checkpoint_scn < start_scn)) || !ddl_kv_mgr_handle.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(table_key), K(start_scn), K(data_format_version), K(execution_id), K(checkpoint_scn),
      "kv_mgr_handle is valid", ddl_kv_mgr_handle.is_valid());
  } else if (table_key.get_tablet_id() != tablet_id_ || table_key_ != table_key) {
    ret = OB_ERR_SYS;
    LOG_WARN("tablet id not same", K(ret), K(table_key), K(table_key_), K(tablet_id_));
  } else {
    if (start_scn_.is_valid_and_not_min()) {
      if (execution_id >= execution_id_ && start_scn >= start_scn_) {
        is_brand_new = true;
        LOG_INFO("execution id changed, need cleanup", K(tablet_id_), K(execution_id_), K(execution_id), K(start_scn_), K(start_scn));
      } else {
        if (!checkpoint_scn.is_valid_and_not_min()) {
          // only return error code when not start from checkpoint.
          ret = OB_TASK_EXPIRED;
        }
        LOG_INFO("ddl start ignored", K(tablet_id_), K(execution_id_), K(execution_id), K(start_scn_), K(start_scn), K(checkpoint_scn));
      }
    } else {
      is_brand_new = true;
      FLOG_INFO("ddl start brand new", K(table_key), K(start_scn), K(execution_id), KPC(this));
    }
    if (OB_SUCC(ret) && is_brand_new) {
      if (OB_FAIL(cleanup_unlock())) {
        LOG_WARN("cleanup unlock failed", K(ret));
      } else {
        table_key_ = table_key;
        tenant_data_version_ = data_format_version;
        execution_id_ = execution_id;
        start_scn_.atomic_store(start_scn);
        ddl_kv_mgr_handle.get_obj()->set_max_freeze_scn(SCN::max(start_scn, checkpoint_scn));
        sqc_build_ctx_.reset_slice_ctx_on_demand();
      }
    }
  }
  if (OB_SUCC(ret) && lob_mgr_handle_.is_valid()) {
    // For lob meta tablet recover from checkpoint, execute start itself to avoid the data loss when,
    // 1. lob meta tablet recover from checkpoint;
    // 2. replay some data redo log on lob meta tablet.
    // 3. data tablet recover from checkpoint, and cleanup will be triggered if lob meta tablet
    //    execute start again.
    ObDDLKvMgrHandle unused_kv_mgr_handle;
    ObITable::TableKey lob_table_key;
    lob_table_key.tablet_id_ = lob_mgr_handle_.get_full_obj()->get_tablet_id();
    lob_table_key.table_type_ = ObITable::TableType::MAJOR_SSTABLE;
    lob_table_key.version_range_ = table_key.version_range_;
    if (OB_FAIL(lob_mgr_handle_.get_full_obj()->start_nolock(lob_table_key, start_scn, data_format_version, execution_id, checkpoint_scn,
        lob_kv_mgr_handle, unused_kv_mgr_handle))) {
      LOG_WARN("start nolock for lob meta tablet failed", K(ret));
    }
  }
  FLOG_INFO("start_nolock full direct load mgr finished", K(ret), K(start_scn), K(execution_id), KPC(this));
  return ret;
}

int ObTabletFullDirectLoadMgr::commit(
    ObTablet &tablet,
    const share::SCN &start_scn,
    const share::SCN &commit_scn,
    const uint64_t table_id,
    const int64_t ddl_task_id,
    const bool is_replay)
{
  int ret = OB_SUCCESS;
  ObDDLKvMgrHandle ddl_kv_mgr_handle;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret), K(is_inited_));
  } else if (!is_started()) {
    ret = OB_STATE_NOT_MATCH;
    LOG_WARN("ddl not started", K(ret), KPC(this));
  } else if (start_scn < get_start_scn()) {
    ret = OB_TASK_EXPIRED;
    LOG_INFO("skip ddl commit log", K(start_scn), K(*this));
  } else if (OB_FAIL(tablet.get_ddl_kv_mgr(ddl_kv_mgr_handle))) {
    LOG_WARN("create ddl kv mgr failed", K(ret));
  } else if (OB_FAIL(ddl_kv_mgr_handle.get_obj()->freeze_ddl_kv(
    start_scn, table_key_.get_snapshot_version(), tenant_data_version_, commit_scn))) {
    LOG_WARN("failed to start prepare", K(ret), K(tablet_id_), K(commit_scn));
  } else if (OB_FAIL(set_commit_scn(commit_scn))) {
    LOG_WARN("failed to set commit scn", K(ret));
  } else {
    ret = OB_EAGAIN;
    while (OB_EAGAIN == ret) {
      if (OB_FAIL(update_major_sstable())) {
        LOG_WARN("update ddl major sstable failed", K(ret), K(tablet_id_), K(start_scn), K(commit_scn));
      }
      if (OB_EAGAIN == ret) {
        usleep(1000L);
      }
    }

    if (OB_SUCC(ret)) {
      if (OB_FAIL(schedule_merge_task(start_scn, commit_scn, false/*wait_major_generate*/, is_replay))) {
        LOG_WARN("schedule major merge task failed", K(ret));
      }
    }
  }
  if (OB_SUCC(ret) && lob_mgr_handle_.is_valid()) {
    const ObTabletID &lob_tablet_id = lob_mgr_handle_.get_full_obj()->get_tablet_id();
    ObLS *ls = nullptr;
    ObTabletHandle lob_tablet_handle;
    if (OB_ISNULL(share::g_mp->ls_service())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected err", K(ret));
    } else if (OB_FAIL(share::g_mp->ls_service()->get_ls(ls))) {
      LOG_WARN("get ls failed", K(ret));
    } else if (OB_FAIL(ls->get_tablet(lob_tablet_id, lob_tablet_handle,
            ObTabletCommon::DEFAULT_GET_TABLET_DURATION_US, ObMDSGetTabletMode::READ_WITHOUT_CHECK))) {
      LOG_WARN("get tablet handle failed", K(ret), K(lob_tablet_id));
    } else if (OB_FAIL(lob_mgr_handle_.get_full_obj()->commit(*lob_tablet_handle.get_obj(), start_scn, commit_scn, table_id, ddl_task_id, is_replay))) {
      LOG_WARN("commit for lob failed", K(ret), K(start_scn), K(commit_scn));
    }
  }
  return ret;
}

int ObTabletFullDirectLoadMgr::schedule_merge_task(
    const share::SCN &start_scn,
    const share::SCN &commit_scn,
    const bool wait_major_generated,
    const bool is_replay)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret), K(is_inited_));
  } else if (OB_UNLIKELY(!start_scn.is_valid_and_not_min() || !commit_scn.is_valid_and_not_min() || (is_replay && wait_major_generated))) {
    ret = OB_ERR_SYS;
    LOG_WARN("unknown start scn or commit snc", K(ret), K(start_scn), K(commit_scn), K(is_replay), K(wait_major_generated));
  } else {
    const int64_t wait_start_ts = ObTimeUtility::fast_current_time();
    while (OB_SUCC(ret)) {
      if (OB_FAIL(THIS_WORKER.check_status())) {
        LOG_WARN("check status failed", K(ret));
      } else {
        ObDDLTableMergeDagParam param;
        param.direct_load_type_    = direct_load_type_;
        param.tablet_id_           = tablet_id_;
        param.rec_scn_             = commit_scn;
        param.is_commit_           = true;
        param.start_scn_           = start_scn;
        param.data_format_version_ = tenant_data_version_;
        param.snapshot_version_    = table_key_.get_snapshot_version();
        if (OB_FAIL(compaction::ObScheduleDagFunc::schedule_ddl_table_merge_dag(param))) {
          if (OB_SIZE_OVERFLOW != ret && OB_EAGAIN != ret) {
            LOG_WARN("schedule ddl merge dag failed", K(ret), K(param));
          } else {
            ret = OB_SUCCESS;
            if (is_replay) {
              break;
            }
          }
        } else if (!wait_major_generated) {
          // schedule successfully and no need to wait physical major generates.
          break;
        }
      }
      if (OB_SUCC(ret)) {
        const ObSSTable *first_major_sstable = nullptr;
        ObTabletMemberWrapper<ObTabletTableStore> table_store_wrapper;
        if (OB_FAIL(ObTabletDDLUtil::check_and_get_major_sstable(tablet_id_, first_major_sstable, table_store_wrapper))) {
          LOG_WARN("check if major sstable exist failed", K(ret));
        } else if (nullptr != first_major_sstable) {
          FLOG_INFO("major has already existed", KPC(this));
          break;
        }
      }
      if (REACH_TIME_INTERVAL(10L * 1000L * 1000L)) {
        LOG_INFO("wait build ddl sstable", K(ret), K(tablet_id_), K(start_scn), K(commit_scn),
            "wait_elpased_s", (ObTimeUtility::fast_current_time() - wait_start_ts) / 1000000L);
      }
    }
  }
  return ret;
}

void ObTabletFullDirectLoadMgr::set_commit_scn_nolock(const share::SCN &scn)
{
  commit_scn_.atomic_store(scn);
  if (lob_mgr_handle_.is_valid()) {
    lob_mgr_handle_.get_full_obj()->set_commit_scn_nolock(scn);
  }
}

int ObTabletFullDirectLoadMgr::set_commit_scn(const share::SCN &commit_scn)
{
  int ret = OB_SUCCESS;
  ObLS *ls = nullptr;
  ObTabletHandle tablet_handle;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret), K(is_inited_));
  } else if (OB_UNLIKELY(!commit_scn.is_valid_and_not_min())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(commit_scn));
  } else if (OB_FAIL(share::g_mp->ls_service()->get_ls(ls))) {
    LOG_WARN("failed to get log stream", K(ret));
  } else if (OB_FAIL(ls->get_tablet(tablet_id_,
                                                    tablet_handle,
                                                    ObTabletCommon::DEFAULT_GET_TABLET_DURATION_US,
                                                    ObMDSGetTabletMode::READ_WITHOUT_CHECK))) {
    LOG_WARN("get tablet handle failed", K(ret), K(tablet_id_));
  } else {
    uint32_t lock_tid = 0;
    if (OB_FAIL(wrlock(TRY_LOCK_TIMEOUT, lock_tid))) {
      LOG_WARN("failed to wrlock", K(ret), KPC(this));
    } else {
      const share::SCN old_commit_scn = get_commit_scn(tablet_handle.get_obj()->get_tablet_meta());
      if (old_commit_scn.is_valid_and_not_min() && old_commit_scn != commit_scn) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("already committed by others", K(ret), K(commit_scn), KPC(this));
      } else {
        commit_scn_.atomic_store(commit_scn);
      }
    }
    if (0 != lock_tid) {
      unlock(lock_tid);
    }
  }
  return ret;
}

// return latest commit_scn iff tablet_meta is newer than the creation of ObTabletFullDirectLoadMgr
share::SCN ObTabletFullDirectLoadMgr::get_commit_scn(const ObTabletMeta &tablet_meta)
{
  share::SCN mgr_commit_scn = commit_scn_.atomic_load();
  share::SCN commit_scn = share::SCN::min_scn();
  if (tablet_meta.ddl_commit_scn_.is_valid_and_not_min() || mgr_commit_scn.is_valid_and_not_min()) {
    if (tablet_meta.ddl_commit_scn_.is_valid_and_not_min()) {
      commit_scn = tablet_meta.ddl_commit_scn_;
    } else {
      commit_scn = mgr_commit_scn;
    }
  } else {
    commit_scn = share::SCN::min_scn();
  }
  return commit_scn;
}

share::SCN ObTabletFullDirectLoadMgr::get_start_scn()
{
  return start_scn_.atomic_load();
}

int ObTabletFullDirectLoadMgr::can_schedule_major_compaction_nolock(
    const ObTablet &tablet,
    bool &can_schedule)
{
  int ret = OB_SUCCESS;
  can_schedule = false;
  share::SCN commit_scn;
  const ObTabletMeta &tablet_meta = tablet.get_tablet_meta();
  ObTabletMemberWrapper<ObTabletTableStore> table_store_wrapper;

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(tablet.fetch_table_store(table_store_wrapper))) {
    LOG_WARN("fetch table store failed", K(ret));
  } else if (nullptr != table_store_wrapper.get_member()->get_major_sstables().get_boundary_table(false/*first*/)) {
    // major sstable has already existed.
  } else {
    can_schedule = get_commit_scn(tablet_meta).is_valid_and_not_min() ? true : false;
  }
  return ret;
}

int ObTabletFullDirectLoadMgr::prepare_ddl_merge_param(
    const ObTablet &tablet,
    ObDDLTableMergeDagParam &merge_param)
{
  int ret = OB_SUCCESS;
  uint32_t lock_tid = 0;
  bool can_schedule = false;
  if (OB_FAIL(rdlock(TRY_LOCK_TIMEOUT, lock_tid))) {
    LOG_WARN("failed to wrlock", K(ret), KPC(this));
  } else if (OB_FAIL(can_schedule_major_compaction_nolock(tablet, can_schedule))) {
    LOG_WARN("check can schedule major compaction failed", K(ret));
  } else if (can_schedule) {
    merge_param.direct_load_type_ = direct_load_type_;
    merge_param.tablet_id_ = tablet_id_;
    merge_param.rec_scn_ = get_commit_scn(tablet.get_tablet_meta());
    merge_param.is_commit_ = true;
    merge_param.start_scn_ = start_scn_;
    merge_param.data_format_version_ = tenant_data_version_;
    merge_param.snapshot_version_    = table_key_.get_snapshot_version();
  } else {
    merge_param.direct_load_type_ = direct_load_type_;
    merge_param.tablet_id_ = tablet_id_;
    merge_param.start_scn_ = start_scn_;
    merge_param.data_format_version_ = tenant_data_version_;
    merge_param.snapshot_version_    = table_key_.get_snapshot_version();
  }
  if (0 != lock_tid) {
    unlock(lock_tid);
  }
  return ret;
}

int ObTabletFullDirectLoadMgr::prepare_major_merge_param(
    ObTabletDDLParam &param)
{
  int ret = OB_SUCCESS;
  uint32_t lock_tid = 0;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret), K(is_inited_));
  } else if (OB_FAIL(rdlock(TRY_LOCK_TIMEOUT, lock_tid))) {
    LOG_WARN("failed to wrlock", K(ret), KPC(this));
  } else if (!is_started()) {
    ret = OB_STATE_NOT_MATCH;
    LOG_WARN("ddl not started", K(ret));
  } else {
    param.direct_load_type_ = direct_load_type_;
    param.table_key_ = table_key_;
    param.start_scn_ = start_scn_;
    param.commit_scn_ = commit_scn_;
    param.snapshot_version_ = table_key_.get_snapshot_version();
    param.data_format_version_ = tenant_data_version_;
  }
  if (0 != lock_tid) {
    unlock(lock_tid);
  }
  return ret;
}

void ObTabletFullDirectLoadMgr::cleanup_slice_writer(const int64_t context_id)
{
  sqc_build_ctx_.cleanup_slice_writer(context_id);
}

int ObTabletFullDirectLoadMgr::cleanup_unlock()
{
  int ret = OB_SUCCESS;
  LOG_INFO("cleanup expired sstables", K(*this));
  ObLS *ls = nullptr;
  ObLSService *ls_service = nullptr;
  ObTabletHandle tablet_handle;
  ObDDLKvMgrHandle ddl_kv_mgr_handle;
  if (OB_ISNULL(ls_service = share::g_mp->ls_service())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("ls service should not be null", K(ret));
  } else if (OB_FAIL(ls_service->get_ls(ls))) {
    LOG_WARN("get ls failed", K(ret));
  } else if (OB_FAIL(ObDDLUtil::ddl_get_tablet(ls, tablet_id_, tablet_handle))) {
    LOG_WARN("fail to get tablet handle", K(ret), K(tablet_id_));
  } else if (OB_UNLIKELY(!tablet_handle.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("need replay but tablet handle is invalid", K(ret), K(tablet_handle));
  } else if (OB_FAIL(tablet_handle.get_obj()->get_ddl_kv_mgr(ddl_kv_mgr_handle))) {
    LOG_WARN("create ddl kv mgr failed", K(ret));
  } else if (OB_FAIL(ddl_kv_mgr_handle.get_obj()->cleanup())) {
    LOG_WARN("cleanup failed", K(ret));
  } else {
    table_key_.reset();
    tenant_data_version_ = 0;
    start_scn_.atomic_store(share::SCN::min_scn());
    commit_scn_.atomic_store(share::SCN::min_scn());
    execution_id_ = -1;
  }
  return ret;
}

int ObTabletFullDirectLoadMgr::init_ddl_table_store(
    const share::SCN &start_scn, 
    const int64_t snapshot_version, 
    const share::SCN &ddl_checkpoint_scn)
{
  int ret = OB_SUCCESS;
  uint32_t lock_tid = 0;
  ObLS *ls = nullptr;
  ObTabletHandle tablet_handle;
  ObArenaAllocator tmp_arena("DDLUpdateTblTmp", OB_MALLOC_NORMAL_BLOCK_SIZE);
  ObStorageSchema *storage_schema = nullptr;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(rdlock(TRY_LOCK_TIMEOUT, lock_tid))) {
    LOG_WARN("failed to wrlock", K(ret), KPC(this));
  } else if (OB_UNLIKELY(!start_scn.is_valid_and_not_min() || snapshot_version <= 0 || !ddl_checkpoint_scn.is_valid_and_not_min())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(start_scn), K(snapshot_version), K(ddl_checkpoint_scn));
  } else if (OB_FAIL(share::g_mp->ls_service()->get_ls(ls))) {
    LOG_WARN("failed to get log stream", K(ret));
  } else if (OB_FAIL(ObDDLUtil::ddl_get_tablet(ls,
                                               tablet_id_,
                                               tablet_handle,
                                               ObMDSGetTabletMode::READ_WITHOUT_CHECK))) {
    LOG_WARN("get tablet handle failed", K(ret), K(tablet_id_));
  } else if (OB_FAIL(tablet_handle.get_obj()->load_storage_schema(tmp_arena, storage_schema))) {
    LOG_WARN("failed to load storage schema", K(ret), K(tablet_handle));
  } else {
    ObTableHandleV2 table_handle; // empty
    ObTableHandleV2 sstable_handle;
    ObTabletHandle new_tablet_handle;
    ObArray<ObDDLBlockMeta> empty_meta_array;
    empty_meta_array.set_attr(ObMemAttr("TblFDL_EMA"));

    ObTabletDDLParam ddl_param;
    ddl_param.direct_load_type_ = direct_load_type_;
    ddl_param.table_key_ = table_key_;
    ddl_param.start_scn_ = start_scn;
    ddl_param.commit_scn_ = commit_scn_;
    ddl_param.snapshot_version_ = table_key_.get_snapshot_version();
    ddl_param.data_format_version_ = tenant_data_version_;
    ddl_param.table_key_.table_type_ = ObITable::DDL_DUMP_SSTABLE;
    ddl_param.table_key_.scn_range_.start_scn_ = SCN::scn_dec(start_scn);
    ddl_param.table_key_.scn_range_.end_scn_ = start_scn;

    ObUpdateTableStoreParam param(tablet_handle.get_obj()->get_snapshot_version(),
                                  ObVersionRange::MIN_VERSION, // multi_version_start
                                  storage_schema);
    param.ddl_info_.keep_old_ddl_sstable_ = false;
    param.ddl_info_.ddl_start_scn_ = start_scn;
    param.ddl_info_.ddl_snapshot_version_ = snapshot_version;
    param.ddl_info_.ddl_checkpoint_scn_ = ddl_checkpoint_scn;
    param.ddl_info_.ddl_execution_id_ = execution_id_;
    param.ddl_info_.data_format_version_ = tenant_data_version_;
    if (OB_FAIL(ObTabletDDLUtil::create_ddl_sstable(*tablet_handle.get_obj(), ddl_param, empty_meta_array, ObArray<MacroBlockId>(), nullptr/*first_ddl_sstable*/, 
        storage_schema, nullptr /* mutex not need*/, tmp_arena, sstable_handle))) {
      LOG_WARN("create empty ddl sstable failed", K(ret));
    }
    if (OB_FAIL(ret)) {
    } else if (FALSE_IT(param.sstable_ = static_cast<ObSSTable *>(sstable_handle.get_table()))) {
    } else if (OB_FAIL(ls->update_tablet_table_store(tablet_id_, param, new_tablet_handle))) {
      LOG_WARN("failed to update tablet table store", K(ret), K(tablet_id_), K(param));
    } else {
      LOG_INFO("update tablet success", K(tablet_id_),
          K(ddl_param), "update_table_store_param", param, K(start_scn), K(snapshot_version), K(ddl_checkpoint_scn));
    }
  }
  if (0 != lock_tid) {
    unlock(lock_tid);
  }
  ObTabletObjLoadHelper::free(tmp_arena, storage_schema);
  return ret;
}

int ObTabletFullDirectLoadMgr::update_major_sstable()
{
  int ret = OB_SUCCESS;
  ObLS *ls = nullptr;
  ObTabletHandle tablet_handle;
  ObArenaAllocator tmp_arena("DDLUpdateTblTmp", OB_MALLOC_NORMAL_BLOCK_SIZE);
  ObStorageSchema *storage_schema = nullptr;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(share::g_mp->ls_service()->get_ls(ls))) {
    LOG_WARN("failed to get log stream", K(ret));
  } else if (OB_FAIL(ObDDLUtil::ddl_get_tablet(ls,
                                               tablet_id_,
                                               tablet_handle,
                                               ObMDSGetTabletMode::READ_WITHOUT_CHECK))) {
    LOG_WARN("get tablet handle failed", K(ret), K(tablet_id_));
  } else if (OB_FAIL(tablet_handle.get_obj()->load_storage_schema(tmp_arena, storage_schema))) {
    LOG_WARN("load storage schema failed", K(ret), K(tablet_id_));
  } else {
    ObTabletHandle new_tablet_handle;
    ObUpdateTableStoreParam param(tablet_handle.get_obj()->get_snapshot_version(),
                                  ObVersionRange::MIN_VERSION, // multi_version_start
                                  storage_schema);
    param.ddl_info_.keep_old_ddl_sstable_ = true;
    param.ddl_info_.ddl_commit_scn_ = get_commit_scn(tablet_handle.get_obj()->get_tablet_meta()); // ddl commit scn may larger than ddl checkpoint scn
    if (OB_FAIL(ls->update_tablet_table_store(tablet_id_, param, new_tablet_handle))) {
      LOG_WARN("failed to update tablet table store", K(ret), K(tablet_id_), K(param));
    }
  }
  ObTabletObjLoadHelper::free(tmp_arena, storage_schema);
  return ret;
}
