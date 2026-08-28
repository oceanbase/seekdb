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

#include "ob_tablet.h"
#include "storage/tx/ob_ts_mgr.h"
#include "share/rc/ob_server_runtime.h"
#include "share/schema/ob_schema_runtime_service.h"
#include "storage/ob_sync_tablet_seq_clog.h"
#include "storage/compaction/ob_tablet_scheduler.h"
#include "storage/access/ob_table_scan_iterator.h"
#include "storage/access/ob_rows_info.h"
#include "storage/blocksstable/ob_shared_macro_block_manager.h"
#include "storage/ddl/ob_tablet_ddl_kv.h"
#include "storage/tablet/ob_tablet_medium_info_reader.h"
#include "storage/tablet/ob_tablet_mds_node_dump_operator.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "storage/tx/ob_ts_mgr.h"
#include "storage/truncate_info/ob_tablet_truncate_info_reader.h"
#include "storage/truncate_info/ob_truncate_info_array.h"
#include "storage/tablet/ob_tablet_medium_info_reader.h"
#include "storage/ob_storage_schema_util.h"
#include "storage/compaction/ob_medium_list_checker.h"
#include "storage/memtable/ob_row_conflict_handler.h"
#include "storage/meta_store/ob_local_storage_meta_service.h"
#include "storage/slog_ckpt/ob_linked_macro_block_writer.h"
#include "storage/tablet/ob_mds_scan_param_helper.h"
#include "storage/tablet/ob_mds_schema_helper.h"
#include "storage/tablet/ob_tablet_binding_info.h"
#include "storage/tablet/ob_tablet_macro_info_iterator.h"
#include "storage/tablet/ob_tablet_mds_table_mini_merger.h"
#include "share/ob_tablet_local_checksum_operator.h"
#include "share/ob_structured_event_logger.h"
namespace oceanbase
{
using namespace memtable;
using namespace share;
using namespace common;
using namespace share::schema;
using namespace blocksstable;
using namespace logservice;
using namespace compaction;
using namespace palf;
ERRSIM_POINT_DEF(EN_COMPACTION_RECORD_TRUNCATE_CACHE);
namespace storage
{
#define ALLOC_AND_INIT(allocator, addr, args...)                                  \
  do {                                                                            \
    if (OB_SUCC(ret)) {                                                           \
      if (OB_FAIL(ObTabletObjLoadHelper::alloc_and_new(allocator, addr.ptr_))) {  \
        LOG_WARN("fail to allocate and new object", K(ret));                      \
      } else if (OB_FAIL(addr.get_ptr()->init(allocator, args))) {                \
        LOG_WARN("fail to initialize tablet member", K(ret), K(addr));            \
      }                                                                           \
    }                                                                             \
  } while (false)                                                                 \

#define IO_AND_DESERIALIZE(allocator, meta_addr, meta_ptr, args...)                                         \
  do {                                                                                                      \
    if (OB_SUCC(ret)) {                                                                                     \
      ObArenaAllocator io_allocator((common::ObMemAttr("TmpIO")));                                  \
      char *io_buf = nullptr;                                                                               \
      int64_t buf_len = -1;                                                                                 \
      int64_t io_pos = 0;                                                                                   \
      if (OB_FAIL(ObTabletObjLoadHelper::read_from_addr(io_allocator, meta_addr, io_buf, buf_len))) {       \
        LOG_WARN("read table store failed", K(ret));                                                        \
      } else if (OB_FAIL(ObTabletObjLoadHelper::alloc_and_new(allocator, meta_ptr))) {                      \
        LOG_WARN("alloc and new table store ptr failed", K(ret), K(buf_len), K(io_pos));                    \
      } else if (OB_FAIL(meta_ptr->deserialize(allocator, ##args, io_buf, buf_len, io_pos))) {              \
        LOG_WARN("deserialize failed", K(ret), K(buf_len), K(io_pos));                                      \
      }                                                                                                     \
    }                                                                                                       \
  } while (false)                                                                                           \

ObTableStoreCache::ObTableStoreCache()
  : last_major_snapshot_version_(0),
    major_table_cnt_(0),
    minor_table_cnt_(0),
    recycle_version_(0),
    last_major_column_count_(0),
    last_major_macro_block_cnt_(0),
    last_major_compressor_type_(ObCompressorType::INVALID_COMPRESSOR),
    last_major_latest_row_store_type_(ObRowStoreType::MAX_ROW_STORE)
{
}

void ObTableStoreCache::reset()
{
  last_major_snapshot_version_ = 0;
  major_table_cnt_ = 0;
  minor_table_cnt_ = 0;
  recycle_version_ = 0;
  last_major_column_count_ = 0;
  last_major_macro_block_cnt_ = 0;
  last_major_compressor_type_ = ObCompressorType::INVALID_COMPRESSOR;
  last_major_latest_row_store_type_ = ObRowStoreType::MAX_ROW_STORE;
}

int ObTableStoreCache::init(
    const ObSSTableArray &major_tables,
    const ObSSTableArray &minor_tables)
{
  int ret = OB_SUCCESS;
  major_table_cnt_ = major_tables.count();
  minor_table_cnt_ = minor_tables.count();
  last_major_snapshot_version_ = 0;
  recycle_version_ = 0;

  ObSSTableMetaHandle sst_meta_hdl;
  if (major_table_cnt_ > 0) {
    const blocksstable::ObSSTable *last_major =
      static_cast<const blocksstable::ObSSTable *>(major_tables.get_boundary_table(true /*last*/));
    if (OB_ISNULL(last_major)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("get unexpected null major table", K(ret), KPC(this), K(major_tables));
    } else if (OB_FAIL(last_major->get_meta(sst_meta_hdl))) {
    } else {
      const ObSSTableMeta &sstable_meta = sst_meta_hdl.get_sstable_meta();
      last_major_snapshot_version_ = last_major->get_snapshot_version();
      recycle_version_ = last_major_snapshot_version_;
      last_major_compressor_type_ = sstable_meta.get_basic_meta().get_compressor_type();
      last_major_latest_row_store_type_ = sstable_meta.get_basic_meta().get_latest_row_store_type();
      last_major_column_count_ = sstable_meta.get_column_count();
      last_major_macro_block_cnt_ = sstable_meta.get_basic_meta().get_total_macro_block_count();
    }
  }

  for (int64_t i = 0; OB_SUCC(ret) && i < minor_table_cnt_; ++i) {
    const blocksstable::ObSSTable *table = static_cast<const blocksstable::ObSSTable *>(minor_tables[i]);
    if (OB_ISNULL(table)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("get unexpected null table", K(ret), KPC(this), K(minor_tables));
    } else if (OB_FAIL(table->get_meta(sst_meta_hdl))) {
    } else {
      recycle_version_ = MAX(recycle_version_, sst_meta_hdl.get_sstable_meta().get_recycle_version());
    }
  }
  return ret;
}

template <typename T1,
          typename T2,
          std::size_t expected_size,
          std::size_t t1_size = sizeof(T1),
          std::size_t t2_size = sizeof(T2)>
void check_size()
{
  static_assert(expected_size == t1_size + t2_size,
      "The size of ObTablet will affect the meta memory manager, and the necessity of adding new fields needs to be considered");
}

ObTablet::ObTablet(const bool is_external_tablet)
  : version_(TABLET_PAYLOAD_VERSION),
    length_(0),
    wash_score_(INT64_MIN),
    ref_cnt_(0),
    next_tablet_guard_(),
    tablet_meta_(),
    rowkey_read_info_(nullptr),
    table_store_addr_(),
    storage_schema_addr_(),
    macro_info_addr_(),
    memtable_count_(0),
    ddl_kvs_(nullptr),
    ddl_kv_count_(0),
    pointer_hdl_(),
    tablet_addr_(),
    allocator_(nullptr),
    memtables_lock_(),
    mds_cache_lock_(),
    log_handler_(nullptr),
    next_tablet_(nullptr),
    tablet_status_cache_(),
    ddl_data_cache_(),
    truncate_info_cache_(),
    table_store_cache_(),
    gc_occupy_flag_(false),
    hold_ref_cnt_(false),
    is_inited_(false),
    is_external_tablet_(is_external_tablet)
{
#if defined(__x86_64__) && !defined(_WIN32)
  check_size<ObTablet, ObRowkeyReadInfo, 1360>();
#endif
  MEMSET(memtables_, 0x0, sizeof(memtables_));
}

ObTablet::~ObTablet()
{
  reset();
}

void ObTablet::reset()
{
  FLOG_INFO("reset tablet", KP(this), "tablet_id", tablet_meta_.tablet_id_, K(lbt()));

  reset_memtable();
  reset_ddl_memtables();
  storage_schema_addr_.reset();
  table_store_addr_.reset();
  macro_info_addr_.reset();
  wash_score_ = INT64_MIN;
  tablet_meta_.reset();
  table_store_cache_.reset();

  tablet_addr_.reset();
  log_handler_ = nullptr;
  pointer_hdl_.reset();
  if (nullptr != rowkey_read_info_) {
    rowkey_read_info_->reset();
    rowkey_read_info_->~ObRowkeyReadInfo();
    rowkey_read_info_ = nullptr;
  }
  tablet_status_cache_.reset();
  ddl_data_cache_.reset();
  gc_occupy_flag_ = false;
  next_tablet_guard_.reset();
  // allocator_ = nullptr;  can't reset allocator_ which would be used when gc tablet
  version_ = TABLET_PAYLOAD_VERSION;
  length_ = 0;
  next_tablet_ = nullptr;
  hold_ref_cnt_ = false;
  is_inited_ = false;
}

int64_t ObTablet::get_try_cache_size() const
{
  int64_t size = 0;
  size += sizeof(ObTablet);

  if (OB_NOT_NULL(rowkey_read_info_)) {
    size += rowkey_read_info_->get_deep_copy_size();
  }
  if (ddl_kv_count_ > 0) {
    size += (sizeof(ObDDLKV *) * DDL_KV_ARRAY_SIZE);
  }
  return size;
}

int ObTablet::init_for_first_time_creation(
    common::ObArenaAllocator &allocator,
    const common::ObTabletID &tablet_id,
    const common::ObTabletID &data_tablet_id,
    const share::SCN &create_scn,
    const int64_t snapshot_version,
    const ObCreateTabletSchema &create_tablet_schema,
    const bool need_create_empty_major_sstable,
    const share::SCN &clog_checkpoint_scn,
    const share::SCN &mds_checkpoint_scn,
    const bool micro_index_clustered,
    ObFreezer *freezer,
    const share::ObForkTabletInfo &fork_info)
{
  int ret = OB_SUCCESS;
  const int64_t default_max_sync_medium_scn = 0;
  ObTableHandleV2 table_handle;
  bool is_table_row_store = false;
  ObTabletTableStoreFlag table_store_flag;
  table_store_flag.set_with_major_sstable();

  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret), K(is_inited_));
  } else if (OB_UNLIKELY(!tablet_id.is_valid())
      || OB_UNLIKELY(!data_tablet_id.is_valid())
      //|| OB_UNLIKELY(create_scn <= OB_INVALID_TIMESTAMP)
      || OB_UNLIKELY(OB_INVALID_VERSION == snapshot_version)
      || OB_UNLIKELY(!create_tablet_schema.is_valid())
      || OB_ISNULL(freezer)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(tablet_id), K(data_tablet_id),
        K(create_scn), K(snapshot_version), K(create_tablet_schema), KP(freezer));
  } else if (OB_UNLIKELY(!pointer_hdl_.is_valid())
      || OB_ISNULL(log_handler_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet pointer handle is invalid", K(ret), K_(pointer_hdl), K_(log_handler));
  } else if (!need_create_empty_major_sstable && FALSE_IT(table_store_flag.set_without_major_sstable())) {
  } else if (FALSE_IT(table_store_flag.set_is_user_data_table(create_tablet_schema.is_user_data_table()))) {
  } else if (OB_FAIL(init_shared_params(tablet_id))) {
  } else if (OB_FAIL(tablet_meta_.init(tablet_id, data_tablet_id,
      create_scn, snapshot_version, table_store_flag, create_tablet_schema.get_schema_version()/*create_schema_version*/,
      clog_checkpoint_scn, mds_checkpoint_scn, micro_index_clustered,
      false/*has_truncate_info*/, fork_info))) {
  } else if (OB_FAIL(pull_memtables(allocator))) {
  } else {
    if (OB_FAIL(ObTabletObjLoadHelper::alloc_and_new(allocator, storage_schema_addr_.ptr_))) {
    } else if (OB_FAIL(storage_schema_addr_.get_ptr()->init(allocator, create_tablet_schema, false /*skip_column_info*/))) {
    }
  }
  if (OB_FAIL(ret)) {
  } else if (need_create_empty_major_sstable
      && OB_FAIL(ObTabletCreateDeleteHelper::create_empty_sstable(
          allocator, *storage_schema_addr_.get_ptr(), tablet_id, snapshot_version, table_handle))) {
    LOG_WARN("failed to make empty co sstable", K(ret), K(snapshot_version));
  } else {
    ALLOC_AND_INIT(allocator, table_store_addr_, (*this), static_cast<ObSSTable *>(table_handle.get_table()));
  }

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(try_update_start_scn())) {
  } else if (OB_FAIL(table_store_cache_.init(table_store_addr_.get_ptr()->get_major_sstables(),
      table_store_addr_.get_ptr()->get_minor_sstables()))) {
  } else if (OB_FAIL(check_sstable_column_checksum())) {
  } else if (OB_FAIL(build_read_info(allocator, nullptr /*tablet*/))) {
  } else if (OB_FAIL(init_aggregated_info(allocator, nullptr/* link_writer, tmp_tablet do no write */))) {
  } else if (FALSE_IT(set_initial_addr())) {
  } else if (OB_FAIL(inner_inc_macro_ref_cnt())) {
  } else {
    is_inited_ = true;
    LOG_INFO("succeeded to init tablet for first time creation", K(ret), K(*this));
  }

  if (OB_UNLIKELY(!is_inited_)) {
    reset();
  }

  return ret;
}

int ObTablet::init_for_physical_restore(
    common::ObArenaAllocator &allocator,
    const ObTabletMeta &tablet_meta,
    const ObStorageSchema &storage_schema)
{
  int ret = OB_SUCCESS;
  allocator_ = &allocator;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret), K_(is_inited));
  } else if (OB_UNLIKELY(!tablet_meta.is_valid() || !storage_schema.is_valid()
      || tablet_meta.is_empty_shell_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid physical restore tablet metadata", K(ret), K(tablet_meta), K(storage_schema));
  } else if (OB_UNLIKELY(!pointer_hdl_.is_valid()) || OB_ISNULL(log_handler_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet pointer handle is invalid", K(ret), K_(pointer_hdl), K_(log_handler));
  } else if (OB_FAIL(init_shared_params(tablet_meta.tablet_id_))) {
    LOG_WARN("failed to init shared tablet parameters", K(ret), K(tablet_meta));
  } else if (OB_FAIL(tablet_meta_.assign(tablet_meta))) {
    LOG_WARN("failed to copy physical restore tablet metadata", K(ret), K(tablet_meta));
  } else if (OB_FAIL(pull_memtables(allocator))) {
    LOG_WARN("failed to pull tablet memtables", K(ret), K(tablet_meta));
  } else {
    ALLOC_AND_INIT(allocator, table_store_addr_, (*this), static_cast<ObSSTable *>(nullptr));
    if (OB_FAIL(ret)) {
      LOG_WARN("failed to init empty restore table store", K(ret), K(tablet_meta));
    } else {
      ALLOC_AND_INIT(allocator, storage_schema_addr_, storage_schema);
    }
  }

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(table_store_cache_.init(table_store_addr_.get_ptr()->get_major_sstables(),
                                             table_store_addr_.get_ptr()->get_minor_sstables()))) {
    LOG_WARN("failed to init restore table store cache", K(ret), K(tablet_meta));
  } else if (OB_FAIL(try_update_start_scn())) {
    LOG_WARN("failed to update restored tablet start scn", K(ret), K(tablet_meta));
  } else if (OB_FAIL(build_read_info(allocator, nullptr))) {
    LOG_WARN("failed to build restored tablet read info", K(ret), K(tablet_meta));
  } else if (OB_FAIL(init_aggregated_info(allocator, nullptr))) {
    LOG_WARN("failed to init restored tablet aggregated info", K(ret), K(tablet_meta));
  } else if (FALSE_IT(set_initial_addr())) {
  } else if (OB_FAIL(check_table_store_flag_match_with_table_store_(table_store_addr_.get_ptr()))) {
    LOG_WARN("restore tablet table store flag mismatch", K(ret), K(tablet_meta));
  } else if (OB_FAIL(inner_inc_macro_ref_cnt())) {
    LOG_WARN("failed to increase restored tablet macro ref count", K(ret), K(tablet_meta));
  } else {
    is_inited_ = true;
    LOG_INFO("initialized tablet for physical restore", K(tablet_meta));
  }

  if (OB_UNLIKELY(!is_inited_)) {
    reset();
  }
  return ret;
}

#ifdef ERRSIM
void record_truncate_flag(
  const ObTabletID &tablet_id,
  const bool has_truncate_info)
{
  LOG_INFO("[TRUNCATE INFO] record truncate flag", K(tablet_id), K(has_truncate_info));
  if (has_truncate_info) {
    SERVER_EVENT_SYNC_ADD("merge_errsim", "record_truncate_flag",
                        "tablet_id", tablet_id.id(),
                        "has_truncate_info", has_truncate_info);
  }
}
#endif
int ObTablet::init_for_merge(
    common::ObArenaAllocator &allocator,
    const ObUpdateTableStoreParam &param,
    const ObTablet &old_tablet)
{
  int ret = OB_SUCCESS;
  int64_t max_sync_schema_version = 0;
  int64_t input_max_sync_schema_version = 0;
  common::ObArenaAllocator tmp_arena_allocator(common::ObMemAttr("InitTablet"));
  if (share::is_reserve_mode()) {
    tmp_arena_allocator.set_ctx_id(ObCtxIds::MERGE_RESERVE_CTX_ID);
  }

  ObTabletMemberWrapper<ObTabletTableStore> old_table_store_wrapper;
  const ObTabletTableStore *old_table_store = nullptr;
  ObStorageSchema *old_storage_schema = nullptr;
  const bool need_report_major = param.need_report_major();

  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret), K(is_inited_));
  } else if (OB_UNLIKELY(!param.is_valid())
      || OB_UNLIKELY(!old_tablet.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(param), K(old_tablet));
  } else if (OB_UNLIKELY(!pointer_hdl_.is_valid())
      || OB_ISNULL(log_handler_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet pointer handle is invalid", K(ret), K_(pointer_hdl), K_(log_handler));
  } else if (OB_FAIL(old_tablet.get_max_sync_storage_schema_version(max_sync_schema_version))) {
  } else if (OB_FAIL(old_tablet.load_storage_schema(tmp_arena_allocator, old_storage_schema))) {
  } else if (FALSE_IT(input_max_sync_schema_version = MIN(MAX(param.storage_schema_->schema_version_,
      old_storage_schema->schema_version_), max_sync_schema_version))) {
    // use min schema version to avoid lose storage_schema in replay/reboot
  } else if (OB_FAIL(tablet_meta_.init(old_tablet.tablet_meta_,
      param.snapshot_version_, param.multi_version_start_,
      input_max_sync_schema_version,
      param.get_clog_checkpoint_scn(), param.ddl_info_, param.compaction_info_.has_truncate_info_))) {
  } else if (OB_FAIL(ObStorageSchemaUtil::update_tablet_storage_schema(
      old_tablet.tablet_meta_.tablet_id_, allocator, *old_storage_schema,
      *param.storage_schema_, storage_schema_addr_.ptr_))) {
  } else if (OB_FAIL(old_tablet.fetch_table_store(old_table_store_wrapper))) {
  } else if (OB_FAIL(old_table_store_wrapper.get_member(old_table_store))) {
  } else if (OB_FAIL(pull_memtables(allocator))) {
  } else {
    ALLOC_AND_INIT(allocator, table_store_addr_, (*this), param, (*old_table_store));
    if (OB_FAIL(ret)) {
    } else if (OB_UNLIKELY(ddl_kv_count_ != table_store_addr_.get_ptr()->get_ddl_memtable_count())) {
      // This is defense code. If it runs at here, it must be a bug.
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("It encounters a ddl kv array bug, please pay attention", K(ret), K(ddl_kv_count_),
          "table_store_ddl_kv_count", table_store_addr_.get_ptr()->get_ddl_memtable_count());
    }
  }

  if (FAILEDx(table_store_cache_.init(table_store_addr_.get_ptr()->get_major_sstables(),
                                      table_store_addr_.get_ptr()->get_minor_sstables()))) {
    LOG_WARN("failed to init table store cache", K(ret), KPC(this));
  } else if (OB_FAIL(try_update_start_scn())) {
  } else if (OB_FAIL(try_update_table_store_flag(param.get_update_with_major_flag()))) {
  } else {
    int64_t finish_medium_scn = 0;
    finish_medium_scn = get_last_major_snapshot_version();
    tablet_meta_.update_extra_medium_info(param.compaction_info_.merge_type_, finish_medium_scn);
  }

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(build_read_info(allocator, nullptr /*tablet*/))) {
  } else if (OB_FAIL(check_medium_list())) {
  } else if (OB_FAIL(check_sstable_column_checksum())) {
  } else if (OB_FAIL(init_aggregated_info(allocator, nullptr/* link_writer, tmp_tablet do no write */))) {
  } else if (FALSE_IT(set_initial_addr())) {
  } else if (OB_FAIL(check_table_store_flag_match_with_table_store_(table_store_addr_.get_ptr()))) {
  } else if (OB_FAIL(inner_inc_macro_ref_cnt())) {
  } else {
    if (old_tablet.get_tablet_meta().has_next_tablet_) {
      set_next_tablet_guard(old_tablet.next_tablet_guard_);
    }

    if (OB_SUCC(ret) && need_report_major) {
      int tmp_ret = OB_SUCCESS;
      if (OB_TMP_FAIL(ObTabletMeta::init_report_info(param.sstable_,
          old_tablet.tablet_meta_.report_status_.cur_report_version_, tablet_meta_.report_status_))) {
      }
    }

    is_inited_ = true;
    LOG_INFO("succeeded to init tablet for mini/minor/major merge", K(ret), K(param), K(old_tablet), KPC(this));
  }

  if (OB_UNLIKELY(!is_inited_)) {
    reset();
  }
  ObTabletObjLoadHelper::free(tmp_arena_allocator, old_storage_schema);
#ifdef ERRSIM
  if (OB_SUCC(ret) && EN_COMPACTION_RECORD_TRUNCATE_CACHE) {
    record_truncate_flag(get_tablet_id(), tablet_meta_.has_truncate_info_);
  }
#endif
  return ret;
}


int ObTablet::init_for_defragment(
    common::ObArenaAllocator &allocator,
    const ObIArray<ObITable *> &tables,
    const ObTablet &old_tablet)
{
  int ret = OB_SUCCESS;
  common::ObArenaAllocator tmp_arena_allocator(common::ObMemAttr("InitTablet"));
  ObTabletMemberWrapper<ObTabletTableStore> old_table_store_wrapper;
  const ObTabletTableStore *old_table_store = nullptr;
  ObStorageSchema *old_storage_schema = nullptr;
  allocator_ = &allocator;

  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("tablet has been inited", K(ret));
  } else if (OB_UNLIKELY(!old_tablet.is_valid() || 0 == tables.count())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("old tablet is invalid", K(ret), K(old_tablet));
  } else if (OB_UNLIKELY(!pointer_hdl_.is_valid())
      || OB_ISNULL(log_handler_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet pointer handle is invalid", K(ret), K_(pointer_hdl), K_(log_handler));
  } else if (OB_FAIL(old_tablet.load_storage_schema(tmp_arena_allocator, old_storage_schema))) {
  } else if (OB_FAIL(old_tablet.fetch_table_store(old_table_store_wrapper))) {
  } else if (OB_FAIL(old_table_store_wrapper.get_member(old_table_store))) {
  } else if (OB_FAIL(tablet_meta_.init(old_tablet.tablet_meta_,
      old_tablet.get_snapshot_version(),
      old_tablet.get_multi_version_start(),
      old_tablet.tablet_meta_.max_sync_storage_schema_version_))) {
  } else if (OB_FAIL(pull_memtables(allocator))) {
  } else if (OB_FAIL(ObTabletObjLoadHelper::alloc_and_new(allocator, table_store_addr_.ptr_))) {
  } else if (OB_FAIL(table_store_addr_.get_ptr()->init(*allocator_, *this, *old_table_store, &(tables)))) {
  } else if (OB_FAIL(try_update_start_scn())) {
  } else {
    ALLOC_AND_INIT(allocator, storage_schema_addr_, *old_storage_schema);
  }

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(build_read_info(*allocator_, nullptr /*tablet*/))) {
  } else if (OB_FAIL(check_medium_list())) {
  } else if (OB_FAIL(check_sstable_column_checksum())) {
  } else if (OB_FAIL(init_aggregated_info(allocator, nullptr/* link_writer, tmp_tablet do no write */))) {
  } else if (FALSE_IT(set_initial_addr())) {
  } else if (OB_FAIL(table_store_cache_.init(table_store_addr_.get_ptr()->get_major_sstables(),
  table_store_addr_.get_ptr()->get_minor_sstables()))) {
  } else if (OB_FAIL(check_table_store_flag_match_with_table_store_(table_store_addr_.get_ptr()))) {
  } else if (OB_FAIL(inner_inc_macro_ref_cnt())) {
  } else {
    if (old_tablet.get_tablet_meta().has_next_tablet_) {
      set_next_tablet_guard(old_tablet.next_tablet_guard_);
    }
    is_inited_ = true;
    LOG_INFO("succeeded to init tablet for sstable defragmentation", K(ret), K(old_tablet), KPC(this));
  }

  if (OB_UNLIKELY(!is_inited_)) {
    reset();
  }
  ObTabletObjLoadHelper::free(tmp_arena_allocator, old_storage_schema);

  return ret;
}

int ObTablet::init_for_sstable_replace(
    common::ObArenaAllocator &allocator,
    const ObBatchUpdateTableStoreParam &param,
    const ObTablet &old_tablet)
{
  int ret = OB_SUCCESS;
  allocator_ = &allocator;
  common::ObArenaAllocator tmp_arena_allocator(common::ObMemAttr("InitTablet"));
  ObTabletMemberWrapper<ObTabletTableStore> old_table_store_wrapper;
  const ObTabletTableStore *old_table_store = nullptr;
  ObStorageSchema *old_storage_schema = nullptr;
  const ObStorageSchema *storage_schema = nullptr;
  int64_t max_sync_schema_version = 0;
  const bool is_tablet_fork = param.tablet_fork_param_.is_valid();
  ObForkTabletInfo fork_info = old_tablet.tablet_meta_.fork_info_;
  if (is_tablet_fork) {
    fork_info.set_complete();
  }

  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret), K(is_inited_));
  } else if (OB_UNLIKELY(!param.is_valid()) || OB_UNLIKELY(!old_tablet.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(param), K(old_tablet));
  } else if (OB_UNLIKELY(!pointer_hdl_.is_valid())
      || OB_ISNULL(log_handler_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet pointer handle is invalid", K(ret), K_(pointer_hdl), K_(log_handler));
  } else if (OB_FAIL(old_tablet.load_storage_schema(tmp_arena_allocator, old_storage_schema))) {
  } else if (OB_FAIL(old_tablet.fetch_table_store(old_table_store_wrapper))) {
  } else if (OB_FAIL(old_table_store_wrapper.get_member(old_table_store))) {
  } else if (FALSE_IT(storage_schema = OB_ISNULL(param.source_storage_schema_)
      ? old_storage_schema : param.source_storage_schema_)) {
  } else if (OB_FAIL(old_tablet.get_max_sync_storage_schema_version(max_sync_schema_version))) {
  } else if (is_tablet_fork && OB_FAIL(tablet_meta_.init(old_tablet.tablet_meta_,
      param.tablet_fork_param_.snapshot_version_, param.tablet_fork_param_.multi_version_start_,
      max_sync_schema_version, param.tablet_fork_param_.clog_checkpoint_scn_,
      param.tablet_fork_param_.mds_checkpoint_scn_, fork_info))) {
    // init fork tablet meta.
    LOG_WARN("failed to init fork tablet meta", K(ret), K(old_tablet), K(param), K(max_sync_schema_version), K(fork_info));
  } else if (OB_FAIL(pull_memtables(allocator))){
  } else if (OB_FAIL(ObTabletObjLoadHelper::alloc_and_new(allocator, table_store_addr_.ptr_))) {
  } else if (is_tablet_fork && OB_FAIL(table_store_addr_.ptr_->build_fork_new_table_store(allocator, *this, param, *old_table_store))) {
    LOG_WARN("failed to init fork tablet table store", K(ret), K(old_tablet));
  } else if (OB_FAIL(ObStorageSchemaUtil::update_tablet_storage_schema(
    tablet_meta_.tablet_id_, *allocator_, *old_storage_schema, *storage_schema, storage_schema_addr_.ptr_))) {
  } else if (is_tablet_fork && OB_FAIL(try_update_table_store_flag(is_major_merge_type(param.tablet_fork_param_.merge_type_)))) {
    LOG_WARN("failed to update table store flag for fork", K(ret), K(param), K(table_store_addr_));
  } else if (OB_FAIL(try_update_start_scn())) {
  } else if (OB_FAIL(table_store_cache_.init(table_store_addr_.get_ptr()->get_major_sstables(),
      table_store_addr_.get_ptr()->get_minor_sstables()))) {
  } else if (is_tablet_fork && FALSE_IT(tablet_meta_.extra_medium_info_.reset())) {
    // Fork table should not inherit source extra_medium_info; keep it consistent with forked major.
  } else if (OB_FAIL(build_read_info(*allocator_, nullptr /*tablet*/))) {
  } else if (OB_FAIL(check_medium_list())) {
  } else if (OB_FAIL(check_sstable_column_checksum())) {
  } else if (OB_FAIL(init_aggregated_info(allocator, nullptr/* link_writer, tmp_tablet do no write */))) {
  } else if (FALSE_IT(set_initial_addr())) {
  } else if (OB_FAIL(check_table_store_flag_match_with_table_store_(table_store_addr_.get_ptr()))) {
  } else if (OB_FAIL(inner_inc_macro_ref_cnt())) {
  } else {
    if (old_tablet.get_tablet_meta().has_next_tablet_) {
      set_next_tablet_guard(old_tablet.next_tablet_guard_);
    }
    is_inited_ = true;
    LOG_INFO("succeeded to init tablet with local batch tables", K(ret), K(param), K(old_tablet), KPC(this));
  }

  if (OB_SUCC(ret)) {
    const ObSSTable *last_major = static_cast<const ObSSTable *>(table_store_addr_.get_ptr()->get_major_sstables().get_boundary_table(true/*last*/));
    int tmp_ret = OB_SUCCESS;
    if (OB_ISNULL(last_major)) { // init tablet with no major table, skip to init report info
    } else if (OB_TMP_FAIL(ObTabletMeta::init_report_info(last_major,
      old_tablet.tablet_meta_.report_status_.cur_report_version_, tablet_meta_.report_status_))) {
    }
  }

  if (OB_UNLIKELY(!is_inited_)) {
    reset();
  }
  ObTabletObjLoadHelper::free(tmp_arena_allocator, old_storage_schema);

#ifdef ERRSIM
  ObErrsimBackfillPointType point_type(ObErrsimBackfillPointType::TYPE::ERRSIM_REPLACE_SWAP_BEFORE);
  if (param.errsim_point_info_.is_errsim_point(point_type)) {
    ret = OB_EAGAIN;
    LOG_WARN("[ERRSIM] errsim replace swap tablet before", K(ret), K(param));
  }
#endif
  return ret;
}

int ObTablet::fetch_table_store(ObTabletMemberWrapper<ObTabletTableStore> &wrapper) const
{
  TIMEGUARD_INIT(STORAGE, 10_ms);
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!table_store_addr_.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid table store addr", K(ret), K_(table_store_addr));
  } else if (table_store_addr_.is_memory_object()
             || table_store_addr_.is_none_object()) {
    if (OB_ISNULL(table_store_addr_.get_ptr())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("table store addr ptr is null", K(ret), K_(table_store_addr));
    } else {
      wrapper.set_member(table_store_addr_.get_ptr());
    }
  } else {
    ObStorageMetaHandle handle;
    ObStorageMetaKey meta_key(table_store_addr_.addr_);
    if (CLICK_FAIL(OB_STORE_CACHE.get_storage_meta_cache().get_meta(
                   ObStorageMetaValue::MetaType::TABLE_STORE, meta_key, handle, this))) {
      LOG_WARN("get meta failed", K(ret), K(meta_key));
    } else if (CLICK_FAIL(wrapper.set_cache_handle(handle))) {
      LOG_WARN("wrapper set cache handle failed", K(ret), K(meta_key), K_(table_store_addr));
    }
  }
  return ret;
}

int ObTablet::load_storage_schema(common::ObIAllocator &allocator, ObStorageSchema *&storage_schema) const
{
  int ret = OB_SUCCESS;
  ObStorageSchema *schema = nullptr;
  if (OB_UNLIKELY(!storage_schema_addr_.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid storage schema address", K(ret), K(storage_schema_addr_));
  } else if (storage_schema_addr_.is_memory_object()) {
    if (OB_FAIL(ObTabletObjLoadHelper::alloc_and_new(allocator, schema))) {
    } else if (OB_FAIL(schema->init(allocator, *storage_schema_addr_.ptr_))) {
    }
  } else {
    IO_AND_DESERIALIZE(allocator, storage_schema_addr_.addr_, schema);
  }

  if (OB_FAIL(ret)) {
    ObTabletObjLoadHelper::free(allocator, schema);
  } else if (OB_ISNULL(schema)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("failed to load storage schema", K(ret), K_(storage_schema_addr));
  } else if (OB_UNLIKELY(!schema->is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected invalid schema", K(ret), K_(storage_schema_addr), KPC(schema));

    ObTabletObjLoadHelper::free(allocator, schema);
  } else {
    storage_schema = schema;
  }
  return ret;
}

int ObTablet::read_medium_info_list(
    common::ObArenaAllocator &allocator,
    const compaction::ObMediumCompactionInfoList *&medium_info_list) const
{
  int ret = OB_SUCCESS;
  const common::ObTabletID &tablet_id = tablet_meta_.tablet_id_;
  compaction::ObMediumCompactionInfoList *tmp_list = nullptr;
  common::ObSEArray<compaction::ObMediumCompactionInfo*, 1> medium_info_array;

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else if (OB_FAIL(read_medium_array(allocator, medium_info_array))) {
  } else if (OB_FAIL(ObTabletObjLoadHelper::alloc_and_new(allocator, tmp_list))) {
  } else if (OB_FAIL(tmp_list->init(allocator, tablet_meta_.extra_medium_info_, medium_info_array))) {
  } else {
    medium_info_list = tmp_list;
  }

  if (OB_FAIL(ret)) {
    ObTabletObjLoadHelper::free(allocator, tmp_list);
  }

  // always free memory for medium info array
  for (int64_t i = 0; i < medium_info_array.count(); ++i) {
    compaction::ObMediumCompactionInfo *&info = medium_info_array.at(i);
    ObTabletObjLoadHelper::free(allocator, info);
  }

  return ret;
}

int ObTablet::read_medium_array(
    common::ObArenaAllocator &allocator,
    common::ObIArray<compaction::ObMediumCompactionInfo*> &medium_info_array) const
{
  int ret = OB_SUCCESS;
  const common::ObTabletID &tablet_id = tablet_meta_.tablet_id_;
  const int64_t finish_medium_scn = get_last_major_snapshot_version();
  ObMdsReadInfoCollector unused_collector;
  if (OB_SUCC(ret)) {
    SMART_VARS_2((ObTableScanParam, scan_param), (ObTabletMediumInfoReader, medium_info_reader)) {
      if (OB_FAIL((ObMdsScanParamHelper::build_customized_scan_param<compaction::ObMediumCompactionInfoKey, compaction::ObMediumCompactionInfo>(
          allocator,
          tablet_id,
          ObMdsScanParamHelper::get_whole_read_version_range(),
          unused_collector,
          scan_param)))) {
      } else if (OB_FAIL(medium_info_reader.init(*this, scan_param))) {
      } else {
        compaction::ObMediumCompactionInfoKey key;
        compaction::ObMediumCompactionInfo *info = nullptr;
        while (OB_SUCC(ret)) {
          if (OB_FAIL(ObTabletObjLoadHelper::alloc_and_new(allocator, info))) {
          } else if (OB_FAIL(medium_info_reader.get_next_medium_info(allocator, key, *info))) {
            if (OB_ITER_END == ret) {
              ObTabletObjLoadHelper::free(allocator, info);
              ret = OB_SUCCESS;
              break;
            } else {
              LOG_WARN("fail to get next medium info", K(ret));
            }
          } else if (info->medium_snapshot_ <= finish_medium_scn) {
            // filter medium info, whose medium snapshot is less than finish medium scn from last major sstable
            ObTabletObjLoadHelper::free(allocator, info);
          } else if (OB_FAIL(medium_info_array.push_back(info))) {
          }

          if (OB_FAIL(ret)) {
            ObTabletObjLoadHelper::free(allocator, info);
          }
        }
      }
    }

    // free medium info and reset array if failed
    if (OB_FAIL(ret)) {
      for (int64_t i = 0; i < medium_info_array.count(); ++i) {
        compaction::ObMediumCompactionInfo *&info = medium_info_array.at(i);
        ObTabletObjLoadHelper::free(allocator, info);
      }

      medium_info_array.reset();
    }
  }

  return ret;
}

int ObTablet::get_truncate_info_newest_version(int64_t &newest_commit_version, int64_t &count)
{
  int ret = OB_SUCCESS;
  const ObTabletID &tablet_id = tablet_meta_.tablet_id_;
  bool cache_valid = false;
  count = 0;
  newest_commit_version = 0;
  {
    SpinRLockGuard guard(mds_cache_lock_);
    if (truncate_info_cache_.is_valid()) {
      cache_valid = true;
      if (!truncate_info_cache_.is_empty()
          && truncate_info_cache_.newest_commit_version() > get_last_major_snapshot_version()) {
        count = truncate_info_cache_.count();
        newest_commit_version = truncate_info_cache_.newest_commit_version();
      }
    }
  }
  if (!cache_valid) {
    ObArenaAllocator tmp_allocator(ObMemAttr("TruncateInfoArr"));
    ObTruncateInfoArray tmp_array;
    SCN max_readable_scn;
    if (OB_FAIL(OB_TS_MGR.get_gts_sync(GCONF.rpc_timeout, max_readable_scn))) {
    } else if (OB_FAIL(read_truncate_info_array(
                   tmp_allocator,
                   ObVersionRange(get_last_major_snapshot_version(),
                                  max_readable_scn.get_val_for_tx()),
                   false /*for_access*/, tmp_array))) {
    } else if (!tmp_array.empty()) {
      count = tmp_array.count();
      newest_commit_version = tmp_array.at(tmp_array.count() - 1)->commit_version_;
      LOG_INFO("[TRUNCATE INFO] success to get newest truncate info version", KR(ret), K(tablet_id), K(count), K(newest_commit_version));
    }
  }
  return ret;
}

#ifdef ERRSIM
void record_truncate_cache(
  const ObTabletID &tablet_id,
  const ObMdsReadInfoCollector &collector,
  const ObTruncateInfoCache &truncate_info_cache)
{
  LOG_INFO("[TRUNCATE INFO] record truncate cache", K(tablet_id), K(truncate_info_cache), K(collector));
  SERVER_EVENT_SYNC_ADD("merge_errsim", "record_truncate_cache",
                        "tablet_id", tablet_id.id(),
                        "exist_new_committed_node", collector.exist_new_committed_node_,
                        "exist_uncommitted_node", collector.exist_uncommitted_node_,
                        "cache_valid", truncate_info_cache.is_valid(),
                        "cache_empty", truncate_info_cache.is_empty());
}
#endif

void ObTablet::check_truncate_info_state(
    const common::ObVersionRange &read_version_range,
    bool &has_truncate_flag,
    bool &contain_truncate_info)
{
  has_truncate_flag = has_truncate_info();
  contain_truncate_info = true;
  const int64_t last_major_snapshot = get_last_major_snapshot_version();
  SpinRLockGuard guard(mds_cache_lock_);
  if (truncate_info_cache_.is_valid()) {
    if (read_version_range.base_version_ >= last_major_snapshot &&
        (truncate_info_cache_.is_empty() || last_major_snapshot >= truncate_info_cache_.newest_commit_version())) {
      contain_truncate_info = false;
    }
  }
}

bool ObTablet::has_truncate_info() const
{
  return tablet_meta_.has_truncate_info_;
}

int ObTablet::read_truncate_info_array(
    ObArenaAllocator &allocator,
    const ObVersionRange &read_version_range,
    const bool for_access,
    storage::ObTruncateInfoArray &truncate_info_array)
{
  int ret = OB_SUCCESS;
  ObTimeGuard time_guard("ObTablet::read_truncate_info_array", 10 * 1000 * 1000/* 10s */);
  bool need_read_truncate_info = true;
  ObMdsReadInfoCollector collector;
  const ObTabletID &tablet_id = tablet_meta_.tablet_id_;
  const int64_t last_major_snapshot = get_last_major_snapshot_version();
  int64_t replay_seq = -1;
  int64_t newest_schema_version = 0;
  int64_t newest_commit_version = 0;
  // TODO change INFO log to TRACE log later
  if (OB_UNLIKELY(INT64_MAX == read_version_range.snapshot_version_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument for version range", KR(ret), K(read_version_range));
  } else {
    SpinRLockGuard guard(mds_cache_lock_);
    if (truncate_info_cache_.is_valid()) {
      if (read_version_range.base_version_ >= last_major_snapshot &&
          (truncate_info_cache_.is_empty() || last_major_snapshot >= truncate_info_cache_.newest_commit_version())) {
        need_read_truncate_info = false;
        LOG_INFO("[TRUNCATE INFO] no truncate info in exsit version_range", KR(ret), K(tablet_id), K(read_version_range), K_(truncate_info_cache));
      } else {
        newest_schema_version = truncate_info_cache_.newest_schema_version();
        newest_commit_version = truncate_info_cache_.newest_commit_version();
      }
    } else {
      replay_seq = truncate_info_cache_.replay_seq();
    }
  }
  if (OB_SUCC(ret) && need_read_truncate_info) {
    // step1. try to get from kv cache
    const ObTruncateInfoCacheKey cache_key(tablet_id, newest_schema_version, last_major_snapshot);
    const bool read_kv_cache = (read_version_range.base_version_ >= last_major_snapshot)
        && newest_schema_version > 0
        // if exist new version truncate_info, cache is incomplete for old_snapshot_query
        && (read_version_range.snapshot_version_ >= newest_commit_version);
    if (read_kv_cache && OB_SUCC(ObTruncateInfoKVCacheUtil::get_truncate_info_array(allocator, cache_key, truncate_info_array))) {
      LOG_INFO("[TRUNCATE INFO] read truncate info from kv cache", KR(ret), K(tablet_id), K(read_version_range), K(truncate_info_array), K_(truncate_info_cache));
    } else if (OB_UNLIKELY(read_kv_cache && OB_ENTRY_NOT_EXIST != ret)) {
      LOG_WARN("unexpected errno when get truncate info from kv cache", KR(ret), K(cache_key));
    } else if (OB_FAIL(inner_read_truncate_info_array_from_mds(allocator, read_version_range, truncate_info_array, collector))) {
    } else if (collector.exist_new_node() && for_access) {
      ret = OB_SNAPSHOT_DISCARDED;
      LOG_INFO("need to refresh read_snapshot or retry to wait mds trans commit", KR(ret), K(read_version_range), K(collector));
    } else if (collector.exist_new_node() || read_version_range.base_version_ > last_major_snapshot) {
      LOG_INFO("[TRUNCATE INFO] exist new truncate info or not complete version range, should not update cache", KR(ret), K(tablet_id),
        K(collector), K(read_version_range), K(last_major_snapshot));
    } else {
      LOG_INFO("[TRUNCATE INFO] read truncate info from mds", KR(ret), K(tablet_id), K(truncate_info_array.count()), K(read_version_range), K_(truncate_info_cache), K(collector));
      if (replay_seq >= 0 && mds_cache_lock_.try_wrlock()) {
        // step3. update tablet cache
        if (replay_seq == truncate_info_cache_.replay_seq()) { // there is no new mds trans during mds_query
          if (truncate_info_array.empty()) {
            truncate_info_cache_.set_empty();
          } else {
            const ObTruncateInfo *last_info = truncate_info_array.at(truncate_info_array.count() - 1);
            if (OB_ISNULL(last_info)) {
              ret = OB_INVALID_DATA;
              LOG_WARN("invalid data", KR(ret), K(tablet_id), K(truncate_info_array));
            } else {
              truncate_info_cache_.set_value(last_info->commit_version_, last_info->schema_version_, truncate_info_array.count());
              LOG_INFO("[TRUNCATE INFO] success to set truncate info value", KR(ret), K(tablet_id), K(read_version_range), K_(truncate_info_cache), K(cache_key));
            }
          }
        }
        mds_cache_lock_.unlock();
      }
    }
  }
#ifdef ERRSIM
  if (OB_SUCC(ret) && EN_COMPACTION_RECORD_TRUNCATE_CACHE) {
    record_truncate_cache(tablet_id, collector, truncate_info_cache_);
  }
#endif
  return ret;
}

int ObTablet::inner_read_truncate_info_array_from_mds(
      common::ObArenaAllocator &allocator,
      const common::ObVersionRange &read_version_range,
      storage::ObTruncateInfoArray &truncate_info_array,
      ObMdsReadInfoCollector &collector) const
{
  int ret = OB_SUCCESS;
  const common::ObTabletID &tablet_id = tablet_meta_.tablet_id_;
  SMART_VARS_2((ObTableScanParam, scan_param), (ObTabletTruncateInfoReader, truncate_info_reader)) {
    if (OB_FAIL(truncate_info_array.init_for_first_creation(allocator))) {
    } else if (OB_FAIL((ObMdsScanParamHelper::build_customized_scan_param<ObTruncateInfoKey, ObTruncateInfo>(
        allocator,
        tablet_id,
        read_version_range,
        collector,
        scan_param)))) {
    } else if (OB_FAIL(truncate_info_reader.init(*this, scan_param))) {
    } else {
      ObTruncateInfoKey key;
      ObTruncateInfo *info = nullptr;
      while (OB_SUCC(ret)) {
        if (OB_FAIL(ObTabletObjLoadHelper::alloc_and_new(allocator, info))) {
        } else if (OB_FAIL(truncate_info_reader.get_next_truncate_info(allocator, key, *info))) {
          if (OB_ITER_END == ret) {
            ObTabletObjLoadHelper::free(allocator, info);
            ret = OB_SUCCESS;
            break;
          } else {
            LOG_WARN("fail to get next truncate info", K(ret));
          }
        } else if (OB_FAIL(truncate_info_array.append_ptr(*info))) {
        }
        if (OB_FAIL(ret)) {
          ObTabletObjLoadHelper::free(allocator, info);
        }
      } // while
    }
  }

  if (OB_FAIL(ret)) {
    truncate_info_array.reset();
  }

  return ret;
}

int ObTablet::init_with_update_medium_info(
    common::ObArenaAllocator &allocator,
    const ObTablet &old_tablet,
    const bool clear_wait_check_flag)
{
  int ret = OB_SUCCESS;
  ObTabletMemberWrapper<ObTabletTableStore> table_store_wrapper;
  ObTabletMemberWrapper<ObTabletAutoincSeq> auto_inc_seqwrapper;
  const ObTabletMeta &old_tablet_meta = old_tablet.tablet_meta_;
  const ObTabletTableStore *old_table_store = nullptr;
  ObStorageSchema *old_storage_schema = nullptr;

  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret), K(is_inited_));
  } else if (OB_UNLIKELY(!old_tablet.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(old_tablet));
  } else if (OB_UNLIKELY(!pointer_hdl_.is_valid())
      || OB_ISNULL(log_handler_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet pointer handle is invalid", K(ret), K_(pointer_hdl), K_(pointer_hdl), K_(log_handler));
  } else if (OB_FAIL(assign_memtables(old_tablet.memtables_, old_tablet.memtable_count_))) {
  } else if (OB_ISNULL(ddl_kvs_ = static_cast<ObDDLKV **>(allocator.alloc(sizeof(ObDDLKV *) * DDL_KV_ARRAY_SIZE)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to allocate memory for ddl_kvs_", K(ret), KP(ddl_kvs_));
  } else if (OB_FAIL(assign_ddl_kvs(old_tablet.ddl_kvs_, old_tablet.ddl_kv_count_))) {
  } else if (OB_FAIL(old_tablet.fetch_table_store(table_store_wrapper))) {
  } else if (OB_FAIL(table_store_wrapper.get_member(old_table_store))) {
  } else if (OB_FAIL(old_tablet.load_storage_schema(allocator, old_storage_schema))) {
  } else if (OB_FAIL(tablet_meta_.assign(old_tablet_meta))) {
  } else if (OB_FAIL(ObTabletObjLoadHelper::alloc_and_new(allocator, table_store_addr_.ptr_))) {
  } else if (OB_FAIL(table_store_addr_.ptr_->init(allocator, *this, *old_table_store))) {
  } else if (OB_FAIL(try_update_start_scn())) {
  } else {
    tablet_meta_.extra_medium_info_.wait_check_flag_ = false;
    ALLOC_AND_INIT(allocator, storage_schema_addr_, *old_storage_schema);
  }

  if (FAILEDx(table_store_cache_.init(table_store_addr_.get_ptr()->get_major_sstables(),
      table_store_addr_.get_ptr()->get_minor_sstables()))) {
    LOG_WARN("failed to init table store cache", K(ret), KPC(this));
  } else if (OB_FAIL(init_aggregated_info(allocator, nullptr/* link_writer, tmp_tablet do no write */))) {
  } else if (FALSE_IT(set_initial_addr())) {
  } else if (OB_FAIL(check_medium_list())) {
  } else if (OB_FAIL(check_table_store_flag_match_with_table_store_(table_store_addr_.get_ptr()))) {
  } else {
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(build_read_info(allocator, nullptr /*tablet*/))) {
    } else if (OB_FAIL(inner_inc_macro_ref_cnt())) {
    } else {
      if (old_tablet.get_tablet_meta().has_next_tablet_) {
        set_next_tablet_guard(old_tablet.next_tablet_guard_);
      }
      LOG_INFO("succeeded to init tablet with update medium info", K(ret), KPC(this));
      is_inited_ = true;
    }
  }

  ObTabletObjLoadHelper::free(allocator, old_storage_schema);

  if (OB_UNLIKELY(!is_inited_)) {
    reset();
  }
  return ret;
}

int ObTablet::update_meta_last_persisted_committed_tablet_status_from_sstable(
    const ObUpdateTableStoreParam &param,
    const ObTabletCreateDeleteMdsUserData &old_last_persisted_committed_tablet_status)
{
  int ret = OB_SUCCESS;
  if (is_mds_minor_merge(param.compaction_info_.merge_type_)) {
    if (OB_FAIL(tablet_meta_.last_persisted_committed_tablet_status_.assign(
        old_last_persisted_committed_tablet_status))) {
    } else {
    }
  } else if (OB_FAIL(update_tablet_status_from_sstable(true/*expect_persist_status*/))) {
  }
  return ret;
}

int ObTablet::update_tablet_status_from_sstable(const bool expect_persist_status)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_ls_inner_tablet())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected ls inner tablet to update last persisted tablet status", K(ret));
  } else {
    ObArenaAllocator allocator(ObMemAttr("mds_reader", ObCtxIds::DEFAULT_CTX_ID));
    ObTabletCreateDeleteMdsUserData last_tablet_status;

    if (OB_FAIL((read_data_from_mds_sstable<mds::DummyKey, ObTabletCreateDeleteMdsUserData>(
        allocator, mds::DummyKey(), tablet_meta_.mds_checkpoint_scn_/*snapshot*/,
        ObTabletCommon::DEFAULT_GET_TABLET_DURATION_US/*timeout_us*/,
        ReadTabletStatusOp(last_tablet_status))))) {
      if (OB_ITER_END == ret) {
        if (expect_persist_status && tablet_meta_.local_status_.check_allow_read()) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unexpect none tablet status in sstables", K(ret), K(tablet_meta_));
        } else {
          // A tablet whose local data is not readable has no persisted status to validate here.
          ret = OB_SUCCESS;
        }
      } else {
        LOG_WARN("fail to read data from mds sstable", K(ret));
      }
    } else if (OB_FAIL(tablet_meta_.last_persisted_committed_tablet_status_.assign(last_tablet_status))) {
    } else {
      LOG_INFO("succeed to read last tablet status from sstable", K(ret),
          "tablet_id", tablet_meta_.tablet_id_, "local_status", tablet_meta_.local_status_,
          "last_tablet_status", last_tablet_status,
          "last_persisted_commmited_tablet_status", tablet_meta_.last_persisted_committed_tablet_status_);
    }
  }

  return ret;
}

int ObTablet::init_with_updated_members(
    common::ObArenaAllocator &allocator,
    const ObTablet &old_tablet,
    const int64_t snapshot_version
)
{
  int ret = OB_SUCCESS;
  ObTabletMemberWrapper<ObTabletTableStore> table_store_wrapper;
  const ObTabletMeta &old_tablet_meta = old_tablet.tablet_meta_;
  const ObTabletTableStore *old_table_store = nullptr;
  ObStorageSchema *old_storage_schema = nullptr;

  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret), K(is_inited_));
  } else if (OB_UNLIKELY(!old_tablet.is_valid() || 0 >= snapshot_version
)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(old_tablet), K(snapshot_version));
  } else if (OB_UNLIKELY(!pointer_hdl_.is_valid())
      || OB_ISNULL(log_handler_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet pointer handle is invalid", K(ret), K_(pointer_hdl), K_(pointer_hdl), K_(log_handler));
  } else if (OB_FAIL(assign_memtables(old_tablet.memtables_, old_tablet.memtable_count_))) {
  } else if (OB_ISNULL(ddl_kvs_ = static_cast<ObDDLKV **>(allocator.alloc(sizeof(ObDDLKV *) * DDL_KV_ARRAY_SIZE)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to allocate memory for ddl_kvs_", K(ret), KP(ddl_kvs_));
  } else if (OB_FAIL(assign_ddl_kvs(old_tablet.ddl_kvs_, old_tablet.ddl_kv_count_))) {
  } else if (OB_FAIL(old_tablet.fetch_table_store(table_store_wrapper))) {
  } else if (OB_FAIL(table_store_wrapper.get_member(old_table_store))) {
  } else if (OB_FAIL(old_tablet.load_storage_schema(allocator, old_storage_schema))) {
  } else if (OB_FAIL(tablet_meta_.assign(old_tablet_meta))) {
  } else if (FALSE_IT(tablet_meta_.snapshot_version_ = MAX(snapshot_version, tablet_meta_.snapshot_version_))) {
  }

  if (FAILEDx(ObTabletObjLoadHelper::alloc_and_new(allocator, table_store_addr_.ptr_))) {
    LOG_WARN("fail to allocate and new table store", K(ret));
  } else if (OB_FAIL(table_store_addr_.ptr_->init(allocator, *this, *old_table_store))) {
  } else if (OB_FAIL(try_update_start_scn())) {
  } else if (OB_FAIL(init_aggregated_info(allocator, nullptr/* link_writer, tmp_tablet do no write */))) {
  } else if (FALSE_IT(set_initial_addr())) {
  } else if (OB_FAIL(check_table_store_flag_match_with_table_store_(table_store_addr_.get_ptr()))) {
  } else {
    ALLOC_AND_INIT(allocator, storage_schema_addr_, *old_storage_schema);
  }

  if (FAILEDx(build_read_info(allocator, nullptr /*tablet*/))) {
    LOG_WARN("failed to build read info", K(ret));
  } else if (OB_FAIL(table_store_cache_.init(table_store_addr_.get_ptr()->get_major_sstables(),
      table_store_addr_.get_ptr()->get_minor_sstables()))) {
  } else if (OB_FAIL(inner_inc_macro_ref_cnt())) {
  } else {
    if (old_tablet.get_tablet_meta().has_next_tablet_) {
      set_next_tablet_guard(old_tablet.next_tablet_guard_);
    }
    LOG_INFO("succeeded to init tablet with updated members", K(ret), K(snapshot_version), KPC(this), K(old_tablet));
    is_inited_ = true;
  }

  if (OB_UNLIKELY(!is_inited_)) {
    reset();
  }

  ObTabletObjLoadHelper::free(allocator, old_storage_schema);

  return ret;
}

int ObTablet::init_with_mds_sstable(
    common::ObArenaAllocator &allocator,
    const ObTablet &old_tablet,
    const share::SCN &flush_scn,
    const ObUpdateTableStoreParam &param)
{
  TIMEGUARD_INIT(STORAGE, 10_ms);
  int ret = OB_SUCCESS;
  allocator_ = &allocator;
  common::ObArenaAllocator tmp_arena_allocator(common::ObMemAttr("InitTabletMDS"));
  ObTabletMemberWrapper<ObTabletTableStore> old_table_store_wrapper;
  const ObTabletTableStore *old_table_store = nullptr;
  ObStorageSchema *old_storage_schema = nullptr;
  const share::SCN &mds_checkpoint_scn = is_mds_minor_merge(param.compaction_info_.merge_type_) ? old_tablet.tablet_meta_.mds_checkpoint_scn_ : flush_scn;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret), K(is_inited_));
  } else if (OB_UNLIKELY(!pointer_hdl_.is_valid())
      || OB_ISNULL(log_handler_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet pointer handle is invalid", K(ret), K_(pointer_hdl), K_(log_handler));
  } else if (OB_UNLIKELY(!is_mds_merge(param.compaction_info_.merge_type_))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected merge type", K(ret), K(param));
  } else if (CLICK_FAIL(old_tablet.fetch_table_store(old_table_store_wrapper))) {
    LOG_WARN("failed to fetch old table store", K(ret), K(old_tablet));
  } else if (CLICK_FAIL(old_table_store_wrapper.get_member(old_table_store))) {
    LOG_WARN("failed to get old table store", K(ret));
  } else if (CLICK_FAIL(old_tablet.load_storage_schema(tmp_arena_allocator, old_storage_schema))) {
    LOG_ERROR("failed to load storage schema", K(ret), K(old_tablet));
  } else if (CLICK_FAIL(tablet_meta_.init(old_tablet.tablet_meta_, mds_checkpoint_scn))) {
    LOG_WARN("failed to init tablet meta", K(ret), K(old_tablet), K(flush_scn));
  } else if (CLICK_FAIL(pull_memtables(allocator))) {
    LOG_WARN("fail to pull memtable", K(ret));
  } else if (CLICK_FAIL(ObTabletObjLoadHelper::alloc_and_new(allocator, table_store_addr_.ptr_))) {
    LOG_WARN("fail to alloc and new table store object", K(ret), K_(table_store_addr));
  } else if (CLICK_FAIL(table_store_addr_.get_ptr()->init(*allocator_, *this, param, *old_table_store))) {
    LOG_WARN("fail to init table store", K(ret), K(param), KPC(old_table_store));
  } else if (OB_FAIL(try_update_start_scn())) {
  } else if (!is_ls_inner_tablet() && CLICK_FAIL(update_meta_last_persisted_committed_tablet_status_from_sstable(
      param, old_tablet.tablet_meta_.last_persisted_committed_tablet_status_))) {
    LOG_WARN("failed to update last_persisted_committed_tablet_status_ ", K(ret), K(param),
        "old_last_persisted_committed_tablet_status",
        old_tablet.tablet_meta_.last_persisted_committed_tablet_status_);
  } else {
    ALLOC_AND_INIT(allocator, storage_schema_addr_, *old_storage_schema);
  }

  if (FAILEDx(table_store_cache_.init(table_store_addr_.get_ptr()->get_major_sstables(),
      table_store_addr_.get_ptr()->get_minor_sstables()))) {
    LOG_WARN("failed to init table store cache", K(ret), KPC(this));
  } else if (CLICK_FAIL(build_read_info(*allocator_, nullptr /*tablet*/))) {
    LOG_WARN("failed to build read info", K(ret));
  } else if (CLICK_FAIL(check_medium_list())) {
    LOG_WARN("failed to check medium list", K(ret), KPC(this));
  } else if (CLICK_FAIL(check_sstable_column_checksum())) {
    LOG_ERROR("failed to check sstable column checksum", K(ret), KPC(this));
  } else if (OB_FAIL(init_aggregated_info(allocator, nullptr))) {
  } else if (FALSE_IT(set_initial_addr())) {
  } else if (OB_FAIL(check_table_store_flag_match_with_table_store_(table_store_addr_.get_ptr()))) {
  } else if (CLICK_FAIL(inner_inc_macro_ref_cnt())) {
    LOG_WARN("failed to increase macro ref cnt", K(ret));
  /* NOTICE!!!
   * Subsequently, skipping `is_inited_ = true` is prohibited (i.e., OB_FAIL must not occur), otherwise
   * it will lead to a macro block refcnt leak. */
  } else {
    if (old_tablet.get_tablet_meta().has_next_tablet_) {
      set_next_tablet_guard(old_tablet.next_tablet_guard_);
    }
    is_inited_ = true;
    LOG_INFO("succeeded to init tablet with mds sstable",
        K(ret), K(flush_scn), K(param), K(old_tablet), KPC(this));
  }
  ObTabletObjLoadHelper::free(tmp_arena_allocator, old_storage_schema);
  return ret;
}

int ObTablet::init_empty_shell(
    ObArenaAllocator &allocator,
    const ObTablet &old_tablet)
{
  int ret = OB_SUCCESS;
  ObTabletCreateDeleteMdsUserData user_data;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret), K(is_inited_));
  } else if (OB_UNLIKELY(old_tablet.get_tablet_meta().has_next_tablet_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("old tablet should not have next tablet", K(ret), K(old_tablet.get_tablet_meta()));
  } else if (OB_FAIL(pre_check_empty_shell(old_tablet, user_data))) {
  } else if (OB_FAIL(tablet_meta_.assign(old_tablet.tablet_meta_))) {
  } else if (OB_FAIL(tablet_meta_.last_persisted_committed_tablet_status_.assign(user_data))) {
  } else if (OB_FAIL(wait_release_memtables_())) {
  } else if (OB_FAIL(mark_mds_table_switched_to_empty_shell_())) {
  } else if (OB_FAIL(ObTabletObjLoadHelper::alloc_and_new(allocator, table_store_addr_.ptr_))) {
  } else if (OB_FAIL(table_store_addr_.ptr_->init(allocator, *this))) {
  } else if (OB_FAIL(try_update_start_scn())) {
  } else {
    tablet_meta_.extra_medium_info_.reset();
    table_store_addr_.addr_.set_none_addr();
    storage_schema_addr_.addr_.set_none_addr();
    macro_info_addr_.addr_.set_none_addr();
    tablet_meta_.clog_checkpoint_scn_ = user_data.delete_commit_scn_ > tablet_meta_.clog_checkpoint_scn_ ?
                                          user_data.delete_commit_scn_ : tablet_meta_.clog_checkpoint_scn_;
    tablet_meta_.mds_checkpoint_scn_ = user_data.delete_commit_scn_;
    tablet_meta_.is_empty_shell_ = true;
    is_inited_ = true;
    LOG_INFO("init empty shell", K(ret), K(old_tablet), KPC(this));
  }

  if (OB_UNLIKELY(!is_inited_)) {
    reset();
  }

  return ret;
}

int ObTablet::check_sstable_column_checksum() const
{
  int ret = OB_SUCCESS;
  common::ObArenaAllocator allocator(common::ObMemAttr("CKColCKS"));
  if (share::is_reserve_mode()) {
    allocator.set_ctx_id(ObCtxIds::MERGE_RESERVE_CTX_ID);
  }
  ObStorageSchema *storage_schema = nullptr;
  ObTableStoreIterator iter;
  int64_t schema_col_cnt = 0;
  int64_t sstable_col_cnt = 0;
  if (OB_UNLIKELY(!table_store_addr_.is_valid() || !storage_schema_addr_.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("failed to check tablet ", K(ret), K(table_store_addr_), K(storage_schema_addr_));
  } else if (OB_FAIL(load_storage_schema(allocator, storage_schema))) {
  } else if (OB_FAIL(storage_schema->get_stored_column_count_in_sstable(schema_col_cnt))) {
  } else if (OB_FAIL(inner_get_all_sstables(iter))) {
  } else {
    ObITable *table = nullptr;
    while (OB_SUCC(ret)) {
      if (OB_FAIL(iter.get_next(table))) {
        if (OB_ITER_END == ret) {
          ret = OB_SUCCESS;
          break;
        } else {
          LOG_WARN("failed to get next table", K(ret), KPC(this));
        }
      } else if (OB_ISNULL(table) || OB_UNLIKELY(!table->is_sstable())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected error, table is nullptr", K(ret), KPC(table));
      } else {
        ObSSTable *cur = static_cast<ObSSTable *>(table);
        ObSSTableMetaHandle meta_handle;
        if (OB_FAIL(cur->get_meta(meta_handle))) {
        } else if ((sstable_col_cnt = meta_handle.get_sstable_meta().get_col_checksum_cnt()) > schema_col_cnt) {
          ret = OB_ERR_UNEXPECTED;
          LOG_ERROR("The storage schema is older than the sstable, and cann’t explain the data.",
              K(ret), K(sstable_col_cnt), K(schema_col_cnt), KPC(cur), KPC(storage_schema));
        }
      }
    }
  }
  ObTabletObjLoadHelper::free(allocator, storage_schema);
  return ret;
}

int ObTablet::serialize(char *buf, const int64_t len, int64_t &pos, const ObSArray<ObInlineSecondaryMeta> &meta_arr) const
{
  int ret = OB_SUCCESS;
  ObTabletBlockHeader block_header;
  const int64_t total_length = get_serialize_size(meta_arr);
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else if (OB_ISNULL(buf)
      || OB_UNLIKELY(len <= 0)
      || OB_UNLIKELY(pos < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(buf), K(len), K(pos));
  } else if (OB_UNLIKELY(!is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("tablet is invalid", K(ret), K(*this));
  } else if (OB_UNLIKELY(TABLET_PAYLOAD_VERSION != version_)) {
    ret = OB_VERSION_NOT_MATCH;
    LOG_ERROR("tablet payload version mismatch", K(ret), K_(version), K(TABLET_PAYLOAD_VERSION));
  } else if (OB_FAIL(block_header.init(meta_arr.count()))) {
  } else if (OB_UNLIKELY(1 < meta_arr.count())) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("shouldn't have more than one inline meta", K(meta_arr.count()));
  } else {
    const int64_t header_size = block_header.get_serialize_size();
    const int64_t self_size = get_self_serialize_size();
    int64_t payload_pos = pos + header_size;
    int64_t header_pos = pos;
    if (OB_FAIL(self_serialize(buf, len, payload_pos))) {
    } else {
      block_header.length_ = self_size;
      block_header.checksum_ = ob_crc64(buf + (pos + header_size), self_size);
      const ObTabletMacroInfo *macro_info = nullptr;
      for (int64_t i = 0; OB_SUCC(ret) && i < meta_arr.count(); i++) {
        if (OB_ISNULL(meta_arr[i].obj_)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("obj is nullptr", K(ret), K(meta_arr[i].meta_type_));
        } else if (OB_UNLIKELY(ObSecondaryMetaType::TABLET_MACRO_INFO != meta_arr[i].meta_type_)) {
          ret = OB_NOT_SUPPORTED;
          LOG_WARN("only support tablet macro info", K(ret), K(meta_arr[i].meta_type_));
        } else if (FALSE_IT(macro_info = reinterpret_cast<const ObTabletMacroInfo *>(meta_arr[i].obj_))) {
        } else if (OB_FAIL(macro_info->serialize(buf, len, payload_pos))) {
        } else if (OB_FAIL(block_header.push_inline_meta(ObInlineSecondaryMetaDesc(meta_arr[i].meta_type_, macro_info->get_serialize_size())))) {
        }
      }
    }
    if (OB_FAIL(ret)) {
      // do nothing
    } else if (OB_UNLIKELY(payload_pos - pos != total_length)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("tablet's length doesn't match calculated length", K(ret), K(payload_pos), K(pos), K(total_length));
    } else if (OB_FAIL(block_header.serialize(buf, len, header_pos))) {
    } else if (OB_UNLIKELY(header_pos - pos != header_size)) {
      LOG_WARN("block header's length doesn't match calculated length", K(ret), K(header_pos), K(pos), K(header_pos), K(block_header));
    } else {
      pos = payload_pos;
    }
  }
  return ret;
}

int ObTablet::self_serialize(char *buf, const int64_t len, int64_t &pos) const
{
  int ret = OB_SUCCESS;
  int64_t new_pos = pos;
  const int64_t length = get_self_serialize_size();
  if (OB_UNLIKELY(length > len - pos)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("buffer's length is not enough", K(ret), K(length), K(len - new_pos));
  } else if (OB_FAIL(serialization::encode_i32(buf, len, new_pos, version_))) {
  } else if (new_pos - pos < length && OB_FAIL(serialization::encode_i32(buf, len, new_pos, length))) {
    LOG_WARN("failed to serialize tablet meta's length", K(ret), K(len), K(new_pos), K(length));
  } else if (new_pos - pos < length && OB_FAIL(tablet_meta_.serialize(buf, len, new_pos))) {
    LOG_WARN("failed to serialize tablet meta", K(ret), K(len), K(new_pos));
  } else if (new_pos - pos < length && OB_FAIL(table_store_addr_.addr_.serialize(buf, len, new_pos))) {
    LOG_WARN("failed to serialize table store addr", K(ret), K(len), K(new_pos), K(table_store_addr_));
  } else if (new_pos - pos < length && OB_FAIL(storage_schema_addr_.addr_.serialize(buf, len, new_pos))) {
    LOG_WARN("failed to serialize storage schema addr", K(ret), K(len), K(new_pos), K(table_store_addr_));
  } else if ((OB_NOT_NULL(rowkey_read_info_))
      && new_pos - pos < length && OB_FAIL(rowkey_read_info_->serialize(buf, len, new_pos))) {
    LOG_WARN("fail to serialize rowkey read info", K(ret), KPC(rowkey_read_info_));
  } else if (new_pos - pos < length && OB_FAIL(macro_info_addr_.addr_.serialize(buf, len, new_pos))) {
    LOG_WARN("failed to serialize macro info addr", K(ret), K(len), K(new_pos), K(macro_info_addr_));
  } else if (OB_UNLIKELY(length != new_pos - pos)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet's length doesn't match standard length", K(ret), K(new_pos), K(pos), K(length), KPC(this));
  } else {
    pos = new_pos;
  }
  return ret;
}

int ObTablet::release_ref_cnt(
    common::ObArenaAllocator &allocator,
    const char *buf,
    const int64_t len,
    int64_t &pos)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(partial_deserialize(allocator, buf, len, pos))) {
  } else {
    hold_ref_cnt_ = true;
    dec_macro_ref_cnt();
  }
  return ret;
}

int ObTablet::inc_snapshot_ref_cnt(
    common::ObArenaAllocator &allocator,
    const char *buf,
    const int64_t len,
    int64_t &pos)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(partial_deserialize(allocator, buf, len, pos))) {
  } else if (OB_FAIL(inner_inc_macro_ref_cnt())) {
  }
  return ret;
}

int ObTablet::partial_deserialize(
    common::ObArenaAllocator &allocator,
    const char *buf,
    const int64_t len,
    int64_t &pos)
{
  int ret = OB_SUCCESS;
  int64_t new_pos = pos;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("cannot deserialize inited tablet meta", K(ret), K_(is_inited));
  } else if (OB_ISNULL(buf)
      || OB_UNLIKELY(len <= 0)
      || OB_UNLIKELY(pos < 0)
      || OB_UNLIKELY(len <= pos)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(buf), K(len), K(pos));
  } else {
    do {
      if (OB_FAIL(load_deserialize_current(allocator, buf, len, new_pos, false/*pull memtable*/))) {
      }
    } while (ignore_ret(ret));
  }

  if (OB_FAIL(ret)) {
    // do nothing
  } else if (tablet_meta_.has_next_tablet_) {
    ObTablet next_tablet;
    next_tablet.set_tablet_addr(tablet_addr_);
    if (OB_FAIL(next_tablet.partial_deserialize(allocator, buf, len, new_pos))) {
    }
  }
  if (OB_SUCC(ret)) {
    pos = new_pos;
  }
  return ret;
}

int ObTablet::deserialize_for_replay(
    common::ObArenaAllocator &allocator,
    const char *buf,
    const int64_t len,
    int64_t &pos)
{
  int ret = OB_SUCCESS;
  int64_t new_pos = pos;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("cannot deserialize inited tablet meta", K(ret), K_(is_inited));
  } else if (OB_ISNULL(buf)
      || OB_UNLIKELY(len <= 0)
      || OB_UNLIKELY(pos < 0)
      || OB_UNLIKELY(len <= pos)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(buf), K(len), K(pos));
  } else if (OB_FAIL(load_deserialize_current(
      allocator, buf, len, new_pos, false/*prepare_memtable*/))) {
  } else if (OB_FAIL(inner_inc_macro_ref_cnt())) {
  } else {
    /* No failing operation may be added after increasing macro references,
     * otherwise replay failure would leak those references. */
    pos = new_pos;
    is_inited_ = true;
    LOG_INFO("succeed to deserialize tablet for replay", K(ret), KPC(this));
  }
  if (OB_FAIL(ret) && OB_UNLIKELY(!is_inited_)) {
    reset();
  }
  return ret;
}

// deserialize to a full tablet
int ObTablet::deserialize(
    common::ObArenaAllocator &allocator,
    const char *buf,
    const int64_t len,
    int64_t &pos)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(load_deserialize(allocator, buf, len, pos))) {
  } else if (OB_FAIL(deserialize_post_work(allocator))) {
  }
  return ret;
}

int ObTablet::load_deserialize(
    common::ObArenaAllocator &allocator,
    const char *buf,
    const int64_t len,
    int64_t &pos)
{
  int ret = OB_SUCCESS;
  int64_t new_pos = pos;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("cannot deserialize inited tablet meta", K(ret), K_(is_inited));
  } else if (OB_ISNULL(buf)
      || OB_UNLIKELY(len <= 0)
      || OB_UNLIKELY(pos < 0)
      || OB_UNLIKELY(len <= pos)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(buf), K(len), K(pos));
  } else if (OB_FAIL(load_deserialize_current(allocator, buf, len, new_pos, true/*prepare_memtable*/))) {
  }

  if (OB_FAIL(ret)) {
  } else if (tablet_meta_.has_next_tablet_) {
    const ObTabletMapKey key(tablet_meta_.tablet_id_);
    if (OB_FAIL(ObTabletCreateDeleteHelper::acquire_tmp_tablet(key, allocator, next_tablet_guard_))) {
    } else if (OB_ISNULL(next_tablet_guard_.get_obj())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("next tablet is null", K(ret));
    } else if (FALSE_IT(next_tablet_guard_.get_obj()->tablet_addr_ = tablet_addr_)) {
    } else if (OB_FAIL(next_tablet_guard_.get_obj()->load_deserialize(allocator, buf, len, new_pos))) {
    }
  }

  if (OB_SUCC(ret)) {
    pos = new_pos;
  } else if (OB_UNLIKELY(!is_inited_)) {
    reset();
  }
  return ret;
}

int ObTablet::deserialize_post_work(common::ObArenaAllocator &allocator)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("cannot deserialize inited tablet meta", K(ret), K_(is_inited));
  } else if (TABLET_PAYLOAD_VERSION != version_) {
    ret = OB_VERSION_NOT_MATCH;
    LOG_WARN("tablet payload version mismatch", K(ret), K_(version), K(TABLET_PAYLOAD_VERSION));
  } else {
    if (!table_store_addr_.addr_.is_none()) {
      IO_AND_DESERIALIZE(allocator, table_store_addr_.addr_, table_store_addr_.ptr_, *this);
      if (FAILEDx(ObCacheSSTableHelper::batch_cache_sstable_meta(
          allocator, INT64_MAX, table_store_addr_.ptr_))) {
        LOG_WARN("failed to cache sstable meta", K(ret), KPC(table_store_addr_.ptr_));
      }
    } else {
      ALLOC_AND_INIT(allocator, table_store_addr_, (*this));
    }
  }

  if (OB_FAIL(ret)) {
  } else {
    ObArenaAllocator arena_allocator(common::ObMemAttr("TmpSchema"));
    ObStorageSchema *schema = nullptr;
    if (!storage_schema_addr_.addr_.is_none()) {
      if (OB_FAIL(load_storage_schema(arena_allocator, schema))) {
      } else if (OB_FAIL(table_store_cache_.init(
          table_store_addr_.get_ptr()->get_major_sstables(),
          table_store_addr_.get_ptr()->get_minor_sstables()))) {
      } else if (OB_UNLIKELY(tablet_meta_.max_sync_storage_schema_version_ > schema->schema_version_)) {
        ret = OB_DESERIALIZE_ERROR;
        LOG_WARN("tablet meta and storage schema versions are inconsistent", K(ret),
            K(tablet_meta_.max_sync_storage_schema_version_), K(schema->schema_version_));
      }
      ObTabletObjLoadHelper::free(arena_allocator, schema);
    }
    if (OB_SUCC(ret) && tablet_meta_.has_next_tablet_
        && OB_FAIL(next_tablet_guard_.get_obj()->deserialize_post_work(allocator))) {
      LOG_WARN("failed to finish deserializing next tablet", K(ret));
    }
    if (FAILEDx(inner_inc_macro_ref_cnt())) {
      LOG_WARN("failed to increase macro ref cnt", K(ret));
    }
    /* NOTICE!!!
     * Subsequently, skipping `is_inited_ = true` is prohibited (i.e., OB_FAIL must not occur), otherwise
     * it will lead to a macro block refcnt leak. */
    if (OB_SUCC(ret)) {
      is_inited_ = true;
      LOG_INFO("succeed to load current tablet format", K(ret), KPC(this));
    }
  }
  if (OB_FAIL(ret) && OB_UNLIKELY(!is_inited_)) {
    reset();
  }
  return ret;
}

int ObTablet::load_deserialize_current(
    common::ObArenaAllocator &allocator,
    const char *buf,
    const int64_t len,
    int64_t &pos,
    const bool prepare_memtable)
{
  int ret = OB_SUCCESS;
  const int64_t block_start_pos = pos;
  int64_t payload_pos = block_start_pos;
  int64_t new_pos = block_start_pos;
  int64_t payload_end = 0;
  int32_t crc = 0;
  ObTabletBlockHeader header;
  macro_info_addr_.addr_.set_none_addr();

  if (OB_FAIL(header.deserialize(buf, len, payload_pos))) {
  } else if (FALSE_IT(new_pos = payload_pos)) {
  } else if (FALSE_IT(payload_end = payload_pos + header.length_)) {
  } else if (FALSE_IT(crc = static_cast<int32_t>(ob_crc64(buf + payload_pos, header.length_)))) {
  } else if (OB_UNLIKELY(header.checksum_ != crc)) {
    ret = OB_CHECKSUM_ERROR;
    LOG_ERROR("tablet payload checksum mismatch", K(ret), K(header), K(crc));
  } else if (OB_UNLIKELY(header.inline_meta_count_ > 1)) {
    ret = OB_DESERIALIZE_ERROR;
    LOG_WARN("invalid inline tablet meta count", K(ret), K(header));
  } else if (OB_FAIL(serialization::decode_i32(buf, payload_end, new_pos, &version_))) {
  } else if (OB_UNLIKELY(TABLET_PAYLOAD_VERSION != version_)) {
    ret = OB_VERSION_NOT_MATCH;
    LOG_WARN("tablet payload version mismatch", K(ret), K_(version), K(TABLET_PAYLOAD_VERSION));
  } else if (OB_FAIL(serialization::decode_i32(buf, payload_end, new_pos, &length_))) {
  } else if (OB_UNLIKELY(length_ != header.length_)) {
    ret = OB_DESERIALIZE_ERROR;
    LOG_WARN("tablet header and payload lengths mismatch", K(ret), K_(length), K(header));
  } else if (OB_FAIL(tablet_meta_.deserialize(buf, payload_end, new_pos))) {
  } else if (OB_FAIL(table_store_addr_.addr_.deserialize(buf, payload_end, new_pos))) {
  } else if (FALSE_IT(tablet_meta_.is_empty_shell_ = table_store_addr_.addr_.is_none())) {
  } else if (FALSE_IT(table_store_addr_.addr_.set_seq(tablet_addr_.seq()))) {
  } else if (OB_FAIL(storage_schema_addr_.addr_.deserialize(buf, payload_end, new_pos))) {
  } else if (!table_store_addr_.addr_.is_none()
      && OB_FAIL(ObTabletObjLoadHelper::alloc_and_new(allocator, rowkey_read_info_))) {
    LOG_WARN("failed to allocate rowkey read info", K(ret));
  } else if (!table_store_addr_.addr_.is_none()
      && OB_FAIL(rowkey_read_info_->deserialize(allocator, buf, payload_end, new_pos))) {
    LOG_WARN("failed to deserialize rowkey read info", K(ret), K(payload_end), K(new_pos));
  } else if (OB_FAIL(macro_info_addr_.addr_.deserialize(buf, payload_end, new_pos))) {
  } else if (OB_UNLIKELY(new_pos != payload_end)) {
    ret = OB_DESERIALIZE_ERROR;
    LOG_WARN("tablet payload length does not match current format", K(ret), K(new_pos), K(payload_end), K(header));
  } else if (1 == header.inline_meta_count_) {
    const ObInlineSecondaryMetaDesc &desc = header.desc_array_[0];
    const int64_t secondary_meta_pos = new_pos;
    int64_t offset = 0;
    int64_t size = 0;
    MacroBlockId macro_id;
    if (OB_UNLIKELY(ObSecondaryMetaType::TABLET_MACRO_INFO != desc.type_ || desc.length_ <= 0)) {
      ret = OB_DESERIALIZE_ERROR;
      LOG_WARN("invalid inline tablet macro info descriptor", K(ret), K(desc));
    } else if (OB_UNLIKELY(!tablet_addr_.is_valid() || !tablet_addr_.is_block())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("tablet address is invalid", K(ret), K(tablet_addr_));
    } else if (OB_FAIL(tablet_addr_.get_block_addr(macro_id, offset, size))) {
    } else if (OB_UNLIKELY(size < desc.length_)) {
      ret = OB_DESERIALIZE_ERROR;
      LOG_WARN("tablet block is smaller than inline tablet macro info", K(ret), K(size), K(desc));
    } else if (OB_FAIL(macro_info_addr_.addr_.set_block_addr(
        macro_id,
        offset + (size - desc.length_),
        desc.length_,
        ObMetaDiskAddr::DiskType::RAW_BLOCK))) {
    } else if (len - new_pos >= desc.length_) {
      // The first-level tablet load intentionally reads only a prefix of a RAW_BLOCK. Keep the
      // address so macro info can be loaded lazily when the prefix does not contain the inline
      // bytes in full.
      if (OB_FAIL(deserialize_macro_info(
          allocator, buf, new_pos + desc.length_, new_pos, macro_info_addr_.ptr_))) {
      } else if (OB_UNLIKELY(new_pos - secondary_meta_pos != desc.length_)) {
        ret = OB_DESERIALIZE_ERROR;
        LOG_WARN("inline tablet macro info length mismatch", K(ret), K(new_pos), K(secondary_meta_pos), K(desc));
      }
    }
  }

  if (OB_FAIL(ret)) {
  } else if (prepare_memtable && OB_FAIL(pull_memtables(allocator))) {
    LOG_WARN("failed to pull memtables", K(ret), K(len), K(block_start_pos));
  } else {
    pos = new_pos;
  }

  if (OB_FAIL(ret)) {
    ObTabletObjLoadHelper::free(allocator, rowkey_read_info_);
    ObTabletObjLoadHelper::free(allocator, macro_info_addr_.ptr_);
  }
  return ret;
}

// deserialize to a tiny tablet
int ObTablet::deserialize(
    const char *buf,
    const int64_t len,
    int64_t &pos)
{
  int ret = OB_SUCCESS;
  char *tablet_buf = reinterpret_cast<char *>(this);
  ObMetaObjBufferHeader &buf_header = ObMetaObjBufferHelper::get_buffer_header(tablet_buf);
  int64_t remain = buf_header.buf_len_ - sizeof(ObTablet);
  int64_t start_pos = sizeof(ObTablet);
  ObArenaAllocator allocator(common::ObMemAttr("deserialize"));
  ObDDLKV **ddl_kvs_addr = nullptr;
  int64_t ddl_kv_count = 0;
  ObTabletBlockHeader header;
  const int64_t block_start_pos = pos;
  int64_t payload_pos = block_start_pos;
  int64_t payload_end = 0;
  int32_t crc = 0;
  macro_info_addr_.addr_.set_none_addr();

  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("cannot deserialize inited tablet meta", K(ret), K_(is_inited));
  } else if (OB_ISNULL(buf)
      || OB_UNLIKELY(len <= 0)
      || OB_UNLIKELY(pos < 0)
      || OB_UNLIKELY(len <= pos)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(buf), K(len), K(pos));
  } else if (OB_FAIL(header.deserialize(buf, len, payload_pos))) {
  } else if (FALSE_IT(payload_end = payload_pos + header.length_)) {
  } else if (FALSE_IT(crc = static_cast<int32_t>(ob_crc64(buf + payload_pos, header.length_)))) {
  } else if (OB_UNLIKELY(header.checksum_ != crc)) {
    ret = OB_CHECKSUM_ERROR;
    LOG_ERROR("tablet payload checksum mismatch", K(ret), K(header), K(crc));
  } else if (OB_UNLIKELY(header.inline_meta_count_ > 1)) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("more than one inline tablet meta is not supported", K(ret), K(header));
  } else {
    int64_t new_pos = payload_pos;
    int64_t rowkey_info_copy_size = 0;
    if (OB_FAIL(serialization::decode_i32(buf, payload_end, new_pos, &version_))) {
    } else if (OB_UNLIKELY(TABLET_PAYLOAD_VERSION != version_)) {
      ret = OB_VERSION_NOT_MATCH;
      LOG_WARN("tablet payload version mismatch", K(ret), K_(version), K(TABLET_PAYLOAD_VERSION));
    } else if (OB_FAIL(serialization::decode_i32(buf, payload_end, new_pos, &length_))) {
    } else if (OB_UNLIKELY(length_ != header.length_)) {
      ret = OB_DESERIALIZE_ERROR;
      LOG_WARN("tablet header and payload lengths mismatch", K(ret), K_(length), K(header));
    } else if (OB_FAIL(tablet_meta_.deserialize(buf, payload_end, new_pos))) {
    } else if (OB_FAIL(pull_memtables_without_ddl())) {
    } else if (OB_FAIL(pull_ddl_memtables(allocator, ddl_kvs_addr, ddl_kv_count))) {
    } else if (OB_FAIL(table_store_addr_.addr_.deserialize(buf, payload_end, new_pos))) {
    } else if (FALSE_IT(tablet_meta_.is_empty_shell_ = table_store_addr_.addr_.is_none())) {
    } else if (FALSE_IT(table_store_addr_.addr_.set_seq(tablet_addr_.seq()))) {
    } else if (OB_FAIL(storage_schema_addr_.addr_.deserialize(buf, payload_end, new_pos))) {
    } else if (!table_store_addr_.addr_.is_none()) {
      ObRowkeyReadInfo rowkey_read_info;
      if (OB_FAIL(rowkey_read_info.deserialize(allocator, buf, payload_end, new_pos))) {
      } else if (remain < (rowkey_info_copy_size = rowkey_read_info.get_deep_copy_size())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("tablet memory buffer not enough for rowkey read info", K(ret), K(remain), K(rowkey_info_copy_size));
      } else if (OB_FAIL(rowkey_read_info.deep_copy(tablet_buf + start_pos, remain, rowkey_read_info_))) {
      } else if (OB_ISNULL(rowkey_read_info_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected null rowkey read info", K(ret));
      } else {
        remain -= rowkey_info_copy_size;
        start_pos += rowkey_info_copy_size;
      }
    }

    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(macro_info_addr_.addr_.deserialize(buf, payload_end, new_pos))) {
    } else if (OB_UNLIKELY(new_pos != payload_end)) {
      ret = OB_DESERIALIZE_ERROR;
      LOG_WARN("tablet payload length does not match current format", K(ret), K(new_pos), K(payload_end), K(header));
    }

    if (OB_FAIL(ret)) {
      ddl_kvs_ = ddl_kvs_addr;
      ddl_kv_count_ = ddl_kv_count;
      reset_ddl_memtables();
    } else if (OB_NOT_NULL(ddl_kvs_addr)) {
      // pull_ddl_memtables() already increments each DDL KV reference. The tiny tablet only copies
      // the pointer array into its own buffer and must not increment those references again.
      const int64_t ddl_kv_size = sizeof(ObDDLKV *) * DDL_KV_ARRAY_SIZE;
      if (remain < ddl_kv_size) {
        ret = OB_BUF_NOT_ENOUGH;
        LOG_WARN("tablet memory buffer not enough for ddl kvs", K(ret), K(remain), K(ddl_kv_size), K(ddl_kv_count));
      } else {
        ddl_kv_count_ = ddl_kv_count;
        ddl_kvs_ = reinterpret_cast<ObDDLKV **>(tablet_buf + start_pos);
        MEMCPY(ddl_kvs_, ddl_kvs_addr, ddl_kv_size);
        start_pos += ddl_kv_size;
        remain -= ddl_kv_size;
      }
    }

    if (OB_SUCC(ret)) {
      ObTabletTableStore *table_store = nullptr;
      if (table_store_addr_.addr_.is_none()) {
        if (OB_FAIL(ObTabletObjLoadHelper::alloc_and_new(allocator, table_store))) {
        } else if (OB_FAIL(table_store->init(allocator, *this))) {
        }
      } else {
        IO_AND_DESERIALIZE(allocator, table_store_addr_.addr_, table_store, *this);
      }
      if (OB_SUCC(ret)) {
        int64_t table_store_size = table_store->get_deep_copy_size();
        ObIStorageMetaObj *table_store_obj = nullptr;
        if (remain < table_store_size) {
          LOG_INFO("tablet memory buffer not enough for table store", K(ret), K(remain), K(table_store_size));
        } else if (OB_FAIL(ObCacheSSTableHelper::batch_cache_sstable_meta(
            allocator, remain - table_store_size, table_store))) {
        } else if (FALSE_IT(table_store_size = table_store->get_deep_copy_size())) {
        } else if (OB_FAIL(table_store->deep_copy(tablet_buf + start_pos, remain, table_store_obj))) {
        } else if (OB_ISNULL(table_store_obj)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unexpected null table store after deep copy", K(ret));
        } else {
          table_store_addr_.ptr_ = static_cast<ObTabletTableStore *>(table_store_obj);
          remain -= table_store_size;
          start_pos += table_store_size;
        }
      }
      if (OB_SUCC(ret) && !storage_schema_addr_.addr_.is_none()) {
        if (OB_FAIL(table_store_cache_.init(table_store->get_major_sstables(),
                table_store->get_minor_sstables()))) {
        }
      }
    }

    if (OB_FAIL(ret)) {
    } else if (1 == header.inline_meta_count_) {
      const ObInlineSecondaryMetaDesc &desc = header.desc_array_[0];
      const int64_t secondary_meta_pos = new_pos;
      int64_t offset = 0;
      int64_t size = 0;
      MacroBlockId macro_id;
      if (OB_UNLIKELY(ObSecondaryMetaType::TABLET_MACRO_INFO != desc.type_ || desc.length_ <= 0)) {
        ret = OB_DESERIALIZE_ERROR;
        LOG_WARN("invalid inline tablet macro info descriptor", K(ret), K(desc));
      } else if (OB_UNLIKELY(!tablet_addr_.is_valid() || !tablet_addr_.is_block())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("tablet address is invalid", K(ret), K(tablet_addr_));
      } else if (OB_FAIL(tablet_addr_.get_block_addr(macro_id, offset, size))) {
      } else if (OB_UNLIKELY(size < desc.length_)) {
        ret = OB_DESERIALIZE_ERROR;
        LOG_WARN("tablet block is smaller than inline tablet macro info", K(ret), K(size), K(desc));
      } else if (OB_FAIL(macro_info_addr_.addr_.set_block_addr(
          macro_id,
          offset + (size - desc.length_),
          desc.length_,
          ObMetaDiskAddr::DiskType::RAW_BLOCK))) {
      } else if (len - new_pos >= desc.length_) {
        ObTabletMacroInfo *tablet_macro_info = nullptr;
        int64_t macro_info_size = 0;
        if (OB_FAIL(deserialize_macro_info(
            allocator, buf, new_pos + desc.length_, new_pos, tablet_macro_info))) {
        } else if (OB_UNLIKELY(new_pos - secondary_meta_pos != desc.length_)) {
          ret = OB_DESERIALIZE_ERROR;
          LOG_WARN("inline tablet macro info length mismatch", K(ret), K(new_pos), K(secondary_meta_pos), K(desc));
        } else if (FALSE_IT(macro_info_size = tablet_macro_info->get_deep_copy_size())) {
        } else if (remain >= macro_info_size) {
          if (OB_FAIL(tablet_macro_info->deep_copy(tablet_buf + start_pos, remain, macro_info_addr_.ptr_))) {
          } else if (OB_ISNULL(macro_info_addr_.ptr_)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("unexpected null tablet macro info after deep copy", K(ret), K(macro_info_addr_));
          } else {
            remain -= macro_info_size;
            start_pos += macro_info_size;
          }
        }
      }
    }

    if (OB_SUCC(ret) && tablet_meta_.has_next_tablet_) {
      ObTabletHandle next_tablet_handle;
      const ObTabletMapKey key(tablet_meta_.tablet_id_);
      if (OB_FAIL(ObTabletCreateDeleteHelper::acquire_tablet_from_pool(ObTabletPoolType::TP_NORMAL, key, next_tablet_handle))) {
      } else if (OB_ISNULL(next_tablet_handle.get_obj())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_ERROR("next tablet is null", K(ret));
      } else if (FALSE_IT(next_tablet_handle.get_obj()->set_tablet_addr(tablet_addr_))) {
      } else if (OB_FAIL(next_tablet_handle.get_obj()->deserialize(buf, len, new_pos))) {
      } else {
        set_next_tablet_guard(next_tablet_handle);
      }
    }

    if (OB_SUCC(ret)) {
      pos = new_pos;
      is_inited_ = true;
      // must succeed if hold_ref_cnt_ has been set to true
      hold_ref_cnt_ = true;
      LOG_INFO("succeed to deserialize current tablet format", K(ret), KPC(this), K(header));
    }
  }

  if (OB_UNLIKELY(!is_inited_)) {
    reset();
  }
  return ret;
}

int ObTablet::deserialize_macro_info(
    common::ObArenaAllocator &allocator,
    const char *buf,
    const int64_t len,
    int64_t &pos,
    ObTabletMacroInfo *&tablet_macro_info)
{
  int ret = OB_SUCCESS;
  void *macro_info_buf = nullptr;
  if (OB_ISNULL(macro_info_buf = allocator.alloc(sizeof(ObTabletMacroInfo)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to allocate buf for tablet macro info", K(ret));
  } else if (FALSE_IT(tablet_macro_info = new (macro_info_buf) ObTabletMacroInfo)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to allocate buf for tablet macro info", K(ret));
  } else if (OB_FAIL(tablet_macro_info->deserialize(allocator, buf, len, pos))) {
  }
  return ret;
}

int ObTablet::get_tablet_first_second_level_meta_ids(ObIArray<MacroBlockId> &meta_ids) const
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(tablet_meta_.has_next_tablet_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("shouldn't have next tablet", K(ret), K(tablet_meta_), K(next_tablet_guard_));
  } else if (OB_FAIL(parse_meta_addr(tablet_addr_, meta_ids))) {
  } else if (OB_FAIL(parse_meta_addr(table_store_addr_.addr_, meta_ids))) {
  } else if (OB_FAIL(parse_meta_addr(storage_schema_addr_.addr_, meta_ids))) {
  }

  return ret;
}

int ObTablet::parse_meta_addr(const ObMetaDiskAddr &addr, ObIArray<MacroBlockId> &meta_ids)
{
  int ret = OB_SUCCESS;
  if (addr.is_block()) {
    if (OB_UNLIKELY(!addr.is_valid())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("tablet_status_uncommitted_kv_addr is invalid", K(ret), K(addr));
    } else {
      const MacroBlockId macro_id = addr.block_id();
      bool found = false;
      for (int64_t i = 0; !found && i < meta_ids.count(); i++) {
        if (macro_id == meta_ids.at(i)) {
          found = true;
        }
      }
      if (!found && OB_FAIL(meta_ids.push_back(macro_id))) {
        LOG_WARN("fail to push back macro id", K(ret), K(macro_id));
      }
    }
  }
  return ret;
}

int ObTablet::load_macro_info(
    const int64_t ls_epoch,
    ObArenaAllocator &allocator,
    ObTabletMacroInfo *&tablet_macro_info,
    bool &in_memory) const
{
  int ret = OB_SUCCESS;
  in_memory = false;
  if (macro_info_addr_.is_none_object()) {
    // return a macro_info with cnt_ = 0;
    // default construct param: EMPTY_LIST_ENTRY_BLOCK
    tablet_macro_info = static_cast<ObTabletMacroInfo*>(allocator.alloc(sizeof(ObTabletMacroInfo)));
    new (tablet_macro_info)ObTabletMacroInfo();
  } else if (macro_info_addr_.is_memory_object()) { // MEM
    if (OB_ISNULL(macro_info_addr_.ptr_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("tablet macro info ptr is null", K(ret), K_(macro_info_addr));
    } else {
      tablet_macro_info = macro_info_addr_.ptr_;
      in_memory = true;
    }
  } else if (macro_info_addr_.is_disk_object()) { // BLOCK, RAW_BLOCK
    char *buf = nullptr;
    int64_t buf_len = 0;
    int64_t pos = 0;
    void *macro_info_buf = nullptr;
    if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::storage::ObLocalStorageMetaService>()->read_from_disk(
        macro_info_addr_.addr_, allocator, buf, buf_len))) {
    } else if (OB_ISNULL(macro_info_buf = allocator.alloc(sizeof(ObTabletMacroInfo)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to allocate memory for tablet macro info", K(ret));
    } else if (FALSE_IT(tablet_macro_info = new (macro_info_buf) ObTabletMacroInfo)) {
    } else if (OB_FAIL(tablet_macro_info->deserialize(allocator, buf, buf_len, pos))) {
    }
  } else {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("this type may don't have macro info", K(ret), K(macro_info_addr_));
  }
  return ret;
}

int ObTablet::inc_macro_ref_cnt()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("tablet hasn't been inited", K(ret), K(is_inited_));
  } else if (OB_FAIL(inner_inc_macro_ref_cnt())) {
  } else if (tablet_meta_.has_next_tablet_
      && OB_FAIL(next_tablet_guard_.get_obj()->inc_macro_ref_cnt())) {
    LOG_WARN("fail to increase macro ref cnt for next tablet",
        K(ret), KPC(next_tablet_guard_.get_obj()));
  }
  return ret;
}

int ObTablet::inner_inc_macro_ref_cnt()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_meta_addr())) {
  } else if (OB_UNLIKELY(!is_empty_shell() && macro_info_addr_.is_none_object())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("normal tablet's macro_info_addr_ shouldn't be none object", K(ret), KPC(this));
  } else if (macro_info_addr_.is_none_object() || is_empty_shell()) {
    if (OB_FAIL(inc_ref_without_aggregated_info())) {
    }
  } else {
    if (OB_FAIL(inc_ref_with_aggregated_info())) {
    }
  }
  if (OB_SUCC(ret)) {
    hold_ref_cnt_ = true;
  }
  return ret;
}

int ObTablet::inc_ref_with_aggregated_info()
{
  int ret = OB_SUCCESS;
    ObArenaAllocator allocator("IncMacroRef", OB_MALLOC_NORMAL_BLOCK_SIZE);
    ObTabletMacroInfo *macro_info = nullptr;
    bool in_memory = true;
    bool inc_tablet_ref = false;
    bool inc_other_ref = false;
    ObMacroInfoIterator info_iter;

    if (OB_FAIL(load_macro_info(0, allocator, macro_info, in_memory))) {
    } else if (OB_FAIL(info_iter.init(ObTabletMacroType::MAX, *macro_info))) {
    } else if (OB_FAIL(inc_addr_ref_cnt(tablet_addr_, inc_tablet_ref))) {
    } else if (OB_FAIL(inc_ref_with_macro_iter(info_iter, inc_other_ref))) {
    }
    if (OB_FAIL(ret)) {
      if (inc_tablet_ref) {
        dec_addr_ref_cnt(tablet_addr_);
      }
      if (inc_other_ref) {
        int tmp_ret = OB_SUCCESS;
        if (OB_TMP_FAIL(info_iter.reuse())) {
        } else {
          dec_ref_with_macro_iter(info_iter);
        }
      }
    }
    if (OB_NOT_NULL(macro_info) && !in_memory) {
      macro_info->reset();
    }

  return ret;
}

int ObTablet::inc_ref_with_macro_iter(ObMacroInfoIterator &macro_iter, bool &inc_success) const
{
  int ret = OB_SUCCESS;
  int inc_other_ref_cnt = 0;
  ObTabletBlockInfo block_info;
  inc_success = false;
  ObSArray<MacroBlockId> print_arr;
  print_arr.set_attr(ObMemAttr("PrintId", ObCtxIds::DEFAULT_CTX_ID));
  while (OB_SUCC(ret)) {
    block_info.reset();
    if (OB_FAIL(macro_iter.get_next(block_info))) {
      if (OB_ITER_END != ret) {
        LOG_WARN("fail to get next block info", K(ret));
      }
    } else if (OB_FAIL(OB_STORAGE_OBJECT_MGR.inc_ref(block_info.macro_id_))) {
    } else if (FALSE_IT(inc_other_ref_cnt++)) {
    } else if (ObTabletMacroType::SHARED_DATA_BLOCK == block_info.block_type_
        && (OB_ISNULL(::oceanbase::share::server_service<::oceanbase::blocksstable::ObSharedMacroBlockMgr>())
            || OB_FAIL(::oceanbase::share::server_service<::oceanbase::blocksstable::ObSharedMacroBlockMgr>()->add_block(
                block_info.macro_id_, block_info.occupy_size_)))) {
      if (OB_SUCC(ret)) {
        ret = OB_ERR_UNEXPECTED;
      }
      LOG_WARN("fail to account shared macro block", K(ret), K(block_info));
    }
#ifndef OB_BUILD_PACKAGE
    int tmp_ret = OB_SUCCESS;
    if (OB_FAIL(ret)) {
      // do nothing
    } else if (OB_TMP_FAIL(print_arr.push_back(block_info.macro_id_))) {
    } else if (MAX_PRINT_COUNT == print_arr.size()) {
      print_arr.reuse();
    }
#endif
  }
#ifndef OB_BUILD_PACKAGE
  if (0 != print_arr.count()) {
    print_arr.reuse();
  }
#endif

  if (OB_LIKELY(OB_ITER_END == ret) || OB_SUCC(ret)) {
    inc_success = true;
    ret = OB_SUCCESS;
  } else if (inc_other_ref_cnt > 0) {
    int tmp_ret = OB_SUCCESS;
    if (OB_TMP_FAIL(macro_iter.reuse())) {
    } else {
      for (int64_t i = 0; i < inc_other_ref_cnt; i++) {
        block_info.reset();
        if (OB_TMP_FAIL(macro_iter.get_next(block_info))) {
        } else if (OB_TMP_FAIL(OB_STORAGE_OBJECT_MGR.dec_ref(block_info.macro_id_))) {
        } else if (ObTabletMacroType::SHARED_DATA_BLOCK == block_info.block_type_
            && OB_NOT_NULL(::oceanbase::share::server_service<::oceanbase::blocksstable::ObSharedMacroBlockMgr>())
            && OB_TMP_FAIL(::oceanbase::share::server_service<::oceanbase::blocksstable::ObSharedMacroBlockMgr>()->free_block(
                block_info.macro_id_, block_info.occupy_size_))) {
          LOG_WARN("fail to rollback shared macro block accounting", K(tmp_ret), K(block_info));
        }
#ifndef OB_BUILD_PACKAGE
        if (OB_TMP_FAIL(tmp_ret)) {
          // do nothing
        } else if (OB_TMP_FAIL(print_arr.push_back(block_info.macro_id_))) {
        } else if (MAX_PRINT_COUNT == print_arr.size()) {
          print_arr.reuse();
        }
#endif
      }
#ifndef OB_BUILD_PACKAGE
      if (0 != print_arr.count()) {
        print_arr.reuse();
      }
#endif
    }
  }

  FLOG_INFO("the tablet that inner increases ref cnt is",
      K(ret), K(hold_ref_cnt_), "tablet_id", tablet_meta_.tablet_id_,
      K(table_store_addr_.addr_), K(storage_schema_addr_.addr_),
      K(tablet_addr_), KP(this), K(macro_iter), K(lbt()));

  return ret;
}

int ObTablet::inc_ref_without_aggregated_info()
{
  int ret = OB_SUCCESS;
  bool inc_table_store_ref = false;
  bool inc_storage_schema_ref = false;
  bool inc_tablet_ref = false;
  bool inc_table_store_member_ref = false;

  if (OB_FAIL(inc_addr_ref_cnt(table_store_addr_.addr_, inc_table_store_ref))) {
  } else if (OB_FAIL(inc_addr_ref_cnt(storage_schema_addr_.addr_, inc_storage_schema_ref))) {
  } else if (OB_FAIL(inc_addr_ref_cnt(tablet_addr_, inc_tablet_ref))) {
  } else if (OB_FAIL(inc_table_store_ref_cnt(inc_table_store_member_ref))) {
  }

  if (OB_FAIL(ret)) {
    if (inc_table_store_ref) {
      dec_addr_ref_cnt(table_store_addr_.addr_);
    }
    if (inc_storage_schema_ref) {
      dec_addr_ref_cnt(storage_schema_addr_.addr_);
    }
    if (inc_tablet_ref) {
      dec_addr_ref_cnt(tablet_addr_);
    }
    if (inc_table_store_member_ref) {
      dec_table_store_ref_cnt();
    }
  }
  return ret;
}

void ObTablet::dec_macro_ref_cnt()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!hold_ref_cnt_)) {
    FLOG_INFO("tablet doesn't hold ref cnt, no need to dec ref cnt",
      K(is_inited_), K(tablet_meta_.tablet_id_),
      K(table_store_addr_.addr_), K(tablet_addr_), KP(this), K(lbt()));
  } else if (OB_FAIL(check_meta_addr())) {
  } else if (OB_UNLIKELY(!is_empty_shell() && macro_info_addr_.is_none_object())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("normal tablet's macro_info_addr_ shouldn't be none object", K(ret), KPC(this));
  } else if (macro_info_addr_.is_none_object() || is_empty_shell()) {
    dec_ref_without_aggregated_info();
  } else {
    dec_ref_with_aggregated_info();
  }
}

void ObTablet::dec_ref_without_aggregated_info()
{
  // 1. We don't need to recursively decrease macro ref cnt, since we will push both them to gc queue
  // 2. the order can't be changed, must be sstable blocks' ref cnt -> tablet meta blocks' ref cnt

  dec_table_store_ref_cnt();
  dec_addr_ref_cnt(table_store_addr_.addr_);
  dec_addr_ref_cnt(storage_schema_addr_.addr_);
  dec_addr_ref_cnt(tablet_addr_);
}

void ObTablet::dec_ref_with_aggregated_info()
{
  int ret = OB_SUCCESS;
    ObArenaAllocator allocator("DecMacroRef", OB_MALLOC_NORMAL_BLOCK_SIZE);
    ObTabletMacroInfo *macro_info = nullptr;
    bool in_memory = true;
    ObMacroInfoIterator info_iter;
    if (OB_FAIL(load_macro_info(0, allocator, macro_info, in_memory))) {
    } else if (OB_FAIL(info_iter.init(ObTabletMacroType::MAX, *macro_info))) {
    } else {
      dec_addr_ref_cnt(tablet_addr_);
      dec_ref_with_macro_iter(info_iter);
    }
    if (OB_NOT_NULL(macro_info) && !in_memory) {
      macro_info->reset();
    }

    FLOG_INFO("the tablet that decreases ref cnt is",
        K(is_inited_), K(tablet_meta_.tablet_id_), K(table_store_addr_.addr_),
        K(storage_schema_addr_.addr_), K(tablet_addr_), KP(this), K(info_iter), K(lbt()));
}

void ObTablet::dec_ref_with_macro_iter(ObMacroInfoIterator &macro_iter) const
{
  int ret = OB_SUCCESS;
  ObTabletBlockInfo block_info;
  ObSArray<MacroBlockId> print_arr;
  print_arr.set_attr(ObMemAttr("PrintId", ObCtxIds::DEFAULT_CTX_ID));
  while (OB_SUCC(ret)) {
    block_info.reset();
    if (OB_FAIL(macro_iter.get_next(block_info))) {
      if (OB_ITER_END != ret) {
        LOG_ERROR("fail to get next block info, macro block may leak", K(ret));
      }
    } else {
      if (OB_FAIL(OB_STORAGE_OBJECT_MGR.dec_ref(block_info.macro_id_))) {
      } else if (ObTabletMacroType::SHARED_DATA_BLOCK == block_info.block_type_
          && (OB_ISNULL(::oceanbase::share::server_service<::oceanbase::blocksstable::ObSharedMacroBlockMgr>())
              || OB_FAIL(::oceanbase::share::server_service<::oceanbase::blocksstable::ObSharedMacroBlockMgr>()->free_block(
                  block_info.macro_id_, block_info.occupy_size_)))) {
        if (OB_SUCC(ret)) {
          ret = OB_ERR_UNEXPECTED;
        }
        LOG_WARN("fail to release shared macro block accounting", K(ret), K(block_info));
      }
      int tmp_ret = OB_SUCCESS;
      if (OB_FAIL(ret)) {
        ret = OB_SUCCESS;
        // ignore ret, continue
      } else {
#ifndef OB_BUILD_PACKAGE
        if (OB_TMP_FAIL(print_arr.push_back(block_info.macro_id_))) {
        } else if (MAX_PRINT_COUNT == print_arr.size()) {
          print_arr.reuse();
        }
#endif
      }
    }
  }
#ifndef OB_BUILD_PACKAGE
  if (0 != print_arr.size()) {
    print_arr.reuse();
  }
#endif
}

int ObTablet::inc_table_store_ref_cnt(bool &inc_success)
{
  int ret = OB_SUCCESS;
  inc_success = false;
  ObTabletMemberWrapper<ObTabletTableStore> table_store_wrapper;

  if (table_store_addr_.addr_.is_none()) {
    // skip empty shell
  } else if (OB_FAIL(fetch_table_store(table_store_wrapper))) {
  } else if (OB_FAIL(table_store_wrapper.get_member()->inc_macro_ref())) {
  } else {
    inc_success = true;
  }
  return ret;
}

void ObTablet::dec_table_store_ref_cnt()
{
  int ret = OB_SUCCESS;
  ObTabletMemberWrapper<ObTabletTableStore> table_store_wrapper;
  if (table_store_addr_.addr_.is_none()) {
    // skip empty shell
  } else {
    do {
      ret = fetch_table_store(table_store_wrapper);
    } while (ignore_ret(ret));
    if (OB_FAIL(ret)) {
    } else {
      table_store_wrapper.get_member()->dec_macro_ref();
    }
  }
}

int ObTablet::inc_addr_ref_cnt(const ObMetaDiskAddr &addr, bool &inc_success)
{
  int ret = OB_SUCCESS;
  inc_success = false;
  MacroBlockId macro_id;
  int64_t offset;
  int64_t size;
  if (addr.is_block()) { // skip full/old/empty_shell tablet
    if (OB_FAIL(addr.get_block_addr(macro_id, offset, size))) {
    } else if (OB_FAIL(OB_STORAGE_OBJECT_MGR.inc_ref(macro_id))) {
    } else {
      inc_success = true;
    }
  }
  return ret;
}

void ObTablet::dec_addr_ref_cnt(const ObMetaDiskAddr &addr)
{
  int ret = OB_SUCCESS;
  MacroBlockId macro_id;
  int64_t offset;
  int64_t size;
  if (addr.is_block()) { // skip full/old/empty_shell tablet
    if (OB_FAIL(addr.get_block_addr(macro_id, offset, size))) {
    } else if (OB_FAIL(OB_STORAGE_OBJECT_MGR.dec_ref(macro_id))) {
    }
  }
}

int ObTablet::inc_linked_block_ref_cnt(const ObMetaDiskAddr &head_addr, bool &inc_success)
{
  int ret = OB_SUCCESS;
  inc_success = false;
  ObObjectLinkIter iter;
  MacroBlockId macro_id;
  int64_t block_cnt = 0;

  if (head_addr.is_block()) {
    if (OB_FAIL(iter.init(head_addr))) {
    } else {
      while (OB_SUCC(ret)) {
        if (OB_FAIL(iter.get_next_macro_id(macro_id))) {
          if (OB_ITER_END == ret) {
            ret = OB_SUCCESS;
            break;
          } else {
            LOG_WARN("fail to get next macro id", K(ret));
          }
        } else if (OB_FAIL(OB_STORAGE_OBJECT_MGR.inc_ref(macro_id))) {
        } else {
          block_cnt++;
        }
      }
    }
  }
  if (OB_FAIL(ret) && 0 != block_cnt) {
    int tmp_ret = OB_SUCCESS;
    if (OB_TMP_FAIL(iter.reuse())) {
    } else {
      for (int64_t i = 0; i < block_cnt; i++) {
        if (OB_TMP_FAIL(iter.get_next_macro_id(macro_id))) {
        } else if (OB_TMP_FAIL(OB_STORAGE_OBJECT_MGR.dec_ref(macro_id))) {
        }
      }
    }
  }
  if (OB_SUCC(ret)) {
    inc_success = true;
  }
  return ret;
}

void ObTablet::dec_linked_block_ref_cnt(const ObMetaDiskAddr &head_addr)
{
  int ret = OB_SUCCESS;
  ObObjectLinkIter iter;
  MacroBlockId macro_id;
  if (head_addr.is_block()) {
    if (OB_FAIL(iter.init(head_addr))) {
    } else {
      while (OB_SUCC(ret)) {
        if (OB_FAIL(iter.get_next_macro_id(macro_id))) {
          if (OB_ITER_END == ret) {
            ret = OB_SUCCESS;
            break;
          } else if (ignore_ret(ret)) {
            ret = OB_SUCCESS;
            // retry
          } else {
            LOG_ERROR("fail to get next macro id, macro block leaks", K(ret));
          }
        } else if (OB_FAIL(OB_STORAGE_OBJECT_MGR.dec_ref(macro_id))) {
        }
      }
    }
  }
}

bool ObTablet::ignore_ret(const int ret)
{
  return OB_ALLOCATE_MEMORY_FAILED == ret || OB_TIMEOUT == ret || OB_DISK_HUNG == ret;
}

void ObTablet::set_initial_addr()
{
  if (!table_store_addr_.addr_.is_none() && !storage_schema_addr_.addr_.is_none()) {
    table_store_addr_.addr_.set_mem_addr(0, sizeof(ObTabletTableStore));
    storage_schema_addr_.addr_.set_mem_addr(0, sizeof(ObStorageSchema));
  }
  macro_info_addr_.addr_.set_mem_addr(0, sizeof(ObTabletMacroInfo));
  tablet_addr_.set_mem_addr(0, sizeof(ObTablet));
}

int ObTablet::check_meta_addr() const
{
  int ret = OB_SUCCESS;
  const common::ObTabletID &tablet_id = tablet_meta_.tablet_id_;

  if (OB_UNLIKELY(!table_store_addr_.addr_.is_valid()
      || !storage_schema_addr_.addr_.is_valid()
      || !tablet_addr_.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("meta addrs are invalid", K(ret), K(tablet_id), K(tablet_addr_), K(table_store_addr_.addr_),
        K(storage_schema_addr_.addr_));
  }

  if (OB_FAIL(ret)) {
  } else if (((tablet_addr_.is_block() ^ table_store_addr_.addr_.is_block())
      || (tablet_addr_.is_block() ^ storage_schema_addr_.addr_.is_block()))
      && ((tablet_addr_.is_block() ^ table_store_addr_.addr_.is_none())
      || (tablet_addr_.is_block() ^ storage_schema_addr_.addr_.is_none()))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("meta addrs are inconsistent", K(ret), K(tablet_id),
        K(tablet_addr_), K(table_store_addr_.addr_), K(storage_schema_addr_.addr_));
  }

  return ret;
}

int ObTablet::get_snapshot_version(SCN &scn) const
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(scn.convert_for_tx(tablet_meta_.snapshot_version_))) {
  }
  return ret;
}

int64_t ObTablet::get_serialize_size(const ObSArray<ObInlineSecondaryMeta> &meta_arr) const
{
  ObTabletBlockHeader header;
  int64_t size = 0;
  int ret = OB_SUCCESS;
  if (OB_FAIL(header.init(meta_arr.count()))) {
    LOG_WARN("fail to init tablet block header", K(ret), K(meta_arr));
    size = -1;
  } else {
    size += header.get_serialize_size();
    size += get_self_serialize_size();
    for (int64_t i = 0; OB_SUCC(ret) && i < meta_arr.count(); i++) {
      if (OB_ISNULL(meta_arr[i].obj_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("obj is nullptr", K(ret), K(meta_arr[i].meta_type_));
        size = -1;
      } else if (OB_UNLIKELY(ObSecondaryMetaType::TABLET_MACRO_INFO != meta_arr[i].meta_type_)) {
        ret = OB_NOT_SUPPORTED;
        LOG_WARN("only support tablet macro info", K(ret), K(meta_arr[i].meta_type_));
        size = -1;
      } else {
        size += reinterpret_cast<const ObTabletMacroInfo *>(meta_arr[i].obj_)->get_serialize_size();
      }
    }
  }
  return size;
}

int64_t ObTablet::get_self_serialize_size() const
{
  int64_t size = 0;
  size += serialization::encoded_length_i32(version_);
  size += serialization::encoded_length_i32(length_);
  size += tablet_meta_.get_serialize_size();
  size += storage_schema_addr_.addr_.get_serialize_size();
  size += table_store_addr_.addr_.get_serialize_size();
  size += is_empty_shell() ? 0 : rowkey_read_info_->get_serialize_size();
  size += macro_info_addr_.addr_.get_serialize_size();
  return size;
}

void ObTablet::set_next_tablet_guard(const ObTabletHandle &next_tablet_guard)
{
  if (OB_UNLIKELY(next_tablet_guard.is_valid())) {
    int ret = OB_NOT_SUPPORTED;
    LOG_ERROR("shouldn't have next tablet", K(ret), KPC(this));
  }
}

void ObTablet::set_tablet_addr(const ObMetaDiskAddr &tablet_addr)
{
  tablet_addr_ = tablet_addr;
  if (tablet_meta_.has_next_tablet_ && next_tablet_guard_.is_valid()) {
    next_tablet_guard_.get_obj()->set_tablet_addr(tablet_addr);
  }
}

int ObTablet::set_macro_info_addr(
    const blocksstable::MacroBlockId &macro_id,
    const int64_t offset,
    const int64_t size,
    const ObMetaDiskAddr::DiskType block_type)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("tablet hasn't been inited", K(ret));
  } else if (OB_UNLIKELY(!macro_id.is_valid() || 0 > offset || 0 >= size)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(macro_id), K(offset), K(size));
  } else if (OB_FAIL(macro_info_addr_.addr_.set_block_addr(macro_id, offset, size, block_type))) {
  }
  return ret;
}

void ObTablet::trim_tablet_list()
{
  tablet_meta_.has_next_tablet_ = false;
  next_tablet_guard_.reset();
}

int ObTablet::get_max_sync_medium_scn(int64_t &max_medium_snapshot) const
{
  int ret = OB_SUCCESS;
  ObProtectedMemtableMgrHandle *protected_handle = NULL;
  max_medium_snapshot = 0;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret));
  } else if (tablet_meta_.tablet_id_.is_special_merge_tablet()) {
    // do nothing
  } else if (OB_FAIL(get_protected_memtable_mgr_handle(protected_handle))) {
  } else {
    max_medium_snapshot = protected_handle->get_max_saved_version_from_medium_info_recorder();
  }
  return ret;
}

int ObTablet::get_max_sync_storage_schema_version(int64_t &max_schema_version) const
{
  int ret = OB_SUCCESS;
  max_schema_version = 0;
  ObProtectedMemtableMgrHandle *protected_handle = NULL;
  if (is_ls_inner_tablet()) {
    // do nothing
  } else if (OB_FAIL(get_protected_memtable_mgr_handle(protected_handle))) {
  } else {
    max_schema_version = protected_handle->get_max_saved_version_from_storage_schema_recorder();
  }
  return ret;
}

int ObTablet::get_max_column_cnt_on_schema_recorder(int64_t &max_column_cnt)
{
  int ret = OB_SUCCESS;
  max_column_cnt = 0;
  ObProtectedMemtableMgrHandle *protected_handle = NULL;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret));
  } else if (tablet_meta_.tablet_id_.is_special_merge_tablet()) {
    // do nothing
  } else if (OB_FAIL(get_protected_memtable_mgr_handle(protected_handle))) {
  } else {
    max_column_cnt = protected_handle->get_max_column_cnt_from_storage_schema_recorder();
  }
  return ret;
}

int ObTablet::get_ls_epoch(int64_t &ls_epoch)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret));
  } else if (OB_UNLIKELY(!pointer_hdl_.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid tablet pointer handle", K(ret), K(pointer_hdl_));
    if (is_external_tablet()) {
      LOG_ERROR("is_external_tablet, and pointer_hdl_ is invalid", K(lbt()));
    }
  } else {
    ls_epoch = static_cast<ObTabletPointer *>(pointer_hdl_.get_resource_ptr())->get_ls()->get_ls_epoch();
  }
  return ret;
}

// be careful to use this max_schem_version on storage_schema
int ObTablet::get_max_schema_version(int64_t &schema_version)
{
  int ret = OB_SUCCESS;
  schema_version = -1;
  common::ObSEArray<ObTableHandleV2, 8> table_handle_array;
  if (OB_FAIL(get_all_memtables_from_memtable_mgr(table_handle_array))) {
  } else {
    const ObITabletMemtable *memtable = nullptr;
    for (int64_t i = 0; OB_SUCC(ret) && i < table_handle_array.count(); ++i) {
      const ObTableHandleV2 &handle = table_handle_array[i];
      if (OB_UNLIKELY(!handle.is_valid())) {
        ret = OB_ERR_SYS;
        LOG_WARN("invalid memtable", K(ret), K(handle));
      } else if (OB_FAIL(handle.get_tablet_memtable(memtable))) {
      } else if (OB_ISNULL(memtable)) {
        ret = OB_ERR_SYS;
        LOG_WARN("memtable is null", K(ret), KP(memtable));
      } else {
        schema_version = common::max(schema_version, memtable->get_max_schema_version());
      }
    }
  }
  return ret;
}

int ObTablet::check_schema_version_for_bounded_staleness_read(
    const int64_t table_version_for_read,
    const int64_t data_max_schema_version,
    const uint64_t table_id)
{
  int ret = OB_SUCCESS;
  int64_t cur_table_version = OB_INVALID_VERSION;
  int64_t runtime_schema_version = OB_INVALID_VERSION;

  if (table_version_for_read >= data_max_schema_version) {
    // read schema version is biger than max schema version of data, pass
  } else {
    // read schema version is smaller than max schema version of data, two possible cases:
    // 1. max schema version of data is max schema version of table, return schema error, asking for schema refresh
    //
    //    standalone pg is in this case
    //
    // 2. max schema version of data is max schema version of multiple table partitions
    //
    //    It is the case when pg contains multiple partitions, it can only return max schema version of all partitions
    //
    // To differentiate the above two cases, check with the help of local schema version

    
    ObMultiVersionSchemaService *schema_service = ::oceanbase::share::server_service<::oceanbase::share::schema::ObSchemaRuntimeService>()->get_schema_service();
    ObSchemaGetterGuard schema_guard;
    // get schema version of this table in schema service
    if (OB_ISNULL(schema_service)) {
      ret = OB_NOT_INIT;
      LOG_WARN("invalid schema service", K(ret), K(schema_service));
    } else if (OB_FAIL(schema_service->get_runtime_schema_guard(schema_guard))) {
    } else if (OB_FAIL(schema_guard.get_schema_version(TABLE_SCHEMA, table_id, cur_table_version))) {
    }

    // check whether input table version and schema version of this table in schema service same
    // if not same, refresh schema
    else if (OB_UNLIKELY(table_version_for_read != cur_table_version)) {
      ret = OB_SCHEMA_ERROR;
      LOG_WARN("schema version for read mismatch", K(ret), K(table_id),
          K(table_version_for_read), K(cur_table_version), K(data_max_schema_version));
    }
    // Get the latest schema version for the database runtime.
    else if (OB_FAIL(schema_service->get_runtime_refreshed_schema_version(
        runtime_schema_version))) {
    } else if (runtime_schema_version >= data_max_schema_version) {
      // If the runtime schema is newer than the tablet schema,
      // then schema of read operation is newer than data's
    } else {
      ret = OB_SCHEMA_NOT_UPTODATE;
      LOG_WARN("schema is not up to date for read, need refresh", K(ret),
          K(table_version_for_read), K(cur_table_version), K(runtime_schema_version),
          K(data_max_schema_version), K(table_id));
    }
  }

  return ret;
}

int ObTablet::lock_row(
    ObRelativeTable &relative_table,
    ObStoreCtx &store_ctx,
    ObColDescArray &col_desc,
    blocksstable::ObDatumRow &row)
{
  int ret = OB_SUCCESS;
  ObStorageTableGuard guard(this, store_ctx, true);

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret));
  } else if (!relative_table.is_valid()
             || !store_ctx.is_valid()
             || !row.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument",
             K(ret), K(relative_table), K(store_ctx), K(row));
  } else if (OB_UNLIKELY(relative_table.get_tablet_id() != tablet_meta_.tablet_id_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet id doesn't match", K(ret), K(relative_table.get_tablet_id()), K(tablet_meta_.tablet_id_));
  } else if (OB_FAIL(guard.refresh_and_protect_memtable_for_write(relative_table))) {
  }
  if (OB_SUCC(ret)) {
    ObArenaAllocator allocator((common::ObMemAttr(ObModIds::OB_STORE_ROW_LOCK_CHECKER)));
    ObMemtable *write_memtable = nullptr;
    ObTableIterParam param;
    ObTableAccessContext context;

    if (OB_FAIL(prepare_memtable(relative_table, store_ctx, write_memtable))) {
    } else if (OB_FAIL(prepare_param_ctx(allocator, relative_table, store_ctx, param, context))) {
    } else if (OB_FAIL(write_memtable->lock(param, context, col_desc, row))) {
    }
  }
  return ret;
}

int ObTablet::lock_row(
    ObRelativeTable &relative_table,
    storage::ObStoreCtx &store_ctx,
    const ObDatumRowkey &rowkey)
{
  int ret = OB_SUCCESS;
  ObStorageTableGuard guard(this, store_ctx, true);

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else if (OB_UNLIKELY(!relative_table.is_valid()
             || !store_ctx.is_valid()
             || !rowkey.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument",
                K(ret), K(relative_table), K(store_ctx), K(rowkey));
  } else if (OB_UNLIKELY(relative_table.get_tablet_id() != tablet_meta_.tablet_id_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet id doesn't match", K(ret), K(relative_table.get_tablet_id()), K(tablet_meta_.tablet_id_));
  } else if (OB_FAIL(guard.refresh_and_protect_memtable_for_write(relative_table))) {
  } else {
    ObArenaAllocator allocator((common::ObMemAttr(ObModIds::OB_STORE_ROW_LOCK_CHECKER)));
    ObMemtable *write_memtable = nullptr;
    ObTableIterParam param;
    ObTableAccessContext context;
    const uint64_t table_id = relative_table.get_table_id();

    if (OB_FAIL(prepare_memtable(relative_table, store_ctx, write_memtable))) {
    } else if (OB_FAIL(prepare_param_ctx(allocator, relative_table, store_ctx, param, context))) {
    } else if (OB_FAIL(write_memtable->lock(param, context, rowkey))) {
    }
  }
  return ret;
}

int ObTablet::check_row_locked_by_myself(
    ObRelativeTable &relative_table,
    ObStoreCtx &store_ctx,
    const blocksstable::ObDatumRowkey &rowkey,
    bool &locked)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  ObArenaAllocator allocator((common::ObMemAttr(ObModIds::OB_STORE_ROW_LOCK_CHECKER)));
  ObMemtable *write_memtable = nullptr;
  ObTableIterParam param;
  ObTableAccessContext context;
  locked = false;

  if (OB_FAIL(prepare_memtable(relative_table, store_ctx, write_memtable))) {
  } else if (OB_FAIL(prepare_param_ctx(allocator, relative_table, store_ctx, param, context))) {
  } else if (OB_TMP_FAIL(ObRowConflictHandler::check_row_locked(param, context, rowkey, true /* by_myself */))) {
    if (OB_TRY_LOCK_ROW_CONFLICT == tmp_ret) {
      locked = true;
    } else if (OB_TRANSACTION_SET_VIOLATION != tmp_ret) {
      ret = tmp_ret;
      LOG_WARN("failed to check row locked by myself", K(tmp_ret), K(rowkey));
    }
  }
  return ret;
}

int ObTablet::get_read_tables(
    const int64_t snapshot_version,
    ObTabletTableIterator &iter,
    const bool allow_no_ready_read)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else if (OB_FAIL(allow_to_read_())) {
  } else if (OB_UNLIKELY(!iter.is_valid() || iter.get_tablet() != this)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(iter), K(this));
  } else if (OB_FAIL(auto_get_read_tables(snapshot_version, iter, allow_no_ready_read))) {
  }

  return ret;
}

int ObTablet::auto_get_read_tables(
    const int64_t snapshot_version,
    ObTabletTableIterator &iter,
    const bool allow_no_ready_read)
{
  int ret = OB_SUCCESS;
  const common::ObTabletID &tablet_id = tablet_meta_.tablet_id_;
  iter.table_store_iter_.reset();
  bool fork_get_src_tables = false;

  if (!tablet_meta_.fork_info_.is_complete() && tablet_meta_.fork_info_.get_fork_src_tablet_id().is_valid()) {
    fork_get_src_tables = true;
    if (OB_FAIL(get_fork_src_read_tables_(iter, allow_no_ready_read))) {
    }
  }
  if (OB_FAIL(ret)) {
  } else {
    ObGetReadTablesMode mode = allow_no_ready_read
        ? ObGetReadTablesMode::ALLOW_NO_READY_READ
        : ObGetReadTablesMode::NORMAL;
    mode = fork_get_src_tables ? ObGetReadTablesMode::SKIP_MAJOR : mode;
    if (OB_FAIL(get_read_tables_(snapshot_version, iter.table_store_iter_, iter.table_store_iter_.table_store_handle_, mode))) {
    }
  }
  if (OB_SUCC(ret)) {
    // Pass fork infos pointer from ObTabletTableIterator to ObTableStoreIterator
    iter.table_store_iter_.set_fork_infos(iter.get_fork_infos());
  }

#ifdef ENABLE_DEBUG_LOG
  if (OB_SUCC(ret) && fork_get_src_tables) {
    LOG_DEBUG("get read tables during tablet forking", K(tablet_id), K(snapshot_version),
        "table_cnt", iter.table_store_iter_.table_ptr_array_.count(), K(fork_get_src_tables));
  }
#endif
  return ret;
}

int ObTablet::get_read_tables_(
    const int64_t snapshot_version,
    ObTableStoreIterator &iter,
    ObStorageMetaHandle &table_store_handle,
    const ObGetReadTablesMode mode)
{
  int ret = OB_SUCCESS;
  const ObTabletTableStore *table_store = nullptr;
  if (OB_UNLIKELY(!table_store_addr_.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid table store addr", K(ret), K_(table_store_addr));
  } else if (table_store_addr_.is_memory_object()) {
    table_store = table_store_addr_.get_ptr();
  } else {
    ObStorageMetaKey meta_key(table_store_addr_.addr_);
    const ObStorageMetaValue *value = nullptr;
    if (OB_FAIL(OB_STORE_CACHE.get_storage_meta_cache().get_meta(
                ObStorageMetaValue::MetaType::TABLE_STORE, meta_key, table_store_handle, this))) {
    } else if (OB_FAIL(table_store_handle.get_value(value))) {
    } else if (OB_FAIL(value->get_table_store(table_store))) {
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(const_cast<ObTabletTableStore*>(table_store)->get_read_tables(
        snapshot_version, *this, iter, mode))) {
    }
  }
  return ret;
}

int ObTablet::get_read_major_sstable(
    const int64_t &major_snapshot_version,
    ObTabletTableIterator &iter)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else if (OB_FAIL(allow_to_read_())) {
  } else if (OB_UNLIKELY(!iter.is_valid() || iter.get_tablet() != this)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(iter), K(this));
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(get_read_major_sstable(major_snapshot_version, *iter.table_iter()))) {
    }
  }
  return ret;
}

int ObTablet::get_fork_src_read_tables_(
    ObTabletTableIterator &iter,
    const bool allow_no_ready_read)
{
  int ret = OB_SUCCESS;
  const ObTabletID &src_tablet_id = tablet_meta_.fork_info_.get_fork_src_tablet_id();
  ObLSService *ls_service = nullptr;
  ObLS *tenant_ls = nullptr;
  ObTabletHandle src_tablet_handle;
  ObTablet *src_tablet = nullptr;
  if (OB_ISNULL(ls_service = ::oceanbase::share::server_service<::oceanbase::storage::ObLSService>())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("failed to get ObLSService from server module provider", K(ret), KP(ls_service));
  } else if (OB_FAIL(ls_service->get_ls(tenant_ls))) {
  } else if (OB_ISNULL(tenant_ls)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("local ls is null", K(ret));
  } else if (OB_FAIL(tenant_ls->get_tablet(src_tablet_id, src_tablet_handle, ObTabletCommon::DEFAULT_GET_TABLET_DURATION_US,
          ObMDSGetTabletMode::READ_WITHOUT_CHECK))) {
  } else if (OB_ISNULL(src_tablet = src_tablet_handle.get_obj())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet handle obj is nullptr", K(ret));
  } else if (OB_UNLIKELY(src_tablet->is_empty_shell())) {
    ret = OB_TABLET_NOT_EXIST;
    LOG_WARN("src tablet becomes empty shell", K(ret), K(src_tablet_id));
  } else if (OB_FAIL(iter.add_fork_tablet_handle(src_tablet_handle, tablet_meta_.fork_info_))) {
  } else if (OB_FAIL(src_tablet->auto_get_read_tables(
      tablet_meta_.fork_info_.get_fork_snapshot_version(),
      iter,
      allow_no_ready_read))) {
  } else {
  }

  return ret;
}

int ObTablet::get_read_major_sstable(
    const int64_t &major_snapshot_version,
    ObTableStoreIterator &iter) const
{
  int ret = OB_SUCCESS;
  ObTabletMemberWrapper<ObTabletTableStore> table_store_wrapper;
  if (OB_FAIL(fetch_table_store(table_store_wrapper))) {
  } else if (OB_FAIL(table_store_wrapper.get_member()->get_read_major_sstable(
      major_snapshot_version, iter))) {
  } else if (!table_store_addr_.is_memory_object()
      && OB_FAIL(iter.set_handle(table_store_wrapper.get_meta_handle()))) {
    LOG_WARN("fail to set storage meta handle", K(ret), K_(table_store_addr), K(table_store_wrapper));
  }
  return ret;
}

int ObTablet::get_fork_info(share::ObForkTabletInfo &fork_info) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else if (!tablet_meta_.fork_info_.is_valid()) {
    ret = OB_ENTRY_NOT_EXIST;
    LOG_WARN("fork info is not valid", K(ret), K(tablet_meta_.fork_info_));
  } else {
    fork_info = tablet_meta_.fork_info_;
  }
  return ret;
}

int ObTablet::get_ddl_kvs(common::ObIArray<ObDDLKV *> &ddl_kvs) const
{
  int ret = OB_SUCCESS;
  ddl_kvs.reset();
  for (int64_t i = 0; OB_SUCC(ret) && i < ddl_kv_count_; ++i) {
    if (OB_ISNULL(ddl_kvs_[i])) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected null ddl mem table", K(ret), K(i), KPC(this));
    } else if (OB_FAIL(ddl_kvs.push_back(ddl_kvs_[i]))) {
    }
  }
  return ret;
}

int ObTablet::get_all_sstables(ObTableStoreIterator &iter) const
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else if (OB_FAIL(inner_get_all_sstables(iter))) {
  }
  return ret;
}

int ObTablet::get_all_tables(ObTableStoreIterator &iter) const
{
  int ret = OB_SUCCESS;
  ObSEArray<storage::ObITable *, MAX_MEMSTORE_CNT> memtables;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else if (OB_FAIL(inner_get_all_sstables(iter))) {
  } else if (OB_FAIL(get_memtables(memtables))) {
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < memtables.count(); ++i) {
      if (OB_FAIL(iter.add_table(memtables.at(i)))) {
      }
    }
  }
  return ret;
}

int ObTablet::inner_get_all_sstables(ObTableStoreIterator &iter) const
{
  int ret = OB_SUCCESS;
  ObTabletMemberWrapper<ObTabletTableStore> table_store_wrapper;
  if (OB_FAIL(fetch_table_store(table_store_wrapper))) {
  } else if (OB_FAIL(table_store_wrapper.get_member()->get_all_sstable(iter))) {
  } else if (!table_store_addr_.is_memory_object()
      && OB_FAIL(iter.set_handle(table_store_wrapper.get_meta_handle()))) {
    LOG_WARN("fail to set storage meta handle", K(ret), K_(table_store_addr), K(table_store_wrapper));
  }
  return ret;
}

int ObTablet::get_memtables(common::ObIArray<storage::ObITable *> &memtables) const
{
  common::SpinRLockGuard guard(memtables_lock_);
  return inner_get_memtables(memtables);
}

int ObTablet::update_row(
    ObRelativeTable &relative_table,
    storage::ObStoreCtx &store_ctx,
    const ObColDescIArray &col_descs,
    const ObIArray<int64_t> &update_idx,
    const blocksstable::ObDatumRow &old_row,
    blocksstable::ObDatumRow &new_row)
{
  int ret = OB_SUCCESS;

  {
    ObStorageTableGuard guard(this, store_ctx, true);
    ObMemtable *write_memtable = nullptr;

    if (OB_UNLIKELY(!is_inited_)) {
      ret = OB_NOT_INIT;
      LOG_WARN("not inited", K(ret), K_(is_inited));
    } else if (OB_UNLIKELY(!store_ctx.is_valid()
        || col_descs.count() <= 0
        || !old_row.is_valid()
        || !new_row.is_valid()
        || !relative_table.is_valid())) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid args", K(ret), K(store_ctx),
          K(relative_table), K(col_descs), K(update_idx),
          K(old_row), K(new_row));
    } else if (OB_UNLIKELY(relative_table.get_tablet_id() != tablet_meta_.tablet_id_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("tablet id doesn't match", K(ret), K(relative_table.get_tablet_id()), K(tablet_meta_.tablet_id_));
    } else if (OB_FAIL(guard.refresh_and_protect_memtable_for_write(relative_table))) {
    } else if (OB_FAIL(prepare_memtable(relative_table, store_ctx, write_memtable))) {
    } else {
      ObArenaAllocator allocator(common::ObMemAttr("update_row"));
      ObTableIterParam param;
      ObTableAccessContext context;
      const ObMemtableSetArg arg(&new_row,
                                 &col_descs,
                                 store_ctx.update_full_column_ ? nullptr : &update_idx,
                                 &old_row,
                                 1,     /*row_count*/
                                 false  /*check_exist*/);

      if (OB_FAIL(prepare_param_ctx(allocator, relative_table, store_ctx, param, context))) {
      } else if (OB_FAIL(write_memtable->set(param,
                                             context,
                                             arg))) {
      }
    }
  }
  return ret;
}

int ObTablet::update_rows(
    ObRelativeTable &relative_table,
    ObStoreCtx &store_ctx,
    const ObColDescIArray &col_descs,
    const ObIArray<int64_t> &update_idx,
    const blocksstable::ObDatumRow *old_rows,
    ObRowsInfo &rows_info)
{
  int ret = OB_SUCCESS;
  {
    ObStorageTableGuard guard(this, store_ctx, true);
    ObMemtable *write_memtable = nullptr;
    if (OB_UNLIKELY(!is_inited_)) {
      ret = OB_NOT_INIT;
      LOG_WARN("Not inited", K(ret), K_(is_inited));
    } else if (OB_UNLIKELY(!store_ctx.is_valid()
               || col_descs.count() <= 0
               || !relative_table.is_valid())) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("Invalid argument", K(ret), K(store_ctx), K(relative_table), K(col_descs));
    } else if (OB_UNLIKELY(relative_table.get_tablet_id() != tablet_meta_.tablet_id_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("Tablet id doesn't match", K(ret), K(relative_table.get_tablet_id()), K(tablet_meta_.tablet_id_));
    } else if (OB_FAIL(guard.refresh_and_protect_memtable_for_write(relative_table))) {
    } else if (OB_FAIL(prepare_memtable(relative_table, store_ctx, write_memtable))) {
    } else {
      const int64_t row_count = rows_info.get_rowkey_cnt();
      ObTableIterParam param;
      ObTableAccessContext &context = rows_info.get_access_context();
      ObMemtableSetArg arg(rows_info.rows_,
                           &col_descs,
                           store_ctx.update_full_column_ ? nullptr : &update_idx,
                           old_rows,
                           row_count,
                           false/*check_exist*/);
      if (OB_FAIL(prepare_param(relative_table, param))) {
      } else if (1 == row_count) {
        if (OB_FAIL(write_memtable->set(param, context, arg))) {
        }
      } else if (OB_FAIL(write_memtable->multi_set(param, context, arg, rows_info))) {
      }
    }
  }
  return ret;
}

int ObTablet::insert_rows(
    ObRelativeTable &relative_table,
    ObStoreCtx &store_ctx,
    const bool check_exist,
    const ObColDescIArray &col_descs,
    ObRowsInfo &rows_info)
{
  int ret = OB_SUCCESS;
  {
    ObStorageTableGuard guard(this, store_ctx, true);
    ObMemtable *write_memtable = nullptr;
    const int64_t row_count = rows_info.get_rowkey_cnt();
    if (OB_UNLIKELY(!is_inited_)) {
      ret = OB_NOT_INIT;
      LOG_WARN("Not inited", K(ret), K_(is_inited));
    } else if (OB_UNLIKELY(!store_ctx.is_valid()
               || col_descs.count() <= 0
               || !relative_table.is_valid())) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("Invalid argument", K(ret), K(store_ctx), K(relative_table), K(col_descs));
    } else if (OB_UNLIKELY(relative_table.get_tablet_id() != tablet_meta_.tablet_id_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("Tablet id doesn't match", K(ret), K(relative_table.get_tablet_id()), K(tablet_meta_.tablet_id_));
    } else if (OB_FAIL(guard.refresh_and_protect_memtable_for_write(relative_table))) {
    } else if (OB_FAIL(prepare_memtable(relative_table, store_ctx, write_memtable))) {
    } else {
      ObArenaAllocator allocator(common::ObMemAttr("insert_rows"));
      ObTableIterParam param;
      ObTableAccessContext &context = rows_info.get_access_context();
      const ObMemtableSetArg arg(rows_info.rows_,
                                 &col_descs,
                                 nullptr, /*update_idx*/
                                 nullptr, /*old_row*/
                                 row_count,
                                 check_exist);
      if (OB_FAIL(prepare_param(relative_table, param))) {
      } else if (1 == row_count) {
        if (OB_FAIL(write_memtable->set(param, context, arg))) {
          LOG_WARN("fail to set memtable", K(ret), K(row_count),
              "need_find_all_duplicate_key", rows_info.need_find_all_duplicate_key());
          rows_info.set_row_conflict_error(0, ret);
        }
      } else if (OB_FAIL(write_memtable->multi_set(param, context, arg, rows_info))) {
      }
    }
  }
  return ret;
}

int ObTablet::insert_row(
    ObRelativeTable &relative_table,
    ObStoreCtx &store_ctx,
    const bool check_exist,
    const common::ObIArray<share::schema::ObColDesc> &col_descs,
    blocksstable::ObDatumRow &row)
{
  int ret = OB_SUCCESS;
  {
    ObStorageTableGuard guard(this, store_ctx, true);
    ObMemtable *write_memtable = nullptr;

    if (OB_UNLIKELY(!is_inited_)) {
      ret = OB_NOT_INIT;
      LOG_WARN("not inited", K(ret), K_(is_inited));
    } else if (OB_UNLIKELY(!store_ctx.is_valid()
        || col_descs.count() <= 0
        || !row.is_valid()
        || !relative_table.is_valid())) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid args", K(ret), K(store_ctx), K(relative_table),
          K(col_descs), K(row));
    } else if (OB_UNLIKELY(relative_table.get_tablet_id() != tablet_meta_.tablet_id_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("tablet id doesn't match", K(ret), K(relative_table.get_tablet_id()), K(tablet_meta_.tablet_id_));
    } else if (OB_FAIL(guard.refresh_and_protect_memtable_for_write(relative_table))) {
    } else if (OB_FAIL(prepare_memtable(relative_table, store_ctx, write_memtable))) {
    } else {
      ObArenaAllocator allocator(common::ObMemAttr("insert_row"));
      ObTableIterParam param;
      ObTableAccessContext context;
      const ObMemtableSetArg arg(&row,
                                 &col_descs,
                                 nullptr, /*update_idx*/
                                 nullptr, /*old_row*/
                                 1,       /*row_count*/
                                 check_exist);

      if (OB_FAIL(prepare_param_ctx(allocator, relative_table, store_ctx, param, context))) {
      } else if (OB_FAIL(write_memtable->set(param,
                                             context,
                                             arg))) {
      }
    }
  }
  return ret;
}

int ObTablet::prepare_memtable(
    ObRelativeTable &relative_table,
    ObStoreCtx &store_ctx,
    memtable::ObMemtable *&write_memtable)
{
  int ret = OB_SUCCESS;
  write_memtable = nullptr;
  store_ctx.table_iter_ = relative_table.tablet_iter_.table_iter();
  ObITable* last_table = nullptr;
  if (OB_FAIL(relative_table.tablet_iter_.table_iter()->get_boundary_table(true, last_table))) {
  } else if (OB_ISNULL(last_table)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("last table is null", K(relative_table));
  } else if (OB_UNLIKELY(!last_table->is_data_memtable())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("last table is not memtable", K(ret), K(*last_table));
  } else {
    write_memtable = reinterpret_cast<ObMemtable*>(last_table);
  }
  return ret;
}

int ObTablet::get_meta_disk_addr(ObMetaDiskAddr &addr) const
{
  int ret = OB_SUCCESS;
  const ObTabletID &tablet_id = tablet_meta_.tablet_id_;
  ObTabletPointer *tablet_ptr = nullptr;

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else if (!pointer_hdl_.is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet_pointer_hdl is in_valid", K(ret), K(pointer_hdl_));
    if (is_external_tablet()) {
      LOG_ERROR("is_external_tablet, and pointer_hdl_ is invalid", K(lbt()));
    }
  } else if (OB_ISNULL(tablet_ptr = static_cast<ObTabletPointer*>(pointer_hdl_.get_resource_ptr()))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet pointer is null", K(ret), K(tablet_id));
  } else {
    addr = tablet_ptr->get_addr();
  }

  return ret;
}

int ObTablet::assign_pointer_handle(const ObTabletPointerHandle &ptr_hdl)
{
  int ret = OB_SUCCESS;
  if (is_external_tablet()) {
    // external tablet will hold tablet_pointer_hdl, this func will be called only once
    LOG_INFO("is_external_tablet, should not hold tablet_pointer", K(ret), K(ptr_hdl), KP(this), K(lbt()));
  }
  if (OB_FAIL(pointer_hdl_.assign(ptr_hdl))) {
  }
  return ret;
}

int ObTablet::replay_update_storage_schema(
    const SCN &scn,
    const char *buf,
    const int64_t buf_size,
    int64_t &pos)
{
  int ret = OB_SUCCESS;
  int64_t new_pos = pos;
  ObProtectedMemtableMgrHandle *protected_handle = NULL;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret));
  } else if (tablet_meta_.tablet_id_.is_special_merge_tablet()) {
    // do nothing
  } else if (OB_FAIL(get_protected_memtable_mgr_handle(protected_handle))) {
  } else if (OB_FAIL(protected_handle->replay_schema_log(get_tablet_meta(),
          scn, buf, buf_size, new_pos))) {
  } else {
    pos = new_pos;
  }
  if (OB_TIMEOUT == ret) {
    ret = OB_EAGAIN; // need retry.
  }
  return ret;
}

int ObTablet::submit_medium_compaction_clog(
    ObMediumCompactionInfo &medium_info,
    ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  ObProtectedMemtableMgrHandle *protected_handle = NULL;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret));
  } else if (tablet_meta_.tablet_id_.is_special_merge_tablet()) {
    // do nothing
  } else if (OB_FAIL(get_protected_memtable_mgr_handle(protected_handle))) {
  } else if (OB_FAIL(protected_handle->submit_medium_compaction_info(get_tablet_meta(),
          medium_info, allocator))) {
  } else {
  }
  return ret;
}

int ObTablet::replay_medium_compaction_clog(
    const share::SCN &scn,
    const char *buf,
    const int64_t buf_size,
    int64_t &pos)
{
  int ret = OB_SUCCESS;
  int64_t new_pos = pos;
  ObProtectedMemtableMgrHandle *protected_handle = NULL;

  if (IS_NOT_INIT) {
    LOG_WARN("not inited", K(ret));
  } else if (OB_UNLIKELY(buf_size <= pos || pos < 0 || buf_size <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(buf_size), K(pos));
  } else if (tablet_meta_.tablet_id_.is_ls_inner_tablet()) {
    // do nothing
  } else if (OB_FAIL(get_protected_memtable_mgr_handle(protected_handle))) {
  } else if (OB_FAIL(protected_handle->replay_medium_compaction_log(get_tablet_meta(),
          scn, buf, buf_size, new_pos))) {
  } else {
    pos = new_pos;
  }
  return ret;
}

int ObTablet::get_schema_version_from_storage_schema(int64_t &schema_version) const
{
  int ret = OB_SUCCESS;
  const common::ObTabletID &tablet_id = tablet_meta_.tablet_id_;
  ObStorageSchema *storage_schema = nullptr;
  ObArenaAllocator arena_allocator(common::ObMemAttr("TmpSchema"));
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited), K(tablet_id));
  } else if (OB_FAIL(load_storage_schema(arena_allocator, storage_schema))) {
  } else {
    schema_version = storage_schema->schema_version_;
  }
  ObTabletObjLoadHelper::free(arena_allocator, storage_schema);
  return ret;
}

int ObTablet::get_newest_schema_version(int64_t &schema_version) const
{
  int ret = OB_SUCCESS;
  schema_version = 0;

  ObArenaAllocator tmp_allocator;
  ObSEArray<storage::ObITable *, MAX_MEMSTORE_CNT> memtables;
  ObStorageSchema *schema_on_tablet = nullptr;
  int64_t store_column_cnt_in_schema = 0;
  if (OB_FAIL(get_memtables(memtables))) {
  } else if (OB_FAIL(load_storage_schema(tmp_allocator, schema_on_tablet))) {
  } else if (OB_FAIL(schema_on_tablet->get_store_column_count(store_column_cnt_in_schema, true/*full_col*/))) {
  } else {
    schema_version = schema_on_tablet->get_schema_version();
    int64_t max_schema_version_on_memtable = 0;
    int64_t unused_max_column_cnt_on_memtable = 0;
    for (int64_t idx = 0; OB_SUCC(ret) && idx < memtables.count(); ++idx) {
      ObITable *table = memtables.at(idx);
      if (table->is_memtable()) {
        ObITabletMemtable *memtable = static_cast<ObITabletMemtable *>(table);
        if (OB_FAIL(memtable->get_schema_info(
                store_column_cnt_in_schema, max_schema_version_on_memtable, unused_max_column_cnt_on_memtable))) {
        }
      }
    }
    if (OB_SUCC(ret)) {
      schema_version = MAX(max_schema_version_on_memtable, schema_version);
    }
  }
  ObTabletObjLoadHelper::free(tmp_allocator, schema_on_tablet);
  return ret;
}

int ObTablet::get_active_memtable(ObTableHandleV2 &handle) const
{
  int ret = OB_SUCCESS;
  ObProtectedMemtableMgrHandle *protected_handle = NULL;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else if (OB_FAIL(get_protected_memtable_mgr_handle(protected_handle))) {
  } else if (OB_FAIL(protected_handle->get_active_memtable(handle))) {
    if (OB_ENTRY_NOT_EXIST != ret) {
      LOG_WARN("failed to get active memtable", K(ret), KPC(this));
    }
  }
  return ret;
}

int ObTablet::create_memtable(CreateMemtableArg &arg)
{
  int ret = OB_SUCCESS;
  const common::ObTabletID &tablet_id = tablet_meta_.tablet_id_;
  ObTimeGuard time_guard("ObTablet::create_memtable", 10 * 1000);
  common::SpinWLockGuard guard(memtables_lock_);
  time_guard.click("lock");
  // we use the parameter clog_checkpoint_scn to double check whether the
  // clog_checkpoint_scn has been changed during memtable replay check.
  // So we complement the input_clog_checkpoint_scn for other scenario.
  if (arg.clog_checkpoint_scn_.is_min()){
    arg.clog_checkpoint_scn_ = tablet_meta_.clog_checkpoint_scn_;
  }

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else if (OB_UNLIKELY(arg.schema_version_ < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid schema version", K(ret), K(arg));
  } else if (FALSE_IT(time_guard.click("prepare_memtables"))) {
  } else if (OB_FAIL(inner_create_memtable(arg))) {
    if (OB_ENTRY_EXIST == ret) {
      ret = OB_SUCCESS;
    } else if (OB_MINOR_FREEZE_NOT_ALLOW != ret) {
      LOG_WARN("failed to create memtable", K(ret), K(arg));
    }
  } else {
    time_guard.click("inner_create_memtable");
    do {
      if (OB_FAIL(update_memtables())) {
      } else if (FALSE_IT(time_guard.click("update_memtables"))) {
      } else {
        tablet_addr_.inc_seq();
        table_store_addr_.addr_.inc_seq();
        if (table_store_addr_.is_memory_object()) {
          ObSEArray<ObITable *, MAX_MEMSTORE_CNT> memtable_array;
          if (OB_FAIL(inner_get_memtables(memtable_array))) {
          } else if (OB_FAIL(table_store_addr_.get_ptr()->update_memtables(memtable_array))) {
          } else {
           time_guard.click("ts update mem");
           LOG_INFO("table store update memtable success", K(ret), K(tablet_id), K_(table_store_addr), KP(this));
          }
        }
      }
      if (OB_FAIL(ret) && REACH_COUNT_INTERVAL(100)) {
        LOG_ERROR("fail to refresh tablet memtables, which may cause hang", K(ret), KPC(this));
      }
    } while(OB_FAIL(ret));
  }

  STORAGE_LOG(DEBUG, "Tablet finish create memtable", K(arg), K(lbt()));
  return ret;
}

int ObTablet::inner_create_memtable(CreateMemtableArg &arg)
{
  int ret = OB_SUCCESS;
  const common::ObTabletID &tablet_id = tablet_meta_.tablet_id_;
  arg.new_clog_checkpoint_scn_ = tablet_meta_.clog_checkpoint_scn_;
  ObProtectedMemtableMgrHandle *protected_handle = NULL;

  if (OB_UNLIKELY(!arg.clog_checkpoint_scn_.is_valid_and_not_min()) || OB_UNLIKELY(arg.schema_version_ < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(arg));
  } else if (OB_UNLIKELY(MAX_MEMSTORE_CNT == memtable_count_)) {
    ret = OB_MINOR_FREEZE_NOT_ALLOW;
    if (TC_REACH_TIME_INTERVAL(1_s)) {
      LOG_WARN("The memtable array in the tablet reaches the upper limit, and no more memtable can "
          "be created", K(ret), K(memtable_count_), KPC(this));
    }
  } else if (OB_FAIL(get_protected_memtable_mgr_handle(protected_handle))) {
  } else if (OB_FAIL(protected_handle->create_memtable(tablet_meta_, arg))) {
    if (OB_ENTRY_EXIST != ret && OB_MINOR_FREEZE_NOT_ALLOW != ret) {
      LOG_WARN("failed to create memtable", K(ret), K(tablet_id), KPC(this));
    }
  } else {
    LOG_INFO("succeeded to create memtable for tablet", K(ret), K(tablet_id), K(arg));
  }

  return ret;
}

int ObTablet::inner_get_memtables(common::ObIArray<storage::ObITable *> &memtables) const
{
  int ret = OB_SUCCESS;
  memtables.reset();
  for (int64_t i = 0; OB_SUCC(ret) && i < memtable_count_; ++i) {
    if (OB_ISNULL(memtables_[i])) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("memtable must not null", K(ret), K(memtables_));
    } else if (OB_FAIL(memtables.push_back(memtables_[i]))) {
    }
  }
  return ret;
}

int ObTablet::rebuild_memtables(const share::SCN scn)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(release_memtables(scn))) {
  } else {
    reset_memtable();
    if (OB_FAIL(pull_memtables_without_ddl())) {
    } else {
      tablet_addr_.inc_seq();
      table_store_addr_.addr_.inc_seq();
      if (table_store_addr_.is_memory_object()) {
        ObSEArray<ObITable *, MAX_MEMSTORE_CNT> memtable_array;
        if (OB_FAIL(table_store_addr_.get_ptr()->clear_memtables())) {
        } else if (OB_FAIL(inner_get_memtables(memtable_array))) {
        } else if (OB_FAIL(table_store_addr_.get_ptr()->update_memtables(memtable_array))) {
        } else {
          LOG_INFO("table store update memtable success", KPC(this));
        }
      }
    }
  }
  return ret;
}

int ObTablet::inner_release_memtables(const SCN scn)
{
  int ret = OB_SUCCESS;
  ObProtectedMemtableMgrHandle *protected_handle = NULL;
  if (OB_FAIL(get_protected_memtable_mgr_handle(protected_handle))) {
  } else if (OB_FAIL(protected_handle->release_memtables_and_try_reset_memtable_mgr_handle(tablet_meta_.tablet_id_, scn))) {
  }
  return ret;
}

int ObTablet::release_memtables(const SCN scn)
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else if (OB_FAIL(inner_release_memtables(scn))) {
  }
  return ret;
}

int ObTablet::release_memtables()
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else if (is_empty_shell()) {
  } else if (OB_FAIL(inner_release_memtables(share::SCN()))) {
  }
return ret;
}

int ObTablet::wait_release_memtables()
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else if (OB_FAIL(wait_release_memtables_())) {
  }

  return ret;
}

int ObTablet::wait_release_memtables_()
{
  int ret = OB_SUCCESS;
  const int64_t start = ObTimeUtility::current_time();

  do {
    if (OB_FAIL(inner_release_memtables(share::SCN()))) {
      const int64_t cost_time = ObTimeUtility::current_time() - start;
      if (cost_time > 1_s) {
        if (TC_REACH_TIME_INTERVAL(1_s)) {
          LOG_WARN("failed to release memtables", K(ret), KPC(this));
        }
      }
    }
  } while (OB_FAIL(ret));

  return ret;
}

int ObTablet::mark_mds_table_switched_to_empty_shell_()
{
  int64_t ret = OB_SUCCESS;
  mds::MdsTableHandle mds_table;

  if (OB_FAIL(inner_get_mds_table(mds_table))) {
    if (OB_ENTRY_NOT_EXIST == ret) {
      ret = OB_SUCCESS;
    } else {
      LOG_WARN("failed to get mds table", K(ret));
    }
  } else if (OB_FAIL(mds_table.mark_switched_to_empty_shell())) {
  }

  return ret;
}

int ObTablet::get_ddl_kv_mgr(ObDDLKvMgrHandle &ddl_kv_mgr_handle, bool try_create)
{
  int ret = OB_SUCCESS;
  ddl_kv_mgr_handle.reset();
  if (!pointer_hdl_.is_valid()) {
    if (try_create) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("tablet pointer not valid", K(ret));
    } else {
      ret = OB_ENTRY_NOT_EXIST;
    }
    if (is_external_tablet()) {
      LOG_ERROR("is_external_tablet, and pointer_hdl_ is invalid", K(ret), K(lbt()));
    }
  } else {
    ObTabletPointer *tablet_ptr = static_cast<ObTabletPointer*>(pointer_hdl_.get_resource_ptr());
    if (try_create) {
      bool is_created = false;
      if (OB_FAIL(tablet_ptr->create_ddl_kv_mgr(tablet_meta_.tablet_id_, ddl_kv_mgr_handle, is_created))) {
      } else if (is_created) {
        ddl_kv_mgr_handle.get_obj()->set_max_freeze_scn(tablet_meta_.ddl_checkpoint_scn_);
      }
    } else {
      tablet_ptr->get_ddl_kv_mgr(ddl_kv_mgr_handle);
      if (!ddl_kv_mgr_handle.is_valid()) {
        ret = OB_ENTRY_NOT_EXIST;
      }
    }
  }
  return ret;
}

int ObTablet::init_shared_params(const common::ObTabletID &tablet_id)
{
  int ret = OB_SUCCESS;
  ObProtectedMemtableMgrHandle *protected_handle = NULL;
  ObLS *ls = NULL;

  if (OB_UNLIKELY(!pointer_hdl_.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet pointer handle is invalid", K(ret), K_(pointer_hdl));
    if (is_external_tablet()) {
      LOG_ERROR("is_external_tablet, and pointer_hdl_ is invalid", K(lbt()));
    }
  } else if (!tablet_id.is_ls_inner_tablet()) {
    // tablet_memtable_mgr init in ObProtectedMemtableMgrHandle
  } else if (OB_ISNULL(ls = static_cast<ObTabletPointer *>(
                                pointer_hdl_.get_resource_ptr())->get_ls())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet has no log stream", K(ret), K(tablet_id));
  } else {
    if (OB_FAIL(get_protected_memtable_mgr_handle(protected_handle))) {
    } else if (OB_FAIL(protected_handle->init(tablet_id,
                                               0 /* max_saved_schema_version */,
                                               0 /* max_saved_medium_scn */,
                                               ls->get_log_handler(),
                                               ls->get_freezer(),
                                               ::oceanbase::share::server_service<::oceanbase::storage::ObStorageMetaMemMgr>()))) {
    }
  }
  return ret;
}

int ObTablet::build_read_info(
    common::ObArenaAllocator &allocator,
    const ObTablet *tablet)
{
  int ret = OB_SUCCESS;
  int64_t full_stored_col_cnt = 0;
  ObStorageSchema *storage_schema = nullptr;
  ObSEArray<share::schema::ObColDesc, 16> cols_desc;
  tablet = (tablet == nullptr) ? this : tablet;
  if (OB_FAIL(tablet->load_storage_schema(allocator, storage_schema))) {
  } else if (OB_FAIL(storage_schema->get_mulit_version_rowkey_column_ids(cols_desc))) {
  } else if (OB_FAIL(ObTabletObjLoadHelper::alloc_and_new(allocator, rowkey_read_info_))) {
  } else if (OB_FAIL(storage_schema->get_store_column_count(full_stored_col_cnt, true/*full col*/))) {
  } else if (OB_FAIL(rowkey_read_info_->init(allocator,
                                             full_stored_col_cnt,
                                             storage_schema->get_rowkey_column_num(),
                                             cols_desc,
                                             storage_schema->is_global_index_table()))) {
  }
  ObTabletObjLoadHelper::free(allocator, storage_schema);
  return ret;
}

int ObTablet::try_update_start_scn()
{
  int ret = OB_SUCCESS;
  ObTabletMemberWrapper<ObTabletTableStore> table_store_wrapper;
  ObTableStoreIterator iter;
  if (OB_FAIL(fetch_table_store(table_store_wrapper))) {
  } else {
    ObSSTable *first_minor = static_cast<ObSSTable *>(
        table_store_wrapper.get_member()->get_minor_sstables().get_boundary_table(false /*first*/));
    const SCN &start_scn = OB_NOT_NULL(first_minor) ? first_minor->get_start_scn() : tablet_meta_.clog_checkpoint_scn_;
    const SCN &tablet_meta_scn = tablet_meta_.start_scn_;
    tablet_meta_.start_scn_ = start_scn;
    if (OB_UNLIKELY(start_scn < tablet_meta_scn)) {
      FLOG_INFO("tablet start scn is small than tablet meta start scn", K(start_scn), K(tablet_meta_scn), K(tablet_meta_));
    }
  }
  return ret;
}

int ObTablet::try_update_table_store_flag(const bool with_major)
{
  int ret = OB_SUCCESS;
  if (with_major) {
    tablet_meta_.table_store_flag_.set_with_major_sstable();
  }
  return ret;
}

int ObTablet::build_sstable_clone_param(
    const ObITable::TableKey &table_key,
    blocksstable::ObSSTableCloneParam &clone_sstable_param) const
{
  int ret = OB_SUCCESS;
  ObTableHandleV2 handle;
  ObTabletMemberWrapper<ObTabletTableStore> table_store_wrapper;
  ObSSTableMetaHandle sstable_meta_handle;
  ObSSTable *sstable = nullptr;

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K(is_inited_));
  } else if (OB_UNLIKELY(!table_key.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(table_key));
  } else if (OB_FAIL(fetch_table_store(table_store_wrapper))) {
  } else if (OB_FAIL(table_store_wrapper.get_member()->get_table(
      table_store_wrapper.get_meta_handle(), table_key, handle))) {
  } else if (OB_FAIL(handle.get_sstable(sstable))) {
  } else if (OB_ISNULL(sstable)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected error", K(ret), KPC(sstable));
  } else if (OB_FAIL(sstable->get_meta(sstable_meta_handle))) {
  } else {
    const ObSSTableMeta &sstable_meta = sstable_meta_handle.get_sstable_meta();
    clone_sstable_param.basic_meta_ = sstable_meta.get_basic_meta();
    clone_sstable_param.is_meta_root_ = sstable_meta.get_macro_info().is_meta_root();

    for (int64_t i = 0; OB_SUCC(ret) && i < sstable_meta.get_col_checksum_cnt(); ++i) {
      if (OB_FAIL(clone_sstable_param.column_checksums_.push_back(
          sstable_meta.get_col_checksum()[i]))) {
      }
    }

    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(copy_embedded_meta_(
        sstable_meta.get_root_info(),
        clone_sstable_param.allocator_,
        clone_sstable_param.root_block_addr_,
        clone_sstable_param.root_block_buf_))) {
    } else if (OB_FAIL(copy_embedded_meta_(
        sstable_meta.get_macro_info().get_macro_meta_info(),
        clone_sstable_param.allocator_,
        clone_sstable_param.data_block_macro_meta_addr_,
        clone_sstable_param.data_block_macro_meta_buf_))) {
    }
  }

  if (OB_FAIL(ret)) {
    clone_sstable_param.reset();
  } else {
    STORAGE_LOG(INFO, "succeed to build sstable clone param",
        K(clone_sstable_param), K(sstable_meta_handle.get_sstable_meta()));
  }
  return ret;
}

int ObTablet::fetch_tablet_autoinc_seq_cache(
    const uint64_t cache_size,
    share::ObTabletAutoincInterval &result)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator allocator(common::ObMemAttr("FetchAutoSeq"));
  ObTabletAutoincSeq autoinc_seq;
  mds::MdsWriter writer;// will be removed later
  mds::TwoPhaseCommitState trans_stat;// will be removed later
  share::SCN trans_version;// will be removed later
  uint64_t auto_inc_seqvalue = 0;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else if (OB_FAIL(get_latest_autoinc_seq(autoinc_seq, allocator, writer, trans_stat, trans_version))) {
    if (OB_EMPTY_RESULT == ret) {
      ret = OB_SUCCESS;
      autoinc_seq.reset();
      trans_stat = mds::TwoPhaseCommitState::ON_COMMIT;
    } else {
      LOG_WARN("fail to get latest autoinc seq", K(ret), K(tablet_meta_.tablet_id_));
    }
  }

  if (OB_FAIL(ret)) {
  } else if (OB_UNLIKELY(mds::TwoPhaseCommitState::ON_COMMIT != trans_stat)) {
    ret = OB_EAGAIN;
    LOG_WARN("tablet autoinc not committed", K(ret), K(autoinc_seq));
  } else if (OB_FAIL(autoinc_seq.get_autoinc_seq_value(auto_inc_seqvalue))) {
  } else {
    const uint64_t interval_start = auto_inc_seqvalue;
    const uint64_t interval_end = auto_inc_seqvalue + cache_size - 1;
    const uint64_t result_autoinc_seq = auto_inc_seqvalue + cache_size;
    const ObTabletID &tablet_id = tablet_meta_.tablet_id_;
    SCN scn = SCN::min_scn();
    if (OB_FAIL(autoinc_seq.set_autoinc_seq_value(allocator, result_autoinc_seq))) {
    } else if (OB_FAIL(write_sync_tablet_seq_log(autoinc_seq, false/*is_tablet_creating*/, scn))) {
    } else {
      result.start_ = interval_start;
      result.end_ = interval_end;
      result.tablet_id_ = tablet_id;
    }
  }
  return ret;
}

// MIN { ls min_reserved_snapshot, freeze_info, all_acquired_snapshot}
int ObTablet::get_kept_snapshot_info(
    const int64_t min_reserved_snapshot_on_ls,
    ObStorageSnapshotInfo &snapshot_info) const
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  snapshot_info.reset();
  const common::ObTabletID &tablet_id = tablet_meta_.tablet_id_;

  const int64_t last_major_snapshot_version = get_last_major_snapshot_version();
  bool need_check_medium_info = false;
  int64_t max_merged_snapshot = 0;
  if (0 == last_major_snapshot_version) {
    // do nothing
  } else {
    max_merged_snapshot = last_major_snapshot_version;
  }

  int64_t min_medium_snapshot = INT64_MAX;
  if (!is_ls_inner_tablet()) {
    common::ObArenaAllocator allocator(common::ObMemAttr("GetKeptShotInfo"));
    ObMdsReadInfoCollector unused_collector;
    SMART_VARS_2((ObTableScanParam, scan_param), (ObTabletMediumInfoReader, medium_info_reader)) {
      if (OB_FAIL((ObMdsScanParamHelper::build_customized_scan_param<ObMediumCompactionInfoKey, ObMediumCompactionInfo>(
          allocator,
          tablet_id,
          ObMdsScanParamHelper::get_whole_read_version_range(),
          unused_collector,
          scan_param)))) {
      } else if (OB_FAIL(medium_info_reader.init(*this, scan_param))) {
      } else if (need_check_medium_info) {
        ObMediumCompactionInfoKey medium_info_key(max_merged_snapshot);
        ObMediumCompactionInfo *medium_info = nullptr;
        if (OB_FAIL(ObTabletObjLoadHelper::alloc_and_new(allocator, medium_info))) {
        } else if (OB_FAIL(medium_info_reader.get_specified_medium_info(allocator, medium_info_key, *medium_info))) {
          if (OB_ENTRY_NOT_EXIST == ret) {
            ret = OB_SUCCESS;
            max_merged_snapshot = last_major_snapshot_version;
          } else {
            LOG_WARN("failed to get specified scn info", K(ret), K(max_merged_snapshot));
          }
        } else if (ObAdaptiveMergePolicy::DURING_DDL != medium_info->medium_merge_reason_) {
          max_merged_snapshot = last_major_snapshot_version;
        }
      }

      if (FAILEDx(medium_info_reader.get_min_medium_snapshot(max_merged_snapshot, min_medium_snapshot))) {
        LOG_WARN("failed to get min medium snapshot", K(ret), K(tablet_id));
      }
    }
  }

  ObStorageSnapshotInfo old_snapshot_info;
  if (FAILEDx(::oceanbase::share::server_service<::oceanbase::storage::ObFreezeInfoMgr>()->get_min_reserved_snapshot(tablet_id, max_merged_snapshot, snapshot_info))) {
    LOG_WARN("failed to get multi version from freeze info mgr", K(ret), K(tablet_id));
  } else {
    old_snapshot_info = snapshot_info;
    bool use_multi_version_start_on_tablet = false;

    if (!tablet_meta_.local_status_.is_data_status_complete()) {
      use_multi_version_start_on_tablet = true;
    } else if (min_reserved_snapshot_on_ls > 0) {
      snapshot_info.update_by_smaller_snapshot(ObStorageSnapshotInfo::SNAPSHOT_FOR_LS_RESERVED, min_reserved_snapshot_on_ls);
      snapshot_info.update_by_smaller_snapshot(ObStorageSnapshotInfo::SNAPSHOT_FOR_MIN_MEDIUM, min_medium_snapshot);
      if (snapshot_info.snapshot_ < get_multi_version_start()) {
        use_multi_version_start_on_tablet = true;
      }
    } else {
      // if not sync ls_reserved_snapshot yet, should use multi_version_start on tablet
      use_multi_version_start_on_tablet = true;
    }
    if (use_multi_version_start_on_tablet) {
      snapshot_info.snapshot_type_ = ObStorageSnapshotInfo::SNAPSHOT_MULTI_VERSION_START_ON_TABLET;
      snapshot_info.snapshot_ = get_multi_version_start();
    }
    // snapshot info should smaller than snapshot on tablet
    snapshot_info.update_by_smaller_snapshot(ObStorageSnapshotInfo::SNAPSHOT_ON_TABLET, get_snapshot_version());
    const int64_t current_time = common::ObTimeUtility::fast_current_time();
    if (current_time - (snapshot_info.snapshot_ / 1000 /*use microsecond here*/) > 40_min) {
      if (REACH_THREAD_TIME_INTERVAL(10_s)) {
        LOG_INFO("tablet multi version start not advance for a long time", K(ret),
                 K(tablet_id),
                 K(snapshot_info), K(old_snapshot_info), K(min_medium_snapshot),
                 K(min_reserved_snapshot_on_ls));
      }
    }
  }
  if (OB_SUCC(ret) && OB_UNLIKELY(!snapshot_info.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("snapshot info is invalid", KR(ret), K(snapshot_info));
  }
  return ret;
}

int ObTablet::write_sync_tablet_seq_log(ObTabletAutoincSeq &autoinc_seq,
                                        const bool is_tablet_creating,
                                        share::SCN &scn)
{
  int ret = OB_SUCCESS;
  const int64_t WAIT_TIME = 1000; // 1ms
  const int64_t SYNC_TABLET_SEQ_LOG_TIMEOUT = 1000L * 1000L * 30L; // 30s
  const common::ObTabletID &tablet_id = tablet_meta_.tablet_id_;
  const enum ObReplayBarrierType replay_barrier_type = is_tablet_creating ? logservice::ObReplayBarrierType::PRE_BARRIER
                                                                          : logservice::ObReplayBarrierType::NO_NEED_BARRIER;
  ObLogBaseHeader base_header(ObLogBaseType::TABLET_SEQ_SYNC_LOG_BASE_TYPE, replay_barrier_type);
  ObSyncTabletSeqLog log;
  // NOTICE: ObLogBaseHeader & ObSyncTabletSeqLog should have fixed serialize size!
  const int64_t buffer_size = base_header.get_serialize_size() + log.get_serialize_size();
  char buffer[buffer_size];
  int64_t retry_cnt = 0;
  int64_t pos = 0;
  ObSyncTabletSeqMdsLogCb *cb = nullptr;
  ObLogHandler *log_handler = get_log_handler();
  palf::LSN lsn;
  const bool need_nonblock = true; // log_handler->append may return OB_EAGAIN, caller is responsible for retry
  const SCN ref_scn = SCN::min_scn();
  uint64_t new_autoinc_seq = 0;
  if (OB_FAIL(autoinc_seq.get_autoinc_seq_value(new_autoinc_seq))) {
  } else if (OB_FAIL(log.init(tablet_id, new_autoinc_seq))) {
  } else if (OB_ISNULL(cb = op_alloc(ObSyncTabletSeqMdsLogCb))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to alloc memory", K(ret));
  } else if (OB_FAIL(base_header.serialize(buffer, buffer_size, pos))) {
  } else if (OB_FAIL(log.serialize(buffer, buffer_size, pos))) {
  } else if (OB_FAIL(cb->init(tablet_id, static_cast<int64_t>(new_autoinc_seq)))) {
  } else if (OB_FAIL(set<ObTabletAutoincSeq>(std::move(autoinc_seq), cb->get_mds_ctx(), THIS_WORKER.is_timeout_ts_valid() ? THIS_WORKER.get_timeout_remain() : OB_DEFAULT_RPC_TIMEOUT))) {
  } else if (OB_FAIL(log_handler->append(buffer,
                                         buffer_size,
                                         ref_scn,
                                         need_nonblock,
                                         cb,
                                         lsn,
                                         scn))) {
    LOG_ERROR("fail to submit sync tablet seq log", K(ret), K(buffer_size));
    cb->on_failure();
  } else {
    // wait until majority
    bool wait_timeout = false;
    int64_t start_time = ObTimeUtility::fast_current_time();
    while (!cb->is_finished() && !wait_timeout) {
      ob_usleep(WAIT_TIME);
      retry_cnt++;
      if (retry_cnt % 1000 == 0) {
        if (ObTimeUtility::fast_current_time() - start_time > SYNC_TABLET_SEQ_LOG_TIMEOUT) {
          wait_timeout = true;
        }
        LOG_WARN("submit sync tablet seq log wait too much time", K(retry_cnt), K(wait_timeout));
      }
    }
    if (wait_timeout) {
      ret = OB_TIMEOUT;
      LOG_WARN("submit sync tablet seq log timeout", K(ret));
    } else if (cb->is_failed()) {
      ret = OB_NOT_MASTER;
      LOG_WARN("submit sync tablet seq log failed", K(ret));
    } else {
      int64_t wait_time = ObTimeUtility::fast_current_time() - start_time;
      LOG_INFO("submit sync tablet seq log succeed", K(ret), K(tablet_id),
          K(new_autoinc_seq), K(lsn), K(scn), K(wait_time));
    }
    if (nullptr != cb) {
      cb->try_release();
      cb = nullptr;
    }
  }
  if (OB_FAIL(ret) && nullptr != cb) {
    op_free(cb);
    cb = nullptr;
  }
  return ret;
}

int ObTablet::update_tablet_autoinc_seq(const uint64_t autoinc_seq, const bool is_tablet_creating)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator allocator(common::ObMemAttr("UpdAutoincSeq"));
  ObTabletAutoincSeq curr_autoinc_seq;
  mds::MdsWriter writer;// will be removed later
  mds::TwoPhaseCommitState trans_stat;// will be removed later
  share::SCN trans_version;// will be removed later
  uint64_t curr_auto_inc_seqvalue;
  SCN scn;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else if (OB_FAIL(get_latest_autoinc_seq(curr_autoinc_seq, allocator, writer, trans_stat, trans_version))) {
    if (OB_EMPTY_RESULT == ret) {
      ret = OB_SUCCESS;
      curr_autoinc_seq.reset();
      trans_stat = mds::TwoPhaseCommitState::ON_COMMIT;
    } else {
      LOG_WARN("fail to get latest autoinc seq", K(ret), K(tablet_meta_.tablet_id_));
    }
  }

  if (OB_FAIL(ret)) {
  } else if (mds::TwoPhaseCommitState::ON_COMMIT != trans_stat) {
    ret = OB_EAGAIN;
    LOG_WARN("tablet autoinc not committed", K(ret), K(autoinc_seq));
  } else if (OB_FAIL(curr_autoinc_seq.get_autoinc_seq_value(curr_auto_inc_seqvalue))) {
  } else if (autoinc_seq > curr_auto_inc_seqvalue) {
    if (OB_FAIL(curr_autoinc_seq.set_autoinc_seq_value(allocator, autoinc_seq))) {
    } else if (OB_FAIL(write_sync_tablet_seq_log(curr_autoinc_seq, is_tablet_creating, scn))) {
    }
  }
  return ret;
}

int ObTablet::start_direct_load_task_for_idem(ObLS *tenant_ls)
{
  int ret = OB_SUCCESS;
  ObTabletMemberWrapper<ObTabletTableStore> table_store_wrapper;
  ObDDLKvMgrHandle ddl_kv_mgr_handle;
  if (OB_ISNULL(tenant_ls)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("local ls is null", K(ret), K(tablet_meta_));
  } else if (OB_FAIL(fetch_table_store(table_store_wrapper))) {
  } else if (nullptr != table_store_wrapper.get_member()->get_major_sstables().get_boundary_table(false/*first*/)) {
    // the major sstable has already existed.
  } else if (OB_FAIL(get_ddl_kv_mgr(ddl_kv_mgr_handle, true /* try create */))) {
  } else if (OB_FAIL(tenant_ls->get_ddl_log_handler()->add_tablet(tablet_meta_.tablet_id_))) {
  }
  return ret;
}

int ObTablet::start_direct_load_task_if_need()
{
  int ret = OB_SUCCESS;
  ObLS *tenant_ls = nullptr;
  ObLSService *ls_service = ::oceanbase::share::server_service<::oceanbase::storage::ObLSService>();
  if (is_empty_shell()) {
  } else if (OB_ISNULL(ls_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls service is null", K(ret));
  } else if (OB_FAIL(ls_service->get_ls(tenant_ls))) {
  } else if (OB_ISNULL(tenant_ls)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("local ls is null", K(ret));
  } else if (OB_FAIL(start_direct_load_task_for_idem(tenant_ls))) {
  }
  return ret;
}

int ObTablet::check_schema_version_elapsed(
    const int64_t schema_version,
    const bool need_wait_trans_end,
    int64_t &max_commit_version,
    transaction::ObTransID &pending_tx_id)
{
  int ret = OB_SUCCESS;
  const common::ObTabletID &tablet_id = tablet_meta_.tablet_id_;
  ObMultiVersionSchemaService *schema_service = ::oceanbase::share::server_service<::oceanbase::share::schema::ObSchemaRuntimeService>()->get_schema_service();
  SCN scn;
  SCN max_commit_scn;
  int64_t runtime_refreshed_schema_version = 0;
  int64_t refreshed_schema_ts = 0;
  int64_t refreshed_schema_version = 0;
  max_commit_version = 0L;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTablet has not been inited", K(ret));
  } else if (OB_UNLIKELY(schema_version <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(schema_version));
  } else if (!need_wait_trans_end) {
    // obtain_snapshot of offline ddl don't need to wait trans end.
    transaction::ObTransService *txs = ::oceanbase::share::server_service<::oceanbase::transaction::ObTransService>();
    if (OB_FAIL(txs->get_max_commit_version(max_commit_scn))) {
    } else if (OB_UNLIKELY(!max_commit_scn.is_valid())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected error, scn is invalid", K(ret), K(max_commit_scn));
    } else {
      max_commit_version = max_commit_scn.get_val_for_tx();
    }
  } else {
    if (OB_FAIL(get_ddl_info(refreshed_schema_version, refreshed_schema_ts))) {
    } else if (refreshed_schema_version >= schema_version) {
      // schema version already refreshed
    } else if (OB_FAIL(schema_service->get_runtime_refreshed_schema_version(runtime_refreshed_schema_version))) {
      ret = OB_ENTRY_NOT_EXIST == ret ? OB_SCHEMA_EAGAIN : ret;
      LOG_WARN("get runtime refreshed schema version failed", K(ret));
    } else if (runtime_refreshed_schema_version < schema_version) {
      ret = OB_EAGAIN;
      LOG_WARN("current schema version not latest, need retry", K(ret), K(schema_version), K(runtime_refreshed_schema_version));
    } else if (OB_FAIL(replay_schema_version_change_log(schema_version))) {
    } else if (OB_FAIL(write_tablet_schema_version_change_clog(schema_version, scn))) {
      LOG_WARN("write partition schema version change clog error", K(ret), K(schema_version));
      // override ret
      ret = OB_EAGAIN;
    } else if (OB_FAIL(update_ddl_info(schema_version, scn, refreshed_schema_ts))) {
    }

    if (OB_SUCC(ret)) {
      transaction::ObTransService *txs = ::oceanbase::share::server_service<::oceanbase::transaction::ObTransService>();
      ObLSService *ls_service = ::oceanbase::share::server_service<::oceanbase::storage::ObLSService>();
      ObLS *tenant_ls = nullptr;
      if (OB_FAIL(ls_service->get_ls(tenant_ls))) {
      } else if (OB_FAIL(tenant_ls->check_modify_schema_elapsed(tablet_id, schema_version, pending_tx_id))) {
        if (OB_EAGAIN != ret) {
          LOG_WARN("check schema version elapsed failed", K(ret), K(tablet_id), K(schema_version));
        } else {
          LOG_INFO("check schema version elapsed again", K(ret), K(tablet_id), K(schema_version), K(refreshed_schema_ts));
        }
      } else if (OB_FAIL(txs->get_max_commit_version(max_commit_scn))) {
      } else {
        max_commit_version = max_commit_scn.get_val_for_tx();
        LOG_INFO("check wait trans end", K(ret), K(tablet_id), K(max_commit_version), K(max_commit_scn), K(refreshed_schema_ts));
      }
    }
  }
  return ret;
}

int ObTablet::write_tablet_schema_version_change_clog(
    const int64_t schema_version,
    SCN &scn)
{
  int ret = OB_SUCCESS;
  const common::ObTabletID &tablet_id = tablet_meta_.tablet_id_;
  ObTabletSchemaVersionChangeLog log;
  if (OB_FAIL(log.init(tablet_id, schema_version))) {
  } else {
    const int64_t CHECK_SCHEMA_VERSION_CHANGE_LOG_US = 1000;
    const int64_t CHECK_SCHEMA_VERSION_CHANGE_LOG_TIMEOUT = 1000L * 1000L * 30L; // 30s
    const enum ObReplayBarrierType replay_barrier_type = ObReplayBarrierType::STRICT_BARRIER;
    ObLogBaseHeader base_header(ObLogBaseType::DDL_LOG_BASE_TYPE, replay_barrier_type);
    ObDDLClogHeader ddl_header(ObDDLClogType::DDL_TABLET_SCHEMA_VERSION_CHANGE_LOG);
    const int64_t buffer_size = base_header.get_serialize_size() + ddl_header.get_serialize_size()
                              + log.get_serialize_size();
    char buffer[buffer_size];
    int64_t retry_cnt = 0;
    int64_t pos = 0;
    ObDDLClogCb *cb = nullptr;
    ObLogHandler *log_handler = get_log_handler();

    palf::LSN lsn;
    const bool need_nonblock= false;
    SCN ref_scn;
    ref_scn.set_min();
    scn.reset();

    if (OB_ISNULL(cb = op_alloc(ObDDLClogCb))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to alloc memory", K(ret));
    } else if (OB_FAIL(base_header.serialize(buffer, buffer_size, pos))) {
    } else if (OB_FAIL(ddl_header.serialize(buffer, buffer_size, pos))) {
    } else if (OB_FAIL(log.serialize(buffer, buffer_size, pos))) {
    } else if (OB_FAIL(log_handler->append(buffer,
                                           buffer_size,
                                           ref_scn,
                                           need_nonblock,
                                           cb,
                                           lsn,
                                           scn))) {
    } else {
      ObDDLClogCb *tmp_cb = cb;
      cb = nullptr;
      // wait unti majority
      bool wait_timeout = false;
      int64_t start_time = ObTimeUtility::fast_current_time();
      while (!tmp_cb->is_finished() && !wait_timeout) {
        ob_usleep(CHECK_SCHEMA_VERSION_CHANGE_LOG_US);
        retry_cnt++;
        if (retry_cnt % 1000 == 0) {
          if (ObTimeUtility::fast_current_time() - start_time > CHECK_SCHEMA_VERSION_CHANGE_LOG_TIMEOUT) {
            wait_timeout = true;
          }
          LOG_WARN_RET(OB_ERR_TOO_MUCH_TIME, "submit schema version change log wait too much time", K(retry_cnt), K(wait_timeout));
        }
      }
      if (wait_timeout) {
        ret = OB_TIMEOUT;
        LOG_WARN("submit schema version change log timeout", K(ret), K(tablet_id));
      } else if (tmp_cb->is_failed()) {
        ret = OB_NOT_MASTER;
        LOG_WARN("submit schema version change log failed", K(ret), K(tablet_id));
      } else {
        LOG_INFO("submit schema version change log succeed", K(ret), K(tablet_id), K(schema_version));
      }
      tmp_cb->try_release(); // release the memory no matter succ or not
    }
    if (nullptr != cb) {
      op_free(cb);
      cb = nullptr;
    }
  }
  return ret;
}

int ObTablet::replay_schema_version_change_log(const int64_t schema_version)
{
  int ret = OB_SUCCESS;
  common::ObSEArray<ObTableHandleV2, 8> table_handle_array;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret));
  } else if (OB_FAIL(get_all_memtables_from_memtable_mgr(table_handle_array))) {
  } else {
    ObITabletMemtable *memtable = nullptr;
    const int64_t table_num = table_handle_array.count();
    if (0 == table_num) {
      // no memtable, no need to replay schema version change
    } else if (!table_handle_array[table_num - 1].is_valid()) {
      ret = OB_ERR_SYS;
      LOG_WARN("latest memtable is invalid", K(ret));
    } else if (OB_FAIL(table_handle_array[table_num - 1].get_tablet_memtable(memtable))) {
    } else if (OB_ISNULL(memtable)) {
      ret = OB_ERR_SYS;
      LOG_WARN("memtable is null", K(ret), KP(memtable));
    } else if (OB_FAIL(memtable->replay_schema_version_change_log(schema_version))) {
    }
  }

  return ret;
}

int ObTablet::get_tablet_runtime_info(
      share::ObTabletRuntimeInfo &runtime_info,
      share::ObTabletLocalChecksumItem &tablet_checksum) const
{
  int ret = OB_SUCCESS;
  ObTabletMemberWrapper<ObTabletTableStore> table_store_wrapper;
  const ObTabletTableStore *table_store = nullptr;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret));
  } else if (OB_FAIL(fetch_table_store(table_store_wrapper))) {
  } else if (OB_FAIL(table_store_wrapper.get_member(table_store))) {
  } else if (table_store->get_major_sstables().empty()) {
    ret = OB_TABLET_NOT_EXIST;
    LOG_INFO("no major sstables in this tablet, cannot report", K(ret));
  } else if (OB_FAIL(get_tablet_runtime_info_by_sstable(
                 *table_store, runtime_info, tablet_checksum))) {
  }
  return ret;
}

int ObTablet::get_tablet_runtime_info_by_sstable(
    const ObTabletTableStore &table_store,
    share::ObTabletRuntimeInfo &runtime_info,
    ObTabletLocalChecksumItem &tablet_checksum) const
{
  int ret = OB_SUCCESS;
  ObSSTable *main_major = static_cast<ObSSTable *>(table_store.get_major_sstables().get_boundary_table(true));
  ObSSTableMetaHandle main_major_meta_hdl;
  const int64_t report_major_snapshot = tablet_meta_.report_status_.merge_snapshot_version_;
  int64_t data_size = 0;
  int64_t required_size = 0;
  share::ObFreezeInfo freeze_info;
  ObArray<int64_t> column_checksums;
  column_checksums.set_attr(ObMemAttr("tmpCkmArr"));
  ObSSTable *table = nullptr;
  if (OB_UNLIKELY(nullptr == main_major || report_major_snapshot != main_major->get_snapshot_version())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("failed to get unexpected null major", K(ret), K(table_store));
  } else if (OB_FAIL(main_major->get_meta(main_major_meta_hdl))) {
  } else {
    data_size = main_major_meta_hdl.get_sstable_meta().get_basic_meta().occupy_size_;
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < table_store.get_major_sstables().count(); ++i) {
    table = table_store.get_major_sstables().at(i);
    if (OB_ISNULL(table)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected error, table is nullptr", K(ret), KPC(table));
    } else {
      required_size += table->get_occupy_size();
    }
  }
  if (FAILEDx(runtime_info.init(
        get_tablet_id(),
        report_major_snapshot,
        data_size,
        required_size,
        0/*report_scn*/,
        ObTabletRuntimeInfo::SCN_STATUS_IDLE))) {
      LOG_WARN("fail to init tablet runtime info", KR(ret), "tablet_id", get_tablet_id(), K(runtime_info));
  } else if (OB_FAIL(get_sstable_column_checksum(*main_major, column_checksums))) {
  } else if (OB_FAIL(tablet_checksum.column_meta_.init(column_checksums))) {
  } else if (OB_FAIL(tablet_checksum.compaction_scn_.convert_for_tx(report_major_snapshot))) {
  } else if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::storage::ObFreezeInfoMgr>()->get_lower_bound_freeze_info_before_snapshot_version(report_major_snapshot, freeze_info))) {
    if (OB_ENTRY_NOT_EXIST == ret) {
      ret = OB_EAGAIN;
    } else {
      LOG_WARN("failed to get freeze info", K(ret), K(report_major_snapshot));
    }
  } else {
    tablet_checksum.tablet_id_ = get_tablet_id();
    tablet_checksum.row_count_ = get_tablet_meta().report_status_.row_count_;
    tablet_checksum.data_checksum_ = get_tablet_meta().report_status_.data_checksum_;
    tablet_checksum.set_data_checksum_type();
    LOG_INFO("success to get tablet runtime info", KR(ret), "tablet_id", get_tablet_id(), "report_status",
      tablet_meta_.report_status_, K(tablet_checksum));
  }
  return ret;
}

int ObTablet::get_ddl_sstables(ObTableStoreIterator &table_store_iter) const
{
  int ret = OB_SUCCESS;
  ObTabletMemberWrapper<ObTabletTableStore> table_store_wrapper;
  const ObTabletTableStore *table_store = nullptr;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret));
  } else if (OB_FAIL(fetch_table_store(table_store_wrapper))) {
  } else if (OB_FAIL(table_store_wrapper.get_member(table_store))) {
  } else if (OB_FAIL(table_store->get_ddl_sstables(table_store_iter))) {
  } else if (!table_store_addr_.is_memory_object()
      && OB_FAIL(table_store_iter.set_handle(table_store_wrapper.get_meta_handle()))) {
    LOG_WARN("fail to set storage meta handle", K(ret), K_(table_store_addr), K(table_store_wrapper));
  }
  return ret;
}

int ObTablet::get_mds_sstables(ObTableStoreIterator &table_store_iter) const
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret));
  } else if (OB_FAIL(inner_get_mds_sstables(table_store_iter))) {
  }
  return ret;
}

int ObTablet::inner_get_mds_sstables(ObTableStoreIterator &table_store_iter) const
{
  int ret = OB_SUCCESS;
  ObTabletMemberWrapper<ObTabletTableStore> table_store_wrapper;
  const ObTabletTableStore *table_store = nullptr;

  if (OB_FAIL(fetch_table_store(table_store_wrapper))) {
  } else if (OB_FAIL(table_store_wrapper.get_member(table_store))) {
  } else if (OB_FAIL(table_store->get_mds_sstables(table_store_iter))) {
  } else if (!table_store_addr_.is_memory_object()
      && OB_FAIL(table_store_iter.set_handle(table_store_wrapper.get_meta_handle()))) {
    LOG_WARN("fail to set storage meta handle", K(ret), K_(table_store_addr), K(table_store_wrapper));
  }

  return ret;
}

int ObTablet::get_mini_minor_sstables(ObTableStoreIterator &table_store_iter) const
{
  int ret = OB_SUCCESS;
  ObTabletMemberWrapper<ObTabletTableStore> table_store_wrapper;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret));
  } else if (OB_FAIL(fetch_table_store(table_store_wrapper))) {
  } else if (OB_FAIL(table_store_wrapper.get_member()->get_mini_minor_sstables(table_store_iter))) {
  } else if (!table_store_addr_.is_memory_object()
      && OB_FAIL(table_store_iter.set_handle(table_store_wrapper.get_meta_handle()))) {
    LOG_WARN("fail to set storage meta handle", K(ret), K_(table_store_addr), K(table_store_wrapper));
  }
  return ret;
}

int ObTablet::get_table(const ObITable::TableKey &table_key, ObTableHandleV2 &handle) const
{
  int ret = OB_SUCCESS;
  ObTabletMemberWrapper<ObTabletTableStore> table_store_wrapper;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret));
  } else if (OB_FAIL(fetch_table_store(table_store_wrapper))) {
  } else if (OB_FAIL(table_store_wrapper.get_member()->get_table(
      table_store_wrapper.get_meta_handle(), table_key, handle))) {
  }
  return ret;
}

int ObTablet::get_recycle_version(const int64_t multi_version_start, int64_t &recycle_version) const
{
  int ret = OB_SUCCESS;
  ObTabletMemberWrapper<ObTabletTableStore> table_store_wrapper;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret));
  } else if (OB_FAIL(fetch_table_store(table_store_wrapper))) {
  } else if (OB_FAIL(table_store_wrapper.get_member()->get_recycle_version(
      multi_version_start, recycle_version))) {
  }
  return ret;
}

int ObTablet::update_ddl_info(
    const int64_t schema_version,
    const SCN &scn,
    int64_t &schema_refreshed_ts)
{
  int ret = OB_SUCCESS;
  ObTabletPointer *tablet_ptr = nullptr;
  if (!pointer_hdl_.is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet_pointer_hdl is in_valid", K(ret), K(pointer_hdl_));
    if (is_external_tablet()) {
      LOG_ERROR("is_external_tablet, and pointer_hdl_ is invalid", K(lbt()));
    }
  } else if (OB_ISNULL(tablet_ptr = static_cast<ObTabletPointer *>(pointer_hdl_.get_resource_ptr()))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet_pointer_hdl is in_valid", K(ret), K(pointer_hdl_));
  } else if (OB_FAIL(tablet_ptr->ddl_info_.update(schema_version, scn, schema_refreshed_ts))) {
  }
  return ret;
}

int ObTablet::get_ddl_info(int64_t &schema_version, int64_t &schema_refreshed_ts) const
{
  int ret = OB_SUCCESS;
  ObTabletPointer *tablet_ptr = nullptr;
  if (!pointer_hdl_.is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet_pointer_hdl is in_valid", K(ret), K(pointer_hdl_));
    if (is_external_tablet()) {
      LOG_ERROR("is_external_tablet, and pointer_hdl_ is invalid", K(lbt()));
    }
  } else if (OB_ISNULL(tablet_ptr = static_cast<ObTabletPointer *>(pointer_hdl_.get_resource_ptr()))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet_pointer_hdl is in_valid", K(ret), K(pointer_hdl_));
  } else if (OB_FAIL(tablet_ptr->ddl_info_.get(schema_version, schema_refreshed_ts))) {
  }
  return ret;
}

int ObTablet::get_mds_table_rec_scn(SCN &rec_scn) const
{
  int ret = OB_SUCCESS;
  const common::ObTabletID &tablet_id = tablet_meta_.tablet_id_;
  mds::MdsTableHandle mds_table;
  rec_scn = SCN::max_scn();
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", KR(ret), K_(is_inited));
  } else if (is_ls_inner_tablet()) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("inner tablet does not have mds table", K(ret));
  } else if (is_empty_shell()) {
    // empty shell tablet is considered persisted and has no mds table
    // however due to table pointer still hold the mds table, manually
    // skip getting mds table from caller
  } else if (OB_FAIL(inner_get_mds_table(mds_table))) {
    if (OB_ENTRY_NOT_EXIST == ret) {
      ret = OB_SUCCESS;
    } else {
      LOG_WARN("failed to get mds table", K(ret), K(tablet_id));
    }
  } else if (OB_FAIL(mds_table.get_rec_scn(rec_scn))) {
  } else if (OB_UNLIKELY(!rec_scn.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get invalid scn from mds table", K(ret));
  }
  return ret;
}

int ObTablet::mds_table_flush(const share::SCN &decided_scn)
{
  int ret = OB_SUCCESS;
  mds::MdsTableHandle mds_table;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", KR(ret), K_(is_inited));
  } else if (is_ls_inner_tablet()) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("inner tablet does not have mds table", K(ret));
  } else if (OB_FAIL(inner_get_mds_table(mds_table))) {
  } else if (OB_FAIL(mds_table.flush(decided_scn, decided_scn))) {
  }
  return ret;
}

int ObTablet::scan_mds_table_with_op(
    const int64_t mds_construct_sequence,
    ObMdsMiniMergeOperator &op) const
{
  TIMEGUARD_INIT(STORAGE, 10_ms);
  int ret = OB_SUCCESS;
  mds::MdsTableHandle mds_table_handle;
  const common::ObTabletID &tablet_id = get_tablet_id();
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", KR(ret), K_(is_inited));
  } else if (is_ls_inner_tablet()) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("inner tablet does not have mds table", K(ret));
  } else if (OB_FAIL(get_mds_table_for_dump(mds_table_handle))) {
    if (OB_EMPTY_RESULT == ret) {
      LOG_INFO("mds table does not exist, may be released", K(ret));
    } else {
      LOG_WARN("failed to get mds table", K(ret), KPC(this));
    }
  } else if (CLICK_FAIL((mds_table_handle.scan_all_nodes_to_dump<mds::ScanRowOrder::ASC, mds::ScanNodeOrder::FROM_NEW_TO_OLD>(
      op, mds_construct_sequence, op.for_flush())))) {
    LOG_WARN("failed to traverse mds table", K(ret), K(tablet_id));
  } else if (CLICK_FAIL(op.finish())) {
    LOG_WARN("Fail to finish dump op", K(ret), K(tablet_id));
  }
  return ret;
}

int ObTablet::get_valid_last_major_column_count(int64_t &last_major_column_cnt) const
{
  int ret = OB_SUCCESS;
  last_major_column_cnt = 0;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", KR(ret), K_(is_inited));
  } else if (OB_UNLIKELY(get_last_major_column_count() < 0)) {
    ret = OB_ERR_UNEXPECTED;
    STORAGE_LOG(WARN, "invalid last major column count", K(ret), KPC(this));
  } else if (get_last_major_column_count() == 0) {
    if (tablet_meta_.table_store_flag_.with_major_sstable()) {
      ret = OB_EAGAIN;
    } else {
      ret = OB_ERR_UNEXPECTED;
    }
    STORAGE_LOG(WARN, "tablet has no major sstable", K(ret), KPC(this));
  } else {
    last_major_column_cnt = get_last_major_column_count();
  }
  return ret;
}

int ObTablet::get_updating_tablet_pointer_param(
    ObUpdateTabletPointerParam &param,
    const bool need_tablet_attr /*= true*/) const
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("this tablet hasn't be initialized", K(ret), KPC(this));
  } else if (need_tablet_attr && OB_FAIL(calc_tablet_attr(param.tablet_attr_))) {
    LOG_WARN("fail to calculate tablet attributes", K(ret));
  } else {
    param.tablet_addr_ = tablet_addr_;
  }
  return ret;
}

int ObTablet::calc_tablet_attr(ObTabletAttr &attr) const
{
  int ret = OB_SUCCESS;
  attr.reset();
  attr.is_empty_shell_ = table_store_addr_.addr_.is_none();
  attr.has_next_tablet_ = tablet_meta_.has_next_tablet_;
  attr.local_status_ = tablet_meta_.local_status_.get_local_status();
  attr.has_nested_table_ = false;

  // calc space_usage
  attr.all_sstable_data_required_size_ = tablet_meta_.space_usage_.all_sstable_data_required_size_;
  attr.all_sstable_data_occupy_size_ = tablet_meta_.space_usage_.all_sstable_data_occupy_size_;
  attr.tablet_meta_size_ = tablet_meta_.space_usage_.all_sstable_meta_size_ + tablet_meta_.space_usage_.tablet_clustered_meta_size_;
  if (!attr.is_empty_shell_) {
    ObTabletMemberWrapper<ObTabletTableStore> wrapper;
    const ObTabletTableStore *table_store = nullptr;
    ObTableStoreIterator table_iter;
    if (OB_FAIL(fetch_table_store(wrapper))) {
    } else if (OB_FAIL(wrapper.get_member(table_store))) {
    } else if (OB_FAIL(table_store->get_all_sstable(table_iter))) {
    } else {
      ObITable *table = nullptr;
      while (OB_SUCC(ret) && OB_SUCC(table_iter.get_next(table))) {
        if (OB_ISNULL(table) || OB_UNLIKELY(!table->is_sstable())) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unexpected table", K(ret), KP(table));
        } else if (static_cast<ObSSTable *>(table)->is_small_sstable()) {
          attr.has_nested_table_ = true;
          break;
        }
      }
      if (OB_ITER_END == ret) {
        ret = OB_SUCCESS;
      }
    }
  }
  if (OB_SUCC(ret)) {
    attr.valid_ = true;
  }

  return ret;
}

int ObTablet::check_and_set_initial_state()
{
  int ret = OB_SUCCESS;
  const ObTabletID &tablet_id = tablet_meta_.tablet_id_;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else {
    // for normal tablet(except ls inner tablet), if mds_checkpoint_scn equals initial SCN(value is 1),
    // it means all kinds of mds data(including tablet status) has never been dumped to disk,
    // then we think that this tablet is in initial state
    bool initial_state = true;
    if (is_ls_inner_tablet()) {
      initial_state = false;
    } else {
      initial_state = (tablet_meta_.mds_checkpoint_scn_ == ObTabletMeta::INIT_CLOG_CHECKPOINT_SCN);
    }

    if (initial_state) {
      // do nothing
    } else if (OB_FAIL(set_initial_state(false/*initial_state*/))) {
    } else {
    }
  }

  return ret;
}

int ObTablet::check_medium_list() const
{
  int ret = OB_SUCCESS;
  const common::ObTabletID &tablet_id = tablet_meta_.tablet_id_;
  if (tablet_meta_.local_status_.check_allow_read()) {
    ObTabletMemberWrapper<ObTabletTableStore> table_store_wrapper;
    ObITable *last_major = nullptr;
    if (OB_FAIL(fetch_table_store(table_store_wrapper))) {
    } else if (OB_NOT_NULL(last_major = table_store_wrapper.get_member()->get_major_sstables().get_boundary_table(true/*last*/))) {
      ObArenaAllocator allocator("check_medium", OB_MALLOC_NORMAL_BLOCK_SIZE);
      if (share::is_reserve_mode()) {
        allocator.set_ctx_id(ObCtxIds::MERGE_RESERVE_CTX_ID);
      }
      common::ObSEArray<compaction::ObMediumCompactionInfo*, 1> medium_info_array;
      if (OB_FAIL(read_medium_array(allocator, medium_info_array))) {
      } else if (OB_FAIL(ObMediumListChecker::validate_medium_info_list(
          tablet_meta_.extra_medium_info_,
          &medium_info_array,
          last_major->get_snapshot_version()))) {
      }

      // always free medium info
      for (int64_t i = 0; i < medium_info_array.count(); ++i) {
        compaction::ObMediumCompactionInfo *&medium_info = medium_info_array.at(i);
        ObTabletObjLoadHelper::free(allocator, medium_info);
      }
    }
  } else {
    LOG_INFO("skip medium list check while local tablet data is not readable",
        KR(ret), K(tablet_id), "local_status", tablet_meta_.local_status_);
  }
  return ret;
}

int ObTablet::prepare_param(
    ObRelativeTable &relative_table,
    ObTableIterParam &param)
{
  int ret = OB_SUCCESS;

  param.table_id_ = relative_table.get_table_id();
  param.tablet_id_ = tablet_meta_.tablet_id_;
  param.read_info_ = rowkey_read_info_;
  param.set_tablet_handle(relative_table.get_tablet_handle());
  param.is_non_unique_local_index_ = relative_table.is_storage_index_table() &&
            relative_table.is_index_local_storage() && !relative_table.is_unique_index() && !relative_table.is_vector_index();
  return ret;
}

int ObTablet::pre_check_empty_shell(const ObTablet &old_tablet, ObTabletCreateDeleteMdsUserData &user_data)
{
  int ret = OB_SUCCESS;
  const common::ObTabletID &tablet_id = old_tablet.get_tablet_meta().tablet_id_;
  mds::MdsWriter writer;// will be removed later
  mds::TwoPhaseCommitState trans_stat;// will be removed later
  share::SCN trans_version;// will be removed later

  if (OB_FAIL(old_tablet.get_latest(user_data, writer, trans_stat, trans_version))) {
    if (OB_EMPTY_RESULT == ret) {
      mds::MdsTableHandle mds_table;
      share::SCN rec_scn;
      if (OB_FAIL(old_tablet.inner_get_mds_table(mds_table, false/*not_exist_create*/))) {
      } else if (OB_FAIL(mds_table.get_rec_scn(rec_scn))) {
      } else if (OB_UNLIKELY(rec_scn.is_max())) {
        ret = OB_STATE_NOT_MATCH;
        LOG_WARN("mds table exists, but rec scn is max, such kind tablet should be deleted instantly",
            K(ret), K(tablet_id));
      } else if (OB_FAIL(build_user_data_for_aborted_tx_tablet(rec_scn, user_data))) {
      }
    } else {
      LOG_WARN("failed to get latest tablet status", K(ret), K(tablet_id));
    }
  } else if (mds::TwoPhaseCommitState::ON_COMMIT != trans_stat || !user_data.tablet_status_.is_deleted_for_gc()) {
    ret = OB_STATE_NOT_MATCH;
    LOG_WARN("tablet is not under deleted tx committed status", K(ret), K(tablet_id), K(trans_stat), K(user_data));
  }

  return ret;
}

int ObTablet::build_user_data_for_aborted_tx_tablet(
    const share::SCN &flush_scn,
    ObTabletCreateDeleteMdsUserData &user_data)
{
  int ret = OB_SUCCESS;
  user_data.tablet_status_ = ObTabletStatus::DELETED;
  user_data.delete_commit_scn_ = flush_scn;
  return ret;
}

int ObTablet::prepare_param_ctx(
    ObIAllocator &allocator,
    ObRelativeTable &relative_table,
    ObStoreCtx &ctx,
    ObTableIterParam &param,
    ObTableAccessContext &context)
{
  int ret = OB_SUCCESS;
  ObVersionRange trans_version_range;
  const bool read_latest = true;
  ObQueryFlag query_flag;

  trans_version_range.base_version_ = 0;
  trans_version_range.multi_version_start_ = 0;
  trans_version_range.snapshot_version_ = EXIST_READ_SNAPSHOT_VERSION;
  query_flag.use_row_cache_ = ObQueryFlag::DoNotUseCache;
  query_flag.read_latest_ = read_latest & ObQueryFlag::OBSF_MASK_READ_LATEST;
  if (relative_table.is_storage_index_table()) {
    query_flag.index_invalid_ = !relative_table.can_read_index();
  }
  memtable::ObMvccMdsFilter mds_filter;
  mds_filter.truncate_part_filter_ = relative_table.get_truncate_part_filter();
  mds_filter.read_info_ = rowkey_read_info_;
  if (OB_FAIL(context.init(query_flag, ctx, allocator, trans_version_range, &mds_filter))) {
  } else if (OB_FAIL(prepare_param(relative_table, param))) {
  }
  return ret;
}

int64_t ObTablet::to_string(char *buf, const int64_t buf_len) const
{
  int64_t pos = 0;
  if (OB_ISNULL(buf) || OB_UNLIKELY(buf_len <= 0)) {
    // do nothing
  } else {
    J_OBJ_START();
    J_KV(KP(this),
         K_(is_inited),
         K_(wash_score),
         K_(hold_ref_cnt),
         K_(ref_cnt),
         K_(version),
         K_(length),
         K_(tablet_addr),
         KP_(allocator),
         K_(tablet_meta),
         K_(table_store_addr),
         K_(storage_schema_addr),
         K_(macro_info_addr),
         KP_(ddl_kvs),
         K_(ddl_kv_count),
         K_(is_external_tablet),
         K_(table_store_cache),
         KP_(rowkey_read_info));
    J_COMMA();
    BUF_PRINTF("memtables:");
    J_ARRAY_START();
    for (int64_t i = 0; i < MAX_MEMSTORE_CNT; ++i) {
      if (i > 0) {
        J_COMMA();
      }
      BUF_PRINTO(OB_P(memtables_[i]));
    }
    J_ARRAY_END();
    J_COMMA();
    J_KV(K_(memtable_count));
    J_OBJ_END();
  }
  return pos;
}

int ObTablet::refresh_memtable_and_update_seq(const uint64_t seq)
{
  int ret = OB_SUCCESS;
  if (table_store_addr_.is_memory_object()) {
    table_store_addr_.get_ptr()->clear_memtables();
  }
  reset_memtable();
  if (OB_FAIL(pull_memtables_without_ddl())) {
  } else {
    tablet_addr_.set_seq(seq);
    table_store_addr_.addr_.set_seq(seq);
    if (table_store_addr_.is_memory_object()) {
      ObSEArray<ObITable *, MAX_MEMSTORE_CNT> memtable_array;
      if (OB_FAIL(inner_get_memtables(memtable_array))) {
      } else if (OB_FAIL(table_store_addr_.get_ptr()->update_memtables(memtable_array))) {
      } else {
       LOG_INFO("table store update memtable success", KPC(table_store_addr_.get_ptr()), KP(this));
      }
    }
  }
  return ret;
}

int ObTablet::pull_memtables(ObArenaAllocator &allocator)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(pull_memtables_without_ddl())) {
  } else if (OB_FAIL(pull_ddl_memtables(allocator, ddl_kvs_, ddl_kv_count_))) {
  }
  return ret;
}

int ObTablet::pull_memtables_without_ddl()
{
  int ret = OB_SUCCESS;
  ObTableHandleArray memtable_handles;

  ObProtectedMemtableMgrHandle *protected_handle = NULL;
  if (OB_FAIL(get_protected_memtable_mgr_handle(protected_handle))) {
  } else if (!protected_handle->has_memtable()) {
  } else if (OB_FAIL(get_all_memtables_from_memtable_mgr(memtable_handles))) {
  } else {
    int64_t start_snapshot_version = get_snapshot_version();
    const SCN& clog_checkpoint_scn = tablet_meta_.clog_checkpoint_scn_;
    int64_t start_pos = -1;

    for (int64_t i = 0; OB_SUCC(ret) && i < memtable_handles.count(); ++i) {
      ObIMemtable *table = static_cast<ObIMemtable*>(memtable_handles.at(i).get_table());
      if (OB_ISNULL(table) || !table->is_memtable()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("table must not null and must be memtable", K(ret), K(table));
      } else if (table->is_resident_memtable()) { // Single full resident memtable will be available always
        LOG_INFO("is_resident_memtable will be pulled always", K(table->get_key().tablet_id_.id()));
        start_pos = i;
        break;
      } else if (table->get_end_scn() == clog_checkpoint_scn) {
        if (table->get_snapshot_version() > start_snapshot_version) {
          start_pos = i;
          break;
        }
      } else if (table->get_end_scn() > clog_checkpoint_scn) {
        start_pos = i;
        break;
      }
    }
    if (OB_FAIL(ret)) {
    } else if (start_pos < 0 || start_pos >= memtable_handles.count()) {
      // all memtables need to be released
      reset_memtable();
    } else if (OB_FAIL(build_memtable(memtable_handles, start_pos))) {
    }
  }
  return ret;
}

int ObTablet::update_memtables()
{
  int ret = OB_SUCCESS;
  ObTableHandleArray inc_memtables;

  ObProtectedMemtableMgrHandle *protected_handle = NULL;
  if (OB_FAIL(get_protected_memtable_mgr_handle(protected_handle))) {
  } else if (!protected_handle->has_memtable()) {
    LOG_INFO("no memtable in memtable mgr", K(ret), "tablet_id", tablet_meta_.tablet_id_);
  } else if (OB_FAIL(get_all_memtables_from_memtable_mgr(inc_memtables))) {
  } else if (is_ls_inner_tablet() && OB_FAIL(rebuild_memtable(inc_memtables))) {
    LOG_ERROR("failed to rebuild table store memtables for ls inner tablet", K(ret), K(inc_memtables), KPC(this));
  } else if (!is_ls_inner_tablet() && memtable_count_ > 0 && OB_FAIL(rebuild_memtable(inc_memtables))) {
    LOG_ERROR("failed to rebuild table store memtables for normal tablet when current memtable exists", K(ret), K(inc_memtables), KPC(this));
  } else if (!is_ls_inner_tablet() && memtable_count_ == 0 && OB_FAIL(rebuild_memtable(tablet_meta_.clog_checkpoint_scn_, inc_memtables))) {
    LOG_ERROR("failed to rebuild table store memtables for normal tablet when current memtable does not exist", K(ret),
        "clog_checkpoint_scn", tablet_meta_.clog_checkpoint_scn_,
        K(inc_memtables), KPC(this));
  }
  return ret;
}

int ObTablet::inner_get_mds_table(mds::MdsTableHandle &mds_table, bool not_exist_create) const
{
  int ret = OB_SUCCESS;
  ObTabletPointer *tablet_ptr = nullptr;
  if (!pointer_hdl_.is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet_pointer_hdl is in_valid", K(ret), K(pointer_hdl_));
    if (is_external_tablet()) {
      LOG_ERROR("is_external_tablet, and pointer_hdl_ is invalid", K(lbt()));
    }
  } else if (OB_ISNULL(tablet_ptr = static_cast<ObTabletPointer*>(pointer_hdl_.get_resource_ptr()))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet pointer is null", K(ret), KPC(this));
  } else if (OB_FAIL(tablet_ptr->get_mds_table(tablet_meta_.tablet_id_, mds_table, not_exist_create))) {
    if (OB_ENTRY_NOT_EXIST != ret) {
      LOG_WARN("failed to get mds table", K(ret));
    }
  }
  return ret;
}

int ObTablet::build_memtable(common::ObIArray<ObTableHandleV2> &handle_array, const int64_t start_pos)
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(start_pos < 0 || start_pos >= handle_array.count() || handle_array.count() > MAX_MEMSTORE_CNT)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("get invalid arguments", K(ret), K(start_pos), K(handle_array));
  }

  ObITable *table = nullptr;
  for (int64_t i = start_pos; OB_SUCC(ret) && i < handle_array.count(); ++i) {
    ObIMemtable *memtable = nullptr;
    table = handle_array.at(i).get_table();
    if (OB_UNLIKELY(nullptr == table || !table->is_memtable())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("table must be memtable", K(ret), K(i), KPC(table));
    } else if (FALSE_IT(memtable = static_cast<ObIMemtable *>(table))) {
    } else if (memtable->is_empty()) {
      FLOG_INFO("Empty memtable discarded", KPC(memtable));
    } else if (OB_FAIL(add_memtable(memtable))) {
    }
  }
  if (OB_FAIL(ret)) {
    if (OB_UNLIKELY(OB_SIZE_OVERFLOW == ret)) {
    } else {
      reset_memtable();
    }
  }
  return ret;
}

int ObTablet::read_mds_table(common::ObIAllocator &allocator,
                             ObTabletMdsData &mds_data,
                             const bool for_flush,
                             const int64_t mds_construct_sequence) const
{
  TIMEGUARD_INIT(STORAGE, 10_ms);
  int ret = OB_SUCCESS;
  const common::ObTabletID &tablet_id = tablet_meta_.tablet_id_;
  mds_data.reset();
  mds::MdsTableHandle mds_table_handle;

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else if (CLICK_FAIL(mds_data.init_for_first_creation())) {
    LOG_WARN("failed to init mds data", K(ret));
  } else if (CLICK_FAIL(inner_get_mds_table(mds_table_handle))) {
    if (OB_ENTRY_NOT_EXIST == ret) {
      ret = OB_EMPTY_RESULT;
      LOG_INFO("mds table does not exist, may be released", K(ret), K(tablet_id));
    } else {
      LOG_WARN("failed to get mds table", K(ret), K(tablet_id));
    }
  } else {
    ObTabletDumpMdsNodeOperator op(mds_data, allocator);
    if (CLICK_FAIL((mds_table_handle.scan_all_nodes_to_dump<mds::ScanRowOrder::ASC,
                                                            mds::ScanNodeOrder::FROM_OLD_TO_NEW>(op,
                                                                                                 mds_construct_sequence,
                                                                                                 for_flush)))) {
      LOG_WARN("failed to traverse mds table", K(ret), K(tablet_id));
    } else if (!op.dumped()) {
      ret = OB_EMPTY_RESULT;
    }
  }

  return ret;
}

int ObTablet::get_mds_table_for_dump(mds::MdsTableHandle &mds_table) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else if (OB_FAIL(inner_get_mds_table(mds_table, false/*not_exist_create*/))) {
    if (OB_ENTRY_NOT_EXIST == ret) {
      ret = OB_EMPTY_RESULT;
      LOG_INFO("mds table does not exist, may be released",
          K(ret), K(tablet_meta_.tablet_id_));
    } else {
      LOG_WARN("failed to get mds table", K(ret), K(tablet_meta_.tablet_id_));
    }
  }
  return ret;
}

int ObTablet::rebuild_memtable(common::ObIArray<ObTableHandleV2> &handle_array)
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_ERROR("ObTablet isn't inited", K(ret), KPC(this), K(handle_array));
  } else {
    int last_idx = memtable_count_ > 0 ? memtable_count_ - 1 : 0;
    ObITable *last_memtable = memtables_[last_idx];
    share::SCN end_scn = (NULL == last_memtable) ? share::SCN() : last_memtable->get_end_scn();

    for (int64_t i = 0; OB_SUCC(ret) && i < handle_array.count(); ++i) {
      ObIMemtable *memtable = nullptr;
      ObITable *table = handle_array.at(i).get_table();
      if (OB_UNLIKELY(nullptr == table || !table->is_memtable())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("table must be memtable", K(ret), K(i), KPC(table));
      } else if (FALSE_IT(memtable = static_cast<ObIMemtable *>(table))) {
      } else if (memtable->is_empty()) {
        FLOG_INFO("Empty memtable discarded", KPC(memtable));
      } else if (table->get_end_scn() < end_scn) {
      } else if (exist_memtable_with_end_scn(table, end_scn)) {
        FLOG_INFO("duplicated memtable with same end_scn discarded", KPC(table), K(end_scn));
      } else if (OB_FAIL(add_memtable(memtable))) {
      } else {
        LOG_INFO("succeed to add memtable", K(ret), KPC(memtable));
      }
    }
  }
  return ret;
}

int ObTablet::rebuild_memtable(
    const share::SCN &clog_checkpoint_scn,
    common::ObIArray<ObTableHandleV2> &handle_array)
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_ERROR("ObMemtableArray not inited", K(ret), KPC(this), K(handle_array));
  } else if (OB_UNLIKELY(0 != memtable_count_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("current memtable array is not empty", K(ret), K(clog_checkpoint_scn), K(memtable_count_));
  } else {
    // use clog checkpoint scn to filter memtable handle array
    for (int64_t i = 0; OB_SUCC(ret) && i < handle_array.count(); ++i) {
      ObIMemtable *memtable = nullptr;
      ObITable *table = handle_array.at(i).get_table();
      if (OB_UNLIKELY(nullptr == table || !table->is_memtable())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("table must be memtable", K(ret), K(i), KPC(table));
      } else if (FALSE_IT(memtable = static_cast<ObIMemtable *>(table))) {
      } else if (memtable->is_empty()) {
        FLOG_INFO("Empty memtable discarded", K(ret), KPC(memtable));
      } else if (table->get_end_scn() <= clog_checkpoint_scn) {
        FLOG_INFO("memtable end scn no greater than clog checkpoint scn, should be discarded", K(ret),
            "end_scn", table->get_end_scn(), K(clog_checkpoint_scn));
      } else if (OB_FAIL(add_memtable(memtable))) {
      }
    }
  }
  return ret;
}

int ObTablet::add_memtable(ObIMemtable* const table)
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(table)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("get invalid argument", K(ret), KPC(table));
  } else if (MAX_MEMSTORE_CNT == memtable_count_) {
    ret = OB_SIZE_OVERFLOW;
  } else {
    memtables_[memtable_count_]=table;
    memtable_count_++;
    table->inc_ref();
  }
  return ret;
}

bool ObTablet::exist_memtable_with_end_scn(const ObITable *table, const SCN &end_scn)
{
  // when frozen memtable's log was not committed, its right boundary is open (end_scn == MAX)
  // the right boundary would be refined asynchronuously
  // we need to make sure duplicate memtable was not added to tablet,
  // and ensure active memtable could be added to tablet
  bool is_exist = false;
  if (0 >= memtable_count_) {
  } else if (table->get_end_scn() == end_scn || end_scn.is_max()) {
    // Pay Attention!!!
    // The end scn of memtable can only be max or a certain value.
    for (int64_t i = memtable_count_ - 1; i >= 0 ; --i) {
      const ObIMemtable *memtable = memtables_[i];
      if (memtable == table) {
        is_exist = true;
        break;
      }
    }
  }
  return is_exist;
}

int ObTablet::assign_memtables(ObIMemtable * const * memtables, const int64_t memtable_count)
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(memtables) || OB_UNLIKELY(memtable_count < 0 || 0 != memtable_count_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid memtable argument", K(ret), KP(memtables), K(memtable_count));
  } else {
    MEMSET(memtables_, 0, sizeof(ObIMemtable*) * MAX_MEMSTORE_CNT);
    // deep copy memtables to tablet.memtables_ and inc ref
    for (int64_t i = 0; OB_SUCC(ret) && i < memtable_count; ++i) {
      ObIMemtable * memtable = memtables[i];
      if (OB_ISNULL(memtable)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected null memtable ptr", K(ret), K(i), KP(memtables));
      } else {
        memtables_[i] = memtable;
        memtable->inc_ref();
        ++memtable_count_;
      }
    }
  }

  return ret;
}

int ObTablet::assign_ddl_kvs(ObDDLKV * const *ddl_kvs, const int64_t ddl_kv_count)
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(OB_NOT_NULL(ddl_kvs) && ddl_kv_count == 0) ||
      OB_UNLIKELY(OB_ISNULL(ddl_kvs) && ddl_kv_count > 0) ||
      OB_UNLIKELY(ddl_kv_count < 0 || 0 != ddl_kv_count_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid ddl_kv argument", K(ret), KP(ddl_kvs), K(ddl_kv_count));
  } else {
    // deep copy ddl_kvs to tablet.ddl_kvs_ and inc ref
    for (int64_t i = 0; OB_SUCC(ret) && i < ddl_kv_count; ++i) {
      ObDDLKV *ddl_kv = ddl_kvs[i];
      if (OB_ISNULL(ddl_kv)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected ddl_kvs", K(ret), K(i), KP(ddl_kvs));
      } else {
        ddl_kvs_[i] = ddl_kv;
        ddl_kv->inc_ref();
        ++ddl_kv_count_;
      }
    }
  }

  return ret;
}

void ObTablet::reset_memtable()
{
  ObStorageMetaMemMgr *t3m = ::oceanbase::share::server_service<::oceanbase::storage::ObStorageMetaMemMgr>();
  for(int i = 0; i < MAX_MEMSTORE_CNT; ++i) {
    if (OB_NOT_NULL(memtables_[i])) {
      const int64_t ref_cnt = memtables_[i]->dec_ref();
      if (0 == ref_cnt) {
        t3m->push_table_into_gc_queue(memtables_[i],
                                      memtables_[i]->get_table_type());
      }
    }
    memtables_[i] = nullptr;
  }
  memtable_count_ = 0;
}

int ObTablet::clear_memtables_on_table_store() // be careful to call this func
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else if (OB_ISNULL(allocator_) || OB_ISNULL(table_store_addr_.get_ptr())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("clear_memtables_on_table_store can only be called by full memory tablet",
        K(ret), KP(allocator_), K(table_store_addr_));
  } else {
    table_store_addr_.get_ptr()->clear_memtables();
    reset_memtable();
  }
  return ret;
}

int ObTablet::get_restore_status(ObTabletRestoreStatus::STATUS &restore_status) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else if (OB_FALSE_IT(tablet_meta_.local_status_.get_restore_status(restore_status))) {
  } else if (!ObTabletRestoreStatus::is_valid(restore_status)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("restore status is invalid", KR(ret), K(restore_status));
  }

  return ret;
}

int ObTablet::get_mds_table_handle_(mds::MdsTableHandle &handle,
                                    const bool create_if_not_exist) const
{
  int ret = OB_SUCCESS;
  if (is_ls_inner_tablet()) {
    ret = OB_ENTRY_NOT_EXIST;// will continue read mds_data on tablet
  } else if (OB_FAIL(inner_get_mds_table(handle, create_if_not_exist))) {
    if (OB_ENTRY_NOT_EXIST != ret) {
      LOG_WARN("inner get mds table failed", KR(ret), "tablet_id", tablet_meta_.tablet_id_);
    } else if (REACH_THREAD_TIME_INTERVAL(10_s)) {
    }
  }
  return ret;
}

int ObTablet::pull_ddl_memtables(ObArenaAllocator &allocator, ObDDLKV **&ddl_kvs_addr, int64_t &ddl_kv_count)
{
  int ret = OB_SUCCESS;
  ObArray<ObITable *> ddl_memtables;
  ObDDLKvMgrHandle kv_mgr_handle;
  bool has_ddl_kv = false;
  ObArray<ObDDLKVHandle> ddl_kvs_handle;
  if (OB_UNLIKELY(0 != ddl_kv_count_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected ddl kv count when pull ddl memtables", K(ret), K(ddl_kv_count_), KPC(this));
  } else if (OB_FAIL(get_ddl_kv_mgr(kv_mgr_handle))) {
    if (OB_ENTRY_NOT_EXIST != ret) {
      LOG_WARN("get ddl kv mgr failed", K(ret), KPC(this));
    } else {
      ret = OB_SUCCESS;
    }
  } else if (OB_FAIL(kv_mgr_handle.get_obj()->get_ddl_kvs_for_query(*this, ddl_kvs_handle))) {
  } else {
    ObITable *temp_ddl_kvs;
    if (ddl_kvs_handle.count() > 0) {
      ddl_kvs_addr = static_cast<ObDDLKV **>(allocator.alloc(sizeof(ObDDLKV *) * DDL_KV_ARRAY_SIZE));
      if (OB_ISNULL(ddl_kvs_addr)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("failed to allocate memory for ddl_kvs_addr", K(ret), K(ddl_kvs_handle.count()));
      }
    }
    SCN ddl_checkpoint_scn = get_tablet_meta().ddl_checkpoint_scn_;
    for (int64_t i = 0; OB_SUCC(ret) && i < ddl_kvs_handle.count(); ++i) {
      ObDDLKV *ddl_kv = ddl_kvs_handle.at(i).get_obj();
      if (OB_ISNULL(ddl_kv)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("get ddl kv failed", K(ret), KP(ddl_kv));
      } else if (ddl_kv->get_freeze_scn() > ddl_checkpoint_scn) {
        if (OB_FAIL(ddl_kv->prepare_sstable(false/*need_check*/))) {
        } else if (OB_UNLIKELY(ddl_kv_count >= DDL_KV_ARRAY_SIZE)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("ddl kv count overflow", K(ret), K(i), K(ddl_kv_count), K(ddl_kvs_handle));
        } else {
          ddl_kvs_addr[ddl_kv_count] = ddl_kv;
          ddl_kv->inc_ref();
          ++ddl_kv_count;
        }
      }
    }
    LOG_INFO("pull ddl memtables", K(ret), K(ddl_kvs_handle), K(ddl_checkpoint_scn),
        K(ddl_kv_count), "ddl_kv", ObArrayWrap<ObDDLKV *>(ddl_kvs_addr, ddl_kv_count));
  }
  if (ddl_kv_count == 0) {
    // In the above for loop, ddl_kvs_addr's assignment can be skipped (e.g. ddl_kv->is_closed()).
    ddl_kvs_addr = nullptr;
  }
  if (OB_SUCC(ret) && OB_ISNULL(ddl_kvs_addr) && ddl_kv_count > 0) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected value on ddl_kvs_addr and ddl_kv_count", KP(ddl_kvs_addr), K(ddl_kv_count));
  }
  return ret;
}

void ObTablet::reset_ddl_memtables()
{
  for(int64_t i = 0; i < ddl_kv_count_; ++i) {
    ObDDLKV *ddl_kv = ddl_kvs_[i];
    const int64_t ref_cnt = ddl_kv->dec_ref();
    if (0 == ref_cnt) {
      ::oceanbase::share::server_service<::oceanbase::storage::ObStorageMetaMemMgr>()->release_ddl_kv(ddl_kv);
    } else if (OB_UNLIKELY(ref_cnt < 0)) {
      LOG_ERROR_RET(OB_ERR_UNEXPECTED, "table ref cnt may be leaked", K(ref_cnt), KP(ddl_kv));
    }
    ddl_kvs_[i] = nullptr;
  }
  ddl_kvs_ = nullptr;
  ddl_kv_count_ = 0;
}

int ObTablet::set_initial_state(const bool initial_state)
{
  int ret = OB_SUCCESS;
  ObTabletPointer *tablet_ptr = nullptr;
  if (!pointer_hdl_.is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet_pointer_hdl is in_valid", K(ret), K(pointer_hdl_));
    if (is_external_tablet()) {
      LOG_ERROR("is_external_tablet, and pointer_hdl_ is invalid", K(lbt()));
    }
  } else if (OB_ISNULL(tablet_ptr = static_cast<ObTabletPointer *>(pointer_hdl_.get_resource_ptr()))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet_pointer_hdl is in_valid", K(ret), K(pointer_hdl_));
  } else {
    tablet_ptr->set_initial_state(initial_state);
  }
  return ret;
}

int ObTablet::clear_ddl_memtables()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else {
    reset_ddl_memtables();
    tablet_addr_.inc_seq();
    table_store_addr_.addr_.inc_seq();
    if (table_store_addr_.is_memory_object()) {
      if (OB_FAIL(table_store_addr_.get_ptr()->clear_ddl_memtables())) {
      } else {
        LOG_INFO("table store clear ddl memtables success", KPC(this));
      }
    }
  }
  return ret;
}

int ObTablet::init_aggregated_info(
    common::ObArenaAllocator &allocator,
    ObLinkedMacroBlockItemWriter *linked_writer)
{
  int ret = OB_SUCCESS;
  ObTableStoreIterator iter;
  ObBlockInfoSet info_set;
  ObTabletPersister::SharedMacroMap shared_macro_map;
  if (OB_FAIL(info_set.init())) {
  } else if (OB_FAIL(shared_macro_map.create(
      ObTabletPersister::SHARED_MACRO_BUCKET_CNT, "SharedBlkMap", "SharedBlkNode"))) {
  } else if (OB_FAIL(inner_get_all_sstables(iter))) {
  } else {
    while (OB_SUCC(ret)) {
      ObITable *table = nullptr;
      ObSSTable *sstable = nullptr;
      ObSSTableMetaHandle meta_handle;
      if (OB_FAIL(iter.get_next(table))) {
        if (OB_UNLIKELY(OB_ITER_END == ret)) {
          ret = OB_SUCCESS;
          break;
        } else {
          LOG_WARN("fail to get next table from iter", K(ret), K(iter));
        }
      } else if (FALSE_IT(sstable = static_cast<ObSSTable *>(table))) {
      } else if (OB_ISNULL(sstable) || !sstable->is_valid()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("the sstable is null or invalid", K(ret), KPC(sstable));
      } else if (OB_FAIL(sstable->get_meta(meta_handle))) {
      } else if (sstable->is_small_sstable() && OB_FAIL(ObTabletPersister::copy_shared_macro_info(
          meta_handle.get_sstable_meta().get_macro_info(),
          shared_macro_map,
          info_set.meta_block_info_set_))) {
        LOG_WARN("fail to copy shared macro info", K(ret), K(meta_handle.get_sstable_meta()));
      } else if (!sstable->is_small_sstable() && OB_FAIL(ObTabletPersister::copy_data_macro_ids(
          meta_handle.get_sstable_meta().get_macro_info(),
          info_set))) {
        LOG_WARN("fail to copy sstable's macro ids", K(ret), K(meta_handle.get_sstable_meta()));
      }
      const ObMetaDiskAddr &sstable_addr = sstable->get_addr();
      if (OB_FAIL(ret)) {
        // do nothing
      } else if (sstable_addr.is_block()) {
        if (OB_FAIL(info_set.meta_block_info_set_.set_refactored(sstable_addr.block_id(), 0 /*whether to overwrite*/))) {
          if (OB_HASH_EXIST != ret) {
            LOG_WARN("fail to push macro id into set", K(ret), K(sstable_addr));
          } else {
            ret = OB_SUCCESS;
          }
        }
      }
    }
  }
  if (OB_FAIL(ret)) {
    // do nothing
  } else if (OB_FAIL(ObTabletPersister::convert_macro_info_map(
      shared_macro_map, info_set.shared_data_block_info_map_))) {
  } else {
    ALLOC_AND_INIT(allocator, macro_info_addr_, info_set, linked_writer);
  }
  if (OB_SUCC(ret)) {
    tablet_meta_.space_usage_.all_sstable_data_required_size_ =
        macro_info_addr_.ptr_->data_block_info_arr_.cnt_ * OB_DEFAULT_MACRO_BLOCK_SIZE;
    tablet_meta_.space_usage_.all_sstable_data_required_size_
        += macro_info_addr_.ptr_->shared_data_block_info_arr_.cnt_ * OB_DEFAULT_MACRO_BLOCK_SIZE;
  }
  return ret;
}

int ObTablet::calc_sstable_occupy_size(
    int64_t &all_sstable_occupy_size)
{
  int ret = OB_SUCCESS;
  all_sstable_occupy_size = 0;
  ObTableStoreIterator iter;
  if (OB_FAIL(inner_get_all_sstables(iter))) {
  }

  while (OB_SUCC(ret)) {
    ObITable *table = nullptr;
    ObSSTable *sstable = nullptr;
    ObSSTableMetaHandle meta_handle;
    uint64_t cur_sstable_occupy_size = 0;
    if (OB_FAIL(iter.get_next(table))) {
      if (OB_UNLIKELY(OB_ITER_END == ret)) {
        ret = OB_SUCCESS;
        break;
      } else {
        LOG_WARN("fail to get next table from iter", K(ret), K(iter));
      }
    } else if (FALSE_IT(sstable = static_cast<ObSSTable *>(table))) {
    } else if (OB_ISNULL(sstable) || !sstable->is_valid()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("the sstable is null or invalid", K(ret), KPC(sstable));
    } else if (OB_FAIL(sstable->get_meta(meta_handle))) {
    } else if (!meta_handle.is_valid()) {
      LOG_WARN("meta_handle is not valid", K(ret), K(meta_handle), KPC(sstable));
    } else {
      cur_sstable_occupy_size = meta_handle.get_sstable_meta().get_occupy_size();
    }

    if (OB_SUCC(ret)) {
      all_sstable_occupy_size += cur_sstable_occupy_size;
    }
  }
  if (OB_FAIL(ret)) {
    all_sstable_occupy_size = 0;
  }
  return ret;
}

int ObTablet::check_new_mds_with_cache(
    const int64_t snapshot_version)
{
  int ret = OB_SUCCESS;
  const ObTabletID &tablet_id = get_tablet_meta().tablet_id_;
  bool r_valid = false;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else {
    {
      SpinRLockGuard guard(mds_cache_lock_);
      if (tablet_status_cache_.is_valid()) {
        if (OB_FAIL(ObTabletCreateDeleteHelper::check_read_snapshot_by_commit_version(
            *this, tablet_status_cache_.get_create_commit_version(), tablet_status_cache_.get_delete_commit_version(),
            snapshot_version, tablet_status_cache_.get_tablet_status()))) {
        }
        r_valid = true;
      }
    }
    if (OB_SUCC(ret) && !r_valid) {
      SpinWLockGuard guard(mds_cache_lock_);
      if (tablet_status_cache_.is_valid()) {
        if (OB_FAIL(ObTabletCreateDeleteHelper::check_read_snapshot_by_commit_version(
            *this, tablet_status_cache_.get_create_commit_version(), tablet_status_cache_.get_delete_commit_version(),
            snapshot_version, tablet_status_cache_.get_tablet_status()))) {
        }
      } else if (OB_FAIL(ObTabletCreateDeleteHelper::check_status_for_new_mds(*this, snapshot_version, tablet_status_cache_))) {
        if (OB_TABLET_NOT_EXIST != ret) {
          LOG_WARN("failed to check status for new mds", KR(ret), K(tablet_id), K(snapshot_version));
        }
      }
    }
  }

  return ret;
}

int ObTablet::check_tablet_status_for_read_all_committed()
{
  int ret = OB_SUCCESS;
  const ObTabletID &tablet_id = get_tablet_meta().tablet_id_;
  ObTabletCreateDeleteMdsUserData user_data;
  mds::MdsWriter writer;// will be removed later
  mds::TwoPhaseCommitState trans_stat;// will be removed later
  share::SCN trans_version;// will be removed later
  // first make sure tablet is in any committed state
  // then check if it is empty shell
  //if (OB_FAIL(get_tablet_status(share::SCN::max_scn(), user_data, ObTabletCommon::DEFAULT_GET_TABLET_DURATION_US/*timeout*/))) {
  if (OB_FAIL(get_latest_committed(user_data))) {
    if (OB_EMPTY_RESULT == ret) {
      ret = OB_TABLET_NOT_EXIST;
      LOG_WARN("tablet creation has not been committed, or has been roll backed", K(ret), K(tablet_id));
    } else if (OB_ERR_SHARED_LOCK_CONFLICT == ret) {
      if (OB_FAIL(get_latest(user_data, writer, trans_stat, trans_version))) {
        if (OB_EMPTY_RESULT == ret) {
          ret = OB_TABLET_NOT_EXIST;
          LOG_WARN("tablet creation has no been committed, or has been roll backed", K(ret), K(tablet_id));
        }
      } else if (mds::TwoPhaseCommitState::ON_COMMIT != trans_stat) {
        if (transaction::ObTransVersion::INVALID_TRANS_VERSION == user_data.create_commit_version_) {
          ret = OB_TABLET_NOT_EXIST;
          LOG_WARN("create commit version is invalid", K(ret), K(tablet_id), K(user_data));
        }
      }
    } else {
      LOG_WARN("failed to get tablet status", K(ret), K(tablet_id));
    }
  } else if (OB_UNLIKELY(is_empty_shell())) {
    ret = OB_TABLET_NOT_EXIST;
    LOG_WARN("tablet become empty shell", K(ret), K(tablet_id));
  }
  return ret;
}

int ObTablet::set_tablet_status(
    const ObTabletCreateDeleteMdsUserData &tablet_status,
    mds::MdsCtx &ctx)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else {
    SpinWLockGuard guard(mds_cache_lock_);
    if (OB_FAIL(ObITabletMdsInterface::set(tablet_status, ctx))) {
    } else {
      tablet_status_cache_.reset();
    }
  }
  return ret;
}

int ObTablet::replay_set_tablet_status(
    const share::SCN &scn,
    const ObTabletCreateDeleteMdsUserData &tablet_status,
    mds::MdsCtx &ctx)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else {
    SpinWLockGuard guard(mds_cache_lock_);
    if (OB_FAIL(ObITabletMdsInterface::replay(tablet_status, ctx, scn))) {
    } else {
      tablet_status_cache_.reset();
    }
  }
  return ret;
}

int ObTablet::check_schema_version_with_cache(const int64_t schema_version)
{
  int ret = OB_SUCCESS;
  const ObTabletID &tablet_id = get_tablet_meta().tablet_id_;
  bool r_valid = false;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else {
    {
      SpinRLockGuard guard(mds_cache_lock_);
      if (ddl_data_cache_.is_valid()) {
        if (OB_FAIL(check_schema_version(ddl_data_cache_, schema_version))) {
        }
        r_valid = true;
      }
    }

    if (OB_SUCC(ret) && !r_valid) {
      SpinWLockGuard guard(mds_cache_lock_);
      if (ddl_data_cache_.is_valid()) {
        if (OB_FAIL(check_schema_version(ddl_data_cache_, schema_version))) {
        }
      } else {
        ObTabletBindingMdsUserData tmp_ddl_data;
        ObDDLInfoCache tmp_ddl_data_cache;
        ObDDLInfoCache *candidate_cache = nullptr;
        mds::MdsWriter unused_writer;
        mds::TwoPhaseCommitState trans_stat;
        share::SCN unused_trans_version;
        if (OB_FAIL(ObITabletMdsInterface::get_latest(tmp_ddl_data, unused_writer, trans_stat, unused_trans_version))) {
          if (OB_EMPTY_RESULT == ret) {
            trans_stat = mds::TwoPhaseCommitState::ON_COMMIT;
            tmp_ddl_data.set_default_value(); // use default value
            ret = OB_SUCCESS;
          } else {
            LOG_WARN("failed to get latest ddl data", KR(ret));
          }
        }

        if (OB_FAIL(ret)) {
        } else if (mds::TwoPhaseCommitState::ON_COMMIT == trans_stat) {
          // already get valid tmp_ddl_data
        } else if (OB_FAIL(get_ddl_data(tmp_ddl_data))) {
        }

        if (OB_SUCC(ret)) {
          // only enable cache without any on going transaction during the write lock
          if (mds::TwoPhaseCommitState::ON_COMMIT == trans_stat) {
            ddl_data_cache_.set_value(tmp_ddl_data);
            candidate_cache = &ddl_data_cache_;
            LOG_INFO("refresh ddl data cache", K(ret), K(tablet_id), K(ddl_data_cache_));
          } else {
            tmp_ddl_data_cache.set_value(tmp_ddl_data);
            candidate_cache = &tmp_ddl_data_cache;
          }

          if (OB_FAIL(ret)) {
          } else if (OB_ISNULL(candidate_cache)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("ddl data cache is null", K(ret), KP(candidate_cache), K(tablet_id));
          } else if (OB_FAIL(check_schema_version(*candidate_cache, schema_version))) {
          }
        }
      }
    }
  }

  return ret;
}

/*static*/ int ObTablet::check_schema_version(const ObDDLInfoCache& ddl_info_cache, const int64_t schema_version)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(schema_version < ddl_info_cache.get_schema_version())) {
    ret = OB_SCHEMA_EAGAIN;
    LOG_WARN("use stale schema before ddl", K(ret), K(ddl_info_cache), K(schema_version));
  }
  return ret;
}

int ObTablet::check_snapshot_readable_with_cache(
    const int64_t snapshot_version,
    const int64_t schema_version,
    const int64_t timeout)
{
  int ret = OB_SUCCESS;
  const ObTabletID &tablet_id = tablet_meta_.tablet_id_;
  bool r_valid = false;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else {
    {
      SpinRLockGuard guard(mds_cache_lock_);
      if (ddl_data_cache_.is_valid()) {
        if (OB_FAIL(check_snapshot_readable(ddl_data_cache_, snapshot_version, schema_version))) {
        }
        r_valid = true;
      }
    }

    if (OB_SUCC(ret) && !r_valid) {
      SpinWLockGuard guard(mds_cache_lock_);
      if (ddl_data_cache_.is_valid()) {
        if (OB_FAIL(check_snapshot_readable(ddl_data_cache_, snapshot_version, schema_version))) {
        }
      } else {
        ObTabletBindingMdsUserData tmp_ddl_data;
        ObDDLInfoCache tmp_ddl_data_cache;
        ObDDLInfoCache *candidate_cache = nullptr;
        mds::MdsWriter unused_writer;
        mds::TwoPhaseCommitState trans_stat;
        share::SCN unused_trans_version;
        if (OB_FAIL(ObITabletMdsInterface::get_latest_ddl_data(tmp_ddl_data, unused_writer, trans_stat, unused_trans_version))) {
          if (OB_EMPTY_RESULT == ret) {
            trans_stat = mds::TwoPhaseCommitState::ON_COMMIT;
            tmp_ddl_data.set_default_value(); // use default value
            ret = OB_SUCCESS;
          } else {
            LOG_WARN("failed to get latest ddl data", KR(ret));
          }
        }

        if (OB_FAIL(ret)) {
        } else if (mds::TwoPhaseCommitState::ON_COMMIT == trans_stat) {
          // already get valid tmp_ddl_data
        } else if (OB_FAIL(get_ddl_data(tmp_ddl_data))) {
        }

        if (OB_SUCC(ret)) {
          // only enable cache without any on going transaction during the write lock
          if (mds::TwoPhaseCommitState::ON_COMMIT == trans_stat) {
            ddl_data_cache_.set_value(tmp_ddl_data);
            candidate_cache = &ddl_data_cache_;
            LOG_INFO("refresh ddl data cache", K(ret), K(tablet_id), K(ddl_data_cache_));
          } else {
            tmp_ddl_data_cache.set_value(tmp_ddl_data);
            candidate_cache = &tmp_ddl_data_cache;
          }

          if (OB_FAIL(ret)) {
          } else if (OB_ISNULL(candidate_cache)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("ddl data cache is null", K(ret), KP(candidate_cache), K(tablet_id));
          } else if (OB_FAIL(check_snapshot_readable(*candidate_cache, snapshot_version, schema_version))) {
          }
        }
      }
    }
  }

  return ret;
}

int ObTablet::check_snapshot_readable(const ObDDLInfoCache& ddl_info_cache, const int64_t snapshot_version, const int64_t schema_version)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ddl_info_cache.is_redefined() && snapshot_version >= ddl_info_cache.get_snapshot_version())) {
    ret = OB_SCHEMA_EAGAIN;
    LOG_WARN("read data after ddl, need to retry on new tablet", K(ret), K(snapshot_version), K(ddl_info_cache));
  } else if (OB_UNLIKELY(!ddl_info_cache.is_redefined() && snapshot_version < ddl_info_cache.get_snapshot_version())) {
    if (schema_version < ddl_info_cache.get_schema_version()) {
    } else {
      ret = OB_SNAPSHOT_DISCARDED;
      LOG_WARN("read data before ddl", K(ret), K(ddl_info_cache), K(snapshot_version), K(schema_version));
    }
  }
  return ret;
}

int ObTablet::get_sstable_column_checksum(
    const blocksstable::ObSSTable &sstable,
    common::ObIArray<int64_t> &column_checksums) const
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(sstable.fill_column_ckm_array(column_checksums))) {
  }
  return ret;
}

int ObTablet::set_ddl_info(
    const ObTabletBindingMdsUserData &ddl_info,
    mds::MdsCtx &ctx,
    const int64_t lock_timeout_us)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else {
    SpinWLockGuard guard(mds_cache_lock_);
    if (OB_FAIL(ObITabletMdsInterface::set(ddl_info, ctx, lock_timeout_us))) {
    } else {
      ddl_data_cache_.reset();
    }
  }
  return ret;
}

int ObTablet::set_ddl_complete(
    const mds::DummyKey &key,
    const ObTabletDDLCompleteMdsUserData &ddl_complete,
    mds::MdsCtx &ctx,
    const int64_t lock_timeout_us)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K(is_inited_));
  } else {
    SpinWLockGuard guard(mds_cache_lock_);
    if (OB_FAIL(set(key, ddl_complete, ctx, lock_timeout_us))) {
    }
  }
  return ret;
}

int ObTablet::replay_set_ddl_info(
    const share::SCN &scn,
    const ObTabletBindingMdsUserData &ddl_info,
    mds::MdsCtx &ctx)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else {
    SpinWLockGuard guard(mds_cache_lock_);
    if (OB_FAIL(replay(ddl_info, ctx, scn))) {
    } else {
      ddl_data_cache_.reset();
    }
  }
  return ret;
}

int ObTablet::replay_set_ddl_complete(
    const share::SCN &scn,
    const mds::DummyKey &key,
    const ObTabletDDLCompleteMdsUserData &ddl_complete,
    mds::MdsCtx &ctx)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K(is_inited_));
  } else {
    SpinWLockGuard guard(mds_cache_lock_);
    if (OB_FAIL(ObITabletMdsInterface::replay(key, ddl_complete, ctx, scn))) {
    }
  }
  return ret;
}

int ObTablet::set_truncate_info(
    const ObTruncateInfoKey &key,
    const ObTruncateInfo &value,
    mds::MdsCtx &ctx,
    const int64_t lock_timeout_us)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else {
    SpinWLockGuard guard(mds_cache_lock_);
    if (OB_FAIL(ObITabletMdsInterface::set(key, value, ctx, lock_timeout_us))) {
    } else {
      truncate_info_cache_.replay_truncate_info();
    }
  }
  return ret;
}

int ObTablet::replay_set_truncate_info(
    const share::SCN &scn,
    const ObTruncateInfoKey &key,
    const ObTruncateInfo &value,
    mds::MdsCtx &ctx)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else {
    SpinWLockGuard guard(mds_cache_lock_);
    if (OB_FAIL(ObITabletMdsInterface::replay(key, value, ctx, scn))) {
    } else {
      truncate_info_cache_.replay_truncate_info();
    }
  }
  return ret;
}

bool ObTablet::is_empty_shell() const
{
  return tablet_meta_.is_empty_shell_;
}

bool ObTablet::is_data_complete() const
{
  return !is_empty_shell()
      && tablet_meta_.local_status_.is_data_status_complete();
}

int ObTablet::check_valid(const bool ignore_local_status) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else {
    ret = inner_check_valid(ignore_local_status);
  }
  return ret;
}

// Check the medium list only when the local tablet data is readable.
int ObTablet::inner_check_valid(const bool ignore_local_status) const
{
  int ret = OB_SUCCESS;
  const bool need_check_local_status = tablet_meta_.local_status_.check_allow_read() && !ignore_local_status;
  if (need_check_local_status && OB_FAIL(check_medium_list())) {
    LOG_WARN("failed to check medium list", K(ret), KPC(this));
  } else if (OB_FAIL(check_sstable_column_checksum())) {
  }
  return ret;
}

int ObTablet::set_frozen_for_all_memtables()
{
  int ret = OB_SUCCESS;

  ObProtectedMemtableMgrHandle *protected_handle = NULL;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else if (OB_FAIL(get_protected_memtable_mgr_handle(protected_handle))) {
  } else if (OB_FAIL(protected_handle->set_frozen_for_all_memtables())) {
  }

  return ret;
}

int ObTablet::get_all_memtables_from_memtable_mgr(ObTableHdlArray &handle) const
{
  int ret = OB_SUCCESS;
  ObProtectedMemtableMgrHandle *protected_handle = NULL;
  if (OB_FAIL(get_protected_memtable_mgr_handle(protected_handle))) {
  } else if (OB_FAIL(protected_handle->get_all_memtables(handle))) {
  }
  return ret;
}

int ObTablet::get_boundary_memtable_from_memtable_mgr(ObTableHandleV2 &handle) const
{
  int ret = OB_SUCCESS;
  ObProtectedMemtableMgrHandle *protected_handle = NULL;
  if (OB_FAIL(get_protected_memtable_mgr_handle(protected_handle))) {
  } else if (OB_FAIL(protected_handle->get_boundary_memtable(handle))) {
    if (OB_ENTRY_NOT_EXIST != ret) {
      LOG_WARN("failed to get boundary memtable", K(ret));
    }
  }
  return ret;
}

int ObTablet::get_protected_memtable_mgr_handle(ObProtectedMemtableMgrHandle *&handle) const
{
  int ret = OB_SUCCESS;
  handle = NULL;
  ObTabletPointer *tablet_ptr = nullptr;
  if (!pointer_hdl_.is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet_pointer_hdl is in_valid", K(ret), K(pointer_hdl_));
    if (is_external_tablet()) {
      LOG_ERROR("is_external_tablet, and pointer_hdl_ is invalid", K(lbt()));
    }
  } else if (OB_ISNULL(tablet_ptr = static_cast<ObTabletPointer *>(pointer_hdl_.get_resource_ptr()))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet_pointer_hdl is in_valid", K(ret), K(pointer_hdl_));
  } else  {
    handle = &tablet_ptr->protected_memtable_mgr_handle_;
  }
  return ret;
}
const ObTabletPointerHandle& ObTablet::get_pointer_handle() const {
  int ret = OB_SUCCESS;
  if (is_external_tablet()) {
    LOG_WARN("is_external_tablet, should not hold tablet_pointer", K(pointer_hdl_), K(lbt()));
  }
  return pointer_hdl_;
}

ObTabletPointer* ObTablet::get_tablet_pointer_() const {
  int ret = OB_SUCCESS;
  if (is_external_tablet()) {
    LOG_WARN("is_external_tablet, should not hold tablet_pointer", K(pointer_hdl_), K(lbt()));
  }
  return static_cast<ObTabletPointer*>(pointer_hdl_.get_resource_ptr());
}

int ObTablet::get_ready_for_read_param(ObReadyForReadParam &param) const
{
  int ret = OB_SUCCESS;
  param.ddl_commit_scn_ = tablet_meta_.ddl_commit_scn_;
  param.clog_checkpoint_scn_ = tablet_meta_.clog_checkpoint_scn_;
  return ret;
}

int ObTablet::check_ready_for_read_if_need(const ObTablet &old_tablet)
{
  int ret = OB_SUCCESS;
  if (old_tablet.tablet_meta_.ddl_commit_scn_ == tablet_meta_.ddl_commit_scn_ &&
      old_tablet.tablet_meta_.clog_checkpoint_scn_ == tablet_meta_.clog_checkpoint_scn_) {
    // no change, nothing to do
  } else if (table_store_addr_.is_memory_object()) {
    if (OB_FAIL(table_store_addr_.get_ptr()->check_ready_for_read(*this))) {
    }
  } else {
    // invalid the cache to force reload table store from disk, for updating the ready_for_read flag
    table_store_addr_.addr_.inc_seq();
  }
  return ret;
}

int ObTablet::get_all_minor_sstables(ObTableStoreIterator &table_store_iter) const
{
  int ret = OB_SUCCESS;
  ObTabletMemberWrapper<ObTabletTableStore> table_store_wrapper;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret));
  } else if (OB_FAIL(fetch_table_store(table_store_wrapper))) {
  } else if (OB_FAIL(table_store_wrapper.get_member()->get_all_minor_sstables(table_store_iter))) {
  } else if (!table_store_addr_.is_memory_object()
      && OB_FAIL(table_store_iter.set_handle(table_store_wrapper.get_meta_handle()))) {
    LOG_WARN("fail to set storage meta handle", K(ret), K_(table_store_addr), K(table_store_wrapper));
  }
  return ret;
}

int ObTablet::get_sstable_read_info(
    const blocksstable::ObSSTable *sstable,
    const storage::ObITableReadInfo *&index_read_info) const
{
  int ret = OB_SUCCESS;
  index_read_info = NULL;
  if (OB_ISNULL(sstable)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("sstable should not be null", K(ret), KP(sstable));
  } else if (get_tablet_id() != sstable->get_key().tablet_id_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet id do not match", K(ret), "self_tablet_id", get_tablet_id(),
      "other_tablet_id", sstable->get_key().tablet_id_);
  } else if (sstable->is_mds_sstable()) {
    index_read_info = storage::ObMdsSchemaHelper::get_instance().get_rowkey_read_info();
  } else {
    index_read_info = &get_rowkey_read_info();
  }
  if (OB_SUCC(ret) && OB_ISNULL(index_read_info)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("index read info is null", K(ret), KPC(sstable), KP(index_read_info));
  }
  return ret;
}


int ObTablet::get_memtables(common::ObIArray<ObTableHandleV2> &memtables) const
{
  int ret = OB_SUCCESS;
  memtables.reset();
  ObTableHandleV2 memtable;
  ObStorageMetaMemMgr *t3m = ::oceanbase::share::server_service<::oceanbase::storage::ObStorageMetaMemMgr>();
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else if (OB_ISNULL(t3m)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("storage metadata memory manager should not be null", K(ret), KP(t3m));
  } else {
    common::SpinRLockGuard guard(memtables_lock_);
    for (int64_t i = 0; OB_SUCC(ret) && i < memtable_count_; ++i) {
      memtable.reset();
      if (OB_ISNULL(memtables_[i])) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("memtable must not null", K(ret), K(memtables_));
      } else if (OB_FAIL(memtable.set_table(memtables_[i], t3m, memtables_[i]->get_key().table_type_))) {
      } else if (OB_FAIL(memtables.push_back(memtable))) {
      }
    }
  }

  return ret;
}

int ObTablet::check_table_store_flag_match_with_table_store_(const ObTabletTableStore *table_store)
{
  int ret = OB_SUCCESS;
  ObTabletTableStoreFlag table_store_flag = tablet_meta_.table_store_flag_;
  if (!tablet_meta_.local_status_.is_none()) {
    // tablet meta is not none, do not check it
  } else if (OB_ISNULL(table_store)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("table store should not be null", K(ret), KP(table_store));
  } else if (table_store_flag.with_major_sstable() && table_store->get_major_sstables().empty()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("tablet table store flag is with major flag but tablet has no major sstable",
        K(ret), KPC(this), K(table_store_flag), KPC(table_store));
  }
  return ret;
}

int ObTablet::copy_embedded_meta_(
    const ObRootBlockInfo &block_info,
    common::ObIAllocator &allocator,
    storage::ObMetaDiskAddr &addr,
    char *&buf) const
{
  int ret = OB_SUCCESS;
  buf = nullptr;
  addr = block_info.get_addr();

  if (!block_info.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid embedded meta block", K(ret));
  } else if (!block_info.get_addr().is_memory()) {
    buf = nullptr;
  } else {
    int64_t buf_offset = 0;
    int64_t buf_size = 0;
    const char *orig_block_buf = nullptr;

    if (OB_FAIL(block_info.get_addr().get_mem_addr(buf_offset, buf_size))) {
    } else if (buf_size <= 0) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("root block buf size is unexpected", K(ret), K(buf_size), K(block_info));
    } else if (OB_ISNULL(orig_block_buf = block_info.get_orig_block_buf())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("orig block buf should not be NULL", K(ret), K(block_info));
    } else if (OB_ISNULL(buf = static_cast<char *>(allocator.alloc(buf_size)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to alloc buf", K(ret), K(buf_size));
    } else {
      MEMCPY(buf, orig_block_buf, buf_size);
    }
  }
  return ret;
}

} // namespace storage
} // namespace oceanbase
