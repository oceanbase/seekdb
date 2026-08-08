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

#ifndef OCEANBASE_STORAGE_TABLET_OB_TABLET_MDS_PART
#define OCEANBASE_STORAGE_TABLET_OB_TABLET_MDS_PART

#include "lib/ob_errno.h"
#include "common/meta_programming/ob_meta_serialization.h"
#include "common/meta_programming/ob_meta_copy.h"
#include "storage/multi_data_source/mds_table_handle.h"
#include "storage/meta_mem/ob_tablet_pointer.h"
#include "storage/tablet/ob_mds_row_iterator.h"
#include "storage/tablet/ob_tablet_mds_data.h"
#include "storage/tablet/ob_tablet_mds_node_filter.h"
#include "storage/tablet/ob_tablet_member_wrapper.h"
#include "storage/tablet/ob_tablet_obj_load_helper.h"
#include "storage/ls/ob_ls_switch_checker.h"

namespace oceanbase
{
namespace storage
{
class ObTabletCreateDeleteHelper;
class ObMdsRowIterator;
template <typename K, typename T>
class ObMdsRangeQueryIterator;

template <typename T>
struct MdsDefaultDeepCopyOperation {
  MdsDefaultDeepCopyOperation(T &value, ObIAllocator *alloc) : value_(value), alloc_(alloc) {}
  int operator()(const T &value) {
    int ret = OB_SUCCESS;
    if (nullptr == alloc_) {
      ret = meta::copy_or_assign(value, value_);
    } else {
      ret = meta::copy_or_assign(value, value_, *alloc_);
    }
    return ret;
  }
  T &value_;
  ObIAllocator *alloc_;
};

class ObITabletMdsInterface
{
  friend class ObTabletCreateDeleteHelper;
  friend class ObDirectLoadMgr; // TODO(@gaishun.gs): refactor later
public:
  // new mds
  // Currently, we only support read LATEST multi source data, so please pass MAX_SCN as snapshot.
  // Other value will cause OB_NOT_SUPPOTED error.
  // Snapshot read operation will be implemented after multi source data dumped into macro blocks.
  template <typename T>// general set for dummy key unit
  int set(T &&data, mds::MdsCtx &ctx, const int64_t lock_timeout_us = 0);
  template <typename Key, typename Value>// general set for multi key unit
  int set(const Key &key, Value &&data, mds::MdsCtx &ctx, const int64_t lock_timeout_us = 0);
  template <typename Key, typename Value>// general remove for multi key unit
  int remove(const Key &key, mds::MdsCtx &ctx, const int64_t lock_timeout_us = 0);
  // sometimes mds ndoes needed be forcely released, e.g.: ls offline
  template <typename T>
  int is_locked_by_others(bool &is_locked, const mds::MdsWriter &self = mds::MdsWriter()) const;

  int check_tablet_status_written(bool &written) const;
  // belows are wrapper interfaces for default getter for simple data structure
  // specialization get for each module
  int get_latest_tablet_status(ObTabletCreateDeleteMdsUserData &data,
                               mds::MdsWriter &writer,
                               mds::TwoPhaseCommitState &trans_stat,
                               share::SCN &trans_version,
                               const int64_t read_seq = 0) const;
  int get_tablet_status(const share::SCN &snapshot,
                        ObTabletCreateDeleteMdsUserData &data,
                        const int64_t timeout = ObTabletCommon::DEFAULT_GET_TABLET_DURATION_US) const;
  int get_latest_ddl_data(ObTabletBindingMdsUserData &data,
                          mds::MdsWriter &writer,
                          mds::TwoPhaseCommitState &trans_stat,
                          share::SCN &trans_version,
                          const int64_t read_seq = 0) const;
  int get_ddl_data(const share::SCN &snapshot,
                   ObTabletBindingMdsUserData &data,
                   const int64_t timeout = ObTabletCommon::DEFAULT_GET_TABLET_DURATION_US) const;
  int get_autoinc_seq(ObIAllocator &allocator,
                      const share::SCN &snapshot,
                      ObTabletAutoincSeq &data,
                      const int64_t timeout = ObTabletCommon::DEFAULT_GET_TABLET_DURATION_US) const;

  // if trans_stat < BEFORE_PREPARE, trans_version is explained as prepare_version(which is MAX).
  // else if trans_stat < ON_PREAPRE, trans_version is explained as prepare_version(which is MIN).
  // else if trans_stat < ON_COMMIT, trans_version is explained as prepare_version(which is a valid data).
  // else if trans_stat == ON_COMMIT, trans_version is explained as commit_version(which is a valid data).
  template <typename T, typename T2 = T, ENABLE_IF_NOT_LIKE_FUNCTION(T2, int(const T &))>
  int get_latest(T &value,
                 mds::MdsWriter &writer,// FIXME(zk250686): should not exposed, will be removed later
                 mds::TwoPhaseCommitState &trans_stat,// FIXME(zk250686): should not exposed, will be removed later
                 share::SCN &trans_version,// FIXME(zk250686): should not exposed, will be removed later
                 ObIAllocator *alloc = nullptr,
                 const int64_t read_seq = 0) const {
    MdsDefaultDeepCopyOperation<T> default_get_op(value, alloc);
    return get_latest<T, MdsDefaultDeepCopyOperation<T> &>(default_get_op, writer, trans_stat, trans_version, read_seq);
  }
  template <typename T, typename T2 = T, ENABLE_IF_NOT_LIKE_FUNCTION(T2, int(const T &))>
  int get_latest_committed(T &value, ObIAllocator *alloc = nullptr) const {
    MdsDefaultDeepCopyOperation<T> default_get_op(value, alloc);
    return get_latest_committed<T, MdsDefaultDeepCopyOperation<T> &>(default_get_op);
  }
  template <typename T, typename T2 = T, ENABLE_IF_NOT_LIKE_FUNCTION(T2, int(const T &))>
  int get_snapshot(T &value,
                   const share::SCN snapshot,
                   const int64_t timeout_us,
                   ObIAllocator *alloc = nullptr,
                   const int64_t read_seq = 0) const {
    MdsDefaultDeepCopyOperation<T> default_get_op(value, alloc);
    return get_snapshot<T, MdsDefaultDeepCopyOperation<T> &>(default_get_op, snapshot, timeout_us, read_seq);
  }
  // belows are general get interfaces, which could be customized for complicated data structure
  template <typename T, typename OP, ENABLE_IF_LIKE_FUNCTION(OP, int(const T &))>
  int get_latest(OP &&read_op,
                 mds::MdsWriter &writer,// FIXME(zk250686): should not exposed, will be removed later
                 mds::TwoPhaseCommitState &trans_stat,// FIXME(zk250686): should not exposed, will be removed later
                 share::SCN &trans_version,// FIXME(zk250686): should not exposed, will be removed later
                 const int64_t read_seq = 0) const;
  template <typename T, typename OP, ENABLE_IF_LIKE_FUNCTION(OP, int(const T &))>
  int get_latest_committed(OP &&read_op) const;
  template <typename T, typename OP, ENABLE_IF_LIKE_FUNCTION(OP, int(const T &))>
  int get_snapshot(OP &&read_op,
                   const share::SCN snapshot,
                   const int64_t timeout_us) const;
  template <typename Key, typename Value, typename OP>
  int get_snapshot(const Key &key,
                   OP &&read_op,
                   const share::SCN snapshot,
                   const int64_t timeout_us) const;
  int fill_virtual_info(ObIArray<mds::MdsNodeInfoForVirtualTable> &mds_node_info_array) const;
  TO_STRING_KV(KP(this), "is_inited", check_is_inited_(),
               "tablet_id", get_tablet_id_(), KP(get_tablet_pointer_()));
  int get_mds_table_rec_scn(share::SCN &rec_scn);
  int mds_table_flush(const share::SCN &recycle_scn);
  template <typename T>
  int get_latest_committed_data(T &value, ObIAllocator *alloc = nullptr);
protected:// implemented by ObTablet
  // TODO(@gaishun.gs): remove these virtual functions later
  virtual bool check_is_inited_() const = 0;
  virtual const ObTabletMeta &get_tablet_meta_() const = 0;
  virtual int get_mds_table_handle_(mds::MdsTableHandle &handle,
                                    const bool create_if_not_exist) const = 0;
  virtual ObTabletPointer *get_tablet_pointer_() const = 0;
  template <typename K, typename V>
  int read_data_from_tablet_cache(const K &key,
                                  const common::ObFunction<int(const V&)> &read_op,
                                  bool &applied_success) const;
  template <typename K, typename V>
  int read_data_from_mds_sstable(common::ObIAllocator &allocator,
                                 const K &key,
                                 const share::SCN &snapshot,
                                 const int64_t timeout_us,
                                 const common::ObFunction<int(const V&)> &read_op) const;
  template <typename K, typename V>
  int read_data_from_cache_or_mds_sstable(common::ObIAllocator &allocator,
                                          const K &key,
                                          const share::SCN &snapshot,
                                          const int64_t timeout_us,
                                          const common::ObFunction<int(const V&)> &read_op) const;
  template <typename K, typename V>
  int get_mds_data_from_tablet(
    const K &key,
    const share::SCN &snapshot,
    const int64_t timeout_us,
    const common::ObFunction<int(const V&)> &read_op) const;
  int read_raw_data(
      common::ObIAllocator &allocator,
      const uint8_t mds_unit_id,
      const common::ObString &udf_key,
      const share::SCN &snapshot,
      const int64_t timeout_us,
      mds::MdsDumpKV &kv) const;
  int mds_table_scan(
      ObTableScanParam &scan_param,
      ObStoreCtx &store_ctx,
      ObMdsRowIterator &iter) const;
  int get_tablet_handle_from_this(
    ObTabletHandle &tablet_handle) const;
  template <typename K, typename T>
  int mds_range_query(
      ObTableScanParam &scan_param,
      ObMdsRangeQueryIterator<K, T> &iter) const;

  template <typename T>
  int replay(T &&mds,
             mds::MdsCtx &ctx,
             const share::SCN &scn);
  template <typename Key, typename Value>
  int replay(const Key &key,
             Value &&mds,
             mds::MdsCtx &ctx,
             const share::SCN &scn);
private:
  template <typename Key, typename Value>
  int replay_remove(const Key &key,
                    mds::MdsCtx &ctx,
                    const share::SCN &scn);// called only by ObTabletReplayExecutor
  common::ObTabletID get_tablet_id_() const;
  template <typename T>
  int obj_to_string_holder_(const T &obj, ObStringHolder &holder) const;
  template <typename T>
  int fill_virtual_info_by_obj_(const T &obj, const mds::NodePosition position, ObIArray<mds::MdsNodeInfoForVirtualTable> &mds_node_info_array) const;
  template <typename K, typename T>
  int fill_virtual_info_from_mds_sstable(ObIArray<mds::MdsNodeInfoForVirtualTable> &mds_node_info_array) const;
  template <class T, ENABLE_IF_IS_SAME_CLASS(T, ObTabletCreateDeleteMdsUserData)>
  int check_mds_data_complete_(bool &is_complete) const  { is_complete = true; return OB_SUCCESS; } // Only for tablet_Status, which doesn't need data integrity check.
  template <class T, ENABLE_IF_NOT_SAME_CLASS(T, ObTabletCreateDeleteMdsUserData)>
  int check_mds_data_complete_(bool &is_complete) const;
};

struct GetTabletStatusNodeFromMdsTableOp
{
  GetTabletStatusNodeFromMdsTableOp(ObTabletCreateDeleteMdsUserData &tablet_status, share::SCN &redo_scn)
  : tablet_status_(tablet_status),
  redo_scn_(redo_scn) {}
  int operator()(const mds::UserMdsNode<mds::DummyKey, ObTabletCreateDeleteMdsUserData> &node) {
    tablet_status_.assign(node.user_data_);
    redo_scn_ = node.redo_scn_;
    return OB_SUCCESS;
  }
  ObTabletCreateDeleteMdsUserData &tablet_status_;
  share::SCN &redo_scn_;
};

struct ReadTabletStatusOp
{
  ReadTabletStatusOp(ObTabletCreateDeleteMdsUserData &tablet_status) : tablet_status_(tablet_status) {}
  int operator()(const ObTabletCreateDeleteMdsUserData &data)
  {
    return tablet_status_.assign(data);
  }
  ObTabletCreateDeleteMdsUserData &tablet_status_;
};

struct ReadBindingInfoOp
{
  ReadBindingInfoOp(ObTabletBindingMdsUserData &ddl_data) : ddl_data_(ddl_data) {}
  int operator()(const ObTabletBindingMdsUserData &data)
  {
    return ddl_data_.assign(data);
  }
  ObTabletBindingMdsUserData &ddl_data_;
};

struct ReadAutoIncSeqOp
{
  ReadAutoIncSeqOp(common::ObIAllocator &allocator, ObTabletAutoincSeq &auto_inc_seq)
    : allocator_(allocator), auto_inc_seq_(auto_inc_seq) {}
  int operator()(const ObTabletAutoincSeq &data)
  {
    return auto_inc_seq_.assign(allocator_, data);
  }
  common::ObIAllocator &allocator_;
  ObTabletAutoincSeq &auto_inc_seq_;
};

struct ReadAutoIncSeqValueOp
{
  ReadAutoIncSeqValueOp(uint64_t &auto_inc_seq_value)
    : auto_inc_seq_value_(auto_inc_seq_value) {}
  int operator()(const ObTabletAutoincSeq &data)
  {
    return data.get_autoinc_seq_value(auto_inc_seq_value_);
  }
  uint64_t &auto_inc_seq_value_;
};

}
}

#ifndef INCLUDE_OB_TABLET_MDS_PART_IPP
#define INCLUDE_OB_TABLET_MDS_PART_IPP
#include "ob_i_tablet_mds_interface.ipp"
#endif

#endif
