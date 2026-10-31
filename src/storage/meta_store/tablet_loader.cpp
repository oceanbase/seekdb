
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

#include "tablet_loader.h"
#include "share/rc/ob_server_runtime.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "storage/meta_store/ob_local_storage_meta_service.h"

namespace oceanbase
{
using namespace share;
using namespace blocksstable;

namespace storage
{

int TabletLoadTask::init(
    const int64_t task_idx,
    const TabletLoadTask::Type type,
    TabletLoader *handler)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
  } else {
    idx_ = task_idx;
    type_ = type;
    handler_ = handler;
    handler->inc_inflight_task_cnt();
    is_inited_ = true;
  }
  return ret;
}

void TabletLoadTask::destroy()
{
  if (IS_INIT) {
    handler_->dec_inflight_task_cnt();
    idx_ = -1;
    type_ = Type::MAX;
    handler_ = nullptr;
    ranges_.reset();
    tablet_cnt_ = 0;
    is_inited_ = false;
  }
}
int TabletLoadTask::execute()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else {
    if (Type::DISCRETE == type_ &&
        OB_FAIL(handler_->load_discrete_tablets(ranges_))) {
      LOG_WARN("fail to execute load_discrete_tablets", K(ret), KPC(this));
    } else if (Type::AGGREGATE == type_ &&
        OB_FAIL(handler_->load_aggregate_tablets(ranges_))) {
    } else {
      FLOG_INFO("successfully load tablets", KPC(this));
      handler_->inc_finished_tablet_cnt(tablet_cnt_);
    }
  }
  if (OB_FAIL(ret)) {
    handler_->set_errcode(ret);
  }
  return ret;
}

int TabletLoadTask::add_item_range(const TabletLoadRange &range, bool &is_enough)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(ranges_.push_back(range))) {
  } else {
    is_enough = false;
    tablet_cnt_ += range.second - range.first;
    if (AGGREGATE == type_) {
      if (ranges_.count() >= MAX_AGGREGATE_BLOCK_CNT_PER_TASK) {
        is_enough = true;
      }
    } else {
      if (tablet_cnt_ >= MAX_DISCRETE_TABLET_CNT_PER_TASK) {
        is_enough = true;
      }
    }
  }
  return ret;
}

// ObMetaDiskAddr order: LOCAL < BLOCK < RAW_BLOCK
// the LOCAL disk type is only for empty shell tablet
bool TabletLoadItem::operator<(const TabletLoadItem &r) const
{
  // Same-block offsets and local addresses need no ordering among themselves.
  return addr_.type() < r.addr_.type()
      || (addr_.type() == r.addr_.type() && addr_.is_block() && addr_.block_id() < r.addr_.block_id());
}

//============================= TabletLoader ==============================//
TabletLoader::TabletLoader(ObIArray<TabletLoadItem> &tablets)
  : task_idx_(0),
    inflight_task_cnt_(0),
    finished_tablet_cnt_(0),
    errcode_(OB_SUCCESS),
    total_tablet_item_arr_(tablets.get_data()),
    total_tablet_cnt_(tablets.count()),
    aggregate_task_(nullptr),
    discrete_task_(nullptr)
{
}

int TabletLoader::load(ObStartupAccelTaskHandler* startup_accel_handler)
{
  int ret = OB_SUCCESS;
  const int64_t start_time = ObTimeUtility::current_time();

  if (OB_ISNULL(startup_accel_handler)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("startup_accel_handler is unexpected nullptr", K(ret));
  } else if (0 == total_tablet_cnt_) {
    // do nothing
  } else {
    lib::ob_sort(total_tablet_item_arr_, total_tablet_item_arr_ + total_tablet_cnt_);
    auto add_range = [&](int64_t begin, int64_t end, bool aggregate) {
      TabletLoadTask *&task = aggregate ? aggregate_task_ : discrete_task_;
      const int result = add_item_range_to_task_(startup_accel_handler,
          aggregate ? TabletLoadTask::AGGREGATE : TabletLoadTask::DISCRETE, {begin, end}, task);
      if (OB_SUCCESS != result) {
        LOG_WARN("fail to add tablet load range", K(result), KPC(task));
      }
      return result;
    };
    int64_t tablet_cnt_in_block = 0;
    int64_t valid_size_in_block = 0;
    // <1> ObMetaDiskAddr order is LOCAL < BLOCK < RAW_BLOCK, so handle LOCAL type addrs firstly
    int64_t i = 0;
    while (i < total_tablet_cnt_ && total_tablet_item_arr_[i].addr_.is_local()) {
      i++;
    }
    if (i > 0) { // local addresses are expected to be few, so only use one task here
      ret = add_range(0, i, false);
    }

    // <2> handle block addr
    MacroBlockId pre_block_id;
    if (i < total_tablet_cnt_) { pre_block_id = total_tablet_item_arr_[i].addr_.block_id(); }
    MacroBlockId curr_block_id;
    for ( ; OB_SUCC(ret) && i < total_tablet_cnt_; i++ ) {
      curr_block_id = total_tablet_item_arr_[i].addr_.block_id();
      if (pre_block_id == curr_block_id) {
        tablet_cnt_in_block ++;
        valid_size_in_block += upper_align(total_tablet_item_arr_[i].addr_.size(), 4096);
      } else {
        ret = add_range(i - tablet_cnt_in_block, i,
            is_suitable_to_aggregate_(tablet_cnt_in_block, valid_size_in_block));
        pre_block_id = curr_block_id;
        tablet_cnt_in_block = 1;
        valid_size_in_block = upper_align(total_tablet_item_arr_[i].addr_.size(), 4096);
      }
    }
    if (OB_SUCC(ret) && tablet_cnt_in_block > 0) {  // handle last block range
      ret = add_range(total_tablet_cnt_ - tablet_cnt_in_block, total_tablet_cnt_,
          is_suitable_to_aggregate_(tablet_cnt_in_block, valid_size_in_block));
    }
    // Submit remaining tasks, or release them on failure before waiting on workers.
    for (auto **task : {&aggregate_task_, &discrete_task_}) {
      if (OB_NOT_NULL(*task)) {
        if (OB_SUCC(ret) && OB_FAIL(add_task_(startup_accel_handler, *task))) {
          LOG_WARN("fail to add last task", K(ret), KPC(*task), K(inflight_task_cnt_));
        }
        if (OB_FAIL(ret)) {
          (*task)->~TabletLoadTask();
          startup_accel_handler->get_task_allocator().free(*task);
        }
        *task = nullptr;
      }
    }

    // <3> waiting all task finish even if failure has occurred
    while (ATOMIC_LOAD(&inflight_task_cnt_) != 0) {
      LOG_INFO("waiting all inflight tablet load task finish", K(inflight_task_cnt_));
      ob_usleep(20 * 1000); // 20ms
    }

    if (OB_SUCC(ret)) {
      if (OB_FAIL(ATOMIC_LOAD(&errcode_))) {
      } else if (ATOMIC_LOAD(&finished_tablet_cnt_) != total_tablet_cnt_) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("loaded tablet count mismatch", K(ret), K_(finished_tablet_cnt), K(total_tablet_cnt_));
      }
    }
  }

  int64_t cost_time_us = ObTimeUtility::current_time() - start_time;
  FLOG_INFO("finish loading tablets", K(ret), K(total_tablet_cnt_), K(cost_time_us));
  return ret;
}

int TabletLoader::add_item_range_to_task_(ObStartupAccelTaskHandler* startup_accel_handler,
    const TabletLoadTask::Type type, const TabletLoadRange &range, TabletLoadTask *&task)
{
  int ret = OB_SUCCESS;

  if (nullptr == task) {
    if (OB_ISNULL(task = reinterpret_cast<TabletLoadTask*>(
          startup_accel_handler->get_task_allocator().alloc(sizeof(TabletLoadTask))))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else if (FALSE_IT(task = new(task) TabletLoadTask())) {
    } else if (OB_FAIL(task->init(task_idx_++, type, this))) {
    }
  }

  if (OB_SUCC(ret)) {
    bool is_enough = false;
    if (OB_FAIL(task->add_item_range(range, is_enough))) {
    } else if (is_enough) { // tablet count of this task is enough and will create a new task at next round
      if (OB_FAIL(add_task_(startup_accel_handler, task))) {
      } else {
        task = nullptr;
      }
    }
  }

  if (OB_FAIL(ret) && OB_NOT_NULL(task)) {
    task->~TabletLoadTask();
    startup_accel_handler->get_task_allocator().free(task);
    task = nullptr;
  }
  return ret;
}

int TabletLoader::add_task_(ObStartupAccelTaskHandler* startup_accel_handler,
                                           TabletLoadTask *task)
{
  int ret = OB_SUCCESS;
  bool need_retry = false;
  FLOG_INFO("add tablet load task", KPC(task), K(inflight_task_cnt_));
  do {
    need_retry = false;
    if (OB_FAIL(ATOMIC_LOAD(&errcode_))) {
    } else if (OB_FAIL(startup_accel_handler->push_task(task))) {
      if (OB_EAGAIN == ret) {
        LOG_INFO("task queue is full, wait and retry", KPC(task), K(inflight_task_cnt_));
        need_retry = true;
        ob_usleep(20 * 1000); // 20ms
      } else {
      }
    }
  } while(OB_FAIL(ret) && need_retry);

  return ret;
}

int TabletLoader::load_discrete_tablets(const ObIArray<TabletLoadRange> &range_arr)
{
  int ret = OB_SUCCESS;
  char *buf = nullptr;
  int64_t buf_len = 0;
  ObArenaAllocator io_allocator("DiscreteRep", OB_MALLOC_NORMAL_BLOCK_SIZE);

  for (int64_t i = 0; OB_SUCC(ret) && i < range_arr.count(); i++) {
    for (int64_t idx = range_arr.at(i).first; OB_SUCC(ret) && idx < range_arr.at(i).second; idx++) {
      io_allocator.reuse();
      const TabletLoadItem &item = total_tablet_item_arr_[idx];
      if (OB_FAIL(ATOMIC_LOAD(&errcode_))) {
      } else {
        // io maybe timeout, so need retry
        int64_t max_retry_time = 5;
        do {
          if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::storage::ObLocalStorageMetaService>()->read_from_disk(item.addr_, io_allocator, buf, buf_len))) {
          }
        } while (OB_FAIL(ret) && OB_TIMEOUT == ret && max_retry_time-- > 0);
        if (OB_SUCC(ret) && OB_FAIL(replay_create_tablet(item, buf, buf_len))) {
          LOG_ERROR("fail to load tablet", K(ret), K(item));
        }
      }
    }
  }
  return ret;
}

int TabletLoader::load_aggregate_tablets(const ObIArray<TabletLoadRange> &range_arr)
{
  int ret = OB_SUCCESS;
  char *buf = nullptr;
  int64_t buf_len = 0;
  ObArenaAllocator io_allocator("AggregateRep", OB_MALLOC_NORMAL_BLOCK_SIZE);
  char *io_buf = nullptr;
  const int64_t io_buf_size = OB_STORAGE_OBJECT_MGR.get_macro_block_size();

  if (OB_ISNULL(io_buf =
      reinterpret_cast<char*>(io_allocator.alloc(io_buf_size)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < range_arr.count(); i++) {
    ObStorageObjectReadInfo read_info;
    ObStorageObjectHandle object_handle;
    read_info.offset_ = 0;
    read_info.buf_ = io_buf;
    read_info.size_ = io_buf_size;
    read_info.io_timeout_ms_ = 20000; // 20s
    read_info.io_desc_.set_mode(ObIOMode::READ);
    read_info.io_desc_.set_wait_event(ObWaitEventIds::DB_FILE_COMPACT_READ);
    read_info.io_desc_.set_sys_module_id(ObIOModule::LINKED_MACRO_BLOCK_IO);
    read_info.macro_block_id_ = total_tablet_item_arr_[range_arr.at(i).first].addr_.block_id();
    
    if (OB_FAIL(ObObjectManager::read_object(read_info, object_handle))) {
    }
    for (int64_t idx = range_arr.at(i).first; OB_SUCC(ret) && idx < range_arr.at(i).second; idx++) {
      const TabletLoadItem &item = total_tablet_item_arr_[idx];
      if (OB_FAIL(ATOMIC_LOAD(&errcode_))) {
      } else if (OB_FAIL(ObObjectReaderWriter::parse_data_from_object(object_handle, item.addr_, buf, buf_len))) {
      } else if (OB_FAIL(replay_create_tablet(item, buf, buf_len))) {
      }
    }
  }

  return ret;
}

int TabletLoader::replay_create_tablet(const TabletLoadItem &item, const char *buf, const int64_t buf_len)
{
  int ret = OB_SUCCESS;
  ObLS *tenant_ls = nullptr;
  const ObTabletMapKey &key = item.key_;
  const ObMetaDiskAddr &addr = item.addr_;

  if (OB_FAIL(server_service<ObLSService>()->get_ls(tenant_ls))) {
  } else if (OB_FAIL(tenant_ls->get_tablet_svr()->replay_create_tablet(addr, buf, buf_len, key.tablet_id_))) {
  }
  return ret;
}

} // namespace storage
} // namespace oceanbase
