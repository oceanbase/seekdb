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
#define PRINT_TS(x) (ObPrintTableStore(x))

#include "ob_tablet_table_store.h"
#include "storage/ddl/ob_tablet_ddl_kv.h"
#include "storage/concurrency_control/ob_multi_version_garbage_collector.h"
#include "storage/ddl/ob_direct_load_struct.h"
#include "storage/ddl/ob_ddl_merge_task.h"
#include "storage/ddl/ob_tablet_split_task.h"
#include "storage/compaction/ob_schedule_dag_func.h"
#include "storage/compaction/ob_compaction_util.h"

namespace oceanbase
{
using namespace blocksstable;
using namespace compaction;
using namespace oceanbase::share;
using namespace transaction;
namespace storage
{

ObTabletTableStore::ObTabletTableStore()
  : version_(TABLE_STORE_VERSION_V5),
    major_tables_(),
    minor_tables_(),
    ddl_sstables_(),
    meta_major_tables_(),
    mds_sstables_(),
    memtables_(),
    ddl_mem_sstables_(),
    major_ckm_info_(),
    memtables_lock_(),
    is_ready_for_read_(false),
    is_inited_(false)
{
#if defined(__x86_64__)
  static_assert(sizeof(ObTabletTableStore) == 408, "The size of ObTabletTableStore will affect the meta memory manager, and the necessity of adding new fields needs to be considered.");
#endif
}

ObTabletTableStore::~ObTabletTableStore()
{
  reset();
}

void ObTabletTableStore::reset()
{
  // all sub structs of table store will not actively release memory on reset
  major_tables_.reset();
  minor_tables_.reset();
  ddl_sstables_.reset();
  meta_major_tables_.reset();
  mds_sstables_.reset();
  memtables_.reset();
  ddl_mem_sstables_.reset();
  major_ckm_info_.reset();
  is_ready_for_read_ = false;
  is_inited_ = false;
}

int ObTabletTableStore::serialize(char *buf, const int64_t buf_len, int64_t &pos) const
{
  int ret = OB_SUCCESS;
  int64_t serialized_length = get_serialize_size();
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret));
  } else if (OB_ISNULL(buf) || OB_UNLIKELY(buf_len < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(buf_len));
  } else if (OB_FAIL(serialization::encode_i64(buf, buf_len, pos, version_))) {
    LOG_WARN("failed to serialize table_store_version", K(ret));
  } else if (OB_FAIL(serialization::encode_i64(buf, buf_len, pos, serialized_length))) {
    LOG_WARN("failed to seriazlie serialized_length", K(ret));
  } else {
    LST_DO_CODE(OB_UNIS_ENCODE,
        major_tables_,
        minor_tables_,
        ddl_sstables_,
        meta_major_tables_,
        mds_sstables_);
    if (OB_SUCC(ret) && version_ >= TABLE_STORE_VERSION_V4) {
      LST_DO_CODE(OB_UNIS_ENCODE,
        major_ckm_info_);
    }
  }
  return ret;
}

int ObTabletTableStore::deserialize(
    ObArenaAllocator &allocator,
    const ObTablet &tablet,
    const char *buf,
    const int64_t data_len,
    int64_t &pos)
{
  int ret = OB_SUCCESS;
  const share::ObLSID &ls_id = tablet.get_ls_id();
  const common::ObTabletID &tablet_id = tablet.get_tablet_id();
  const int start_pos = pos;
  int64_t serialized_length = 0;
  bool is_compat_deserialize = false;

  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("not init", K(ret));
  } else if (OB_ISNULL(buf) || OB_UNLIKELY(data_len < 0 || data_len < pos)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret));
  } else if (OB_FAIL(serialization::decode_i64(buf, data_len, pos, &version_))) {
    LOG_WARN("failed to deserialize table_store_version", K(ret));
  } else if (OB_UNLIKELY(version_ > TABLE_STORE_VERSION_V5 || version_ < TABLE_STORE_VERSION_V1)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected deserialized version", K(ret), K(data_len), K(pos), K_(version), KPHEX(buf, data_len));
  } else if (OB_FAIL(serialization::decode_i64(buf, data_len, pos, &serialized_length))) {
    LOG_WARN("failed to deserialize serialized_length", K(ret));
  } else if (FALSE_IT(is_compat_deserialize = (version_ < TABLE_STORE_VERSION_V3))) {
  } else if (pos - start_pos < serialized_length && OB_FAIL(major_tables_.deserialize(allocator, buf, data_len, pos, is_compat_deserialize))) {
    LOG_WARN("fail to deserialize major sstables", K(ret));
  } else if (pos - start_pos < serialized_length && OB_FAIL(minor_tables_.deserialize(allocator, buf, data_len, pos, is_compat_deserialize))) {
    LOG_WARN("fail to deserialize minor sstables", K(ret));
  } else if (pos - start_pos < serialized_length && OB_FAIL(ddl_sstables_.deserialize(allocator, buf, data_len, pos, is_compat_deserialize))) {
    LOG_WARN("fail to deserialize ddl sstables", K(ret));
  } else if (pos - start_pos < serialized_length && OB_FAIL(meta_major_tables_.deserialize(allocator, buf, data_len, pos, is_compat_deserialize))) {
    LOG_WARN("fail to deserialize meta major sstables", K(ret));
  } else if (pos - start_pos < serialized_length && OB_FAIL(mds_sstables_.deserialize(allocator, buf, data_len, pos, is_compat_deserialize))) {
    LOG_WARN("fail to deserialize mds sstables", K(ret));
  } else if (TABLE_STORE_VERSION_V4 <= version_ && OB_FAIL(major_ckm_info_.deserialize(allocator, buf, data_len, pos))) {
    LOG_WARN("fail to deserialize major ckm info", K(ret));
  } else if (OB_FAIL(build_memtable_array(tablet))) {
    LOG_WARN("fail to pull memtables from tablet", K(ret));
  } else if (OB_FAIL(pull_ddl_memtables(allocator, tablet))) {
    LOG_WARN("pull_ddl_memtables failed", K(ret));
  } else if (pos - start_pos < serialized_length) {
    LOG_WARN("old server may deserialize value written by new server", K(ret), K(pos), K(start_pos), K(serialized_length));
    pos = start_pos + serialized_length;
  }
  if (OB_SUCC(ret)) {
    version_ = TABLE_STORE_VERSION_V5;
    is_inited_ = true;
    if (OB_FAIL(check_ready_for_read(tablet))) {
      LOG_WARN("failed to check major sstable ready for read", K(ret));
    }
    if (!memtables_.empty()) {
      FLOG_INFO("succeed to deserialize table store", K(ls_id), K(tablet_id),
          K(major_tables_), K(minor_tables_), K(meta_major_tables_), K(mds_sstables_), K(memtables_), K(PRINT_TS(*this)));
    }
  }
  return ret;
}

int64_t ObTabletTableStore::get_serialize_size() const
{
  int64_t len = 0;
  len += serialization::encoded_length_i64(version_);
  len += serialization::encoded_length_i64(len);
  LST_DO_CODE(OB_UNIS_ADD_LEN,
      major_tables_,
      minor_tables_,
      ddl_sstables_,
      meta_major_tables_,
      mds_sstables_);
  if (version_ >= TABLE_STORE_VERSION_V4) {
    LST_DO_CODE(OB_UNIS_ADD_LEN, major_ckm_info_);
  }
  return len;
}

int ObTabletTableStore::init(
    ObArenaAllocator &allocator,
    const ObTablet &tablet,
    const ObSSTable *sstable,
    const ObMajorChecksumInfo *ckm_info)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObTabletTableStore init twice", K(ret), K(*this));
  } else if (OB_ISNULL(sstable)) {
    // skip
  } else if (OB_FAIL(build_memtable_array(tablet))) {
    LOG_WARN("failed to build memtable array", K(ret));
  } else if (OB_UNLIKELY(!sstable->is_major_sstable() || sstable->is_meta_major_sstable())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("get invalid table", K(ret), KPC(sstable));
  } else if (OB_FAIL(major_tables_.init(allocator, sstable))) {
    LOG_WARN("failed to init major tables", K(ret));
  } else {
    is_ready_for_read_ = true; // exist major sstable and no minor sstable, must be ready for read
    try_cache_local_sstables(allocator);
  }
  if (OB_FAIL(ret) || OB_ISNULL(ckm_info)) {
  } else if (OB_UNLIKELY(!ckm_info->is_valid() || !major_ckm_info_.is_empty())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("input checksum info is invalid or checksum info on tablet is not empty", KR(ret), KPC(ckm_info), K_(major_ckm_info));
  } else if (OB_FAIL(major_ckm_info_.assign(*ckm_info, &allocator))) {
    LOG_WARN("fail to assign major ckm info", K(ret), KPC(ckm_info));
  }

  if (OB_SUCC(ret)) {
    is_inited_ = true;
  }
  return ret;
}

int ObTabletTableStore::init(
    ObArenaAllocator &allocator,
    const ObTablet &tablet,
    const ObUpdateTableStoreParam &param,
    const ObTabletTableStore &old_store)
{
  int ret = OB_SUCCESS;
  int64_t inc_base_snapshot_version = -1;
  const ObTabletHAStatus &ha_status = tablet.get_tablet_meta().ha_status_;
  const ObSSTable *new_sstable = param.sstable_;

  if (is_valid() || get_table_count() > 0) {
    ret = OB_INIT_TWICE;
    LOG_WARN("double init", K(ret), KPC(this));
  } else if (OB_UNLIKELY(!param.is_valid() || !old_store.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid arguments", K(ret), K(param), K(old_store));
  } else if (OB_UNLIKELY(nullptr != new_sstable && !new_sstable->is_sstable())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected table type", K(ret), KPC(new_sstable));
  } else if (OB_FAIL(build_major_tables(allocator, param, old_store, inc_base_snapshot_version))) {
    LOG_WARN("failed to build major_tables", K(ret));
  } else if (OB_FAIL(build_minor_tables(allocator, param, old_store.minor_tables_, inc_base_snapshot_version, ha_status))) {
    if (OB_UNLIKELY(OB_NO_NEED_MERGE != ret)) {
      LOG_WARN("failed to build minor_tables", K(ret));
    }
  } else if (OB_FAIL(build_ddl_sstables(allocator, tablet, new_sstable, param.ddl_info_.slice_sstables_, param.ddl_info_.keep_old_ddl_sstable_, old_store))) {
    LOG_WARN("failed to add ddl minor sstable", K(ret));
  } else if (OB_FAIL(build_meta_major_table(allocator, new_sstable, old_store.meta_major_tables_))) {
    LOG_WARN("failed to build meta major tables", K(ret));
  } else if (OB_FAIL(build_memtable_array(tablet))) {
    LOG_WARN("failed to pull memtable from memtable_mgr", K(ret));
  } else if (OB_FAIL(pull_ddl_memtables(allocator, tablet))) {
    LOG_WARN("pull_ddl_memtables failed", K(ret));
  } else if (OB_FAIL(build_major_checksum_info(old_store, &param, allocator))) {
    LOG_WARN("failed to build major checksum info", KR(ret), K(param));
  } else if (OB_FAIL(build_mds_minor_tables(allocator, new_sstable, old_store.mds_sstables_))) {
    LOG_WARN("failed to build mds sstables", K(ret));
  } else {
    is_inited_ = true;
    if (OB_FAIL(check_ready_for_read(tablet))) {
      LOG_WARN("failed to check ready for read", K(ret));
    } else {
      try_cache_local_sstables(allocator);
      FLOG_INFO("succeed to build new table store", "tablet_id", tablet.get_tablet_id(), KP(this), K(major_tables_), K(minor_tables_), K(mds_sstables_), K(memtables_), K(PRINT_TS(*this)));
    }
  }
  return ret;
}

#ifdef OB_BUILD_SHARED_STORAGE
int ObTabletTableStore::init_for_shared_storage(
    common::ObArenaAllocator &allocator,
    const ObUpdateTableStoreParam &param)
  {
    int ret = OB_SUCCESS;
    const ObSSTable *new_table = param.sstable_;

    if (IS_INIT) {
      ret = OB_INIT_TWICE;
      LOG_WARN("double init", K(ret));
    } else if (OB_UNLIKELY(!param.is_valid())) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("Invalid arguments", K(ret), K(param));
    } else if (OB_UNLIKELY(!major_tables_.empty() || !minor_tables_.empty())) {
      ret = OB_ERR_SYS;
      LOG_ERROR("already exists sstable", K(ret), KPC(this));
    } else if (OB_ISNULL(new_table)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("sstable is nullptr", K(ret), K(param));
    } else if (OB_UNLIKELY(!ObITable::is_major_sstable(new_table->get_key().table_type_))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected table type", K(ret), KPC(new_table));
    } else if (OB_FAIL(major_tables_.init(allocator, new_table))) {
      LOG_WARN("failed to init major tables", K(ret));
    } else {
       // shared tablet meta, is_ready_for_read_ is not necessary.
       // just to avoid other validation failure
      is_ready_for_read_ = true;
      is_inited_ = true;
    }
    return ret;
  }

int ObTabletTableStore::process_minor_sstables_for_ss_(
    ObArenaAllocator &allocator,
    const UpdateUpperTransParam &upper_trans_param,
    ObArray<ObITable *> &sstables,
    const int64_t inc_base_snapshot_version,
    int64_t &inc_pos)
{
  int ret = OB_SUCCESS;
  const ObIArray<UpdateUpperTransParam::SCNAndVersion> *new_upper_trans = upper_trans_param.ss_new_upper_trans_;
  if (inc_pos < 0 || OB_ISNULL(new_upper_trans) || new_upper_trans->empty() || sstables.empty()) {
    // skip, inc_pos < 0 means has been processed by others
  } else if (new_upper_trans->count() == 1 && new_upper_trans->at(0).upper_trans_version_ == 0) {
    if (OB_FAIL(cut_minor_sstables_for_ss_(allocator, new_upper_trans->at(0).scn_, sstables, inc_pos))) {
      LOG_WARN("failed to cut minor sstables", K(ret));
    }
  } else if (OB_FAIL(process_minor_sstables_upper_trans_for_ss_(allocator, *new_upper_trans, sstables, inc_base_snapshot_version, inc_pos))) {
    LOG_WARN("failed to set minor sstables upper trans", K(ret));
  }
  return ret;
}

int ObTabletTableStore::cut_minor_sstables_for_ss_(
    ObArenaAllocator &allocator,
    const share::SCN &cut_scn,
    ObArray<ObITable *> &sstables,
    int64_t &inc_pos)
{
  int ret = OB_SUCCESS;
  for (int i = inc_pos; OB_SUCC(ret) && i < sstables.count(); i++) {
    if (sstables.at(i)->is_sstable()) {
      if (sstables.at(i)->get_start_scn() >= cut_scn) {
        break;
      } else if (sstables.at(i)->get_end_scn() <= cut_scn) {
        inc_pos = i + 1;
        FLOG_INFO("recycle minor by shared-min-scn", KPC(sstables.at(i)), K(i), K(cut_scn));
      } else if (sstables.at(i)->get_end_scn() > cut_scn) {
        // cross with cut_scn, modify start_scn to cut_scn
        ObSSTable *sstable = static_cast<ObSSTable*>(sstables.at(i));
        ObSSTable *copied_sstable = NULL;
        if (OB_FAIL(adjust_sstable_start_scn_(*sstable, allocator, cut_scn, copied_sstable))) {
          LOG_WARN("adjust start scn of local sstable fail", K(ret));
        } else {
          sstables.at(i) = copied_sstable;
          FLOG_INFO("adjust start scn of local sstable to cut_scn success", KPC(sstables.at(i)), K(i), K(cut_scn));
        }
      }
    }
  }
  return ret;
}

int ObTabletTableStore::process_minor_sstables_upper_trans_for_ss_(
    ObArenaAllocator &allocator,
    const ObIArray<UpdateUpperTransParam::SCNAndVersion> &new_upper_trans,
    ObArray<ObITable *> &sstables,
    const int64_t inc_base_snapshot_version,
    int64_t &inc_pos)
{
  int ret = OB_SUCCESS;
  int j = 0;
  bool found_first = false;
  bool need_chg_upper_trans_version = false;
  ObSSTableArray &new_sstables = minor_tables_;
  for(int i = inc_pos; i < sstables.count(); i++) {
    if (sstables.at(i)->is_sstable()) {
      ObSSTable *sstable = static_cast<ObSSTable*>(sstables.at(i));
      if (sstable->get_upper_trans_version() == INT64_MAX) {
        bool hit = false;
        for (; j < new_upper_trans.count(); j++) {
          if (new_upper_trans.at(j).scn_ == sstable->get_end_scn()) {
            hit = true;
            if (new_upper_trans.at(j).upper_trans_version_ <= inc_base_snapshot_version) {
              if (!found_first) { inc_pos = i + 1; } // remove this minor
            } else {
              found_first = true;
              need_chg_upper_trans_version |= new_upper_trans.at(j).upper_trans_version_ != INT64_MAX;
            }
          } else if (new_upper_trans.at(j).scn_ > sstable->get_end_scn()) {
            break;
          }
        }
        if (!hit) {
          found_first = true;
        }
      } else if (sstable->get_upper_trans_version() <= inc_base_snapshot_version) {
        if (!found_first) { inc_pos = i + 1; } // remove this minor
      } else {
        found_first = true;
      }
    }
  }
  if (need_chg_upper_trans_version) {
    if (FAILEDx(new_sstables.init(allocator, sstables, inc_pos))) {
      LOG_WARN("failed to init minor_tables", K(ret));
    } else if (new_sstables.count() >= MAX_SSTABLE_CNT_IN_STORAGE) {
      ret = OB_MINOR_MERGE_NOT_ALLOW;
      LOG_WARN("too many sstables, cannot add new minor sstable", K(ret));
    } else {
      int j = 0;
      for (int64_t i = 0; i < new_sstables.count(); i++) {
        ObSSTable *sstable = new_sstables.at(i);
        if (sstable->get_upper_trans_version() == INT64_MAX) {
          for (;  j < new_upper_trans.count(); j++) {
            if (new_upper_trans.at(j).scn_ == sstable->get_end_scn()) {
              if (new_upper_trans.at(j).upper_trans_version_ != INT64_MAX) {
                sstable->set_upper_trans_version(allocator, new_upper_trans.at(j).upper_trans_version_);
              }
            } else if (new_upper_trans.at(j).scn_ > sstable->get_end_scn()) {
              break;
            }
          }
        }
      }
    }
    inc_pos = -1; // has created minor_sstables, skip process in caller
  } else if (inc_pos == sstables.count()) {
    inc_pos = -1; // all minor removed
  }
  return ret;
}
#endif


#define INIT_SSTABLE_ARRAY_WITH_ADDR(table_array, is_right_table_type)                                \
  if (OB_FAIL(ret)) {                                                                                 \
  } else if (idx < sstable_cnt - 1 && is_right_table_type(sstables.at(idx + 1)->get_table_type())) {  \
    continue;                                                                                         \
  } else if (OB_FAIL(table_array.init(allocator, sstables, addrs, start_pos, idx - start_pos + 1))) { \
    LOG_WARN("failed to init sstable array", K(ret), K(start_pos), K(idx), K(sstables));              \
  } else {                                                                                            \
    start_pos = idx + 1;                                                                              \
  }

// the order of sstables in sstable array is kept consistent with that in get_all_sstable
// [meta, major, inc major, minor, ddl, mds]
int ObTabletTableStore::init(
    ObArenaAllocator &allocator,
    common::ObIArray<ObITable *> &sstables,
    common::ObIArray<ObMetaDiskAddr> &addrs,
    const ObMajorChecksumInfo &major_ckm_info)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("double init", K(ret));
  } else if (OB_UNLIKELY(sstables.count() != addrs.count())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid sstable array", K(ret), K(sstables.count()), K(addrs.count()));
  }

  const int64_t sstable_cnt = sstables.count();
  int64_t start_pos = 0;
  for (int64_t idx = 0; OB_SUCC(ret) && idx < sstable_cnt; ++idx) {
    ObITable *cur_table = sstables.at(idx);

    if (OB_UNLIKELY(nullptr == cur_table || !cur_table->is_sstable())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected table", K(ret), KPC(cur_table));
    } else if (cur_table->is_meta_major_sstable()) {
      if (OB_UNLIKELY(!major_tables_.empty() || !minor_tables_.empty() || !ddl_sstables_.empty() || !mds_sstables_.empty())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("get unexpected table seq", K(ret), K(sstables));
      }
      INIT_SSTABLE_ARRAY_WITH_ADDR(meta_major_tables_, ObITable::is_meta_major_sstable);
    } else if (cur_table->is_major_sstable()) {
      if (OB_UNLIKELY(!minor_tables_.empty() || !ddl_sstables_.empty() || !mds_sstables_.empty())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("get unexpected table seq", K(ret), K(sstables));
      }
      INIT_SSTABLE_ARRAY_WITH_ADDR(major_tables_, ObITable::is_major_sstable);
    } else if (cur_table->is_minor_sstable()) { // no need to check table cnt because only tablet persist will call this func
      if (OB_UNLIKELY(!ddl_sstables_.empty() || !mds_sstables_.empty())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("get unexpected table seq", K(ret), K(sstables));
      }
      INIT_SSTABLE_ARRAY_WITH_ADDR(minor_tables_, ObITable::is_minor_sstable);
    } else if (cur_table->is_ddl_sstable()) {
      if (OB_UNLIKELY(!mds_sstables_.empty())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("get unexpected table seq", K(ret), K(sstables));
      }
      INIT_SSTABLE_ARRAY_WITH_ADDR(ddl_sstables_, ObITable::is_ddl_sstable);
    } else if (cur_table->is_mds_sstable()) {
      INIT_SSTABLE_ARRAY_WITH_ADDR(mds_sstables_, ObITable::is_mds_sstable);
    }
  } // end for

  if (FAILEDx(major_ckm_info_.assign(major_ckm_info, &allocator))) {
    LOG_WARN("fail to assign major ckm info", K(ret), K(major_ckm_info));
  } else {
    LOG_INFO("success to assign major ckm info", K(ret), K(major_ckm_info));
    is_ready_for_read_ = false; // can not read temp table store for serialize
    is_inited_ = true;
  }
  return ret;
}

#undef INIT_SSTABLE_ARRAY_WITH_ADDR

// when replace sstable array is null, init new table store with tables in old store directly
int ObTabletTableStore::init(
    ObArenaAllocator &allocator,
    const ObTablet &tablet,
    const ObTabletTableStore &old_store,
    const ObIArray<ObITable *> *replace_sstable_array)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("double init", K(ret));
  } else if (OB_UNLIKELY(!old_store.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("init tablet table store get invalid argument", K(ret), K(tablet), K(old_store));
  } else if (nullptr == replace_sstable_array) {
    // do nothing, assign new table store with old tables directly
  } else {
    ObSSTableWrapper tmp_wrapper;
    for (int64_t i = 0; OB_SUCC(ret) && i < replace_sstable_array->count(); ++i) {
      // check if all input tables exist in the old store
      const ObITable *table = replace_sstable_array->at(i);
      if (OB_UNLIKELY(nullptr == table || !table->is_sstable())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("table must be sstable", K(ret), KPC(table));
      } else if (OB_FAIL(old_store.get_sstable(table->get_key(), tmp_wrapper))) {
        LOG_WARN("failed to get the same key sstable in old store", K(ret), KPC(table), K(old_store));
      }
    }
  }

  if (FAILEDx(replace_sstables(allocator, replace_sstable_array, old_store.major_tables_, major_tables_))) {
    LOG_WARN("failed to get replaced major tables", K(ret));
  } else if (OB_FAIL(replace_sstables(allocator, replace_sstable_array, old_store.minor_tables_, minor_tables_))) {
    LOG_WARN("failed to get replaced minor tables", K(ret));
  } else if (OB_FAIL(replace_sstables(allocator, replace_sstable_array, old_store.mds_sstables_, mds_sstables_))) {
    LOG_WARN("failed to get replaced mds tables", K(ret));
  } else if (OB_FAIL(replace_sstables(allocator, replace_sstable_array, old_store.ddl_sstables_, ddl_sstables_))) {
    LOG_WARN("failed to get replaced ddl tables", K(ret));
  } else if (OB_FAIL(meta_major_tables_.init(allocator, old_store.meta_major_tables_))) {
    LOG_WARN("failed to init meta major tables for new table store", K(ret));
  } else if (OB_FAIL(build_memtable_array(tablet))) {
    LOG_WARN("failed to pull memtable from memtable_mgr", K(ret));
  } else if (OB_FAIL(pull_ddl_memtables(allocator, tablet))) {
    LOG_WARN("pull_ddl_memtables failed", K(ret));
  } else if (OB_FAIL(build_major_checksum_info(old_store, NULL/*param*/, allocator))) {
    LOG_WARN("failed to build major checksum info", KR(ret), K(old_store));
  } else {
    is_inited_ = true;
    if (OB_FAIL(check_ready_for_read(tablet))) {
      LOG_WARN("failed to check ready for read", K(ret));
    } else {
      try_cache_local_sstables(allocator);
      FLOG_INFO("succeed to assign table store with replace sstable array", K(replace_sstable_array),
                K(major_tables_), K(minor_tables_), K(memtables_), K(PRINT_TS(*this)));
    }
  }
  return ret;
}

int ObTabletTableStore::replace_sstables(
    common::ObArenaAllocator &allocator,
    const ObIArray<ObITable *> *replace_sstable_array,
    const ObSSTableArray &old_tables,
    ObSSTableArray &new_tables) const
{
  int ret = OB_SUCCESS;
  ObSEArray<ObITable *, OB_DEFAULT_SE_ARRAY_COUNT> replaced_tables;

  if (OB_FAIL(old_tables.get_all_tables(replaced_tables))) {
    LOG_WARN("failed to get all table from old tables", K(ret), K(old_tables));
  } else if (nullptr == replace_sstable_array) {
    // do nothing, use old tables to init new table array directly
  } else {
    for (int64_t idx = 0; OB_SUCC(ret) && idx < replaced_tables.count(); ++idx) {
      for (int64_t pos = 0; OB_SUCC(ret) && pos < replace_sstable_array->count(); ++pos) {
        if (replaced_tables.at(idx)->get_key() == replace_sstable_array->at(pos)->get_key()) {
          replaced_tables.at(idx) = replace_sstable_array->at(pos);
          break;
        }
      }
    }
  }

  if (FAILEDx(new_tables.init(allocator, replaced_tables))) {
    LOG_WARN("failed to init sstable array", K(ret), K(new_tables), K(replaced_tables));
  }
  return ret;
}

int64_t ObTabletTableStore::get_deep_copy_size() const
{
  return get_try_cache_size() + mds_sstables_.get_deep_copy_size();
}

// TODO(@jinzhu) adapt mds sstable
int64_t ObTabletTableStore::get_try_cache_size() const
{
  return sizeof(ObTabletTableStore)
        + major_tables_.get_deep_copy_size()
        + minor_tables_.get_deep_copy_size()
        + ddl_mem_sstables_.get_deep_copy_size()
        + ddl_sstables_.get_deep_copy_size()
        + meta_major_tables_.get_deep_copy_size()
        + major_ckm_info_.get_deep_copy_size();
}

int ObTabletTableStore::deep_copy(
    char *buf,
    const int64_t buf_len,
    ObIStorageMetaObj *&value) const
{
  int ret = OB_SUCCESS;
  int64_t pos = 0;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_ISNULL(buf) || OB_UNLIKELY(buf_len < get_deep_copy_size())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(buf), K(buf_len));
  } else {
    ObTabletTableStore *new_table_store = new (buf) ObTabletTableStore();
    pos = sizeof(ObTabletTableStore);
    if (OB_FAIL(major_tables_.deep_copy(buf, buf_len, pos, new_table_store->major_tables_))) {
      LOG_WARN("fail to deep copy major sstables", K(ret));
    } else if (OB_FAIL(minor_tables_.deep_copy(buf, buf_len, pos, new_table_store->minor_tables_))) {
      LOG_WARN("fail to deep copy minor sstables", K(ret));
    } else if (OB_FAIL(ddl_mem_sstables_.deep_copy(buf, buf_len, pos, new_table_store->ddl_mem_sstables_))) {
      LOG_WARN("fail to deep copy ddl mem sstables", K(ret));
    } else if (OB_FAIL(ddl_sstables_.deep_copy(buf, buf_len, pos, new_table_store->ddl_sstables_))) {
      LOG_WARN("fail to deep copy ddl sstables", K(ret));
    } else if (OB_FAIL(meta_major_tables_.deep_copy(buf, buf_len, pos, new_table_store->meta_major_tables_))) {
      LOG_WARN("fail to deep copy meta major sstables", K(ret));
    } else if (OB_FAIL(mds_sstables_.deep_copy(buf, buf_len, pos, new_table_store->mds_sstables_))) {
      LOG_WARN("fail to deep copy mds sstables", K(ret));
    } else if (OB_FAIL(major_ckm_info_.deep_copy(buf, buf_len, pos, new_table_store->major_ckm_info_))) {
      LOG_WARN("fail to assign major ckm info", K(ret), K(major_ckm_info_));
    } else if (OB_FAIL(memtables_.assign(new_table_store->memtables_))) {
      LOG_WARN("fail to assign memtable pointers to new table store", K(ret));
    } else {
      new_table_store->version_ = version_;
      new_table_store->is_inited_ = is_inited_;
      new_table_store->is_ready_for_read_ = is_ready_for_read_;
      value = new_table_store;
      LOG_DEBUG("succeed to deep_copy table store", K(new_table_store->major_ckm_info_),
                K(major_tables_), K(minor_tables_), K(memtables_), K(PRINT_TS(*this)));
    }
  }
  return ret;
}

#define TRY_CACHE_SSTABLE_META(sstable_array)                                                                                                     \
  if (FAILEDx(ObCacheSSTableHelper::try_cache_local_sstable_meta(allocator, sstable_array, local_sstable_size_limit, local_sstable_meta_size))) { \
    if (OB_UNLIKELY(OB_BUF_NOT_ENOUGH != ret)) {                                                                                                  \
      LOG_WARN("fail to cache sstable meta", K(ret), K(sstable_array));                                                                           \
    }                                                                                                                                             \
  }

void ObTabletTableStore::try_cache_local_sstables(common::ObArenaAllocator &allocator)
{
  int ret = OB_SUCCESS;
  const int64_t table_store_object_size = get_deep_copy_size();

  if (OB_UNLIKELY(MAX_TABLE_STORE_MEMORY_SIZE < table_store_object_size)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid table store memory context size", K(ret), K(table_store_object_size));
  } else {
    const int64_t local_sstable_size_limit = MAX_TABLE_STORE_MEMORY_SIZE - table_store_object_size;
    int64_t local_sstable_meta_size = 0;
    TRY_CACHE_SSTABLE_META(meta_major_tables_);
    TRY_CACHE_SSTABLE_META(major_tables_);
    TRY_CACHE_SSTABLE_META(minor_tables_);
  }
}
#undef TRY_CACHE_SSTABLE_META

int ObTabletTableStore::inc_macro_ref() const
{
  int ret = OB_SUCCESS;
  bool major_success = false;
  bool meta_major_success = false;
  bool minor_success = false;
  bool ddl_success = false;
  bool mds_success = false;

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("tablet store hasn't been inited", K(ret));
  } else if (!major_tables_.empty()
      && OB_FAIL(major_tables_.inc_macro_ref(major_success))) {
    LOG_WARN("fail to increase major sstables' ref cnt", K(ret), K(major_tables_));
  } else if (!minor_tables_.empty()
      && OB_FAIL(minor_tables_.inc_macro_ref(minor_success))) {
    LOG_WARN("fail to increase minor sstables' ref cnt", K(ret), K(minor_tables_));
  } else if (!ddl_sstables_.empty()
      && OB_FAIL(ddl_sstables_.inc_macro_ref(ddl_success))) {
    LOG_WARN("fail to increase ddl sstables' ref cnt", K(ret), K(ddl_sstables_));
  } else if (!meta_major_tables_.empty()
      && OB_FAIL(meta_major_tables_.inc_macro_ref(meta_major_success))) {
    LOG_WARN("fail to increase meta major sstables' ref cnt", K(ret), K(meta_major_tables_));
  } else if (!mds_sstables_.empty()
      && OB_FAIL(mds_sstables_.inc_macro_ref(mds_success))) {
    LOG_WARN("fail to increase mds sstables' ref cnt", K(ret), K(mds_sstables_));
  }

  if (OB_FAIL(ret)) {
    if (major_success) {
      major_tables_.dec_macro_ref();
    }
    if (minor_success) {
      minor_tables_.dec_macro_ref();
    }
    if (ddl_success) {
      ddl_sstables_.dec_macro_ref();
    }
    if (meta_major_success) {
      meta_major_tables_.dec_macro_ref();
    }
    if (mds_success) {
      mds_sstables_.dec_macro_ref();
    }
  }

  return ret;
}

void ObTabletTableStore::dec_macro_ref() const
{
  if (!major_tables_.empty()) {
    major_tables_.dec_macro_ref();
  }
  if (!minor_tables_.empty()) {
    minor_tables_.dec_macro_ref();
  }
  if (!ddl_sstables_.empty()) {
    ddl_sstables_.dec_macro_ref();
  }
  if (!meta_major_tables_.empty()) {
    meta_major_tables_.dec_macro_ref();
  }
  if (!mds_sstables_.empty()) {
    mds_sstables_.dec_macro_ref();
  }
}

bool ObTabletTableStore::check_read_tables(
    const ObTablet &tablet,
    const int64_t snapshot_version,
    const ObSSTable *base_table) const
{
  bool contain_snapshot_version = false;
  if (OB_ISNULL(base_table)) {
    LOG_WARN_RET(OB_INVALID_ARGUMENT, "null base table, must not contain snapshot_version", KP(base_table));
  } else {
    if (base_table->is_major_sstable()) {
      contain_snapshot_version = base_table->get_snapshot_version() == snapshot_version;
    }
    if (!contain_snapshot_version && snapshot_version >= tablet.get_multi_version_start()) {
      contain_snapshot_version = true;
    }
  }
  if (!contain_snapshot_version) {
    LOG_WARN_RET(OB_ERR_UNEXPECTED, "table store has no contain snapshot version",
        K(snapshot_version), K(tablet), KPC(base_table), KPC(this));
  }
  return contain_snapshot_version;
}

class ObTmpSSTable : public ObSSTable
{
public:
  ObTmpSSTable() : arena_(ObMemAttr(MTL_ID(), "tmp_sst"))
  {
    key_.table_type_ = ObITable::DDL_MERGE_CO_SSTABLE;
    is_tmp_sstable_ = true;// for enable ref count
  }
  virtual ~ObTmpSSTable()
  {
    is_tmp_sstable_ = false; // for disable dec_macro_ref
  }

public:
  ObArenaAllocator arena_;
  ObTableHandleV2 handle_;
};

int ObTabletTableStore::calculate_ddl_read_tables(
    const int64_t snapshot_version,
    const ObTablet &tablet,
    ObTableStoreIterator &iterator,
    const ObSSTable *&base_table) const
{
  int ret = OB_SUCCESS;
  base_table = nullptr;
  if (ddl_sstables_.count() > 0
      && OB_NOT_NULL(ddl_sstables_.at(0))
      && ddl_sstables_.at(0)->is_ddl_merge_sstable()
      && !ObDDLUtil::need_rescan_column_store(tablet.get_tablet_meta().ddl_data_format_version_)) {
    ObSSTable *first_ddl_sstable = ddl_sstables_.at(0);
    if (OB_FAIL(iterator.add_table(first_ddl_sstable))) {
      LOG_WARN("add ddl sstable failed", K(ret), KP(first_ddl_sstable));
    } else {
      base_table = first_ddl_sstable;
    }
    LOG_INFO("use first ddl sstable to drive all slice", K(ret), K(snapshot_version), K(ddl_sstables_.count()), KPC(first_ddl_sstable));
  } else {
    ObSEArray<ObITable *, MAX_SSTABLE_CNT_IN_STORAGE> ddl_major_sstables;
    bool has_co_ddl_memtable = false;
    if (OB_FAIL(get_ddl_major_sstables(ddl_major_sstables, has_co_ddl_memtable))) {
      LOG_WARN("get ddl major sstable failed", K(ret));
    } else if (ddl_major_sstables.count() > 0) {
      ObSSTable *first_ddl_sstable = static_cast<ObSSTable *>(ddl_major_sstables.at(0));
      if (first_ddl_sstable->is_column_store_sstable() && ddl_sstables_.count() > 1) {
        ret = OB_DATA_NOT_UPTODATE;
        LOG_WARN("ddl query only support one co sstable", K(ret), K(ddl_sstables_.count()));
        ObDDLTableMergeDagParam param;
        param.ls_id_               = tablet.get_ls_id();
        param.tablet_id_           = tablet.get_tablet_meta().tablet_id_;
        param.start_scn_           = tablet.get_tablet_meta().ddl_start_scn_;
        param.rec_scn_             = tablet.get_tablet_meta().ddl_commit_scn_;
        param.direct_load_type_    = tablet.get_tablet_meta().ddl_snapshot_version_ >= DDL_IDEM_DATA_FORMAT_VERSION ?
                                     ObDirectLoadType::SN_IDEM_DIRECT_LOAD_DDL : ObDirectLoadType::DIRECT_LOAD_DDL;
        param.is_commit_           = true;
        param.data_format_version_ = tablet.get_tablet_meta().ddl_data_format_version_;
        param.snapshot_version_    = tablet.get_tablet_meta().ddl_snapshot_version_;
        int tmp_ret = OB_SUCCESS;
        if (OB_TMP_FAIL(ObTabletDDLUtil::freeze_ddl_kv(param))) {
          LOG_WARN("try to freeze ddl kv failed", K(tmp_ret), K(param));
        } else if (OB_TMP_FAIL(compaction::ObScheduleDagFunc::schedule_ddl_table_merge_dag(param))) {
          LOG_WARN("try schedule ddl merge dag failed when ddl kv is full ", K(tmp_ret), K(param));
        }
        LOG_INFO("schedule ddl merge dag", K(tmp_ret), K(param));

      } else if (first_ddl_sstable->get_data_version() <= snapshot_version) {
        for (int64_t i = 0; OB_SUCC(ret) && i < ddl_major_sstables.count(); ++i) {
          if (OB_FAIL(iterator.add_table(ddl_major_sstables.at(i)))) {
            LOG_WARN("add ddl major sstable failed", K(ret), K(i), K(ddl_major_sstables));
          }
        }
        if (OB_SUCC(ret)) {
          base_table = first_ddl_sstable;
        }
      } else {
        LOG_DEBUG("the snapshot_version of ddl major sstable is not match",
            "ddl_major_sstable_version", first_ddl_sstable->get_data_version(), K(snapshot_version));
      }
      LOG_INFO("calc ddl read tables", K(ret), K(snapshot_version), K(ddl_major_sstables.count()), KPC(first_ddl_sstable));
    } else if (has_co_ddl_memtable) {
      const SCN ddl_start_scn = ddl_mem_sstables_[0]->get_ddl_start_scn();
      ObTableHandleV2 ddl_tmp_handle;
      ObArenaAllocator arena(ObMemAttr(MTL_ID(), "Ddl_Com_Store"));
      ObTabletDDLCompleteMdsUserData ddl_complete_data;
      ObStorageSchema *storage_schema = nullptr;
      ObTmpSSTable *ddl_tmp_sstable = nullptr;
      if (OB_FAIL(tablet.get_ddl_complete(share::SCN::max_scn(), arena, ddl_complete_data))) {
        LOG_WARN("failed to get ddl complete mds user data", K(ret));
      } else if (ddl_complete_data.snapshot_version_ > snapshot_version) {
        // skip
      } else if (OB_ISNULL(ddl_tmp_sstable = OB_NEW(ObTmpSSTable, ObMemAttr(MTL_ID(), "ddl_tmp")))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("allocate memory failed", K(ret));
      } else if (OB_FAIL(ddl_tmp_handle.set_sstable(ddl_tmp_sstable, ObMallocAllocator::get_instance()))) {
        LOG_WARN("set ddl tmp sstable into table handle failed", K(ret));
        ddl_tmp_sstable->~ObTmpSSTable();
        ob_free(ddl_tmp_sstable);
        ddl_tmp_sstable = nullptr;
      } else if (OB_FAIL(tablet.load_storage_schema(ddl_tmp_sstable->arena_, storage_schema))) { // for cs replica
        LOG_WARN("load storage schema failed", K(ret));
      } else if (OB_FAIL(ObTabletDDLUtil::create_ddl_empty_co_sstable(tablet.get_tablet_id(), ddl_start_scn, ddl_complete_data.snapshot_version_, storage_schema, ddl_tmp_sstable->arena_, ddl_tmp_sstable->handle_))) {
        LOG_WARN("create empty co sstable failed", K(ret));
      } else if (OB_FAIL(iterator.add_ddl_co_table(ddl_tmp_handle, ddl_tmp_sstable->handle_.get_table()))) {
        LOG_WARN("add ddl empty co sstable failed", K(ret));
      } else {
        LOG_INFO("add ddl empty co sstable", KP(ddl_tmp_sstable), K(tablet.get_tablet_id()), K(ddl_complete_data.snapshot_version_));
      }
      if (OB_NOT_NULL(ddl_tmp_sstable)) {
        ObTabletObjLoadHelper::free(ddl_tmp_sstable->arena_, storage_schema);
      }
    }
  }
  return ret;
}

int ObTabletTableStore::calculate_read_tables(
    const int64_t snapshot_version,
    const ObTablet &tablet,
    ObTableStoreIterator &iterator,
    const bool allow_no_ready_read,
    const bool skip_major) const
{
  int ret = OB_SUCCESS;
  const ObSSTable *base_table = nullptr;
  bool is_major_empty = false;

  if (skip_major) {
    // do nothing
  } else if (!meta_major_tables_.empty() && meta_major_tables_.at(0)->get_snapshot_version() < snapshot_version) {
    base_table = meta_major_tables_.at(0);
    if (OB_FAIL(iterator.add_table(meta_major_tables_.at(0)))) {
      LOG_WARN("failed to add meta major table to iterator", K(ret), K(meta_major_tables_));
    }
  } else if (OB_FAIL(check_major_sstable_empty(tablet.get_tablet_meta().ddl_commit_scn_, tablet, is_major_empty))) {
    LOG_WARN("failed to check major sstable empty", K(ret), K(tablet));
  } else if (is_major_empty) {
    LOG_INFO("no base tables in table store, no ready for reading", K(ret), K(snapshot_version), K(*this));
  } else if (!major_tables_.empty()) {
    for (int64_t i = major_tables_.count() - 1; OB_SUCC(ret) && i >= 0; --i) {
      if (major_tables_[i]->get_snapshot_version() <= snapshot_version) {
        base_table = major_tables_[i];
        if (OB_FAIL(iterator.add_table(major_tables_[i]))) {
          LOG_WARN("failed to add major table to iterator", K(ret));
        }
        break;
      }
    }
  } else if (OB_FAIL(calculate_ddl_read_tables(snapshot_version, tablet, iterator, base_table))) {
    LOG_WARN("calculate ddl read tables failed", K(ret));
  }

  if (OB_FAIL(ret)) {
  } else if (OB_NOT_NULL(base_table)) {
    // TODO@wenqu: better abstract to calculate read tables in ddl path
    SCN last_scn = base_table->is_major_sstable() || base_table->is_ddl_type_sstable() ? SCN::max_scn() : base_table->get_end_scn();
    for (int64_t i = 0; OB_SUCC(ret) && i < minor_tables_.count(); ++i) {
      if (((base_table->is_major_sstable() || base_table->is_ddl_type_sstable()) && minor_tables_[i]->get_upper_trans_version() >= base_table->get_data_version())
          || minor_tables_[i]->get_end_scn() >= last_scn) {
        if (OB_FAIL(iterator.add_tables(minor_tables_, i, minor_tables_.count() - i))) {
          LOG_WARN("failed add table to iterator", K(ret));
        }
        break;
      }
    }

    if (FAILEDx(calculate_read_memtables(tablet, iterator))) {
      LOG_WARN("failed to calculate read memtables", K(ret), K(snapshot_version), K(iterator), KPC(this));
    } else if (!check_read_tables(tablet, snapshot_version, base_table)) {
      ret = OB_SNAPSHOT_DISCARDED;
      LOG_WARN("exist base table, but no read table found for specific version",
          K(ret), K(snapshot_version), K(iterator), K(PRINT_TS(*this)));
    }
  } else { // base table is null
    if (skip_major || (allow_no_ready_read && is_major_empty)) {
      if (!minor_tables_.empty() && OB_FAIL(iterator.add_tables(
              minor_tables_, 0, minor_tables_.count()))) {
        LOG_WARN("failed to add all minor tables to iterator", K(ret));
      } else {
        if (OB_FAIL(calculate_read_memtables(tablet, iterator))) {
          LOG_WARN("no base table, but allow no ready read, failed to calculate read memtables",
              K(ret), K(snapshot_version), K(memtables_), K(PRINT_TS(*this)));
        }
      }
    } else if (is_major_empty) {
      ret = OB_REPLICA_NOT_READABLE;
      LOG_WARN("no base table, not allow no ready read, tablet is not readable",
            K(ret), K(snapshot_version), K(allow_no_ready_read), K(PRINT_TS(*this)));
    } else {
      ret = OB_SNAPSHOT_DISCARDED;
      LOG_WARN("no base table found for specific version",
            K(ret), K(snapshot_version), K(allow_no_ready_read), K(PRINT_TS(*this)));
    }
  }
  return ret;
}

int ObTabletTableStore::calculate_read_memtables(
    const ObTablet &tablet,
    ObTableStoreIterator &iterator) const
{
  int ret = OB_SUCCESS;
  int64_t start_snapshot_version = tablet.get_snapshot_version();
  share::SCN start_scn = tablet.get_clog_checkpoint_scn();
  int64_t mem_pos = -1;
  ObITable *memtable = nullptr;

  if (memtables_.empty()) {
  } else if (OB_FAIL(memtables_.find(start_scn, start_snapshot_version, memtable, mem_pos))) {
    LOG_WARN("failed to find memtable", K(ret), K(*this));
  } else if (-1 == mem_pos) {
  } else if (OB_FAIL(iterator.add_tables(memtables_, mem_pos))) {
    LOG_WARN("failed to add memtable to iterator", K(ret));
  }
  return ret;
}

int ObTabletTableStore::get_table(
    const ObStorageMetaHandle &table_store_handle,
    const ObITable::TableKey &table_key,
    ObTableHandleV2 &handle) const
{
  int ret = OB_SUCCESS;
  handle.reset();
  ObITable *table = nullptr;
  ObSSTableWrapper sstable_wrapper;

  if (OB_UNLIKELY(!is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("table store is unexpected invalid", K(ret), KPC(this));
  } else if (!table_key.is_memtable()) {
    if (OB_FAIL(get_sstable(table_key, sstable_wrapper))) {
      LOG_WARN("fail to get sstable wrapper", K(ret), K(table_key));
    } else {
      table = sstable_wrapper.get_sstable();
    }
  } else if (OB_FAIL(get_memtable(table_key, table))) {
    LOG_WARN("fail to get memtable pointer", K(ret), K(table_key));
  }

  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(table)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("found null table pointer", K(ret), K(table_key));
  } else if (table->is_memtable()) {
    ObTenantMetaMemMgr *t3m = MTL(ObTenantMetaMemMgr *);
    if (OB_FAIL(handle.set_table(table, t3m, table->get_key().table_type_))) {
      LOG_WARN("Failed to set memtable to handle", K(ret), K(handle), K(table_key), KPC(table));
    }
  } else if (static_cast<ObSSTable *>(table)->is_loaded()) {
    if (table->is_cg_sstable() && sstable_wrapper.get_meta_handle().is_valid()) {
      if ( OB_FAIL(handle.set_sstable(sstable_wrapper.get_sstable(), sstable_wrapper.get_meta_handle()))) {
        LOG_WARN("fail to set cg sstable to handle", K(ret), K(sstable_wrapper));
      }
    } else if (!table_store_handle.is_valid()) {
      // table store object on tablet meta memory
      if (OB_FAIL(handle.set_sstable_with_tablet(table))) {
        LOG_WARN("failed to set sstable to handle", K(ret));
      }
    } else if (OB_FAIL(handle.set_sstable(table, table_store_handle))) {
      LOG_WARN("failed to set sstable to handle", K(ret));
    }
  } else {
    ObStorageMetaHandle sst_handle;
    ObSSTable *sstable = nullptr;
    if (OB_FAIL(ObCacheSSTableHelper::load_sstable(static_cast<ObSSTable *>(table)->get_addr(),
                                                  table_key.is_co_sstable(),
                                                  sst_handle))) {
      LOG_WARN("fail to load sstable", K(ret), KPC(table));
    } else if (OB_FAIL(sst_handle.get_sstable(sstable))) {
      LOG_WARN("fail to get loaded sstable", K(ret));
    } else if (OB_FAIL(handle.set_sstable(sstable, sst_handle))) {
      LOG_WARN("fail to set sstable to handle", K(ret), KPC(sstable), K(handle));
    }
  }
  return ret;
}

int ObTabletTableStore::get_sstable(const ObITable::TableKey &table_key, ObSSTableWrapper &wrapper) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!table_key.is_valid() || table_key.is_memtable())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(table_key));
  } else {
    wrapper.reset();
    const ObSSTableArray *sst_array = nullptr;
    if (table_key.is_major_sstable()) {
      sst_array = table_key.is_meta_major_sstable()
                ? &meta_major_tables_
                : &major_tables_;
    } else if (table_key.is_minor_sstable()) {
      sst_array = &minor_tables_;
    } else if (table_key.is_ddl_sstable()) {
      sst_array = &ddl_sstables_;
    } else if (table_key.is_mds_sstable()) {
      sst_array = &mds_sstables_;
    }

    if (OB_ISNULL(sst_array)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected null sstable array", K(ret), K(table_key));
    } else if (sst_array->empty()) {
      // not found
    } else if (OB_FAIL(sst_array->get_table(table_key, wrapper))) {
      LOG_WARN("fail to get table from sstable array", K(ret));
    }

    if (OB_FAIL(ret)) {
    } else if (OB_ISNULL(wrapper.get_sstable())) {
      ret = OB_ENTRY_NOT_EXIST;
      LOG_WARN("table not found", K(ret), K(table_key));
    } else if (OB_UNLIKELY(wrapper.get_sstable()->get_key() != table_key)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("table key not match", K(ret), K(table_key), K(wrapper.get_sstable()->get_key()));
    }
  }
  return ret;
}

int ObTabletTableStore::get_memtable(const ObITable::TableKey &table_key, ObITable *&table) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!table_key.is_valid() && !table_key.is_memtable())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(table_key));
  } else {
    table = nullptr;
    if (table_key.is_memtable()) {
      common::SpinRLockGuard guard(memtables_lock_);
      if (OB_FAIL(memtables_.find(table_key, table))) {
        LOG_WARN("fail to get memtable", K(ret), K(table_key), K_(memtables));
      }
    }

    if (OB_FAIL(ret)) {
    } else if (OB_ISNULL(table)) {
      ret = OB_ENTRY_NOT_EXIST;
      LOG_WARN("table not found", K(ret), K(table_key));
    }
  }
  return ret;
}

int ObTabletTableStore::get_read_tables(
    const int64_t snapshot_version,
    const ObTablet &tablet,
    ObTableStoreIterator &iterator,
    const ObGetReadTablesMode mode)
{
  int ret = OB_SUCCESS;
  const bool allow_no_ready_read = ObGetReadTablesMode::NORMAL != mode;
  const bool skip_major = ObGetReadTablesMode::SKIP_MAJOR == mode;
  bool is_major_empty = true;

  common::SpinRLockGuard guard(memtables_lock_);
  if (OB_UNLIKELY(snapshot_version < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(snapshot_version));
  } else if (OB_FAIL(check_ready_for_read(tablet))) {
    LOG_WARN("failed to check major sstable emtpy", K(ret));
  } else if (is_ready_for_read_) {
    // do nothing
  } else if (OB_FAIL(check_major_sstable_empty(tablet.get_tablet_meta().ddl_commit_scn_, tablet, is_major_empty))) {
    LOG_TRACE("no valid major sstable, not ready for read", K(*this));
  } else if (!is_major_empty) {
    is_ready_for_read_ = true;
  }

  if (OB_FAIL(ret)) {
  } else if (OB_UNLIKELY(!is_ready_for_read_ && minor_tables_.empty() && allow_no_ready_read)) {
    if (memtables_.empty()) {
      LOG_INFO("no table in table store, cannot read", K(ret), K(*this));
    } else if (OB_FAIL(iterator.add_tables(memtables_))) {
      LOG_WARN("failed to add tables to iterator", K(ret));
    }
  } else if (OB_UNLIKELY(!allow_no_ready_read && !is_ready_for_read_)) {
    ret = OB_REPLICA_NOT_READABLE;
    LOG_WARN("table store not ready for read", K(ret), K(allow_no_ready_read), K(PRINT_TS(*this)));
  } else if (OB_UNLIKELY(!is_valid())) {
    ret = OB_ERR_SYS;
    LOG_WARN("table store not valid", K(ret), K(snapshot_version), K(*this));
  } else if (OB_FAIL(calculate_read_tables(snapshot_version, tablet, iterator, allow_no_ready_read, skip_major))) {
    LOG_WARN("failed to get read tables", K(ret));
  }
  if (FAILEDx(iterator.set_retire_check())) {
    LOG_WARN("failed to set retire check to iterator", K(ret));
  }
  return ret;
}

int ObTabletTableStore::get_read_major_sstable(
    const int64_t snapshot_version,
    ObTableStoreIterator &iterator) const
{
  int ret = OB_SUCCESS;
  iterator.reset();

  if (OB_UNLIKELY(snapshot_version < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(snapshot_version));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < major_tables_.count(); ++i) {
      ObSSTable *sstable = major_tables_[i];
      if (OB_ISNULL(sstable)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected null sstable pointer", K(ret));
      } else if (snapshot_version < sstable->get_snapshot_version()) {
        break;
      } else if (snapshot_version == sstable->get_snapshot_version()) {
        if (OB_FAIL(iterator.add_table(sstable))) {
          LOG_WARN("failed to add major table to iterator", K(ret), KPC(sstable));
        }
      }
    }
  }
  return ret;
}

int ObTabletTableStore::get_all_sstable(
    ObTableStoreIterator &iter,
    const bool unpack_co_table) const
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!meta_major_tables_.empty() && OB_FAIL(iter.add_tables(meta_major_tables_, 0, meta_major_tables_.count(), unpack_co_table))) {
    LOG_WARN("fail to add all meta major tables to iterator", K(ret), K_(meta_major_tables));
  } else if (!major_tables_.empty() && OB_FAIL(iter.add_tables(major_tables_, 0, major_tables_.count(), unpack_co_table))) {
    LOG_WARN("fail to add all major tables to iterator", K(ret), K_(major_tables));
  } else if (!minor_tables_.empty() && OB_FAIL(iter.add_tables(minor_tables_, 0, minor_tables_.count()))) {
    LOG_WARN("fail to add all minor tables to iterator", K(ret), K_(major_tables));
  } else if (!ddl_sstables_.empty() && OB_FAIL(iter.add_tables(ddl_sstables_, 0, ddl_sstables_.count(), unpack_co_table))) {
    LOG_WARN("fail to add all ddl sstables to iterator", K(ret), K_(ddl_sstables));
  } else if (!mds_sstables_.empty() && OB_FAIL(iter.add_tables(mds_sstables_, 0, mds_sstables_.count()))) {
    LOG_WARN("fail to add all mds sstables to iterator", K(ret), K_(mds_sstables));
  }
  return ret;
}

int ObTabletTableStore::get_major_sstables(
    ObTableStoreIterator &iter,
    const bool unpack_co_table) const
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(iter.add_tables(major_tables_, 0, major_tables_.count(), unpack_co_table))) {
    LOG_WARN("fail to add all major tables to iterator", K(ret), K_(major_tables));
  }
  return ret;
}


int ObTabletTableStore::update_memtables(const common::ObIArray<storage::ObITable *> &memtables)
{
  int ret = OB_SUCCESS;
  common::SpinWLockGuard guard(memtables_lock_);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(memtables_.rebuild(memtables))) {
    LOG_ERROR("failed to rebuild table store memtables", K(ret), K(memtables), KPC(this));
  }
  return ret;
}

int ObTabletTableStore::clear_memtables()
{
  common::SpinWLockGuard guard(memtables_lock_);
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("table store is unexpected invalid", K(ret), KPC(this));
  } else {
    memtables_.reset();
  }
  return ret;
}

int ObTabletTableStore::clear_ddl_memtables()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("table store is unexpected invalid", K(ret), KPC(this));
  } else {
    ddl_mem_sstables_.reset();
  }
  return ret;
}

int ObTabletTableStore::get_first_frozen_memtable(ObITable *&table) const
{
  int ret = OB_SUCCESS;
  common::SpinRLockGuard guard(memtables_lock_);
  for (int64_t i = 0; OB_SUCC(ret) && i < memtables_.count(); ++i) {
    if (OB_ISNULL(memtables_[i])) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("memtable must not null", K(ret), K(memtables_));
    } else if (memtables_[i]->is_frozen_memtable()) {
      table = memtables_[i];
      break;
    }
  }
  return ret;
}

#define ADD_SSTABLES_TO_ITER(array, array_count)                \
  if (IS_NOT_INIT) {                                            \
    ret = OB_NOT_INIT;                                          \
    LOG_WARN("table store is not inited", K(ret));              \
  } else if (OB_FAIL(iter.add_tables(array, 0, array_count))) { \
    LOG_WARN("failed to add ddl sstables to iterator", K(ret)); \
  }

int ObTabletTableStore::get_ddl_sstables(ObTableStoreIterator &iter) const
{
  int ret = OB_SUCCESS;
  ADD_SSTABLES_TO_ITER(ddl_sstables_, ddl_sstables_.count());
  return ret;
}

int ObTabletTableStore::get_mds_sstables(ObTableStoreIterator &iter) const
{
  int ret = OB_SUCCESS;
  ADD_SSTABLES_TO_ITER(mds_sstables_, mds_sstables_.count());
  return ret;
}

#undef ADD_SSTABLES_TO_ITER

int ObTabletTableStore::get_ha_tables(ObTableStoreIterator &iter) const
{
  int ret = OB_SUCCESS;
  iter.reset();
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("table store is not inited", K(ret));
  } else if (!major_tables_.empty() && OB_FAIL(iter.add_tables(major_tables_, 0, major_tables_.count(), true/*unpack_co*/))) {
    LOG_WARN("failed to add major table to iterator", K(ret));
  } else if (!minor_tables_.empty() && OB_FAIL(iter.add_tables(minor_tables_, 0, minor_tables_.count()))) {
    LOG_WARN("failed to add minor table to iterator", K(ret));
  } else if (!ddl_sstables_.empty() && OB_FAIL(iter.add_tables(ddl_sstables_, 0, ddl_sstables_.count(), true/*unpack_co*/))) {
    LOG_WARN("failed to add ddl table to iterator", K(ret));
  } else if (!mds_sstables_.empty() && OB_FAIL(iter.add_tables(mds_sstables_, 0, mds_sstables_.count()))) {
    LOG_WARN("failed to add mds table to iterator", K(ret));
  } else if (OB_FAIL(iter.set_retire_check())) {
    LOG_WARN("failed to set retire check to iterator", K(ret));
  } else {
    LOG_INFO("succeed to get ha tables", K(major_tables_), K(minor_tables_), K(ddl_sstables_), K(mds_sstables_));
  }
  return ret;
}

int ObTabletTableStore::get_mini_minor_sstables(ObTableStoreIterator &iter) const
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("table store is not inited", K(ret));
  } else if (OB_FAIL(get_mini_minor_sstables_(iter))) {
    LOG_WARN("failed to get mini minor sstables", K(ret));
  }
  return ret;
}

int ObTabletTableStore::get_recycle_version(
    const int64_t multi_version_start,
    int64_t &recycle_version) const
{
  int ret = OB_SUCCESS;
  recycle_version = 0;
  ObSEArray<ObITable *, MAX_SSTABLE_CNT_IN_STORAGE> major_tables;
  if (major_tables_.empty()) {
  } else if (OB_FAIL(major_tables_.get_all_tables(major_tables))) {
    LOG_WARN("failed to get major tables from old store", K(ret), KPC(this));
  } else if (OB_FAIL(ObTableStoreUtil::sort_major_tables(major_tables))) {
    LOG_WARN("failed to sort major tables", K(ret));
  } else {
    for (int64_t i = major_tables.count() - 1; OB_SUCC(ret) && i >= 0; --i) {
      if (major_tables.at(i)->get_snapshot_version() <= multi_version_start) {
        recycle_version = major_tables.at(i)->get_snapshot_version();
        break;
      }
    }
    if (0 == recycle_version && major_tables.count() > 0) {
      recycle_version = major_tables.at(0)->get_snapshot_version();
      LOG_WARN("not found inc base snapshot version, use the oldest major table", K(ret));
    }
  }
  return ret;
}

int ObTabletTableStore::build_major_tables(
    ObArenaAllocator &allocator,
    const ObUpdateTableStoreParam &param,
    const ObTabletTableStore &old_store,
    int64_t &inc_base_snapshot_version)
{
  int ret = OB_SUCCESS;
  ObITable *new_table = static_cast<ObITable *>(const_cast<ObSSTable *>(param.sstable_));
  ObSEArray<ObITable *, OB_DEFAULT_SE_ARRAY_COUNT> major_tables;
  inc_base_snapshot_version = -1;

  if (nullptr != new_table && ObITable::is_major_sstable(new_table->get_table_type()) && OB_FAIL(major_tables.push_back(new_table))) {
    LOG_WARN("failed to add table into tables handle", K(ret), K(param));
  } else if (param.ha_info_.is_only_replace_major_ || param.ha_info_.need_replace_remote_sstable_) {
    if (OB_FAIL(inner_build_major_tables_for_ha_(allocator, param, old_store, major_tables, inc_base_snapshot_version))) {
      LOG_WARN("failed to build major tables for ha", K(ret));
    }
  } else if (!major_tables.empty()
          && !old_store.ddl_sstables_.empty()
          && OB_FAIL(check_new_sstable_can_be_accepted_(old_store.ddl_sstables_, new_table))) {
    LOG_WARN("failed to check can accept the new major sstable", K(ret), K(param));
  } else if (OB_FAIL(inner_build_major_tables_(allocator,
                                               old_store,
                                               major_tables,
                                               param.multi_version_start_,
                                               param.allow_duplicate_sstable_,
                                               inc_base_snapshot_version,
                                               is_convert_co_major_merge(param.compaction_info_.merge_type_)))) {
    LOG_WARN("failed to build major tables", K(ret), K(param), K(major_tables));
  }
  return ret;
}

// TODO(@DanLing) split ha update table store from compaction
int ObTabletTableStore::inner_build_major_tables_for_ha_(
    common::ObArenaAllocator &allocator,
    const ObUpdateTableStoreParam &param,
    const ObTabletTableStore &old_store,
    const ObIArray<ObITable *> &major_tables,
    int64_t &inc_base_snapshot_version)
{
  int ret = OB_SUCCESS;
  ObSSTable *new_table = const_cast<ObSSTable *>(param.sstable_);
  if (param.ha_info_.is_only_replace_major_) {
    if (OB_FAIL(only_replace_major_(allocator, old_store, major_tables, inc_base_snapshot_version))) {
      LOG_WARN("failed to do only replace major", K(ret), K(param), K(major_tables));
    }
  } else if (param.ha_info_.need_replace_remote_sstable_) {
    if (OB_FAIL(inner_replace_remote_major_sstable_(allocator, old_store, new_table))) {
      LOG_WARN("failed to inner replace remote major sstable", K(ret));
    }
  }
  return ret;
}

int ObTabletTableStore::check_and_build_new_major_tables(
    const ObIArray<ObITable *> &tables_array,
    const bool allow_duplicate_sstable,
    ObIArray<ObITable *> &major_tables) const
{
  int ret = OB_SUCCESS;

  for (int64_t i = 0; OB_SUCC(ret) && i < tables_array.count(); ++i) {
    ObITable *new_table = tables_array.at(i);
    bool need_add = true;
    if (OB_NOT_NULL(new_table) && ObITable::is_major_sstable(new_table->get_table_type())) {
      for (int64_t j = 0; OB_SUCC(ret) && j < major_tables.count(); ++j) {
        ObITable *table = major_tables.at(j);
        if (OB_ISNULL(table) || OB_UNLIKELY(!table->is_major_sstable())) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unexpected table", K(ret), KPC(table));
        } else if (new_table->get_key() == table->get_key()) {
          if (OB_UNLIKELY(!allow_duplicate_sstable)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("get unexpected new major table which has same range with old major sstable",
                K(ret), KPC(new_table), KPC(table));
          } else {
            ObSSTable *new_sstable = static_cast<ObSSTable *>(new_table);
            ObSSTable *old_sstable = static_cast<ObSSTable *>(table);
            ObSSTableMetaHandle new_sst_meta_hdl;
            ObSSTableMetaHandle old_sst_meta_hdl;
            if (OB_FAIL(new_sstable->get_meta(new_sst_meta_hdl))) {
              LOG_WARN("failed to get new sstable meta handle", K(ret));
            } else if (OB_FAIL(old_sstable->get_meta(old_sst_meta_hdl))) {
              LOG_WARN("failed to get old sstable meta handle", K(ret));
            } else if (OB_FAIL(ObSSTableMetaChecker::check_sstable_meta(
                new_sst_meta_hdl.get_sstable_meta(), old_sst_meta_hdl.get_sstable_meta()))) {
              LOG_WARN("failed to check sstable meta", K(ret), KPC(new_sstable), KPC(old_sstable));
            } else {
              if (new_sst_meta_hdl.get_sstable_meta().get_basic_meta().table_backup_flag_.has_no_backup()
                && old_sst_meta_hdl.get_sstable_meta().get_basic_meta().table_backup_flag_.has_backup()) {
                // if new sstable has no backup macro block and old sstable has backup macro block
                // replace the old sstable with the new one
                FLOG_INFO("replace major sstable with new one", KPC(new_sstable), KPC(old_sstable));
                major_tables.at(j) = new_table;
              }
              need_add = false;
            }
          }
        }
      }

      if (OB_SUCC(ret) && need_add && OB_FAIL(major_tables.push_back(new_table))) {
        LOG_WARN("failed to push new table into array", K(ret), KPC(new_table), K(major_tables));
      }
    }
  }
  return ret;
}

int ObTabletTableStore::inner_build_major_tables_(
    common::ObArenaAllocator &allocator,
    const ObTabletTableStore &old_store,
    const ObIArray<ObITable *> &tables_array,
    const int64_t multi_version_start,
    const bool allow_duplicate_sstable,
    int64_t &inc_base_snapshot_version,
    bool replace_old_row_store_major /*= false*/)
{
  int ret = OB_SUCCESS;
  inc_base_snapshot_version = -1;
  ObSEArray<ObITable *, MAX_SSTABLE_CNT_IN_STORAGE> major_tables;

  if (OB_UNLIKELY(!old_store.major_tables_.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("major in old store is invalid", K(ret), K(old_store));
  } else if (replace_old_row_store_major) {
    if (OB_FAIL(old_store.major_tables_.replace_twin_majors_and_build_new(tables_array, major_tables))) {
      LOG_WARN("failed to replace with row store majors", K(ret), K(old_store), K(tables_array));
    }
  } else if (OB_FAIL(old_store.major_tables_.get_all_tables(major_tables))) {
    LOG_WARN("failed to get major tables from old store", K(ret), K(old_store));
  } else if (OB_FAIL(check_and_build_new_major_tables(tables_array, allow_duplicate_sstable, major_tables))) {
    LOG_WARN("failed to check and add new major tables", K(ret), K(old_store), K(tables_array));
  }

  if (FAILEDx(ObTableStoreUtil::sort_major_tables(major_tables))) {
    LOG_WARN("failed to sort major tables", K(ret));
  } else {
    int64_t start_pos = 0;
    for (int64_t i = major_tables.count() - 1; OB_SUCC(ret) && i >= 0; --i) {
      if (major_tables.at(i)->get_snapshot_version() <= multi_version_start) {
        start_pos = i;
        inc_base_snapshot_version = major_tables.at(i)->get_snapshot_version();
        break;
      }
    }
    if (-1 == inc_base_snapshot_version && major_tables.count() > 0) {
      inc_base_snapshot_version = major_tables.at(0)->get_snapshot_version();
      LOG_WARN("not found inc base snapshot version, use the oldest major table", K(ret));
    }

    if (major_tables.empty()) {
      LOG_INFO("major tables is empty", K(major_tables));
    } else if (OB_FAIL(major_tables_.init(allocator, major_tables, start_pos))) {
      LOG_WARN("failed to init major_tables", K(ret));
    }
  }
  return ret;
}

int ObTabletTableStore::inner_replace_remote_major_sstable_(
    common::ObArenaAllocator &allocator,
    const ObTabletTableStore &old_store,
    ObITable *new_table)
{
  int ret = OB_SUCCESS;
  ObSEArray<ObITable *, OB_DEFAULT_SE_ARRAY_COUNT> old_tables_array;
  ObSEArray<ObITable *, OB_DEFAULT_SE_ARRAY_COUNT> new_tables_array;
  ObITable *old_table = nullptr;
  bool has_backup_macro = false;

  if (OB_ISNULL(new_table)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("new table is null", K(ret), KP(new_table));
  } else if (old_store.major_tables_.empty()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("no major table exist", K(ret), K(old_store), KPC(new_table));
  } else if (OB_FAIL(ObTableStoreUtil::check_has_backup_macro_block(new_table, has_backup_macro))) {
    LOG_WARN("failed to check new table has backup macro block", K(ret), KPC(new_table));
  } else if (has_backup_macro) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("new table still has backup macro block", K(ret), KPC(new_table));
  } else if (OB_FAIL(old_store.major_tables_.get_all_tables(old_tables_array))) {
    LOG_WARN("failed to get major tables from old store", K(ret), K(old_store));
  }

  for (int64_t idx = 0; OB_SUCC(ret) && idx < old_tables_array.count(); ++idx) {
    old_table = old_tables_array.at(idx);
    if (OB_ISNULL(old_table)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("get unexpected null table", K(ret), K(old_store));
    } else if (OB_FAIL(ObTableStoreUtil::check_has_backup_macro_block(old_table, has_backup_macro))) {
      LOG_WARN("failed to check table has backup macro block", K(ret), KPC(old_table));
    } else if (old_table->get_key() != new_table->get_key() || !has_backup_macro) {
      if (OB_FAIL(new_tables_array.push_back(old_table))) {
        LOG_WARN("failed to push back", K(ret));
      }
    } else if (OB_FAIL(new_tables_array.push_back(new_table))) {
      LOG_WARN("failed to push back", K(ret));
    } else {
      LOG_INFO("replace one remote major sstable", KPC(old_table), KPC(new_table));
    }
  }

  if (FAILEDx(major_tables_.init(allocator, new_tables_array))) {
    LOG_WARN("failed to init major tables", K(ret), K(new_tables_array));
  } else {
    LOG_INFO("succeed replace ha remote major sstables", K(old_store), K(new_tables_array), KPC(new_table));
  }
  return ret;
}

int ObTabletTableStore::build_minor_tables(
    common::ObArenaAllocator &allocator,
    const ObUpdateTableStoreParam &param,
    const ObSSTableArray &old_minor_tables,
    const int64_t inc_base_snapshot_version,
    const ObTabletHAStatus &ha_status)
{
  int ret = OB_SUCCESS;
  ObITable *new_table = const_cast<ObITable *>(static_cast<const ObITable *>(param.sstable_)); //table can be null

  if (OB_UNLIKELY((nullptr != new_table && new_table->is_minor_sstable())
               && (!new_table->get_key().scn_range_.is_valid() || new_table->get_key().scn_range_.is_empty()))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("get unexpected invalid scn range", K(ret), KPC(new_table));
  } else if (param.get_need_check_sstable() && OB_NOT_NULL(new_table) && new_table->is_minor_sstable()) { // fix issue 45431762
    if (old_minor_tables.empty() ||
        new_table->get_end_scn() < old_minor_tables.get_boundary_table(false/*first*/)->get_start_scn() ||
        new_table->get_start_scn() > old_minor_tables.get_boundary_table(true/*last*/)->get_end_scn()) {
      ret = OB_NO_NEED_MERGE;
      LOG_WARN("No old table covered by new minor, cannot add the new table", K(ret), KPC(new_table), K(old_minor_tables));
    }
  }

  ObArray<ObITable *> sstables;
  if (FAILEDx(inner_process_minor_tables(allocator, new_table, old_minor_tables, false/*is_mds*/, sstables))) {
    LOG_WARN("failed to inner process tables", K(ret), K(old_minor_tables), KPC(new_table));
  } else if (sstables.empty()) {
    // empty minor tables, do nothing
  } else if (!ha_status.check_allow_read()) {
    LOG_INFO("tablet in ha status, no need recycle minor sstable", K(ha_status));
    if (OB_FAIL(minor_tables_.init(allocator, sstables))) {
      LOG_WARN("failed to init minor_tables", K(ret));
    }
  } else {
    int64_t inc_pos = -1;
    const int64_t minor_cnt = sstables.count();
    const ObIArray<int64_t> *new_upper_trans = param.upper_trans_param_.new_upper_trans_;
    const bool has_valid_update = new_upper_trans != nullptr
                               && minor_cnt == new_upper_trans->count()
                               && minor_cnt > 0
                               && sstables.at(minor_cnt-1)->get_end_scn() == param.upper_trans_param_.last_minor_end_scn_;

    /*
    * if the upper trans version of the ith sstable can't be calculated, the sstables with bigger end_scn can't be calculated either.
    * new_upper_trans means the latest value of upper_trans_version for minor_tables.
    *
    * upper trans versions in old minors:
    * --------- ascending by end_scn -------------->
    * |  0   |  1   |  2   |  3   |  4   |  5   |  6   |
    * | val1 | val2 | val3 | MAX  | MAX  | MAX  | MAX  |
    * new_upper_trans:
    * |  0   |  1   |  2   |  3   |  4   |  5   |  6   |
    * | val1 | val2 | val3 | new1 | new2 | MAX  | MAX  |
    */
    for (int64_t i = 0; OB_SUCC(ret) && i < sstables.count(); ++i) {
      int64_t upper_trans_version = has_valid_update ? new_upper_trans->at(i) : sstables.at(i)->get_upper_trans_version();
      if (upper_trans_version > inc_base_snapshot_version) {
        inc_pos = i;
        break;
      }
    }

#ifdef OB_BUILD_SHARED_STORAGE
    if (OB_SUCC(ret) &&GCTX.is_shared_storage_mode()) {
      if (OB_FAIL(process_minor_sstables_for_ss_(allocator, param.upper_trans_param_, sstables, inc_base_snapshot_version, inc_pos))) {
        LOG_WARN("fail process for ss", K(ret));
      }
    }
#endif

    if (OB_FAIL(ret) || inc_pos < 0) {
    } else if (OB_FAIL(minor_tables_.init(allocator, sstables, inc_pos))) {
      LOG_WARN("failed to init minor_tables", K(ret));
    } else if (has_valid_update) {
      // update upper_trans_version of new table store with latest value
      for (int64_t i = 0; OB_SUCC(ret) && i < minor_tables_.count(); ++i) {
        ObSSTable *sstable = minor_tables_[i];
        if (OB_ISNULL(sstable)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unexpected null sstable pointer", K(ret), K(i));
        } else if (INT64_MAX != sstable->get_upper_trans_version()) {
        } else if (i + inc_pos >= new_upper_trans->count()) {
          ret = OB_SIZE_OVERFLOW;
          LOG_WARN("index of new_upper_trans overflow", K(ret), K(i), K(inc_pos), KPC(new_upper_trans));
        } else if (INT64_MAX == new_upper_trans->at(i+inc_pos)) {
          break;
        } else if (OB_FAIL(sstable->set_upper_trans_version(allocator, new_upper_trans->at(i+inc_pos)))) {
          LOG_WARN("failed to set new upper_trans_version", K(ret), K(i), KPC(sstable));
        }
      }
      LOG_INFO("Finish update upper_trans_version", K(ret), K(param), K(minor_tables_));
    }
  }
  return ret;
}

int ObTabletTableStore::build_mds_minor_tables(
    common::ObArenaAllocator &allocator,
    const blocksstable::ObSSTable *new_sstable,
    const ObSSTableArray &old_mds_tables)
{
  int ret = OB_SUCCESS;
  ObITable *new_table = const_cast<ObITable *>(static_cast<const ObITable *>(new_sstable)); //table can be null
  ObArray<ObITable *> sstables;

  if (OB_UNLIKELY((nullptr != new_table && new_table->is_mds_sstable())
               && (!new_table->get_key().scn_range_.is_valid() || new_table->get_key().scn_range_.is_empty()))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("get unexpected invalid scn range", K(ret), KPC(new_table));
  } else if (OB_FAIL(inner_process_minor_tables(allocator, new_table, old_mds_tables, true/*is_mds*/, sstables))) {
    LOG_WARN("failed to inner process mds tables", K(ret), K(old_mds_tables), KPC(new_table));
  } else if (sstables.empty()) {
    // do nothing
  } else if (OB_FAIL(mds_sstables_.init(allocator, sstables))) {
    LOG_WARN("failed to init mds_tables", K(ret));
  }
  return ret;
}

int ObTabletTableStore::inner_process_minor_tables(
    common::ObArenaAllocator &allocator,
    const ObITable *new_table,
    const ObSSTableArray &old_tables,
    const bool is_mds,
    ObArray<ObITable *> &sstables)
{
  int ret = OB_SUCCESS;
  const bool ignore_new_table = nullptr == new_table
                             || !is_table_valid_mds_or_minor_sstable(*new_table, is_mds);

  for (int64_t i = 0; OB_SUCC(ret) && i < old_tables.count(); ++i) {
    ObSSTable *table = old_tables[i];
    bool need_add = true;
    if (OB_UNLIKELY(NULL == table || !is_table_valid_mds_or_minor_sstable(*table, is_mds))) {
      ret = OB_ERR_SYS;
      LOG_ERROR("table must be valid minor or mds sstable", K(ret), KPC(table));
    } else if (ignore_new_table) {
      // do nothing
    } else if (new_table->get_key() == table->get_key()) {
      if (table->get_max_merged_trans_version() <= new_table->get_max_merged_trans_version()) {
        need_add = false;
        LOG_INFO("new table's max merge trans version is not less than the old table, "
            "add new table when table key is same", KPC(table), KPC(new_table));
      } else {
        ret = OB_NO_NEED_MERGE;
        LOG_WARN("new table with old max merged trans version, no need to merge",
            K(ret), KPC(table), KPC(new_table));
      }
    } else if (ObTableStoreUtil::check_include_by_scn_range(*new_table, *table)) {
      LOG_DEBUG("table purged", K(*new_table), K(*table));
      continue;
    } else if (ObTableStoreUtil::check_include_by_scn_range(*table, *new_table)) {
      ret = OB_MINOR_SSTABLE_RANGE_CROSS;
      LOG_WARN("new_table is contained by existing table", K(ret), KPC(new_table), KPC(table));
    } else if (ObTableStoreUtil::check_intersect_by_scn_range(*table, *new_table)) {
      ret = OB_MINOR_SSTABLE_RANGE_CROSS;
      LOG_WARN("new table's range is crossed with existing table", K(ret), K(*new_table), K(*table));
    }
    if (OB_FAIL(ret) || !need_add) {
    } else if (OB_FAIL(sstables.push_back(table))) {
      LOG_WARN("failed to add table", K(ret));
    }
  }

  if (OB_SUCC(ret) && !ignore_new_table) {
    if (OB_FAIL(check_new_sstable_can_be_accepted_(old_tables, const_cast<ObITable *>(new_table)))) {
      LOG_WARN("failed to check accept the compacted sstable", K(ret));
    } else if (OB_FAIL(sstables.push_back(const_cast<ObITable *>(new_table)))) {
      LOG_WARN("failed to add new table", K(ret), KPC(new_table));
    }
  }

  if (OB_FAIL(ret) || sstables.empty()) {
  } else if (OB_FAIL(ObTableStoreUtil::sort_minor_tables(sstables))) {
    LOG_WARN("failed to sort minor tables", K(ret));
  }
  return ret;
}

int ObTabletTableStore::build_meta_major_table(
    common::ObArenaAllocator &allocator,
    const blocksstable::ObSSTable *new_sstable,
    const ObSSTableArray &old_meta_tables)
{
  int ret = OB_SUCCESS;
  ObITable *last_major = nullptr;
  meta_major_tables_.reset();

  if (OB_ISNULL(last_major = major_tables_.get_boundary_table(true))) {
    LOG_INFO("no major sstable exists, no meta major exists", KPC(this));
  } else if (nullptr == new_sstable || !new_sstable->is_meta_major_sstable()) {
    if (old_meta_tables.empty()) {
      // do nothing
    } else if (old_meta_tables[0]->get_snapshot_version() <= last_major->get_snapshot_version()) {
      FLOG_INFO("meta sstable is covered by major sstable", KPC(last_major), K(old_meta_tables));
    } else if (OB_FAIL(meta_major_tables_.init(allocator, old_meta_tables[0]))) {
      LOG_WARN("failed to init meta major tables", K(ret));
    }
  } else if (new_sstable->get_max_merged_trans_version() <= last_major->get_snapshot_version()) {
    ret= OB_MINOR_SSTABLE_RANGE_CROSS;
    LOG_WARN("the new meta merge sstable is covered by major", K(ret), KPC(new_sstable), KPC(last_major));
  } else if (!old_meta_tables.empty() && new_sstable->get_snapshot_version() <= old_meta_tables[0]->get_snapshot_version()) {
    ret= OB_MINOR_SSTABLE_RANGE_CROSS;
    LOG_WARN("new meta major table is covered by old one", K(ret), KPC(new_sstable), K(old_meta_tables));
  } else if (OB_FAIL(meta_major_tables_.init(allocator, const_cast<ObSSTable *>(new_sstable)))) {
    LOG_WARN("failed to init meta major tables", K(ret));
  }
  return ret;
}

/*
*  is_major_empty is used to check is ddl major ready;
*  note: only in share  nothing, it's ddl major exist when node is merging a dll major in backend;
*             in shared sotrage, leader node directly send full major to follower and not need to check it
*/
OB_INLINE int ObTabletTableStore::check_major_sstable_empty(const share::SCN &ddl_commit_scn, const ObTablet &tablet, bool &is_empty) const
{
  int ret = OB_SUCCESS;
  is_empty = major_tables_.empty() && !ddl_commit_scn.is_valid_and_not_min(); // ddl logic major sstable require commit scn valid
  if (!is_empty) { // major not empty
  } else if (!(tablet.get_tablet_id().is_user_tablet())) { // inner tablet not need to check ddl complete
  } else if (tablet.get_tablet_meta().ddl_data_format_version_ < DDL_IDEM_DATA_FORMAT_VERSION) { // tablet not set ddl complete
  } else if (GCTX.is_shared_storage_mode()) { /* skip, only sn use ddl complete*/
  } else {
    ret = check_ddl_complete(tablet, is_empty);
  }
  return ret;
}

OB_INLINE int ObTabletTableStore::check_ddl_complete(const ObTablet &tablet, bool &is_empty) const
{
  int ret = OB_SUCCESS;
  ObArenaAllocator arena(ObMemAttr(MTL_ID(), "DdlCom_Sto"));
  ObTabletDDLCompleteMdsUserData data;
  if (OB_FAIL(tablet.get_ddl_complete(share::SCN::max_scn(), arena, data))) {
    if (OB_EMPTY_RESULT == ret) {
      ret = OB_SUCCESS;
      is_empty = true;
    } else {
      LOG_WARN("failed to get ddl complete", K(ret));
    }
  } else {
    /* ddl tables count may be empty even if ddl complete have been set
     * when tablet have not data
     */
    is_empty = (data.has_complete_ && (0 != ddl_sstables_.count() + ddl_mem_sstables_.count())) ? false : true;
  }
  return ret;
}

int ObTabletTableStore::get_ddl_major_sstables(ObIArray<ObITable *> &ddl_major_sstables, bool &has_co_ddl_memtable) const
{
  int ret = OB_SUCCESS;
  ddl_major_sstables.reset();
  has_co_ddl_memtable = false;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    const int64_t ddl_sstable_count = ddl_sstables_.count() + ddl_mem_sstables_.count();
    if (ddl_sstable_count > 0 && OB_FAIL(ddl_major_sstables.reserve(ddl_sstable_count))) {
      LOG_WARN("reserve ddl sstable array failed", K(ret), K(ddl_sstable_count));
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < ddl_sstables_.count(); ++i) {
      if (OB_FAIL(ddl_major_sstables.push_back(ddl_sstables_[i]))) {
        LOG_WARN("push back ddl dump sstable failed", K(ret), K(i));
      }
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < ddl_mem_sstables_.count(); ++i) {
      ObIArray<ObDDLMemtable *> &ddl_memtables_in_kv = ddl_mem_sstables_[i]->get_ddl_memtables();
      if (ddl_memtables_in_kv.empty()) {
        // skip
      } else {
        ObDDLMemtable *ddl_memtable = ddl_memtables_in_kv.at(0);
        if (ObITable::DDL_MEM_SSTABLE == ddl_memtable->get_key().table_type_
            && OB_FAIL(ddl_major_sstables.push_back(ddl_memtable))) {
          LOG_WARN("push back old ddl sstable failed", K(ret), K(i));
        } else {
          for (int64_t j = 0; OB_SUCC(ret) && !has_co_ddl_memtable &&  j < ddl_memtables_in_kv.count(); ++j) {
            ObDDLMemtable *cur_ddl_memtable = ddl_memtables_in_kv.at(j);
            if (ObITable::DDL_MEM_CO_SSTABLE == cur_ddl_memtable->get_key().table_type_) {
              has_co_ddl_memtable = true;
            }
          }
        }
      }
    }
  }
  return ret;
}

int ObTabletTableStore::pull_ddl_memtables(
    common::ObArenaAllocator &allocator,
    const ObTablet &tablet)
{
  int ret = OB_SUCCESS;
  ObArray<ObDDLKV *> ddl_memtables;
  if (OB_FAIL(tablet.get_ddl_kvs(ddl_memtables))) {
    LOG_WARN("failed to get ddl memtables array from tablet", K(ret));
  } else if (!ddl_memtables.empty() && OB_FAIL(ddl_mem_sstables_.init(allocator, ddl_memtables))) {
    LOG_WARN("assign ddl memtables failed", K(ret), K(ddl_memtables));
  }
  return ret;
}

int ObTabletTableStore::build_ddl_sstables(
    common::ObArenaAllocator &allocator,
    const ObTablet &tablet,
    const ObSSTable *new_sstable,
    const ObIArray<const ObSSTable *> &slice_sstables,
    const bool keep_old_ddl_sstable,
    const ObTabletTableStore &old_store)
{
  int ret = OB_SUCCESS;
  ObITable *new_table = const_cast<ObITable *>(static_cast<const ObITable *>(new_sstable));
  ObSSTableMetaHandle table_meta_handle;
  ObArray<ObITable *> ddl_dump_sstables;

  bool is_new_table_valid_ddl_sstable = false;
  bool is_slice_sstables_valid_ddl_sstable = false;
  bool need_keep_old_ddl_sstable = true;
  if (slice_sstables.count() > 0) {
    if (nullptr != new_table) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("slice sstables conflict with new_table", K(ret), K(slice_sstables.count()), KPC(new_table));
    } else if (OB_FAIL(check_ddl_slice_sstables_valid(tablet,
                                                      slice_sstables,
                                                      is_slice_sstables_valid_ddl_sstable))) {
      LOG_WARN("failed to check ddl slice sstables valid", KR(ret));
    }
  } else if (OB_ISNULL(new_table)) {
    is_new_table_valid_ddl_sstable = false;
  } else if (OB_FAIL(check_ddl_sstable_valid(tablet,
                                             static_cast<const ObSSTable *>(new_table),
                                             is_new_table_valid_ddl_sstable))) {
    LOG_WARN("fail to check ddl sstable valid", K(ret));
  }
  if (OB_SUCC(ret)) {
    // cleanup ddl sstables only happen in two cases:
    // 1. put major sstable into  table store
    // 2. ddl start log replace with a valid ddl sstable
    if (nullptr != new_sstable && new_sstable->is_major_sstable()) {
      need_keep_old_ddl_sstable = false;
    } else if (is_new_table_valid_ddl_sstable) {
      need_keep_old_ddl_sstable = keep_old_ddl_sstable;
    }
  }
  if (OB_FAIL(ret)) {
  } else if (need_keep_old_ddl_sstable) {
    if (!is_new_table_valid_ddl_sstable && !is_slice_sstables_valid_ddl_sstable) { // not valid ddl sstable, simple keep old ddl sstable
      if (OB_FAIL(old_store.ddl_sstables_.get_all_tables(ddl_dump_sstables))) {
        LOG_WARN("get ddl dump sstables failed", K(ret));
      }
    } else if (is_slice_sstables_valid_ddl_sstable) { // ddl slice sstables is valid
      if (OB_FAIL(build_sorted_ddl_slice_sstables(old_store.ddl_sstables_,
                                                  0,/*start_pos*/
                                                  old_store.ddl_sstables_.count()/*count*/,
                                                  slice_sstables,
                                                  ddl_dump_sstables))) {
        LOG_WARN("failed to build sorted ddl slice sstables", KR(ret));
      }
    } else { // new_table is valid, calculate new ddl sstables
      // since the scn is continuous in each table, so we can make it like this:
      // collect all ddl sstable and sort by:
      //   start_scn asc, means smaller start_scn first
      //   end_scn desc, means larger scn range first
      //   prefer ddl dump sstable
      // compare end_scn of this table with max end_scn, filter out the table whose end_scn is smaller
      if (OB_FAIL(build_sorted_ddl_sstables(old_store.ddl_sstables_,
                                            0,/*start_pos*/
                                            old_store.ddl_sstables_.count()/*count*/,
                                            new_table,
                                            ddl_dump_sstables))) {
        LOG_WARN("failed to build sorted ddl sstables", KR(ret));
      }
    }
  } else { // remove old ddl sstable, only happen when major exist or init ddl table store
    if (OB_FAIL(build_replace_ddl_sstables(old_store.ddl_sstables_,
                                           0,/*start_pos*/
                                           old_store.ddl_sstables_.count()/*count*/,
                                           is_new_table_valid_ddl_sstable,
                                           new_table,
                                           ddl_dump_sstables))) {
      LOG_WARN("failed to build replace ddl sstables", KR(ret));
    }
  }

  if (OB_SUCC(ret)) {
    if (ddl_dump_sstables.empty()) {
      //do nothing
    } else if (!major_tables_.empty()) {
      FLOG_INFO("major sstables is not empty, no need update ddl sstable", K(ddl_dump_sstables), K(major_tables_));
    } else if (tablet.get_tablet_meta().table_store_flag_.with_major_sstable()) {
      FLOG_INFO("tablet has with major sstable flag, no need update ddl sstable", K(tablet), K(ddl_dump_sstables));
    } else if (OB_FAIL(ddl_sstables_.init(allocator, ddl_dump_sstables))) {
      LOG_WARN("failed to init ddl_sstables", K(ret));
    }
  }
  if (slice_sstables.count() > 0) {
    LOG_INFO("build slice ddl sstables", K(ret), K(ddl_sstables_), K(slice_sstables.count()), K(old_store.ddl_sstables_.count()));
  }
  return ret;
}

int ObTabletTableStore::check_ddl_sstable_valid(
    const ObTablet &tablet,
    const ObSSTable *sstable,
    bool &is_valid)
{
  int ret = OB_SUCCESS;
  bool is_valid_ddl_sstable = false;
  ObSSTableMetaHandle table_meta_handle;
  if (OB_ISNULL(sstable)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null sstable", KR(ret), KP(sstable));
  } else if (OB_FAIL(sstable->get_meta(table_meta_handle))) {
    LOG_WARN("fail to get sstable meta handle", K(ret));
  } else {
    is_valid_ddl_sstable = sstable->is_ddl_dump_sstable()
        && table_meta_handle.get_sstable_meta().get_basic_meta().ddl_scn_ >= tablet.get_tablet_meta().ddl_start_scn_;
  }
  if (OB_SUCC(ret)) {
    is_valid = is_valid_ddl_sstable;
  }
  return ret;
}

int ObTabletTableStore::check_ddl_slice_sstables_valid(
    const ObTablet &tablet,
    const ObIArray<const ObSSTable *> &slice_sstables,
    bool &is_valid)
{
  int ret = OB_SUCCESS;
  bool is_valid_slice_sstables = true;
  for (int64_t i = 0; OB_SUCC(ret) && is_valid_slice_sstables && (i < slice_sstables.count()); ++i) {
    const ObSSTable *cur_sstable = slice_sstables.at(i);
    if (OB_FAIL(check_ddl_sstable_valid(tablet, cur_sstable, is_valid_slice_sstables))) {
      LOG_WARN("failed to check ddl sstable valid", KR(ret));
    }
  }
  if (OB_SUCC(ret)) {
    is_valid = is_valid_slice_sstables;
  }
  return ret;
}

int ObTabletTableStore::build_sorted_ddl_slice_sstables(
    const ObSSTableArray &old_ddl_tables,
    const int64_t start_pos,
    const int64_t count,
    const ObIArray<const ObSSTable *> &slice_sstables,
    ObIArray<ObITable *> &ddl_dump_sstables)
{
  int ret = OB_SUCCESS;
  ObArray<ObITable *> ddl_tables;
  int64_t merge_slice_idx = -1;
  const ObSSTable *first_slice_sstable = slice_sstables.at(0);
  ddl_dump_sstables.reset();
  if (OB_UNLIKELY((start_pos < 0)
                   || (count < 0)
                   || (start_pos > old_ddl_tables.count())
                   || (count > old_ddl_tables.count()))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid start_pos or count", KR(ret), K(start_pos), K(count), K(old_ddl_tables.count()));
  } else if (OB_ISNULL(first_slice_sstable)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("first slice sstable is null", K(ret), KP(first_slice_sstable));
  } else if (first_slice_sstable->get_key().slice_range_.is_merge_slice()) {
    // 0. filter out ddl sstables whose slice_idx < merge_slice_idx
    merge_slice_idx = first_slice_sstable->get_key().slice_range_.end_slice_idx_;
    for (int64_t i = start_pos; OB_SUCC(ret) && (i < start_pos + count); ++i) {
      ObITable *cur_table = old_ddl_tables.at(i);
      if (cur_table->get_slice_idx() > merge_slice_idx) {
        if (OB_FAIL(ddl_tables.push_back(cur_table))) {
          LOG_WARN("push back old ddl sstable failed", K(ret), KPC(cur_table));
        }
      } else {
        LOG_INFO("drop ddl sstable before merge slice sstable", K(merge_slice_idx), K(i), KPC(cur_table));
      }
    }
  } else {
    for (int64_t i = start_pos; OB_SUCC(ret) && (i < start_pos + count); ++i) {
      ObITable *cur_table = old_ddl_tables.at(i);
      if (OB_FAIL(ddl_tables.push_back(cur_table))) {
        LOG_WARN("push back old ddl sstable failed", K(ret), KPC(cur_table));
      }
    }
  }

  // 1. push back new slice
  for (int64_t i = 0; OB_SUCC(ret) && i < slice_sstables.count(); ++i) {
    if (OB_FAIL(ddl_tables.push_back(const_cast<ObITable *>(static_cast<const ObITable *>(slice_sstables.at(i)))))) {
      LOG_WARN("push back slice sstable failed", K(ret));
    }
  }

  // 2. sort via: slice_idx asc, start_scn asc, end_scn desc
  if (OB_SUCC(ret)) {
    lib::ob_sort(ddl_tables.begin(), ddl_tables.end(), [](ObITable *left, ObITable *right) {
        return left->get_slice_idx() != right->get_slice_idx() ? left->get_slice_idx() < right->get_slice_idx() :
        (left->get_start_scn() != right->get_start_scn() ? left->get_start_scn() < right->get_start_scn() :
          (left->get_end_scn() > right->get_end_scn()));
        });
  }

  // 3. add head ddl sstables
  for (int64_t i = 0; OB_SUCC(ret) && (i < start_pos); ++i) {
    ObITable *cur_table = old_ddl_tables.at(i);
    if (OB_FAIL(ddl_dump_sstables.push_back(cur_table))) {
      LOG_WARN("push back ddl dump sstable failed", K(ret), K(i));
    }
  }

  // 4. for each slice, filter out the table whose scn_range is covered by the previous table
  if (OB_SUCC(ret)) {
    int64_t last_slice_idx = -1;
    SCN max_end_scn = SCN::min_scn();
    for (int64_t i = 0; OB_SUCC(ret) && i < ddl_tables.count(); ++i) {
      ObITable *cur_table = ddl_tables.at(i);
      if (cur_table->get_slice_idx() < last_slice_idx) {
        ret = OB_ERR_UNDEFINED;
        LOG_WARN("ddl sstables should sorted by slice idx asc", K(ret), K(cur_table->get_key()), K(last_slice_idx));
      } else if (cur_table->get_slice_idx() > last_slice_idx) { // slice switched
        max_end_scn = SCN::min_scn();
        last_slice_idx = cur_table->get_slice_idx();
      }
      if (cur_table->get_end_scn() <= max_end_scn) {
        // drop it
        LOG_INFO("drop ddl sstable", K(i), K(max_end_scn), KPC(cur_table));
      } else if (OB_FAIL(ddl_dump_sstables.push_back(cur_table))) {
        LOG_WARN("push back ddl dump sstable failed", K(ret), K(i));
      } else {
        max_end_scn = cur_table->get_end_scn();
      }
    }
  }

  // 5. add tail ddl sstables
  for (int64_t i = start_pos + count; OB_SUCC(ret) && (i < old_ddl_tables.count()); ++i) {
    ObITable *cur_table = old_ddl_tables.at(i);
    if (OB_FAIL(ddl_dump_sstables.push_back(cur_table))) {
      LOG_WARN("push back ddl dump sstable failed", K(ret), K(i));
    }
  }
  return ret;
}

int ObTabletTableStore::build_sorted_ddl_sstables(
    const ObSSTableArray &old_ddl_tables,
    const int64_t start_pos,
    const int64_t count,
    ObITable *new_table,
    ObIArray<ObITable *> &ddl_dump_sstables)
{
  int ret = OB_SUCCESS;
  const int64_t ddl_sstable_count = old_ddl_tables.count() + 1;
  ObArray<ObITable *> ddl_sstables;
  ddl_dump_sstables.reset();
  if (OB_UNLIKELY((start_pos < 0)
                   || (count < 0)
                   || (start_pos > old_ddl_tables.count())
                   || (count > old_ddl_tables.count()))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid start_pos or count", KR(ret), K(start_pos), K(count), K(old_ddl_tables.count()));
  } else if (OB_ISNULL(new_table)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null new table", KR(ret), KP(new_table));
  } else if (OB_FAIL(ddl_sstables.reserve(ddl_sstable_count))) {
    LOG_WARN("reserve ddl sstable array failed", K(ret), K(ddl_sstable_count));
  }
  for (int64_t i = start_pos; OB_SUCC(ret) && (i < start_pos + count); ++i) {
    if (OB_FAIL(ddl_sstables.push_back(old_ddl_tables.at(i)))) {
      LOG_WARN("push back old ddl sstable failed", K(ret), K(i));
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(check_new_sstable_can_be_accepted_(old_ddl_tables, new_table))) {
      LOG_WARN("failed to check accept the compacted sstable", K(ret));
    } else if (OB_FAIL(ddl_sstables.push_back(new_table))) {
      LOG_WARN("push back new ddl sstable failed", K(ret), KPC(new_table));
    } else {
      lib::ob_sort(ddl_sstables.begin(), ddl_sstables.end(), [](ObITable *left, ObITable *right) {
          return left->get_start_scn() != right->get_start_scn() ?
          left->get_start_scn() < right->get_start_scn() : (left->get_end_scn() != right->get_end_scn() ?
            left->get_end_scn() > right->get_end_scn() : left->get_key().table_type_ < right->get_key().table_type_);
          });
    }
  }

  // add head ddl sstables
  for (int64_t i = 0; OB_SUCC(ret) && (i < start_pos); ++i) {
    ObITable *cur_table = old_ddl_tables.at(i);
    if (OB_FAIL(ddl_dump_sstables.push_back(cur_table))) {
      LOG_WARN("push back ddl dump sstable failed", K(ret), K(i));
    }
  }

  SCN max_end_scn = SCN::min_scn();
  for (int64_t i = 0; OB_SUCC(ret) && i < ddl_sstables.count(); ++i) {
    ObITable *cur_table = ddl_sstables.at(i);
    if (cur_table->get_end_scn() <= max_end_scn) {
      // drop it
      LOG_INFO("drop ddl sstable", K(i), K(max_end_scn), KPC(cur_table));
    } else if (OB_FAIL(ddl_dump_sstables.push_back(cur_table))) {
      LOG_WARN("push back ddl dump sstable failed", K(ret), K(i));
    } else {
      max_end_scn = cur_table->get_end_scn();
    }
  }

  // add tail ddl sstables
  for (int64_t i = start_pos + count; OB_SUCC(ret) && (i < old_ddl_tables.count()); ++i) {
    ObITable *cur_table = old_ddl_tables.at(i);
    if (OB_FAIL(ddl_dump_sstables.push_back(cur_table))) {
      LOG_WARN("push back ddl dump sstable failed", K(ret), K(i));
    }
  }

  return ret;
}

int ObTabletTableStore::build_replace_ddl_sstables(
    const ObSSTableArray &old_ddl_tables,
    const int64_t start_pos,
    const int64_t count,
    const bool is_new_table_valid_ddl_sstable,
    ObITable *new_table,
    ObIArray<ObITable *> &ddl_dump_sstables)
{
  int ret = OB_SUCCESS;
  const int64_t ddl_sstable_count = old_ddl_tables.count() + 1;
  ObArray<ObITable *> ddl_sstables;
  ddl_dump_sstables.reset();
  if (OB_UNLIKELY((start_pos < 0)
                   || (count < 0)
                   || (start_pos > old_ddl_tables.count())
                   || (count > old_ddl_tables.count()))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid start_pos or count", KR(ret), K(start_pos), K(count), K(old_ddl_tables.count()));
  } else if (OB_ISNULL(new_table)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null new table", KR(ret), KP(new_table));
  }
  // add head ddl sstables
  for (int64_t i = 0; OB_SUCC(ret) && (i < start_pos); ++i) {
    ObITable *cur_table = old_ddl_tables.at(i);
    if (OB_FAIL(ddl_dump_sstables.push_back(cur_table))) {
      LOG_WARN("push back ddl dump sstable failed", K(ret), K(i));
    }
  }
  if (OB_SUCC(ret) && is_new_table_valid_ddl_sstable) {
    if (OB_FAIL(check_new_sstable_can_be_accepted_(old_ddl_tables, new_table))) {
      LOG_WARN("failed to check accept the compacted sstable", K(ret));
    } else if (OB_FAIL(ddl_dump_sstables.push_back(new_table))) {
      LOG_WARN("push back ddl dump table failed", K(ret), KPC(new_table));
    } else {
      FLOG_INFO("push back ddl dump sstable success after clean up", KPC(new_table));
    }
  }
  // add tail ddl sstables
  for (int64_t i = start_pos + count; OB_SUCC(ret) && (i < old_ddl_tables.count()); ++i) {
    ObITable *cur_table = old_ddl_tables.at(i);
    if (OB_FAIL(ddl_dump_sstables.push_back(cur_table))) {
      LOG_WARN("push back ddl dump sstable failed", K(ret), K(i));
    }
  }
  return ret;
}

int ObTabletTableStore::build_memtable_array(const ObTablet &tablet)
{
  int ret = OB_SUCCESS;
  ObSEArray<ObITable *, MAX_MEMSTORE_CNT> memtable_array;
  if (OB_FAIL(tablet.get_memtables(memtable_array))) {
    LOG_WARN("failed to get all memtables from memtable_mgr", K(ret));
  } else if (memtable_array.empty()) {
    // empty memtable array
  } else if (OB_FAIL(memtables_.build(memtable_array))) {
    LOG_WARN("failed to build memtable array", K(ret), K(memtable_array));
  }
  return ret;
}

int ObTabletTableStore::check_ready_for_read(const ObTablet &tablet)
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret), KPC(this));
  } else if (is_ready_for_read_) {
    // skip, it's already for read
  } else if (OB_FAIL(check_continuous())) {
    LOG_WARN("failed to check continuous of tables", K(ret));
  } else if (minor_tables_.count() >= MAX_SSTABLE_CNT_IN_STORAGE
          || mds_sstables_.count() >= MAX_SSTABLE_CNT_IN_STORAGE) {
    ret = OB_SIZE_OVERFLOW;
    LOG_WARN("Too Many sstables in table store", K(ret), KPC(this));
  } else if (get_table_count() > ObTabletTableStore::MAX_SSTABLE_CNT) {
    ret = OB_SIZE_OVERFLOW;
    LOG_WARN("Too Many sstables, cannot add another sstable any more", K(ret), KPC(this));
  } else if (minor_tables_.empty()) {
    is_ready_for_read_ = true;
  } else {
    const SCN &clog_checkpoint_scn = tablet.get_tablet_meta().clog_checkpoint_scn_;
    const SCN &last_minor_end_scn = minor_tables_.get_boundary_table(true/*last*/)->get_end_scn();
    if (OB_UNLIKELY(clog_checkpoint_scn != last_minor_end_scn)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("last minor table's end_scn must be equal to clog_checkpoint_scn",
          K(ret), K(last_minor_end_scn), K(clog_checkpoint_scn), KPC(this));
    } else {
      is_ready_for_read_ = true;
    }
  }

  if (OB_SUCC(ret) && get_table_count() > EMERGENCY_SSTABLE_CNT) {
    int tmp_ret = OB_TOO_MANY_SSTABLE;
    LOG_WARN("Emergency SSTable count, maybe frequency freeze occurs, or maybe multi_version_start not adavanced.",
             K(tmp_ret),
             "major table count: ", major_tables_.count(),
             "minor table count: ", minor_tables_.count());
  }

  if (OB_SIZE_OVERFLOW == ret) {
    compaction::ObPartitionMergePolicy::diagnose_table_count_unsafe(compaction::MAJOR_MERGE, ObDiagnoseTabletType::TYPE_SPECIAL, tablet);
    MTL(concurrency_control::ObMultiVersionGarbageCollector *)->report_sstable_overflow();
  }
  return ret;
}

int ObTabletTableStore::check_continuous() const
{
  int ret = OB_SUCCESS;
  ObITable *prev_table = nullptr;
  ObITable *table = nullptr;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("table store not inited", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < major_tables_.count(); ++i) {
      if (OB_UNLIKELY(NULL == (table = major_tables_[i]) || !table->is_major_sstable())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_ERROR("table must be major table", K(ret), K(i), KPC(table));
      } else if (OB_ISNULL(prev_table)) {
      } else if (table->get_snapshot_version() < prev_table->get_snapshot_version()) {
        // recover ddl task may create new sstable with same major version and table key.
        ret = OB_ERR_UNEXPECTED;
        LOG_INFO("table version is invalid", K(ret), K(i), KPC(table), KPC(prev_table));
      }
      prev_table = table;
    }

    if (FAILEDx(check_minor_tables_(minor_tables_))) {
      LOG_WARN("failed to check minor tables continue", K(ret), K(minor_tables_));
    } else if (OB_FAIL(check_minor_tables_(mds_sstables_))) {
      LOG_WARN("failed to check mds tables continue", K(ret), K(mds_sstables_));
    }
  }
  return ret;
}

template <class T>
int ObTabletTableStore::check_minor_tables_(T &minor_tables, const bool no_remote_table) const
{
  int ret = OB_SUCCESS;
  ObITable *prev_table = nullptr;
  for (int64_t i = 0; OB_SUCC(ret) && i < minor_tables.count(); ++i) {
    ObITable *table =  minor_tables.at(i);

    if (OB_UNLIKELY(nullptr == table || !table->is_multi_version_minor_sstable())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("table must be multi version minor table", K(ret), KPC(table));
    } else if (no_remote_table && table->is_remote_logical_minor_sstable()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("get unexpected remote sstable", K(ret), KPC(table));
    } else if (nullptr == prev_table) {
      //do nothing
    } else if (table->get_start_scn() > prev_table->get_end_scn()
            || table->get_end_scn() <= prev_table->get_end_scn()) {
      ret = OB_ERR_SYS;
      LOG_ERROR("table scn range not continuous or overlap", K(ret), KPC(table), KPC(prev_table));
    }
    prev_table = table;
  }
  return ret;
}

int ObTabletTableStore::need_remove_old_table(
    const int64_t multi_version_start,
    bool &need_remove) const
{
  int ret = OB_SUCCESS;
  need_remove = false;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTabletTableStore not init", K(ret), K(*this));
  } else if (major_tables_.empty()) {
    // do nothing
  } else if (multi_version_start <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("get invalid arguments", K(ret), K(multi_version_start));
  } else if (minor_tables_.count() > 0 && minor_tables_[0]->get_upper_trans_version() <= major_tables_[0]->get_snapshot_version()) {
    // at least one minor sstable is coverd by major sstable
    // don't need to care about kept_multi_version_start here
    // becase major_tables_[0]::snapshot_version must <= kept_multi_version_start
    need_remove = true;
    LOG_INFO("need recycle unused minor table", K(ret), KPC(minor_tables_[0]), KPC(major_tables_[0]));
  } else if (major_tables_.count() > 1 && major_tables_[1]->get_snapshot_version() <= multi_version_start) {
    need_remove = true;
    LOG_INFO("need recycle oldest major sstable", K(ret), K(multi_version_start), KPC(major_tables_[1]));
  }
  return ret;
}

int ObTabletTableStore::build_ha_new_table_store(
    common::ObArenaAllocator &allocator,
    ObTablet &tablet,
    const ObBatchUpdateTableStoreParam &param,
    const ObTabletTableStore &old_store)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("double init", K(ret));
  } else if (OB_UNLIKELY(!param.is_valid() || !old_store.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("init tablet table store get invalid argument", K(ret), K(tablet), K(param), K(old_store));
  } else if (OB_FAIL(init(allocator, tablet))) {
    LOG_WARN("failed to init a new empty table store", K(ret));
  } else if (OB_FAIL(build_ha_new_table_store_(allocator, tablet, param, old_store))) {
    LOG_WARN("failed to build new table store with old store", K(ret));
  }
  return ret;
}

int ObTabletTableStore::check_new_sstable_can_be_accepted_(
    const ObSSTableArray &old_tables,
    ObITable *new_table)
{
  int ret = OB_SUCCESS;
  ObITable *old_table = nullptr;
  ObSSTableMetaHandle new_sst_meta_hdl;
  ObSSTableMetaHandle old_sst_meta_hdl;
  if (OB_FAIL(static_cast<ObSSTable *>(new_table)->get_meta(new_sst_meta_hdl))) {
    LOG_WARN("failed to get new sstable meta handle", K(ret), KPC(new_table));
  } else if (new_sst_meta_hdl.get_sstable_meta().get_basic_meta().table_backup_flag_.has_backup()) {
    // compaction during restore, and backup macro block is reused by the new table.
    bool has_remote_sstable = false;
    for (int64_t i = 0; OB_SUCC(ret) && i < old_tables.count(); ++i) {
      old_table = old_tables.at(i);
      if (OB_FAIL(static_cast<ObSSTable *>(old_table)->get_meta(old_sst_meta_hdl))) {
        LOG_WARN("failed to get old sstable meta handle", K(ret), KPC(old_table));
      } else if (old_sst_meta_hdl.get_sstable_meta().get_basic_meta().table_backup_flag_.has_backup()) {
        has_remote_sstable = true;
        break;
      }
    }

    if (!has_remote_sstable) {
      ret = OB_NO_NEED_MERGE;
      LOG_WARN("no remote sstable exist in old table store, but new table has backup macro block",
          K(ret), KPC(new_table), K(old_tables));
    }
  }
  
  return ret;
}

int ObTabletTableStore::build_ha_new_table_store_(
    common::ObArenaAllocator &allocator,
    const ObTablet &tablet,
    const ObBatchUpdateTableStoreParam &param,
    const ObTabletTableStore &old_store)
{
  int ret = OB_SUCCESS;
  const ObSSTableArray &meta_major_table = old_store.meta_major_tables_;
  const int64_t multi_version_start = tablet.get_multi_version_start();
  int64_t inc_base_snapshot_version = 0;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("table store is not inited", K(ret));
  } else if (OB_UNLIKELY(!param.is_valid() || !old_store.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("build ha new table store get invalid argument", K(ret), K(param), K(old_store));
  } else if (OB_FAIL(build_ha_major_tables_(allocator, param, old_store, multi_version_start, inc_base_snapshot_version))) {
    LOG_WARN("failed to build ha major tables", K(ret), K(param), K(multi_version_start), K(old_store));
  } else if (OB_FAIL(build_ha_mds_tables_(allocator, tablet, param, old_store))) {
    LOG_WARN("failed to build ha mds tables", K(ret), K(param), K(old_store));
  } else if (OB_FAIL(build_ha_minor_tables_(allocator, tablet, param, old_store, inc_base_snapshot_version))) {
    LOG_WARN("failed to build ha minor tables", K(ret), K(param), K(old_store));
  } else if (OB_FAIL(build_ha_ddl_tables_(allocator, tablet, param, old_store))) {
    LOG_WARN("failed to build ha ddl tables", K(ret), K(param), K(old_store));
  } else if (!meta_major_table.empty() && OB_FAIL(build_meta_major_table(allocator, nullptr/*new sstable*/, old_store.meta_major_tables_))) {
    LOG_WARN("failed to build meta major table", K(ret), K(old_store));
  } else if (OB_FAIL(build_major_checksum_info(old_store, NULL/*param*/, allocator))) {
    LOG_WARN("failed to build major checksum info", KR(ret), K(param));
  } else if (OB_FAIL(build_memtable_array(tablet))) {
    LOG_WARN("failed to pull memtable from memtable_mgr", K(ret));
  } else if (OB_FAIL(pull_ddl_memtables(allocator, tablet))) {
    LOG_WARN("pull_ddl_memtables failed", K(ret));
  } else {
    is_inited_ = true;
    if (OB_FAIL(check_ready_for_read(tablet))) {
      LOG_WARN("failed to check major sstable ready", K(ret));
    } else {
      try_cache_local_sstables(allocator);
      FLOG_INFO("succeed to build ha new table store", "tablet_id", tablet.get_tablet_meta().tablet_id_,
        K(major_tables_), K(minor_tables_), K(memtables_), K(PRINT_TS(*this)));
    }
  }

  return ret;
}

int ObTabletTableStore::build_ha_major_tables_(
    common::ObArenaAllocator &allocator,
    const ObBatchUpdateTableStoreParam &param,
    const ObTabletTableStore &old_store,
    const int64_t &multi_version_start,
    int64_t &inc_base_snapshot_version)
{
  int ret = OB_SUCCESS;
  inc_base_snapshot_version = -1;
  ObArray<ObITable *> major_tables;
  const bool allow_duplicate_sstable = true;

  if (!param.tables_handle_.empty() && OB_FAIL(param.tables_handle_.get_tables(major_tables))) {
    LOG_WARN("failed to get major tables from param", K(ret));
  } else if (OB_FAIL(inner_build_major_tables_(allocator, old_store, major_tables,
      multi_version_start, allow_duplicate_sstable, inc_base_snapshot_version))) {
    LOG_WARN("failed to inner build major tables", K(ret), K(param));
  }
  return ret;
}

int ObTabletTableStore::replace_transfer_minor_sstables_(
    common::ObArenaAllocator &allocator,
    const ObTablet &tablet,
    const ObBatchUpdateTableStoreParam &param,
    const ObTabletTableStore &old_store)
{
  int ret = OB_SUCCESS;
  ObArray<ObITable *> new_minor_tables;
  ObArray<ObITable *> need_add_minor_tables;
  ObSEArray<ObITable *, common::MAX_SSTABLE_CNT_IN_STORAGE> old_minor_tables;
  const int64_t inc_pos = 0;
  ObArray<ObITable *> cut_minor_tables;

  if (OB_FAIL(param.tables_handle_.get_all_minor_sstables(need_add_minor_tables))) {
    LOG_WARN("failed to add need add minor tables", K(ret), K(param));
  } else if (OB_FAIL(ObTableStoreUtil::sort_minor_tables(need_add_minor_tables))) {
    LOG_WARN("failed to sort minor tables", K(ret));
  } else if (OB_FAIL(old_store.minor_tables_.get_all_tables(old_minor_tables))) {
    LOG_WARN("failed to get old minor tables", K(ret), K(old_store));
  } else if (OB_FAIL(check_minor_tables_(old_minor_tables, true/*no remote table*/))) {
    LOG_WARN("failed to check old store minor sstables", K(ret), K(old_minor_tables));
  } else if (OB_FAIL(combine_transfer_minor_sstables_(allocator, tablet, old_minor_tables,
      need_add_minor_tables, param, new_minor_tables))) {
    LOG_WARN("failed to combine transfer minor sstables", K(ret), K(old_store), K(param));
  }

  if (OB_FAIL(ret)) {
  } else if (new_minor_tables.empty()) {
    LOG_INFO("minor tables is empty, skip it", K(ret), K(new_minor_tables));
  } else if (OB_FAIL(ObTableStoreUtil::sort_minor_tables(new_minor_tables))) {
    LOG_WARN("failed to sort minor tables", K(ret));
  } else if (OB_FAIL(cut_ha_sstable_scn_range_(allocator, new_minor_tables, cut_minor_tables))) {
    LOG_WARN("failed to cut ha sstable log ts range", K(ret), K(old_store), K(param));
  } else if (OB_FAIL(check_minor_tables_(cut_minor_tables))) {
    LOG_WARN("minor tables is not continue", K(ret), K(param), K(new_minor_tables), K(old_store));
  } else if (tablet.get_tablet_meta().ha_status_.is_data_status_complete()
      && cut_minor_tables.at(cut_minor_tables.count() - 1)->get_end_scn() != tablet.get_tablet_meta().clog_checkpoint_scn_) {
    // When the tablet's is_data_status_complete=false, it means that the data of the tablet has not been copied during the migration process.
    // In this scenario, no check is allowed.
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet meta is not match with minor sstables", K(ret), K(cut_minor_tables), K(param), K(tablet.get_tablet_meta()));
  } else if (OB_FAIL(minor_tables_.init(allocator, cut_minor_tables, inc_pos))) {
    LOG_WARN("failed to init minor_tables", K(ret));
  } else {
    LOG_INFO("succeed build transfer minor sstables", K(old_store), K(cut_minor_tables));
  }
  return ret;
}

//migration will add tables with ddl and minor tables. this func will copy old minor
//tables when add ddl tables with param.update_ddl_sstable_ = true.
//new_minor_tables is empty
//   The corresponding reason may be that there is no minor at the source or the transfer_in tablet still exists in the transfer table
//new_minor_tables is not empty
//   Tablet still has a transfer table, so need to check the continuity of the transfer table and minor sstable
int ObTabletTableStore::replace_ha_minor_sstables_(
    common::ObArenaAllocator &allocator,
    const ObTablet &tablet,
    const ObBatchUpdateTableStoreParam &param,
    const ObTabletTableStore &old_store,
    const int64_t inc_base_snapshot_version)
{
  int ret = OB_SUCCESS;
  ObArray<ObITable *> new_minor_tables;
  UNUSED(inc_base_snapshot_version);
  ObArray<ObITable *> need_add_minor_tables;
  ObSEArray<ObITable *, common::MAX_SSTABLE_CNT_IN_STORAGE> old_minor_tables;
  const int64_t inc_pos = 0;
  ObArray<ObITable *> cut_minor_tables;
  const SCN &clog_checkpoint_scn = tablet.get_tablet_meta().clog_checkpoint_scn_;

  if (OB_FAIL(param.tables_handle_.get_all_minor_sstables(need_add_minor_tables))) {
    LOG_WARN("failed to add need add minor tables", K(ret), K(param));
  } else if (OB_FAIL(ObTableStoreUtil::sort_minor_tables(need_add_minor_tables))) {
    LOG_WARN("failed to sort minor tables", K(ret));
  } else if (OB_FAIL(old_store.minor_tables_.get_all_tables(old_minor_tables))) {
    LOG_WARN("failed to get old minor tables", K(ret), K(old_store));
  } else if (OB_FAIL(check_minor_tables_(old_minor_tables, true/*no remote table*/))) {
    LOG_WARN("failed to check old store minor sstables", K(ret), K(old_minor_tables));
  } else if (OB_FAIL(combine_ha_multi_version_sstables_(
      clog_checkpoint_scn, old_minor_tables, need_add_minor_tables, new_minor_tables))) {
    LOG_WARN("failed to combin ha minor sstables", K(ret), K(old_store), K(param));
  } else if (new_minor_tables.empty()) { // no minor tables
    if (tablet.get_tablet_meta().has_transfer_table()
        && tablet.get_tablet_meta().transfer_info_.transfer_start_scn_ != clog_checkpoint_scn) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("tablet meta is not match with minor sstables", K(ret), K(new_minor_tables), K(param), K(old_store));
    } else {
      LOG_INFO("minor tables is empty, skip it", K(ret), K(new_minor_tables));
    }
  } else if (OB_FAIL(ObTableStoreUtil::sort_minor_tables(new_minor_tables))) {
    LOG_WARN("failed to sort minor tables", K(ret));
  } else if (OB_FAIL(cut_ha_sstable_scn_range_(allocator, new_minor_tables, cut_minor_tables))) {
    LOG_WARN("failed to cut ha sstable log ts range", K(ret), K(old_store), K(param));
  } else if (OB_FAIL(check_minor_tables_(cut_minor_tables))) {
    LOG_WARN("minor tables is not continue", K(ret), K(param), K(cut_minor_tables), K(old_store));
  } else if (cut_minor_tables.at(cut_minor_tables.count() - 1)->get_end_scn() != clog_checkpoint_scn
      || (tablet.get_tablet_meta().has_transfer_table()
          && tablet.get_tablet_meta().transfer_info_.transfer_start_scn_ != cut_minor_tables.at(0)->get_start_scn())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet meta is not match with minor sstables", K(ret), K(cut_minor_tables), K(param), K(old_store), "tablet_meta", tablet.get_tablet_meta());
  } else if (OB_FAIL(minor_tables_.init(allocator, cut_minor_tables, inc_pos))) {
    LOG_WARN("failed to init minor_tables", K(ret));
  } else {
    LOG_INFO("succeed build ha minor sstables", K(old_store), K(cut_minor_tables));
  }
  return ret;
}

int ObTabletTableStore::replace_ha_remote_minor_tables_(
    common::ObArenaAllocator &allocator,
    const ObTablet &tablet,
    const ObBatchUpdateTableStoreParam &param,
    const ObTabletTableStore &old_store,
    const int64_t inc_base_snapshot_version)
{
  UNUSED(inc_base_snapshot_version);
  int ret = OB_SUCCESS;
  ObArray<ObITable *> new_minor_tables;
  ObSEArray<ObITable *, common::MAX_SSTABLE_CNT_IN_STORAGE> old_minor_tables;
  const bool check_continue = true;
  const int64_t inc_pos = 0;
  if (OB_FAIL(old_store.minor_tables_.get_all_tables(old_minor_tables))) {
    LOG_WARN("failed to get old minor tables", K(ret), K(old_store));
  } else if (OB_FAIL(replace_ha_remote_sstables_(old_minor_tables, 
                                                 param.tables_handle_, 
                                                 check_continue, 
                                                 new_minor_tables))) {
    LOG_WARN("failed to replace remote minor tables", K(ret), K(old_store), K(param));
  } else if (new_minor_tables.empty()) {
  } else if (tablet.get_tablet_meta().ha_status_.is_data_status_complete()
      && new_minor_tables.at(new_minor_tables.count() - 1)->get_end_scn() != tablet.get_tablet_meta().clog_checkpoint_scn_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet meta is not match with minor sstables", K(ret), K(old_store), K(param), K(new_minor_tables), K(tablet.get_tablet_meta()));
  } else if (OB_FAIL(minor_tables_.init(allocator, new_minor_tables, inc_pos))) {
    LOG_WARN("failed to init minor tables", K(ret));
  } else {
    LOG_INFO("succeed replace ha remote minor sstables", K(old_store), K(new_minor_tables), K(param));
  }

  return ret;
}

// TODO@wenqu: fix check continue, sort ddl sstables
int ObTabletTableStore::build_ha_ddl_tables_(
    common::ObArenaAllocator &allocator,
    const ObTablet &tablet,
    const ObBatchUpdateTableStoreParam &param,
    const ObTabletTableStore &old_store)
{
  int ret = OB_SUCCESS;
  if (param.need_replace_remote_sstable_) {
    if (OB_FAIL(replace_ha_remote_ddl_tables_(allocator, tablet, param, old_store))) {
      LOG_WARN("failed to replace ha remote ddl tables", K(ret), K(param), K(old_store));
    }
  } else {
    if (OB_FAIL(replace_ha_ddl_tables_(allocator, tablet, param, old_store))) {
      LOG_WARN("failed to replace ha ddl tables", K(ret), K(param), K(old_store));
    }
  }
  return ret;
}

int ObTabletTableStore::replace_ha_ddl_tables_(
    common::ObArenaAllocator &allocator,
    const ObTablet &tablet,
    const ObBatchUpdateTableStoreParam &param,
    const ObTabletTableStore &old_store)
{
  //TODO(muwei.ym) need reuse local minor sstable and cut sstable log ts 4.3
  int ret = OB_SUCCESS;
  ObArray<ObITable *> ddl_tables;
  ObITable *new_table = nullptr;
  ObITable *last_ddl_table = nullptr;
  bool need_add_ddl_tables = true;
  ObSSTableMetaHandle new_meta_handle;

  if (!old_store.major_tables_.empty() || tablet.get_tablet_meta().table_store_flag_.with_major_sstable()) {
    need_add_ddl_tables = false;
  }

  for (int64_t i = 0; OB_SUCC(ret) && i < param.tables_handle_.get_count() && need_add_ddl_tables; ++i) {
    new_table = param.tables_handle_.get_table(i);
    if (OB_ISNULL(new_table) || !new_table->is_sstable()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("new table is null or table type is unexpected", K(ret), KPC(new_table));
    } else if (new_table->is_major_sstable()) {
      need_add_ddl_tables = false;
      break;
    } else if (!new_table->is_ddl_sstable()) {
      //do nothing
    } else if (OB_FAIL(static_cast<ObSSTable *>(new_table)->get_meta(new_meta_handle))) {
      LOG_WARN("get new table meta fail", K(ret), KPC(new_table));
    } else if (new_meta_handle.get_sstable_meta().get_basic_meta().ddl_scn_ < tablet.get_tablet_meta().ddl_start_scn_) {
      // the ddl start scn is old, drop it
    } else if (OB_NOT_NULL(last_ddl_table) && new_table->get_start_scn() != last_ddl_table->get_end_scn()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("ddl table is not continue", K(ret), K(param), K(old_store));
    } else if (OB_FAIL(ddl_tables.push_back(new_table))) {
      LOG_WARN("failed to push new table into array", K(ret), KPC(new_table));
    } else {
      last_ddl_table = new_table;
    }
  }

  for (int64_t i = 0; OB_SUCC(ret) && i < old_store.ddl_sstables_.count() && need_add_ddl_tables; ++i) {
    new_table = old_store.ddl_sstables_[i];
    if (OB_ISNULL(new_table) || (!new_table->is_ddl_sstable())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("new table is null or table type is unexpected", K(ret), KPC(new_table));
    } else if (OB_NOT_NULL(last_ddl_table) && new_table->get_start_scn() != last_ddl_table->get_end_scn()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("ddl table is not continue", K(ret), K(param), K(old_store));
    } else if (OB_FAIL(ddl_tables.push_back(new_table))) {
      LOG_WARN("failed to push new table into array", K(ret), KPC(new_table));
    } else {
      last_ddl_table = new_table;
    }
  }

  if (OB_SUCC(ret)) {
    if (!need_add_ddl_tables) {
      LOG_INFO("has major sstable ,no need add ddl sstable", K(param), K(old_store));
    } else if (ddl_tables.empty()) { // no minor tables
      LOG_INFO("ddl tables is empty, skip it", K(ret), K(ddl_tables));
    } else if (OB_FAIL(ddl_sstables_.init(allocator, ddl_tables))) {
      LOG_WARN("failed to init minor_tables", K(ret));
    }
  }
  return ret;
}

int ObTabletTableStore::replace_ha_remote_ddl_tables_(
    common::ObArenaAllocator &allocator,
    const ObTablet &tablet,
    const ObBatchUpdateTableStoreParam &param,
    const ObTabletTableStore &old_store)
{
  int ret = OB_SUCCESS;
  ObArray<ObITable *> new_ddl_tables;
  ObSEArray<ObITable *, common::MAX_SSTABLE_CNT_IN_STORAGE> old_ddl_tables;
  ObITable *major_table = nullptr;
  ObSSTableMetaHandle major_meta_handle;
  const bool check_continue = true;
  bool need_replace_ddl_tables = true;

  for (int64_t i = 0; OB_SUCC(ret) && i < old_store.major_tables_.count(); ++i) {
    major_table = old_store.major_tables_[i];
    if (OB_ISNULL(major_table)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("major table is null", K(ret), K(old_store));
    } else if (!major_table->is_major_sstable()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("table type is unexpected", K(ret), KPC(major_table));
    } else if (OB_FAIL(static_cast<ObSSTable *>(major_table)->get_meta(major_meta_handle))) {
      LOG_WARN("get major table meta fail", K(ret), KPC(major_table));
    } else if (major_meta_handle.get_sstable_meta().get_basic_meta().table_backup_flag_.has_backup()) {
      // This remote major is generated with ddl sstables by ddl commit. In this case, replace cannot
      // continue, should schedule replace remote major action again.
      ret = OB_NO_NEED_MERGE;
      LOG_WARN("remote major table exist, replace cannot continue", K(ret), KPC(major_table));
    } else {
      need_replace_ddl_tables = false;
    }
  }

  if (OB_FAIL(ret)) {
  } else if (!need_replace_ddl_tables) {
    LOG_INFO("major table exist, no need replace", K(old_store));
  } else if (OB_FAIL(old_store.ddl_sstables_.get_all_tables(old_ddl_tables))) {
    LOG_WARN("failed to get old ddl tables", K(ret), K(old_store));
  } else if (OB_FAIL(replace_ha_remote_sstables_(old_ddl_tables, 
                                                 param.tables_handle_, 
                                                 check_continue, 
                                                 new_ddl_tables))) {
    LOG_WARN("failed to replace remote ddl tables", K(ret), K(old_store), K(param));
  } else if (new_ddl_tables.empty()) {
    LOG_INFO("ddl tables is empty, skip it", K(old_store), K(param));
  } else if (OB_FAIL(ddl_sstables_.init(allocator, new_ddl_tables))) {
    LOG_WARN("failed to init ddl tables", K(ret));
  } else {
    LOG_INFO("succeed replace ha remote ddl sstables", K(old_store), K(new_ddl_tables), K(param));
  }

  return ret;
}

int ObTabletTableStore::build_ha_mds_tables_(
    common::ObArenaAllocator &allocator,
    const ObTablet &tablet,
    const ObBatchUpdateTableStoreParam &param,
    const ObTabletTableStore &old_store)
{
  int ret = OB_SUCCESS;
  if (param.need_replace_remote_sstable_) {
    if (OB_FAIL(replace_ha_remote_mds_tables_(allocator, tablet, param, old_store))) {
      LOG_WARN("failed to replace ha remote mds tables", K(ret), K(param), K(old_store));
    }
  } else {
    if (OB_FAIL(replace_ha_mds_tables_(allocator, tablet, param, old_store))) {
      LOG_WARN("failed to replace ha mds tables", K(ret), K(param), K(old_store));
    }
  }
  return ret;
}

int ObTabletTableStore::replace_ha_mds_tables_(
    common::ObArenaAllocator &allocator,
    const ObTablet &tablet,
    const ObBatchUpdateTableStoreParam &param,
    const ObTabletTableStore &old_store)
{
  int ret = OB_SUCCESS;
  ObArray<ObITable *> new_mds_tables;
  ObArray<ObITable *> need_add_mds_tables;
  ObSEArray<ObITable *, common::MAX_SSTABLE_CNT_IN_STORAGE> old_mds_tables;
  const int64_t inc_pos = 0;
  ObArray<ObITable *> cut_mds_tables;
  const SCN &mds_checkpoint_scn = tablet.get_tablet_meta().mds_checkpoint_scn_;

  if (OB_FAIL(param.tables_handle_.get_all_mds_sstables(need_add_mds_tables))) {
    LOG_WARN("failed to add need add mds tables", K(ret), K(param));
  } else if (OB_FAIL(ObTableStoreUtil::sort_minor_tables(need_add_mds_tables))) {
    LOG_WARN("failed to sort mds tables", K(ret), K(param), K(need_add_mds_tables));
  } else if (OB_FAIL(old_store.mds_sstables_.get_all_tables(old_mds_tables))) {
    LOG_WARN("failed to get old mds tables", K(ret), K(old_store));
  } else if (OB_FAIL(check_minor_tables_(old_mds_tables, true/*no remote table*/))) {
    LOG_WARN("failed to check old store minor sstables", K(ret), K(old_mds_tables));
  } else if (OB_FAIL(combine_ha_multi_version_sstables_(
      mds_checkpoint_scn, old_mds_tables, need_add_mds_tables, new_mds_tables))) {
    LOG_WARN("failed to combin ha mds sstables", K(ret), K(old_store), K(param));
  } else if (new_mds_tables.empty()) { // no mds tables
    LOG_INFO("minor tables is empty, skip it", K(old_store), K(param));
  } else if (OB_FAIL(ObTableStoreUtil::sort_minor_tables(new_mds_tables))) {
    LOG_WARN("failed to sort minor tables", K(ret));
  } else if (OB_FAIL(cut_ha_sstable_scn_range_(allocator, new_mds_tables, cut_mds_tables))) {
    LOG_WARN("failed to cut ha sstable log ts range", K(ret), K(old_store), K(param));
  } else if (OB_FAIL(check_minor_tables_(cut_mds_tables))) {
    LOG_WARN("minor tables is not continue", K(ret), K(param), K(cut_mds_tables), K(old_store));
  } else if (cut_mds_tables.at(cut_mds_tables.count() - 1)->get_end_scn() != mds_checkpoint_scn) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet meta is not match with minor sstables", K(ret), K(cut_mds_tables), K(param), K(old_store), "tablet_meta", tablet.get_tablet_meta());
  } else if (OB_FAIL(mds_sstables_.init(allocator, cut_mds_tables, inc_pos))) {
    LOG_WARN("failed to init mds_sstables", K(ret));
  } else {
    LOG_INFO("succeed build ha mds sstables", K(old_store), K(cut_mds_tables));
  }
  return ret;
}

int ObTabletTableStore::replace_ha_remote_mds_tables_(
    common::ObArenaAllocator &allocator,
    const ObTablet &tablet,
    const ObBatchUpdateTableStoreParam &param,
    const ObTabletTableStore &old_store)
{
  int ret = OB_SUCCESS;
  ObArray<ObITable *> new_mds_tables;
  ObSEArray<ObITable *, common::MAX_SSTABLE_CNT_IN_STORAGE> old_mds_tables;
  const bool check_continue = true;
  const int64_t inc_pos = 0;
  const SCN &mds_checkpoint_scn = tablet.get_tablet_meta().mds_checkpoint_scn_;

  if (OB_FAIL(old_store.mds_sstables_.get_all_tables(old_mds_tables))) {
    LOG_WARN("failed to get old minor tables", K(ret), K(old_store));
  } else if (OB_FAIL(replace_ha_remote_sstables_(old_mds_tables, 
                                                 param.tables_handle_, 
                                                 check_continue, 
                                                 new_mds_tables))) {
    LOG_WARN("failed to replace remote mds tables", K(ret), K(old_store), K(param));
  } else if (new_mds_tables.empty()) {
  } else if (tablet.get_tablet_meta().ha_status_.is_data_status_complete()
      && new_mds_tables.at(new_mds_tables.count() - 1)->get_end_scn() != mds_checkpoint_scn) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("mds_checkpoint_scn is not match with mds sstables", K(ret), K(old_store), K(param), K(new_mds_tables), K(tablet.get_tablet_meta()));
  } else if (OB_FAIL(mds_sstables_.init(allocator, new_mds_tables, inc_pos))) {
    LOG_WARN("failed to init mds tables", K(ret));
  } else {
    LOG_INFO("succeed replace ha remote mds sstables", K(old_store), K(new_mds_tables), K(param));
  }

  return ret;
}

int ObTabletTableStore::cut_ha_sstable_scn_range_(
    common::ObArenaAllocator &allocator,
    common::ObIArray<ObITable *> &orig_minor_sstables,
    common::ObIArray<ObITable *> &cut_minor_sstables)
{
  int ret = OB_SUCCESS;
  SCN last_end_scn = SCN::min_scn();
  for (int64_t i = 0; OB_SUCC(ret) && i < orig_minor_sstables.count(); ++i) {
    ObITable *table = orig_minor_sstables.at(i);

    if (OB_ISNULL(table) || !table->is_multi_version_minor_sstable()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("table is null or table type is unexpected", K(ret), KPC(table));
    } else if (0 == i) {
      last_end_scn = table->get_end_scn();
    } else if (last_end_scn < table->get_start_scn() || last_end_scn >= table->get_end_scn()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("minor sstable log ts is not continue or scn has overlap", K(ret), K(orig_minor_sstables));
    } else if (last_end_scn == table->get_start_scn()) {
      last_end_scn = table->get_end_scn();
    } else {
      ObSSTable *sstable = static_cast<ObSSTable *>(table);
      ObSSTable *orig_sstable = nullptr;
      ObSSTable *copied_sstable = nullptr;
      ObMetaDiskAddr addr;
      addr.set_mem_addr(0, sizeof(ObSSTable));
      ObStorageMetaHandle sstable_handle;
      if (sstable->is_loaded()) {
        orig_sstable = sstable;
      } else if (OB_FAIL(ObCacheSSTableHelper::load_sstable(
          sstable->get_addr(), sstable->is_co_sstable(), sstable_handle))) {
        LOG_WARN("failed to load sstable", K(ret), KPC(sstable));
      } else if (OB_FAIL(sstable_handle.get_sstable(orig_sstable))) {
        LOG_WARN("failed to get sstable from sstable handle", K(ret), K(sstable_handle));
      }

      if (FAILEDx(orig_sstable->deep_copy(allocator, copied_sstable))) {
        LOG_WARN("failed to deep copy sstable", K(ret), KPC(orig_sstable), KP(copied_sstable));
      } else if (OB_FAIL(copied_sstable->set_addr(addr))) {
        LOG_WARN("failed to set sstable addr", K(ret), K(addr), KPC(copied_sstable));
      } else {
        table = copied_sstable;
        ObScnRange new_scn_range;
        ObScnRange original_scn_range = table->get_scn_range();
        new_scn_range.start_scn_ = last_end_scn;
        new_scn_range.end_scn_ = table->get_end_scn();

        table->set_scn_range(new_scn_range);
        last_end_scn = table->get_end_scn();
        LOG_INFO("cut ha sstable log ts range", KPC(orig_sstable), KPC(copied_sstable),
            K(new_scn_range), K(original_scn_range));
      }
    }

    if (OB_SUCC(ret)) {
      if (OB_ISNULL(table)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("table should not be NULL", K(ret), KP(table));
      } else if (OB_FAIL(cut_minor_sstables.push_back(table))) {
        LOG_WARN("failed to add table into array", K(ret), KPC(table));
      }
    }
  }
  return ret;
}

int ObTabletTableStore::combine_ha_multi_version_sstables_(
    const share::SCN &scn,
    common::ObIArray<ObITable *> &old_store_sstables,
    common::ObIArray<ObITable *> &need_add_sstables,
    common::ObIArray<ObITable *> &new_sstables)
{
  int ret = OB_SUCCESS;
  SCN max_copy_end_scn;
  max_copy_end_scn.set_min();
  ObArray<ObITable *> tmp_sstables;

  for (int64_t i = 0; OB_SUCC(ret) && i < need_add_sstables.count(); ++i) {
    ObITable *table = need_add_sstables.at(i);
    if (OB_FAIL(tmp_sstables.push_back(table))) {
      LOG_WARN("failed to push table into array", K(ret), KPC(table));
    } else {
      max_copy_end_scn = table->get_end_scn();
    }
  }

  for (int64_t i = 0; OB_SUCC(ret) && i < old_store_sstables.count(); ++i) {
    ObITable *table = old_store_sstables.at(i);
    if (table->is_remote_logical_minor_sstable()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("old store minor sstable contains logical sstable, unexpected", K(ret), K(old_store_sstables));
    } else if (table->get_end_scn() <= max_copy_end_scn) {
      //do nothing
    } else if (OB_FAIL(tmp_sstables.push_back(table))) {
      LOG_WARN("failed to push table into array", K(ret), KPC(table));
    }
  }

  if (OB_SUCC(ret)) {
    //TODO(muwei.ym) remove compare with clog checkpoint scn in 4.2 RC3
    if (tmp_sstables.empty()) {
      //do nothing
    } else if (OB_FAIL(ObTableStoreUtil::sort_minor_tables(tmp_sstables))) {
      LOG_WARN("failed to sort sstables", K(ret), K(tmp_sstables));
    } else if (scn > tmp_sstables.at(tmp_sstables.count() - 1)->get_end_scn()) {
      FLOG_INFO("scn is bigger than all sstables end scn, no need to keep it",
          K(scn), K(tmp_sstables), K(major_tables_));
    } else if (OB_FAIL(new_sstables.assign(tmp_sstables))) {
      LOG_WARN("failed to assign minor sstables", K(ret), K(tmp_sstables));
    }
  }

  return ret;
}

int ObTabletTableStore::combine_transfer_minor_sstables_(
    common::ObArenaAllocator &allocator,
    const ObTablet &tablet,
    common::ObIArray<ObITable *> &old_store_minor_sstables,
    common::ObIArray<ObITable *> &need_add_minor_sstables,
    const ObBatchUpdateTableStoreParam &param,
    common::ObIArray<ObITable *> &new_minor_sstables)
{
  int ret = OB_SUCCESS;

  for (int64_t i = 0; OB_SUCC(ret) && i < need_add_minor_sstables.count(); ++i) {
    ObITable *table = need_add_minor_sstables.at(i);
    if (OB_ISNULL(table) || !table->is_minor_sstable()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("new table is null or table type is unexpected", K(ret), KPC(table));
    } else if (table->get_start_scn() >= tablet.get_tablet_meta().transfer_info_.transfer_start_scn_) {
      //do nothing
    } else if (table->get_end_scn() <= tablet.get_tablet_meta().transfer_info_.transfer_start_scn_) {
      if (OB_FAIL(new_minor_sstables.push_back(table))) {
        LOG_WARN("failed to push table into array", K(ret), KPC(table));
      }
    } else {
      ObSSTable *old_sstable = static_cast<ObSSTable *>(table);
      ObSSTable *sstable = NULL;
      if (OB_FAIL(old_sstable->deep_copy(allocator, sstable))) {
        LOG_WARN("failed to copy sstable", K(ret), KP(old_sstable));
      } else {
        ObScnRange new_scn_range;
        ObScnRange original_scn_range = sstable->get_scn_range();
        new_scn_range.start_scn_ = original_scn_range.start_scn_;
        new_scn_range.end_scn_ = tablet.get_tablet_meta().transfer_info_.transfer_start_scn_;
        sstable->set_scn_range(new_scn_range);
        FLOG_INFO("cut ha sstable log ts range", KPC(sstable), K(new_scn_range), K(original_scn_range), K(param));

        if (OB_FAIL(new_minor_sstables.push_back(sstable))) {
          LOG_WARN("failed to push table into array", K(ret), KPC(sstable));
        }
      }
    }
  }

  for (int64_t i = 0; OB_SUCC(ret) && i < old_store_minor_sstables.count(); ++i) {
    ObITable *table = old_store_minor_sstables.at(i);
    if (OB_ISNULL(table) || !table->is_minor_sstable()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("table is null or table type is unexpected", K(ret), KPC(table));
    } else if (OB_FAIL(new_minor_sstables.push_back(table))) {
      LOG_WARN("failed to push minor table into array", K(ret), K(old_store_minor_sstables), KPC(table));
    }
  }

  return ret;
}

// restore replace remote sstables.
// All remote sstables must be replaced with local sstables at once.
int ObTabletTableStore::replace_ha_remote_sstables_(
    const common::ObIArray<ObITable *> &old_store_sstables,
    const ObTablesHandleArray &new_tables_handle,
    const bool check_continue,
    common::ObIArray<ObITable *> &out_sstables)
{
  int ret = OB_SUCCESS;
  ObITable *old_table = nullptr;
  ObITable *new_table = nullptr;
  ObITable *last_table = nullptr;
  ObSSTable *new_sstable = nullptr;
  ObTableHandleV2 new_table_handle;
  bool has_backup_macro = false;

  for (int64_t i = 0; OB_SUCC(ret) && i < new_tables_handle.get_count(); ++i) {
    new_table = new_tables_handle.get_table(i);
    if (OB_ISNULL(new_table)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("new table is null", K(ret), KP(new_table));
    } else if (!new_table->is_sstable()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("new table is not sstable", K(ret), KPC(new_table));
    } else if (OB_FAIL(ObTableStoreUtil::check_has_backup_macro_block(new_table, has_backup_macro))) {
      LOG_WARN("failed to check new table has backup macro block", K(ret), KPC(new_table));
    } else if (has_backup_macro) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("new table still has backup macro block", K(ret), KPC(new_table));
    }
  }

  for (int64_t i = 0; OB_SUCC(ret) && i < old_store_sstables.count(); ++i) {
    has_backup_macro = false;
    new_table_handle.reset();
    old_table = old_store_sstables.at(i);
    if (OB_ISNULL(old_table)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("table is null", K(ret), KP(old_table));
    } else if (!old_table->is_sstable()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("old table is not sstable", K(ret), KPC(old_table));
    } else if (OB_FAIL(ObTableStoreUtil::check_has_backup_macro_block(old_table, has_backup_macro))) {
      LOG_WARN("failed to check old table has backup macro block", K(ret), KPC(old_table));
    } else if (!has_backup_macro) {
      // this table does not has backup macro block, no need to be replaced.
      if (check_continue && OB_NOT_NULL(last_table) && old_table->get_start_scn() != last_table->get_end_scn()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("table is not continue", K(ret), KPC(last_table), KPC(old_table));
      } else if (OB_FAIL(out_sstables.push_back(old_table))) {
        LOG_WARN("failed to push old table into array", K(ret), KPC(old_table));
      } else {
        last_table = old_table;
      }
    } else {
      // this table needs to be replaced.
      if (OB_FAIL(new_tables_handle.get_table(old_table->get_key(), new_table_handle))) {
        LOG_WARN("failed to get new table", K(ret), KPC(old_table));
        if (OB_ENTRY_NOT_EXIST == ret) {
          new_sstable = static_cast<ObSSTable*>(old_table);
          ret = OB_SUCCESS;
          LOG_INFO("old table may have been merged, keep it", K(new_tables_handle), KPC(old_table));
        }
      } else if (OB_FAIL(new_table_handle.get_sstable(new_sstable))) {
        LOG_WARN("failed to get sstable", K(ret), K(new_table_handle));
      }

      if (OB_FAIL(ret)) {
      } else if (check_continue && OB_NOT_NULL(last_table) && new_sstable->get_start_scn() != last_table->get_end_scn()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("table is not continue", K(ret), KPC(last_table), KPC(new_sstable), KPC(old_table));
      } else if (OB_FAIL(out_sstables.push_back(new_sstable))) {
        LOG_WARN("failed to push new table into array", K(ret), KPC(new_sstable));
      } else {
        last_table = new_sstable;
        LOG_INFO("replace one remote sstable", KPC(old_table), KPC(new_sstable));
      }
    }
  }
  return ret;
}

int ObTabletTableStore::build_ha_minor_tables_(
    common::ObArenaAllocator &allocator,
    const ObTablet &tablet,
    const ObBatchUpdateTableStoreParam &param,
    const ObTabletTableStore &old_store,
    const int64_t inc_base_snapshot_version)
{
  int ret = OB_SUCCESS;
  if (param.is_transfer_replace_) {
    if (OB_FAIL(replace_transfer_minor_sstables_(allocator, tablet, param, old_store))) {
      LOG_WARN("failed to replace transfer minor tables", K(ret), K(param), K(old_store));
    }
  } else if (param.need_replace_remote_sstable_) {
    if (OB_FAIL(replace_ha_remote_minor_tables_(allocator, tablet, param, old_store, inc_base_snapshot_version))) {
      LOG_WARN("failed to replace ha remote minor tables", K(ret), K(param), K(old_store));
    }
  } else {
    if (OB_FAIL(replace_ha_minor_sstables_(allocator, tablet, param, old_store, inc_base_snapshot_version))) {
      LOG_WARN("failed to replace ha minor tables", K(ret), K(param), K(old_store));
    }
  }
  return ret;
}

//TODO (muwei.ym) transfer compatible code, can be removed in next barrier version
int ObTabletTableStore::get_mini_minor_sstables_(ObTableStoreIterator &iter) const
{
  int ret = OB_SUCCESS;
  SCN max_fill_tx_scn(SCN::min_scn());
  SCN max_end_scn(SCN::min_scn());

  for (int64_t i = 0; OB_SUCC(ret) && i < minor_tables_.count(); ++i) {
    ObSSTable *table = minor_tables_[i];
    if (OB_ISNULL(table)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("table should not be NULL", K(ret), K(minor_tables_), KP(table));
    } else {
      max_fill_tx_scn = SCN::max(max_fill_tx_scn, table->get_filled_tx_scn());
      max_end_scn = SCN::max(max_end_scn, table->get_end_scn());
    }
  }

  if (OB_SUCC(ret)) {
    if (max_end_scn < max_fill_tx_scn) {
      //do nothing
      LOG_INFO("max end scn is smaller than max fill tx scn, cannot minor merge", K(max_end_scn), K(max_fill_tx_scn), K(minor_tables_));
    } else if (OB_FAIL(iter.add_tables(minor_tables_, 0, minor_tables_.count()))) {
      LOG_WARN("failed to get all minor tables", K(ret));
    }
  }
  return ret;
}

int ObTabletTableStore::check_skip_split_tables_exist_(
    const ObBatchUpdateTableStoreParam &param,
    const ObTabletTableStore &old_store,
    const ObTablet &tablet)
{
  int ret = OB_SUCCESS;
  const ObTabletID &tablet_id = tablet.get_tablet_meta().tablet_id_;
  const bool is_major_merge = is_major_merge_type(param.tablet_split_param_.merge_type_);
  const ObIArray<ObITable::TableKey> &skip_split_keys = param.tablet_split_param_.skip_split_keys_;
  const ObSSTableArray &old_majors = old_store.major_tables_;
  if (is_major_merge && !skip_split_keys.empty()) {
    for (int64_t i = 0; OB_SUCC(ret) && i < skip_split_keys.count(); i++) {
      ObSSTableWrapper wrapper;
      ObITable::TableKey this_key = skip_split_keys.at(i);
      this_key.tablet_id_ = tablet_id;
      if (OB_UNLIKELY(!this_key.is_major_sstable())) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid param", K(ret), K(this_key), K(param));
      } else if (OB_FAIL(old_majors.get_table(this_key, wrapper))) {
        LOG_WARN("found table in old majors failed", K(ret));
      } else if (OB_UNLIKELY(!wrapper.is_valid())) {
        ret = OB_ENTRY_NOT_EXIST;
        LOG_WARN("skipped split sstable does not exist", K(ret), K(this_key), K(param));
      }
    }
  }
  return ret;
}

int ObTabletTableStore::build_split_new_table_store(
    common::ObArenaAllocator &allocator,
    ObTablet &tablet,
    const ObBatchUpdateTableStoreParam &param,
    const ObTabletTableStore &old_store)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("double init", K(ret));
  } else if (OB_UNLIKELY(!param.is_valid() || !old_store.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("init tablet table store get invalid argument", K(ret), K(tablet), K(param), K(old_store));
  } else if (OB_FAIL(init(allocator, tablet))) {
    LOG_WARN("failed to init a new empty table store", K(ret));
  } else if (OB_FAIL(check_skip_split_tables_exist_(param, old_store, tablet))) {
    LOG_WARN("check skip data split tables exist failed", K(ret), K(param), K(old_store));
  } else if (OB_FAIL(build_split_new_table_store_(allocator, tablet, param, old_store))) {
    LOG_WARN("failed to build new table store with old store", K(ret));
  }
  return ret;
}

int ObTabletTableStore::get_all_minor_sstables(
    ObTableStoreIterator &iter) const
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("table store is not inited", K(ret));
  } else if (OB_FAIL(iter.add_tables(minor_tables_, 0, minor_tables_.count()))) {
    LOG_WARN("failed to get all minor tables", K(ret));
  }
  return ret;
}

int ObTabletTableStore::build_split_new_table_store_(
    common::ObArenaAllocator &allocator,
    const ObTablet &tablet,
    const ObBatchUpdateTableStoreParam &param,
    const ObTabletTableStore &old_store)
{
  int ret = OB_SUCCESS;
  UpdateUpperTransParam unused_param;
  int64_t inc_base_snapshot_version = -1;
  ObSEArray<ObITable *, OB_DEFAULT_SE_ARRAY_COUNT> batch_tables;
  const ObTabletHAStatus &ha_status = tablet.get_tablet_meta().ha_status_;
  ObSSTable *new_mds_sstable = nullptr;
  bool is_update_firstly = is_mds_merge(param.tablet_split_param_.merge_type_);
  if (OB_FAIL(param.tables_handle_.get_tables(batch_tables))) {
    LOG_WARN("get tables failed", K(ret), K(param));
  } else if (OB_FAIL(inner_build_major_tables_(allocator, old_store, batch_tables,
      param.tablet_split_param_.multi_version_start_,
      true/*allow_duplicate_sstable*/,
      inc_base_snapshot_version))) {
    LOG_WARN("failed to inner build major tables", K(ret), K(param), K(batch_tables));
  } else if (OB_FAIL(build_split_minor_tables_(allocator, old_store, batch_tables,
      inc_base_snapshot_version,
      ha_status))) {
    LOG_WARN("failed to inner build minor tables", K(ret), K(param), K(batch_tables));
  } else {
    ObITable *last_major = nullptr;
    // It must be the last one if there is a meta major sstable, which is decided by the `ObTabletSplitUtil::build_update_table_store_param`.
    ObITable *last_sstable = batch_tables.empty() ? nullptr : batch_tables.at(batch_tables.count() - 1);
    if (OB_NOT_NULL(last_sstable) && last_sstable->is_meta_major_sstable()) {
      if (OB_ISNULL(last_major = major_tables_.get_boundary_table(true))) {
        LOG_INFO("no major sstable exists, skip to try to build meta sstable", K(param));
      } else if (last_sstable->get_max_merged_trans_version() <= last_major->get_snapshot_version()) {
        LOG_INFO("the new meta merge sstable is covered by major", K(ret), KPC(last_sstable), KPC(last_major));
      } else if (OB_FAIL(build_meta_major_table(allocator, static_cast<blocksstable::ObSSTable *>(last_sstable), old_store.meta_major_tables_))) {
        LOG_WARN("failed to build meta major tables", K(ret));
      }
    }
  }

  if (FAILEDx(build_memtable_array(tablet))) {
    LOG_WARN("failed to pull memtable from memtable_mgr", K(ret));
  } else if (OB_FAIL(pull_ddl_memtables(allocator, tablet))) {
    LOG_WARN("pull_ddl_memtables failed", K(ret));
  } else if (is_mds_merge(param.tablet_split_param_.merge_type_) && 1 != batch_tables.count()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected new table count", K(ret), K(param));
  } else if (is_mds_merge(param.tablet_split_param_.merge_type_)
      && OB_FAIL(ObTabletSplitUtil::check_split_minors_can_be_accepted(
        old_store.mds_sstables_,
        batch_tables,
        is_update_firstly))) {
    LOG_WARN("check split mds can be accepted failed", K(ret));
  } else if (OB_FAIL(build_mds_minor_tables(allocator,
                                            is_update_firstly ? static_cast<ObSSTable *>(batch_tables.at(0)) : nullptr,
                                            old_store.mds_sstables_))) {
    LOG_WARN("failed to build mds sstables", K(ret));
  } else {
    is_inited_ = true;
    if (OB_FAIL(check_ready_for_read(tablet))) {
      LOG_WARN("failed to check major sstable ready", K(ret));
    } else {
      try_cache_local_sstables(allocator);
      FLOG_INFO("succeed to build split new table store", K(major_tables_), K(minor_tables_), K(memtables_), K(PRINT_TS(*this)));
    }
  }
  return ret;
}

int ObTabletTableStore::build_fork_new_table_store(
    common::ObArenaAllocator &allocator,
    ObTablet &tablet,
    const ObBatchUpdateTableStoreParam &param,
    const ObTabletTableStore &old_store)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("double init when build fork new table store", KR(ret), "tablet_id", tablet.get_tablet_id());
  } else if (OB_UNLIKELY(!param.is_valid() || !old_store.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument to build fork new table store", KR(ret),
        "tablet_id", tablet.get_tablet_id(),
        "param_fork", param.tablet_fork_param_,
        "param_valid", param.is_valid(),
        "old_store_valid", old_store.is_valid());
  } else if (OB_FAIL(init(allocator, tablet))) {
    LOG_WARN("failed to init empty table store for fork", KR(ret), "tablet_id", tablet.get_tablet_id(),
        "param_fork", param.tablet_fork_param_);
  } else if (OB_FAIL(build_fork_new_table_store_(allocator, tablet, param, old_store))) {
    LOG_WARN("failed to build fork new table store", KR(ret), "tablet_id", tablet.get_tablet_id(),
        "param_fork", param.tablet_fork_param_);
  }
  return ret;
}

int ObTabletTableStore::build_fork_new_table_store_(
    common::ObArenaAllocator &allocator,
    const ObTablet &tablet,
    const ObBatchUpdateTableStoreParam &param,
    const ObTabletTableStore &old_store)
{
  int ret = OB_SUCCESS;
  ObSEArray<ObITable *, OB_DEFAULT_SE_ARRAY_COUNT> batch_tables;
  int64_t inc_base_snapshot_version = -1;

  if (OB_FAIL(param.tables_handle_.get_tables(batch_tables))) {
    LOG_WARN("get tables failed when build fork new table store", KR(ret),
        "tablet_id", tablet.get_tablet_id(), "param_fork", param.tablet_fork_param_);
  } else if (OB_FAIL(inner_build_major_tables_(
      allocator,
      old_store,
      batch_tables,
      param.tablet_fork_param_.multi_version_start_,
      false/*allow_duplicate_sstable*/,
      inc_base_snapshot_version))) {
    LOG_WARN("failed to inner build major tables for fork", KR(ret),
        "tablet_id", tablet.get_tablet_id(),
        "param_fork", param.tablet_fork_param_,
        "batch_cnt", batch_tables.count(),
        K(inc_base_snapshot_version));
  } else if (OB_FAIL(build_fork_minor_tables_(allocator, tablet, param, old_store, batch_tables))) {
    LOG_WARN("failed to build fork minor tables", KR(ret),
        "tablet_id", tablet.get_tablet_id(),
        "param_fork", param.tablet_fork_param_,
        "batch_cnt", batch_tables.count());
  } else {
    is_inited_ = true;
    if (OB_FAIL(check_ready_for_read(tablet))) {
      LOG_WARN("failed to check ready for read after fork build", KR(ret), "tablet_id", tablet.get_tablet_id(),
          "param_fork", param.tablet_fork_param_);
    } else {
      try_cache_local_sstables(allocator);
      LOG_INFO("succeed to build fork new table store",
          "tablet_id", tablet.get_tablet_id(),
          "param_fork", param.tablet_fork_param_,
          "major_cnt", major_tables_.count(),
          "minor_cnt", minor_tables_.count(),
          "mem_cnt", memtables_.count(),
          K_(is_ready_for_read));
      LOG_DEBUG("fork new table store detail", "tablet_id", tablet.get_tablet_id(), K(PRINT_TS(*this)));
    }
  }

  return ret;
}

int ObTabletTableStore::build_fork_minor_tables_(
    common::ObArenaAllocator &allocator,
    const ObTablet &tablet,
    const ObBatchUpdateTableStoreParam &param,
    const ObTabletTableStore &dst_store,
    const ObIArray<ObITable *> &tables_array)
{
  int ret = OB_SUCCESS;
  ObArray<ObITable *> src_minor_tables;
  ObSEArray<ObITable *, common::MAX_SSTABLE_CNT_IN_STORAGE> dst_minor_tables;
  const SCN &clog_checkpoint_scn = tablet.get_tablet_meta().clog_checkpoint_scn_;
  const int64_t fork_snapshot_version = param.tablet_fork_param_.snapshot_version_;

  // 1. Extract src minor sstables from tables_array
  for (int64_t i = 0; OB_SUCC(ret) && i < tables_array.count(); i++) {
    ObITable *src_table = tables_array.at(i);
    if (OB_ISNULL(src_table)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected null table when extracting fork minor tables", KR(ret),
          "tablet_id", tablet.get_tablet_id(), K(i), K(fork_snapshot_version));
    } else if (src_table->is_minor_sstable() && OB_FAIL(src_minor_tables.push_back(src_table))) {
      LOG_WARN("failed to push back src minor table", KR(ret),
          "tablet_id", tablet.get_tablet_id(), K(i), K(fork_snapshot_version), KPC(src_table));
    }
  }

  // 2. Sort src minor tables
  if (OB_FAIL(ret)) {
  } else if (!src_minor_tables.empty() && OB_FAIL(ObTableStoreUtil::sort_minor_tables(src_minor_tables))) {
    LOG_WARN("failed to sort src minor tables for fork", KR(ret),
        "tablet_id", tablet.get_tablet_id(), "src_minor_cnt", src_minor_tables.count(), K(fork_snapshot_version));
  }

  // 3. Get dst minor sstables from dst_store
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(dst_store.minor_tables_.get_all_tables(dst_minor_tables))) {
    LOG_WARN("failed to get dst minor tables for fork", KR(ret),
        "tablet_id", tablet.get_tablet_id(), "dst_minor_cnt", dst_minor_tables.count(), K(fork_snapshot_version));
  } else if (OB_FAIL(check_minor_tables_(dst_minor_tables, true/*no remote table*/))) {
    LOG_WARN("failed to check dst store minor sstables for fork", KR(ret),
        "tablet_id", tablet.get_tablet_id(), "dst_minor_cnt", dst_minor_tables.count(), K(fork_snapshot_version));
  }

  // 4. Check continuity and merge with dst minor tables
  if (OB_SUCC(ret)) {
    ObITable *newest_minor_in_src = src_minor_tables.empty() ? nullptr : src_minor_tables.at(src_minor_tables.count() - 1);
    ObITable *oldest_minor_in_dst = dst_minor_tables.empty() ? nullptr : dst_minor_tables.at(0);
    ObArray<ObITable *> merged_minor_tables;

    if (OB_NOT_NULL(newest_minor_in_src) && OB_NOT_NULL(oldest_minor_in_dst) &&
        newest_minor_in_src->get_end_scn() != oldest_minor_in_dst->get_start_scn()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("non-continuous fork minor tables", KR(ret),
          "tablet_id", tablet.get_tablet_id(),
          KPC(newest_minor_in_src), KPC(oldest_minor_in_dst),
          "src_end_scn", newest_minor_in_src->get_end_scn(),
          "dst_start_scn", oldest_minor_in_dst->get_start_scn(),
          "src_minor_cnt", src_minor_tables.count(),
          "dst_minor_cnt", dst_minor_tables.count(),
          K(fork_snapshot_version), K(clog_checkpoint_scn));
    } else if (OB_FAIL(merged_minor_tables.push_back(src_minor_tables))) {
      LOG_WARN("failed to push back src minor tables", KR(ret),
          "tablet_id", tablet.get_tablet_id(), "src_minor_cnt", src_minor_tables.count(), K(fork_snapshot_version));
    } else if (OB_FAIL(merged_minor_tables.push_back(dst_minor_tables))) {
      LOG_WARN("failed to push back dst minor tables", KR(ret),
          "tablet_id", tablet.get_tablet_id(), "dst_minor_cnt", dst_minor_tables.count(), K(fork_snapshot_version));
    } else if (merged_minor_tables.empty()) {
      // No minor tables after merge
    } else if (OB_FAIL(ObTableStoreUtil::sort_minor_tables(merged_minor_tables))) {
      LOG_WARN("failed to sort merged minor tables for fork", KR(ret),
          "tablet_id", tablet.get_tablet_id(), "merged_cnt", merged_minor_tables.count(), K(fork_snapshot_version));
    } else if (OB_FAIL(check_minor_tables_(merged_minor_tables))) {
      ObITable *first = merged_minor_tables.at(0);
      ObITable *last = merged_minor_tables.at(merged_minor_tables.count() - 1);
      LOG_WARN("merged minor tables is not continuous for fork", KR(ret),
          "tablet_id", tablet.get_tablet_id(),
          "merged_cnt", merged_minor_tables.count(),
          "first", OB_ISNULL(first) ? "NULL" : to_cstring(first->get_key()),
          "last", OB_ISNULL(last) ? "NULL" : to_cstring(last->get_key()),
          K(fork_snapshot_version), K(clog_checkpoint_scn));
    } else if (merged_minor_tables.at(merged_minor_tables.count() - 1)->get_end_scn() != clog_checkpoint_scn) {
      ret = OB_ERR_UNEXPECTED;
      const ObITable *last = merged_minor_tables.at(merged_minor_tables.count() - 1);
      LOG_WARN("tablet meta clog_checkpoint_scn not match with merged fork minors", KR(ret),
          "tablet_id", tablet.get_tablet_id(),
          "merged_cnt", merged_minor_tables.count(),
          "last_end_scn", OB_ISNULL(last) ? SCN::invalid_scn() : last->get_end_scn(),
          K(clog_checkpoint_scn), K(fork_snapshot_version));
    } else if (OB_FAIL(minor_tables_.init(allocator, merged_minor_tables, 0))) {
      LOG_WARN("failed to init merged minor tables for fork", KR(ret),
          "tablet_id", tablet.get_tablet_id(), "merged_cnt", merged_minor_tables.count(), K(fork_snapshot_version));
    } else {
      ObITable *first = merged_minor_tables.at(0);
      ObITable *last = merged_minor_tables.at(merged_minor_tables.count() - 1);
      LOG_INFO("succeed to build fork minor tables",
          "tablet_id", tablet.get_tablet_id(),
          "src_minor_cnt", src_minor_tables.count(),
          "dst_minor_cnt", dst_minor_tables.count(),
          "merged_cnt", merged_minor_tables.count(),
          "first_scn", OB_ISNULL(first) ? SCN::invalid_scn() : first->get_start_scn(),
          "last_scn", OB_ISNULL(last) ? SCN::invalid_scn() : last->get_end_scn(),
          K(fork_snapshot_version), K(clog_checkpoint_scn));
    }
  }

  return ret;
}

// TODO(@DanLing) support split inc major sstable here.
int ObTabletTableStore::build_split_minor_tables_(
    common::ObArenaAllocator &allocator,
    const ObTabletTableStore &old_store,
    const ObIArray<ObITable *> &tables_array,
    const int64_t inc_base_snapshot_version,
    const ObTabletHAStatus &ha_status)
{
  int ret = OB_SUCCESS;
  ObArray<ObITable *> minor_tables;
  for (int64_t i = 0; OB_SUCC(ret) && i < tables_array.count(); i++) {
    ObITable *new_table = tables_array.at(i);
    if (OB_ISNULL(new_table)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected err", K(ret));
    } else if (new_table->is_minor_sstable() && OB_FAIL(minor_tables.push_back(new_table))) {
      LOG_WARN("push back failed", K(ret));
    }
  }
  if (OB_FAIL(ret)) {
  } else if (minor_tables.empty()) {
    // not split minor tables.
  } else if (OB_UNLIKELY(minor_tables.count() != tables_array.count())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected err", K(ret), K(minor_tables), K(tables_array));
  } else if (OB_FAIL(ObTableStoreUtil::sort_minor_tables(minor_tables))) {
    LOG_WARN("failed to sort minor tables", K(ret));
  } else {
    // check continuous.
    ObITable *oldest_minor_in_old_store = nullptr;
    ObITable *newest_minor_in_split = nullptr;
    if (old_store.minor_tables_.empty()) {
      LOG_INFO("empty minor in split dst tablet, skip check");
    } else if (OB_ISNULL(oldest_minor_in_old_store = old_store.minor_tables_[0])) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected err", K(ret), K(old_store.minor_tables_));
    } else if (OB_ISNULL(newest_minor_in_split = minor_tables.at(minor_tables.count() - 1))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected err", K(ret), K(minor_tables));
    } else if (newest_minor_in_split->get_end_scn() == oldest_minor_in_old_store->get_start_scn()) {
      // expected status, end_scn of the newest split minor equals to the start_scn of the existed oldest table.
    } else if (newest_minor_in_split->get_end_scn() < oldest_minor_in_old_store->get_start_scn()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected err, non-continuous split minors is caught", K(ret), KPC(oldest_minor_in_old_store), KPC(newest_minor_in_split));
    } else {
      // update batch split-minors to the table store repeatedly is possible, and update repeatedly without error is necessary.
      const SCN &smallest_start_scn_in_old_store = old_store.minor_tables_.at(0)->get_start_scn();
      const SCN &biggest_end_scn_in_old_store = old_store.minor_tables_.at(old_store.minor_tables_.count() - 1)->get_end_scn();
      const SCN &smallest_start_scn_in_new_input = minor_tables.at(0)->get_start_scn();
      const SCN &biggest_end_scn_in_new_input = minor_tables.at(minor_tables.count() - 1)->get_end_scn();
      if (OB_LIKELY(smallest_start_scn_in_old_store <= smallest_start_scn_in_new_input 
        && biggest_end_scn_in_old_store >= biggest_end_scn_in_new_input)) {
        // update batch split-minors to the table store repeatedly again.
        LOG_INFO("update split minors repeatedly, ignore to add again", K(minor_tables), K(old_store.minor_tables_));
        minor_tables.reset(); // reset, to ignore to add split-minors again.
      } else {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected err is caught", K(ret), K(minor_tables), K(old_store.minor_tables_));
      }
    }
  }

  if (OB_SUCC(ret)) {
    bool start_add = false;
    for (int64_t i = 0; OB_SUCC(ret) && i < old_store.minor_tables_.count(); ++i) {
      ObSSTable *table = old_store.minor_tables_[i];
      if (OB_ISNULL(table)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected err", K(ret), K(old_store.minor_tables_));
      } else if (start_add) {
      } else if (!ha_status.check_allow_read()) {
        start_add = true;
      } else if (table->get_upper_trans_version() > inc_base_snapshot_version) {
        start_add = true;
      }
      if (start_add && OB_FAIL(minor_tables.push_back(table))) {
        LOG_WARN("push back failed", K(ret));
      }
    }

    if (OB_FAIL(ret) || minor_tables.empty()) {
    } else if (OB_FAIL(ObTableStoreUtil::sort_minor_tables(minor_tables))) {
      LOG_WARN("failed to sort minor tables", K(ret));
    } else if (OB_FAIL(minor_tables_.init(allocator, minor_tables))) {
      LOG_WARN("failed to init minor_tables", K(ret));
    }
  }
  return ret;
}

int ObTabletTableStore::only_replace_major_(
    common::ObArenaAllocator &allocator,
    const ObTabletTableStore &old_store,
    const ObIArray<ObITable *> &tables_array,
    int64_t &inc_base_snapshot_version)
{
  int ret = OB_SUCCESS;
  inc_base_snapshot_version = -1;
  ObSSTableWrapper wrapper;

  for (int64_t i = 0; OB_SUCC(ret) && i < tables_array.count(); ++i) {
    const ObITable *table = tables_array.at(i);
    ObITable *old_major_table = nullptr;
    wrapper.reset();
    if (OB_ISNULL(table)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("table should not be NULL", K(ret), KP(table));
    } else if (OB_FAIL(old_store.major_tables_.get_table(table->get_key(), wrapper))) {
      LOG_WARN("failed to get table", K(ret), KPC(table), K(old_store));
    } else if (!wrapper.is_valid()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("old major table do not exist, unexpected", K(ret), K(wrapper), KPC(table), K(old_store));
    }
  }

  if (FAILEDx(replace_sstables(allocator, &tables_array, old_store.major_tables_, major_tables_))) {
    LOG_WARN("failed to get replaced major tables", K(ret));
  } else {
    inc_base_snapshot_version = major_tables_.at(0)->get_snapshot_version();
  }
  return ret;
}

int64_t ObTabletTableStore::to_string(char *buf, const int64_t buf_len) const
{
  int64_t pos = 0;
  if (OB_ISNULL(buf) || buf_len <= 0) {
      // do nothing
  } else {
    J_OBJ_START();
    J_KV(KP(this), K_(version), K_(major_tables), K_(minor_tables), K_(ddl_sstables), K_(meta_major_tables),
         K_(mds_sstables), K_(memtables), K_(major_ckm_info), K_(is_ready_for_read));
    J_COMMA();
    J_ARRAY_START();
    if (major_tables_.is_valid()) {
      for (int64_t i = 0; i < major_tables_.count(); ++i) {
        const ObSSTable *table = major_tables_[i];
        J_OBJ_START();
        J_KV(K(i), "addr", table->get_addr(),
            "is_loaded", table->is_loaded(),
            "type", ObITable::get_table_type_name(table->get_key().table_type_),
            "tablet_id", table->get_key().tablet_id_,
            "scn_range", table->get_key().scn_range_,
            "snapshot_version", table->get_snapshot_version(),
            "max_merge_version", table->get_max_merged_trans_version());
        J_OBJ_END();
        J_COMMA();
      }
    }
    if (minor_tables_.is_valid()) {
      for (int64_t i = 0; i < minor_tables_.count(); ++i) {
        const ObSSTable *table = minor_tables_[i];
        J_OBJ_START();
        J_KV(K(i), "addr", table->get_addr(),
            "type", ObITable::get_table_type_name(table->get_key().table_type_),
            "tablet_id", table->get_key().tablet_id_,
            "scn_range", table->get_key().scn_range_,
            "contain_uncommitted_row", table->contain_uncommitted_row() ? "yes" : "no",
            "max_merge_version", table->get_max_merged_trans_version(),
            "upper_trans_version", table->get_upper_trans_version());
        J_OBJ_END();
        J_COMMA();
      }
    }
    if (ddl_sstables_.is_valid()) {
      for (int64_t i = 0; i < ddl_sstables_.count(); ++i) {
        const ObSSTable *table = ddl_sstables_[i];
        J_OBJ_START();
        J_KV(K(i), "type", ObITable::get_table_type_name(table->get_key().table_type_),
            "tablet_id", table->get_key().tablet_id_,
            "scn_range", table->get_key().scn_range_,
            "max_merge_version", table->get_max_merged_trans_version());
        J_OBJ_END();
        J_COMMA();
      }
    }
    if (ddl_mem_sstables_.is_valid()) {
      for (int64_t i = 0; i < ddl_mem_sstables_.count(); ++i) {
        ObDDLKV *table = ddl_mem_sstables_[i];
        if (NULL != table) {
          J_OBJ_START();
          ObScnRange scn_range;
          scn_range.start_scn_ = table->get_start_scn();
          scn_range.end_scn_ = table->get_freeze_scn();
          J_KV(K(i), "type", ObITable::get_table_type_name(ObITable::DDL_MEM_SSTABLE),
              "tablet_id", table->get_tablet_id(),
              "scn_range", scn_range,
              "ref", table->get_ref(),
              "max_merge_version", table->get_snapshot_version());
          J_OBJ_END();
          J_COMMA();
        }
      }
    }
    if (meta_major_tables_.is_valid()) {
      for (int64_t i = 0; i < meta_major_tables_.count(); ++i) {
        const ObSSTable *table = meta_major_tables_[i];
        J_OBJ_START();
        J_KV(K(i), "type", ObITable::get_table_type_name(table->get_key().table_type_),
            "tablet_id", table->get_key().tablet_id_,
            "scn_range", table->get_key().scn_range_,
            "max_merge_version", table->get_max_merged_trans_version());
        J_OBJ_END();
        J_COMMA();
      }
    }
    BUF_PRINTF("}");

    J_OBJ_END();
    J_OBJ_END();
  }
  return pos;
}

int ObTabletTableStore::build_major_checksum_info(
    const ObTabletTableStore &old_store,
    const ObUpdateTableStoreParam *param,
    ObArenaAllocator &allocator)
{
  int ret = OB_SUCCESS;
  const ObMajorChecksumInfo *ptr = NULL;

  if (!GCTX.is_shared_storage_mode()) {
    // do nothing
  } else if (NULL != param
          && is_major_merge_type(param->get_merge_type())
          && nullptr == param->sstable_
          && !param->get_major_ckm_info().is_empty()) {
    // use major ckm info in input param
    if (!old_store.major_ckm_info_.is_empty()) {
      if (OB_UNLIKELY(param->get_major_ckm_info().get_compaction_scn() != old_store.major_ckm_info_.get_compaction_scn())) {
        // only output_mode item could replace validate_mode item
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("major ckm info in old table store is not empty", KR(ret), K(old_store), "new_ckm_info", param->get_major_ckm_info());
      } else {
        // use major ckm with large exec mode
        // EXEC_MODE_OUTPUT > EXEC_MODE_CALC_CKM
        const ObExecMode old_mode = old_store.major_ckm_info_.get_exec_mode();
        const ObExecMode param_mode = param->get_major_ckm_info().get_exec_mode();
        ptr = (old_mode <= param_mode) ? &param->get_major_ckm_info() : &old_store.get_major_ckm_info();
      }
    } else {
      ptr = &param->get_major_ckm_info();
    }
  } else if (!old_store.major_ckm_info_.is_empty()) {
    // assign major ckm info from old table store
    ptr = &old_store.major_ckm_info_;
  }
  if (OB_FAIL(ret) || OB_ISNULL(ptr) || ptr->is_empty()) {
  } else if (major_tables_.empty() || major_tables_.get_boundary_table(true/*last*/)->get_snapshot_version() < ptr->get_compaction_scn()) {
    if (OB_FAIL(major_ckm_info_.assign(*ptr, &allocator))) {
      LOG_WARN("failed to assgin major ckm info", KR(ret), KPC(ptr));
    } else {
      LOG_INFO("success to build major checksum info", KR(ret), K_(major_ckm_info), KPC(param));
    }
  }
  return ret;
}

ObPrintTableStore::ObPrintTableStore(const ObTabletTableStore &table_store)
  : major_tables_(table_store.major_tables_),
    minor_tables_(table_store.minor_tables_),
    memtables_(table_store.memtables_),
    ddl_mem_sstables_(table_store.ddl_mem_sstables_),
    ddl_sstables_(table_store.ddl_sstables_),
    meta_major_tables_(table_store.meta_major_tables_),
    mds_sstables_(table_store.mds_sstables_),
    is_ready_for_read_(table_store.is_ready_for_read_)
{
}

int64_t ObPrintTableStore::to_string(char *buf, const int64_t buf_len) const
{
  int64_t pos = 0;
  if (OB_ISNULL(buf) || buf_len <= 0) {
    // do nothing
  } else {
    J_OBJ_START();
    J_NAME("ObTabletTableStore_Pretty");
    J_COLON();
    J_KV(KP(this), K_(major_tables), K_(minor_tables), K_(mds_sstables), K_(memtables), K_(is_ready_for_read));
    J_COMMA();
    BUF_PRINTF("table_array");
    J_COLON();
    J_OBJ_START();
    if (!major_tables_.empty() || !minor_tables_.empty() || !memtables_.empty() || !ddl_mem_sstables_.empty()
        || !ddl_sstables_.empty() || !meta_major_tables_.empty() || !mds_sstables_.empty()) {
      ObCurTraceId::TraceId *trace_id = ObCurTraceId::get_trace_id();
      J_NEWLINE();
      // table_type|max_merge_version
      //      |upper_trans_version|start_scn|end_scn|ref|buffer_minor
      BUF_PRINTF("[%ld][%s][T%ld] [", GETTID(), GETTNAME(), GET_TENANT_ID());
      BUF_PRINTO(PC(trace_id));
      BUF_PRINTF("] ");
      BUF_PRINTF(" %-18s %-22s %-16s %-16s %-19s %-19s %-19s %-19s %-4s %-16s \n",
          "table_arr", "table_type", "start_slice_idx", "end_slice_idx", "upper_trans_ver", "max_merge_ver",
          "start_scn", "end_scn", "ref", "uncommit_row");
      bool is_print = false;
      print_arr(major_tables_, "MAJOR", buf, buf_len, pos, is_print);
      print_arr(minor_tables_, "MINOR", buf, buf_len, pos, is_print);
      print_ddl_mem(ddl_mem_sstables_, "DDL_MEM", buf, buf_len, pos, is_print);
      print_mem(memtables_, "MEM", buf, buf_len, pos, is_print);
      print_arr(ddl_sstables_, "DDL_SSTABLES", buf, buf_len, pos, is_print);
      print_arr(meta_major_tables_, "META_MAJOR", buf, buf_len, pos, is_print);
      print_arr(mds_sstables_, "MDS", buf, buf_len, pos, is_print);
    } else {
      J_EMPTY_OBJ();
    }
    J_OBJ_END();
    J_OBJ_END();
  }
  return pos;
}

void ObPrintTableStore::print_mem(
    const ObMemtableArray &tables,
    const char* table_arr,
    char *buf,
    const int64_t buf_len,
    int64_t &pos,
    bool &is_print) const
{
  for (int64_t i = 0; i < tables.count(); ++i) {
    if (is_print && 0 == i) {
      J_NEWLINE();
    }
    table_to_string(tables[i], i == 0 ? table_arr : " ", buf, buf_len, pos);
    if (i < tables.count() - 1) {
      J_NEWLINE();
    }
  }
  if (tables.count() > 0) {
    is_print = true;
  }
}
void ObPrintTableStore::print_ddl_mem(
    const ObDDLKVArray &tables,
    const char* table_arr,
    char *buf,
    const int64_t buf_len,
    int64_t &pos,
    bool &is_print) const
{
  for (int64_t i = 0; i < tables.count(); ++i) {
    if (is_print && 0 == i) {
      J_NEWLINE();
    }
    ddl_kv_to_string(tables[i], i == 0 ? table_arr : " ", buf, buf_len, pos);
    if (i < tables.count() - 1) {
      J_NEWLINE();
    }
  }
  if (tables.count() > 0) {
    is_print = true;
  }
}

void ObPrintTableStore::print_arr(
    const ObSSTableArray &tables,
    const char* table_arr,
    char *buf,
    const int64_t buf_len,
    int64_t &pos,
    bool &is_print) const
{
  for (int64_t i = 0; i < tables.count(); ++i) {
    if (is_print && 0 == i) {
      J_NEWLINE();
    }
    table_to_string(tables[i], i == 0 ? table_arr : " ", buf, buf_len, pos);
    if (i < tables.count() - 1) {
      J_NEWLINE();
    }
  }
  if (tables.count() > 0) {
    is_print = true;
  }
}

void ObPrintTableStore::table_to_string(
     ObITable *table,
     const char* table_arr,
     char *buf,
     const int64_t buf_len,
     int64_t &pos) const
{
  if (nullptr != table) {
    ObCurTraceId::TraceId *trace_id = ObCurTraceId::get_trace_id();
    BUF_PRINTF("[%ld][%s][T%ld] [", GETTID(), GETTNAME(), GET_TENANT_ID());
    BUF_PRINTO(PC(trace_id));
    BUF_PRINTF("] ");
    const char* table_name = table->is_sstable()
      ? ObITable::get_table_type_name(table->get_key().table_type_)
      : (table->is_active_memtable() ? "ACTIVE" : "FROZEN");
    const char * uncommit_row = table->is_sstable()
      ? (table->is_co_sstable() ? "unused" : (static_cast<ObSSTable *>(table)->contain_uncommitted_row() ? "true" : "false"))
      : "unused";

    BUF_PRINTF(" %-18s %-22s %-16u %-16u %-19lu %-19lu %-19lu %-19lu %-4ld %-16s ",
      table_arr,
      table_name,
      table->get_key().slice_range_.start_slice_idx_,
      table->get_key().slice_range_.end_slice_idx_,
      table->get_upper_trans_version(),
      table->get_max_merged_trans_version(),
      table->get_start_scn().get_val_for_tx(),
      table->get_end_scn().get_val_for_tx(),
      table->get_ref(),
      uncommit_row);
  }
}

void ObPrintTableStore::ddl_kv_to_string(
     ObDDLKV *table,
     const char* table_arr,
     char *buf,
     const int64_t buf_len,
     int64_t &pos) const
{
  if (nullptr != table) {
    ObCurTraceId::TraceId *trace_id = ObCurTraceId::get_trace_id();
    BUF_PRINTF("[%ld][%s][T%ld] [", GETTID(), GETTNAME(), GET_TENANT_ID());
    BUF_PRINTO(PC(trace_id));
    BUF_PRINTF("] ");
    const char *table_name = ObITable::get_table_type_name(ObITable::DDL_MEM_SSTABLE);
    const char *uncommit_row = "false";

    BUF_PRINTF(" %-18s %-22s %-16u %-16u %-19lu %-19lu %-19lu %-19lu %-4ld %-16s ",
      table_arr,
      table_name,
      table->get_key().slice_range_.start_slice_idx_,
      table->get_key().slice_range_.end_slice_idx_,
      table->get_snapshot_version(),
      table->get_snapshot_version(),
      table->get_start_scn().get_val_for_tx(),
      table->get_freeze_scn().get_val_for_tx(),
      table->get_ref(),
      uncommit_row);
  }
}

int ObTabletTableStore::adjust_sstable_start_scn_(
    ObSSTable &sstable,
    ObArenaAllocator &allocator,
    const share::SCN start_scn,
    ObSSTable *&copied_sstable)
{
  int ret = OB_SUCCESS;
  ObStorageMetaHandle sstable_handle;
  ObSSTable *src = NULL;
  if (sstable.is_loaded()) {
    src = &sstable;
  } else if (OB_FAIL(ObCacheSSTableHelper::load_sstable(sstable.get_addr(), sstable.is_co_sstable(), sstable_handle))) {
    LOG_WARN("load sstable fail", K(ret));
  } else if (OB_FAIL(sstable_handle.get_sstable(src))) {
    LOG_WARN("get sstable fail", K(ret));
  }
  ObMetaDiskAddr addr;
  addr.set_mem_addr(0, sizeof(ObSSTable));
  if (FAILEDx(ObSSTable::copy_from_old_sstable(*src, allocator, copied_sstable))) {
    LOG_WARN("deep copy sstable fail", K(ret));
  } else if (OB_FAIL(copied_sstable->set_addr(addr))) {
    LOG_WARN("set addr for attached sstable fail", K(ret));
  } else {
    copied_sstable->get_scn_range().start_scn_ = start_scn;
    FLOG_INFO("adjust sstable's start_scn equals to new table's end_scn to avoid range crossing",
              "table_key", sstable.get_key(),
              "orig_scn_range", sstable.get_scn_range(),
              "new_scn_range", copied_sstable->get_scn_range());
  }
  return ret;
}
} // namespace storage
} // namespace oceanbase
