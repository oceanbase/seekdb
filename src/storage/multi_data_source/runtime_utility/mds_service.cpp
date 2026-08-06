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

#include "mds_service.h"
#include "share/rc/ob_module_provider.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "storage/tablet/ob_tablet_iterator.h"
#include "storage/allocator/ob_mds_allocator.h"  // relocated-definition owner
#include "storage/allocator/ob_vector_allocator.h"

namespace oceanbase
{
using namespace share;
namespace storage
{
namespace mds
{

int ObMdsService::server_module_init(ObMdsService *&mds_service)
{
  int ret = OB_SUCCESS;
  MDS_TG(10_ms);
  if (mds_service->is_inited_) {
    ret = OB_INIT_TWICE;
    MDS_LOG(ERROR, "init MDS service twice!", KR(ret), KPC(mds_service));
  } else if (MDS_FAIL(mds_service->recyle_timer_.init(
      "MdsTRecyle", common::ObMemAttr("MdsTRecyle")))) {
    MDS_LOG(WARN, "fail to init MdsServiceRecycle timer", K(ret));
  } else if (MDS_FAIL(mds_service->dump_status_timer_.init(
      "MdsTDump", common::ObMemAttr("MdsTDump")))) {
    MDS_LOG(WARN, "fail to init MdsServiceDumpStatus timer", K(ret));
  } else {
    mds_service->is_inited_ = true;
  }
  return ret;
}

int ObMdsService::server_module_start(ObMdsService *&mds_service)
{
  int ret = OB_SUCCESS;
  MDS_TG(10_ms);
  if (MDS_FAIL(mds_service->recyle_timer_.schedule(
          mds_service->recyle_timer_task_, 3_s, true/*repeat*/, false/*immediate*/))) {
    MDS_LOG(ERROR, "fail to register recycle timer task to timer",
        KR(ret), KPC(mds_service));
  } else if (MDS_FAIL(mds_service->dump_status_timer_.schedule(
          mds_service->dump_status_timer_task_, 15_s, true/*repeat*/, false/*immediate*/))) {
    MDS_LOG(ERROR, "fail to register dump mds table status task to timer",
        KR(ret), KPC(mds_service));
  }

  return ret;
}

void ObMdsService::server_module_stop(ObMdsService *&mds_service)
{
  if (nullptr != mds_service && mds_service->is_inited_) {
    mds_service->recyle_timer_.stop();
    mds_service->dump_status_timer_.stop();
  }
}

void ObMdsService::server_module_wait(ObMdsService *&mds_service)
{
  if (nullptr != mds_service && mds_service->is_inited_) {
    mds_service->recyle_timer_.wait();
    mds_service->dump_status_timer_.wait();
  }
}

void ObMdsService::run_recyle_timer_task()
{
  ObCurTraceId::init(GCONF.self_addr_);
  if (REACH_TIME_INTERVAL(30_s)) {
    observer::ObMdsEventBuffer::dump_statistics();
  }
  try_recycle_mds_table_task();
}
void ObMdsService::try_recycle_mds_table_task()
{
  #define PRINT_WRAPPER KR(ret)
  int ret = OB_SUCCESS;
  MDS_TG(1_s);
  ObCurTraceId::init(GCONF.self_addr_);
  if (MDS_FAIL(ObMdsService::for_each_ls([](ObLS &ls) -> int {
    (void) ObMdsService::for_each_mds_table_in_ls(ls, [](ObTablet &tablet) -> int {// FIXME: there is no need scan all tablets
      (void) process_with_tablet_(tablet);
      return OB_SUCCESS;// keep doing ignore error
    });
    return OB_SUCCESS;// keep doing ignore error
  }))) {
    MDS_LOG_NONE(WARN, "fail to scan mds tables");
  }
  #undef PRINT_WRAPPER
}

void ObMdsService::run_dump_status_timer_task()
{
  ObCurTraceId::init(GCONF.self_addr_);
  dump_special_mds_table_status_task();
}

void ObMdsService::dump_special_mds_table_status_task()
{
  #define PRINT_WRAPPER KR(ret)
  int ret = OB_SUCCESS;
  MDS_TG(1_s);
  ObCurTraceId::init(GCONF.self_addr_);
  ObMdsService::for_each_ls([](ObLS &ls) -> int {
    int ret = OB_SUCCESS;
    MDS_TG(1_s);
    MdsTableMgrHandle mds_table_mge_handle;
    share::SCN ls_mds_freezing_scn;
    if (MDS_FAIL(ls.get_mds_table_mgr(mds_table_mge_handle))) {
      MDS_LOG_NONE(WARN, "fail to get mds table mgr");
    } else if (FALSE_IT(ls_mds_freezing_scn = mds_table_mge_handle.get_mds_table_mgr()->get_freezing_scn())) {
    } else {
      (void)mds_table_mge_handle.get_mds_table_mgr()->for_each_in_t3m_mds_table([ls_mds_freezing_scn](MdsTableBase &mds_table) -> int {// with hash map bucket's lock protected
        (void) mds_table.operate([ls_mds_freezing_scn](MdsTableBase &mds_table)-> int {// with MdsTable's lock protected
          int ret = OB_SUCCESS;
          if (mds_table.get_rec_scn() <= ls_mds_freezing_scn) {
            // ignore ret
            MDS_LOG_NOTICE(WARN, "dump rec_scn lagging freeze_scn mds_table", K(ls_mds_freezing_scn), K(mds_table));
          }
          return OB_SUCCESS;// keep iterating
        });
        return OB_SUCCESS;// keep iterating
      });
      (void)mds_table_mge_handle.get_mds_table_mgr()->for_each_removed_mds_table([](MdsTableBase &mds_table) -> int {
        (void) mds_table.operate([](MdsTableBase &mds_table)-> int {// with MdsTable's lock protected
          int ret = OB_SUCCESS;
          if (ObClockGenerator::getClock() - mds_table.get_removed_from_t3m_ts() > 1_min) {
            // ignore ret
            MDS_LOG_NOTICE(WARN, "dump maybe leaked mds_table", K(mds_table));
          }
          return OB_SUCCESS;// keep iterating
        });
        return OB_SUCCESS;// keep iterating
      });
    };
    return OB_SUCCESS;// keep doing ignore error
  });
  #undef PRINT_WRAPPER
}

int ObMdsService::for_each_ls(const ObFunction<int(ObLS &)> &op)
{
  #define PRINT_WRAPPER KR(ret)
  int ret = OB_SUCCESS;
  MDS_TG(3_s);
  ObLS *ls = nullptr;
  int64_t succ_num = 0;
  if (!op.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    MDS_LOG_NONE(WARN, "invalid op");
  } else if (OB_ISNULL(share::g_mp->ls_service())) {
    ret = OB_ERR_UNEXPECTED;
    MDS_LOG_NONE(WARN, "ls service is null", K(ret));
  } else if (MDS_FAIL(share::g_mp->ls_service()->get_ls(ls))) {
    MDS_LOG_NONE(WARN, "fail to get ls");
  } else if (OB_ISNULL(ls)) {
    ret = OB_ERR_UNEXPECTED;
    MDS_LOG_NONE(WARN, "ls is null");
  } else if (MDS_FAIL(op(*ls))) {
    MDS_LOG_NONE(WARN, "fail to operate ls");
  } else {
    succ_num = 1;
    MDS_LOG_NONE(DEBUG, "succeed to operate ls", K(ret));
  }
  MDS_LOG_NONE(INFO, "for each ls", K(succ_num));
  return ret;
  #undef PRINT_WRAPPER
}

int ObMdsService::for_each_tablet_in_ls(ObLS &ls, const ObFunction<int(ObTablet &)> &op)
{
  #define PRINT_WRAPPER KR(ret), K(ls)
  int ret = OB_SUCCESS;
  int64_t succ_num = 0;
  ObLSTabletIterator tablet_iter(storage::ObMDSGetTabletMode::READ_WITHOUT_CHECK);
  MDS_TG(500_ms);
  if (!op.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    MDS_LOG_NONE(WARN, "invalid op");
  } else if (MDS_FAIL(ls.build_tablet_iter(tablet_iter))) {
    MDS_LOG_NONE(WARN, "failed to build ls tablet iter");
  } else {
    ObTabletHandle tablet_handle;
    ObTablet *tablet = nullptr;
    do {
      tablet_handle.reset();
      tablet = nullptr;
      if (MDS_FAIL(tablet_iter.get_next_tablet(tablet_handle))) {
        if (OB_ITER_END != ret && OB_EMPTY_RESULT != ret) {
          MDS_LOG_NONE(WARN, "failed to get tablet");
        } else {
          ret = OB_SUCCESS;
          break;
        }
      } else if (OB_ISNULL(tablet = tablet_handle.get_obj())) {
        ret = OB_ERR_UNEXPECTED;
        MDS_LOG_NONE(WARN, "tablet should not be NULL", KPC(tablet));
      } else if (tablet->get_tablet_meta().tablet_id_.is_ls_inner_tablet()) {
        // FIXME: there is no mds table on ls inner tablet yet, but there will be
      } else {
        op(*tablet);
      }
    } while (++succ_num && OB_SUCC(ret));
    MDS_LOG_NONE(INFO, "for each tablet", K(succ_num));
  }
  return ret;
  #undef PRINT_WRAPPER
}

int ObMdsService::for_each_mds_table_in_ls(ObLS &ls, const ObFunction<int(ObTablet &)> &op)
{
  #define PRINT_WRAPPER KR(ret), K(ids_in_t3m_array.count())
  int ret = OB_SUCCESS;
  MDS_TG(10_s);

  MdsTableMgrHandle mgr_handle;
  ObArray<ObTabletID> ids_in_t3m_array;
  if (!op.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    MDS_LOG_NONE(WARN, "invalid tablet operation");
  } else if (MDS_FAIL(ls.get_mds_table_mgr(mgr_handle))) {
    MDS_LOG_NONE(WARN, "fail to get mds table mgr");
  } else if (MDS_FAIL(mgr_handle.get_mds_table_mgr()->for_each_in_t3m_mds_table(
    [&ids_in_t3m_array](MdsTableBase &mds_table) -> int {// with map's bucket lock protected
      MDS_TG(1_s);
      int ret = OB_SUCCESS;
      if (MDS_FAIL(ids_in_t3m_array.push_back(mds_table.get_tablet_id()))) {
        MDS_LOG_NONE(WARN, "fail to push array");
      }
      return ret;
    }
  ))) {
    MDS_LOG_NONE(WARN, "fail to scan mds_table");
  } else {
    for (int64_t idx = 0; idx < ids_in_t3m_array.count(); ++idx) {// ignore ret
      ObTabletHandle tablet_handle;
      if (OB_FAIL(ls.get_tablet(ids_in_t3m_array[idx], tablet_handle, 1_s, ObMDSGetTabletMode::READ_WITHOUT_CHECK))) {
        MDS_LOG_NONE(WARN, "fail to get tablet_handle", K(ids_in_t3m_array[idx]));
      } else if (OB_ISNULL(tablet_handle.get_obj())) {
        ret = OB_ERR_UNEXPECTED;
        MDS_LOG_NONE(WARN, "tablet is null", K(ids_in_t3m_array[idx]));
      } else if (OB_FAIL(op(*tablet_handle.get_obj()))) {
        MDS_LOG_NONE(WARN, "fail to process with tablet", K(ids_in_t3m_array[idx]));
      }
    }
  }
  return ret;
  #undef PRINT_WRAPPER
}

int ObMdsService::process_with_tablet_(ObTablet &tablet)
{
  #define PRINT_WRAPPER KR(ret), K(tablet_oldest_scn), K(tablet_id)
  int ret = OB_SUCCESS;
  const common::ObTabletID &tablet_id = tablet.get_tablet_id();
  share::SCN tablet_oldest_scn;
  MDS_TG(10_ms);
  if (MDS_FAIL(get_tablet_oldest_scn_(tablet, tablet_oldest_scn))) {
    MDS_LOG_GC(WARN, "fail to get tablet oldest scn");
  } else if (MDS_FAIL(try_recycle_mds_table_(tablet, tablet_oldest_scn))) {
    MDS_LOG_GC(WARN, "fail to recycle mds table");
  } else if (MDS_FAIL(try_gc_mds_table_(tablet))) {
    if (OB_EAGAIN != ret) {
      MDS_LOG_GC(WARN, "fail to gc mds table");
    } else {
      MDS_LOG_GC(TRACE, "try gc mds table need do again later");
    }
  } else {
    MDS_LOG_GC(INFO, "success do try gc mds table");
  }
  return ret;
  #undef PRINT_WRAPPER
}

int ObMdsService::get_tablet_oldest_scn_(ObTablet &tablet, share::SCN &oldest_scn)
{
  #define PRINT_WRAPPER KR(ret), K(tablet_id), K(oldest_scn), K(op.min_mds_ckpt_scn_)
  int ret = OB_SUCCESS;
  const common::ObTabletID &tablet_id = tablet.get_tablet_id();
  MDS_TG(5_ms);
  oldest_scn = SCN::min_scn();// means can not recycle any node
  ScanAllVersionTabletsOp::GetMinMdsCkptScnOp op(oldest_scn);
  if (OB_ISNULL(share::g_mp->storage_meta_mem_mgr())) {
    ret = OB_BAD_NULL_ERROR;
    MDS_LOG_GC(ERROR, "server ObStorageMetaMemMgr is NULL");
  } else if (MDS_FAIL(share::g_mp->storage_meta_mem_mgr()->scan_all_version_tablets(ObTabletMapKey(tablet_id), op))) {
    if (OB_ENTRY_NOT_EXIST == ret) {
      ret = OB_SUCCESS;
      MDS_LOG_GC(WARN, "get_min_mds_ckpt_scn meet OB_ENTRY_NOT_EXIST");
    } else if (OB_ITEM_NOT_SETTED == ret) {
      ret = OB_SUCCESS;
      MDS_LOG_GC(WARN, "get_min_mds_ckpt_scn meet OB_ITEM_NOT_SETTED");
    } else {
      MDS_LOG_GC(WARN, "fail to get oldest tablet min_mds_ckpt_scn");
    }
  }
  if (oldest_scn.is_max() || !oldest_scn.is_valid()) {
    MDS_LOG_GC(WARN, "get min_mds_ckpt_scn, but is invalid");
    oldest_scn.set_min();
  }
  MDS_LOG_GC(DEBUG, "get tablet oldest scn");
  return ret;
  #undef PRINT_WRAPPER
}

int ObMdsService::try_recycle_mds_table_(ObTablet &tablet,
                                             const share::SCN &tablet_oldest_scn)
{
  #define PRINT_WRAPPER KR(ret), K(tablet.get_tablet_meta().tablet_id_), K(tablet_oldest_scn)
  int ret = OB_SUCCESS;
  const ObTabletPointerHandle &pointer_handle = tablet.get_pointer_handle();
  ObTabletPointer *tablet_pointer = pointer_handle.get_resource_ptr();
  MDS_TG(5_ms);
  if (OB_ISNULL(tablet_pointer)) {
    ret = OB_BAD_NULL_ERROR;
    MDS_LOG_GC(ERROR, "down cast to tablet pointer failed");
  } else if (MDS_FAIL(tablet_pointer->try_release_mds_nodes_below(tablet_oldest_scn))) {
    MDS_LOG_GC(WARN, "fail to release mds nodes");
  } else {
    MDS_LOG_GC(DEBUG, "success to release mds nodes");
  }
  return ret;
  #undef PRINT_WRAPPER
}

int ObMdsService::try_gc_mds_table_(ObTablet &tablet)
{
  #define PRINT_WRAPPER KR(ret), K(tablet.get_tablet_meta().tablet_id_)
  int ret = OB_SUCCESS;
  const ObTabletPointerHandle &pointer_handle = tablet.get_pointer_handle();
  ObTabletPointer *tablet_pointer = pointer_handle.get_resource_ptr();
  MDS_TG(5_ms);
  if (OB_ISNULL(tablet_pointer)) {
    ret = OB_BAD_NULL_ERROR;
    MDS_LOG_GC(ERROR, "down cast to tablet pointer failed");
  } else if (MDS_FAIL(tablet_pointer->try_gc_mds_table())) {
    if (OB_EAGAIN != ret) {
      MDS_LOG_GC(WARN, "try gc mds table failed");
    }
  } else {
    MDS_LOG_GC(DEBUG, "success to release mds nodes");
  }
  return ret;
  #undef PRINT_WRAPPER
}

}  // namespace mds
}  // namespace storage
}  // namespace oceanbase

namespace oceanbase
{
namespace share
{

int ObMdsAllocator::init()
{
  int ret = OB_SUCCESS;
  ObMemAttr mem_attr;
  // TODO : @gengli new ctx id?

  mem_attr.ctx_id_ = ObCtxIds::MDS_DATA_ID;
  mem_attr.label_ = "MdsTable";
  ObSharedMemAllocMgr *share_mem_alloc_mgr = share::g_mp->shared_mem_alloc_mgr();
  throttle_tool_ = &(share_mem_alloc_mgr->share_resource_throttle_tool());
  MDS_TG(10_ms);
  if (IS_INIT){
    ret = OB_INIT_TWICE;
    SHARE_LOG(WARN, "init MDS allocator twice", KR(ret), KPC(this));
  } else if (OB_ISNULL(throttle_tool_)) {
    ret = OB_ERR_UNEXPECTED;
    SHARE_LOG(WARN, "throttle tool is unexpected null", KP(throttle_tool_), KP(share_mem_alloc_mgr));
  } else if (MDS_FAIL(allocator_.init(OB_MALLOC_NORMAL_BLOCK_SIZE, block_alloc_, mem_attr))) {
    MDS_LOG(WARN, "init vslice allocator failed", K(ret), K(OB_MALLOC_NORMAL_BLOCK_SIZE), KP(this), K(mem_attr));
  } else {
    allocator_.set_nway(MDS_ALLOC_CONCURRENCY);
    is_inited_ = true;
  }
  return ret;
}


}  // namespace share
}  // namespace oceanbase

namespace oceanbase
{
namespace share
{

int ObVectorAllocator::init()
{
  int ret = OB_SUCCESS;

  lib::ContextParam param;
  param.set_mem_attr("VectorIndex", ObCtxIds::VECTOR_CTX_ID)
    .set_properties(lib::ADD_CHILD_THREAD_SAFE | lib::ALLOC_THREAD_SAFE | lib::RETURN_MALLOC_DEFAULT)
    .set_page_size(OB_MALLOC_MIDDLE_BLOCK_SIZE)
    .set_label("VectorIndex")
    .set_ablock_size(lib::INTACT_MIDDLE_AOBJECT_SIZE);
  ObSharedMemAllocMgr *share_mem_alloc_mgr = share::g_mp->shared_mem_alloc_mgr();
  throttle_tool_ = &(share_mem_alloc_mgr->vector_throttle_tool());
  MDS_TG(10_ms);
  if (IS_INIT){
    ret = OB_INIT_TWICE;
    SHARE_LOG(WARN, "init vector allocator twice", KR(ret), KPC(this));
  } else if (OB_ISNULL(throttle_tool_)) {
    ret = OB_ERR_UNEXPECTED;
    SHARE_LOG(WARN, "throttle tool is unexpected null", KP(throttle_tool_), KP(share_mem_alloc_mgr));
  } else if (OB_FAIL(ROOT_CONTEXT->CREATE_CONTEXT(memory_context_, param))) {
    SHARE_LOG(WARN, "create memory entity failed", K(ret));
  } else if (OB_FAIL(ObVectorMemContext::init(memory_context_, throttle_tool_))) {
    SHARE_LOG(WARN, "vector mem context init failed", K(ret));
  } else {
    is_inited_ = true;
  }

  return ret;
}


}  // namespace share
}  // namespace oceanbase
