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

#ifndef OCEANBASE_STORAGE_OB_MDS_RANGE_QUERY_ITERATOR
#define OCEANBASE_STORAGE_OB_MDS_RANGE_QUERY_ITERATOR

#include <stdint.h>
#include "lib/allocator/page_arena.h"
#include "lib/oblog/ob_log.h"
#include "lib/function/ob_function.h"
#include "storage/multi_data_source/mds_node.h"
#include "storage/multi_data_source/mds_table_iterator.h"
#include "storage/multi_data_source/mds_table_handle.h"
#include "storage/multi_data_source/runtime_utility/common_define.h"
#include "storage/tablet/ob_mds_row_iterator.h"
#include "storage/tablet/ob_tablet_obj_load_helper.h"
#include "storage/tablet/ob_tablet_mds_node_filter.h" // ObMdsReadInfoCollector

namespace oceanbase
{
namespace common
{
class ObIAllocator;
}

namespace storage
{
class ObTabletHandle;
class ObTabletPointer;
class ObTablet;

namespace mds
{
class MdsDumpKV;
}

class ObMdsRangeQueryIteratorHelper
{
public:
  static int get_mds_table(const ObTabletHandle &tablet_handle, mds::MdsTableHandle &mds_table);
  static int check_mds_data_complete(const ObTabletHandle &tablet_handle, bool &is_data_complete);
  static int get_tablet_id(
      const ObTabletHandle &tablet_handle,
      ObTabletID &tablet_id);
};

template <typename K, typename T>
class ObSingleTabletMdsRangeQueryIterator
{
public:
  ObSingleTabletMdsRangeQueryIterator();
  ~ObSingleTabletMdsRangeQueryIterator();
  ObSingleTabletMdsRangeQueryIterator(const ObSingleTabletMdsRangeQueryIterator&) = delete;
  ObSingleTabletMdsRangeQueryIterator &operator=(const ObSingleTabletMdsRangeQueryIterator&) = delete;
public:
  int init(
      ObTableScanParam &scan_param,
      const ObTabletHandle &tablet_handle,
      ObStoreCtx &store_ctx);

  int get_next_mds_kv(common::ObIAllocator &allocator, mds::MdsDumpKV *&kv);
  bool is_inited() const { return is_inited_; }
private:
  int advance_mds_table_iter();
  int advance_mds_sstable_iter();
  int output_from_mds_table(common::ObIAllocator &allocator, mds::MdsDumpKV *&kv);
  int output_from_mds_sstable(common::ObIAllocator &allocator, mds::MdsDumpKV *&kv);
  static int convert_user_node_to_dump_kv(
      common::ObIAllocator &allocator,
      const K &user_key,
      const mds::UserMdsNode<K, T> &user_node,
      mds::MdsDumpKV &kv);
  static int get_key_from_dump_kv(const mds::MdsDumpKV &kv, K &k);
private:
  bool is_inited_;
  common::ObArenaAllocator allocator_;
  mds::ObMdsUnitRowNodeScanIterator<K, T> mds_table_iter_;
  ObMdsRowIterator mds_sstable_iter_;
  bool mds_table_end_;
  bool mds_sstable_end_;
  K mds_table_key_;
  K mds_sstable_key_;
  mds::UserMdsNode<K, T> *mds_table_val_;
  mds::MdsDumpKV mds_sstable_val_;
  common::ObVersionRange read_version_range_;
  ObMdsReadInfoCollector *collector_;
};

template <typename K, typename T>
ObSingleTabletMdsRangeQueryIterator<K, T>::ObSingleTabletMdsRangeQueryIterator()
  : is_inited_(false),
    allocator_(lib::ObMemAttr("range_query")),
    mds_table_iter_(),
    mds_sstable_iter_(),
    mds_table_end_(false),
    mds_sstable_end_(false),
    mds_table_key_(),
    mds_sstable_key_(),
    mds_table_val_(nullptr),
    mds_sstable_val_(),
    read_version_range_(),
    collector_(nullptr)
{
}

template <typename K, typename T>
ObSingleTabletMdsRangeQueryIterator<K, T>::~ObSingleTabletMdsRangeQueryIterator()
{
  is_inited_ = false;
  mds_sstable_iter_.reset();
  mds_table_end_ = false;
  mds_sstable_end_ = false;
  mds_table_key_.reset();
  mds_sstable_key_.reset();
  mds_table_val_ = nullptr;
  mds_sstable_val_.reset();
  allocator_.reset();
  read_version_range_.reset();
  collector_ = nullptr;
}

template <typename K, typename T>
int ObSingleTabletMdsRangeQueryIterator<K, T>::init(
    ObTableScanParam &scan_param,
    const ObTabletHandle &tablet_handle,
    ObStoreCtx &store_ctx)
{
  int ret = common::OB_SUCCESS;
  mds::MdsTableHandle mds_table;
  bool is_mds_data_complete = false;
  ObTabletMdsNodeFilter<K, T> mds_node_filter(scan_param.read_version_range_, scan_param.mds_collector_);

  if (OB_UNLIKELY(is_inited())) {
    ret = common::OB_INIT_TWICE;
    MDS_LOG(WARN, "init twice", K(ret), K_(is_inited));
  } else if (OB_FAIL(ObMdsRangeQueryIteratorHelper::check_mds_data_complete(tablet_handle, is_mds_data_complete))) {
  } else if (!is_mds_data_complete) {
    ret = OB_EAGAIN;
    MDS_LOG(INFO, "MDS data is incomplete. Please try again later to retrieve the full dataset.", K(ret));
  } else if (OB_FAIL(ObMdsRangeQueryIteratorHelper::get_mds_table(tablet_handle, mds_table))) {
    if (common::OB_ENTRY_NOT_EXIST == ret) {
      mds_table_end_ = true; // no mds table, directly mds table end
      ret = common::OB_SUCCESS;
    } else {
      MDS_LOG(WARN, "failed to get mds table", K(ret));
    }
  } else if (OB_FAIL(mds_table_iter_.init(mds_table, mds_node_filter, scan_param.timeout_))) {
  }

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(mds_sstable_iter_.init(scan_param, tablet_handle, store_ctx))) {
  } else {
    read_version_range_ = scan_param.read_version_range_;
    collector_ = scan_param.mds_collector_;
    is_inited_ = true;

  }

  return ret;
}

template <typename K, typename T>
int ObSingleTabletMdsRangeQueryIterator<K, T>::advance_mds_table_iter()
{
  int ret = common::OB_SUCCESS;
  if (mds_table_end_) {
  } else {
    // get next kv whose trans_version <= boundary_snapshot_version
    while (OB_SUCC(ret)) {
      if (OB_FAIL(mds_table_iter_.get_next(mds_table_key_, mds_table_val_))) {
        if (common::OB_ITER_END == ret) {
          mds_table_end_ = true;
          ret = common::OB_SUCCESS;
          break;
        } else {
          MDS_LOG(WARN, "fail to get next from mds table iter", K(ret));
        }
      } else {
        const int64_t commit_version = mds_table_val_->trans_version_.get_val_for_tx();
        if (commit_version <= read_version_range_.base_version_) {
          continue;
        } else if (commit_version > read_version_range_.snapshot_version_) {
          // mds_table need not update exist_new_committed_node_, which is design for updating cache;
          collector_->exist_new_committed_node_ = true;
          continue;
        } else {
          break;
        }
        
      }
    }
  }

  return ret;
}

template <typename K, typename T>
int ObSingleTabletMdsRangeQueryIterator<K, T>::advance_mds_sstable_iter()
{
  int ret = common::OB_SUCCESS;
  if (mds_sstable_end_) {
  } else {
    while (OB_SUCC(ret)) {
      mds_sstable_val_.reset();
      if (OB_FAIL(mds_sstable_iter_.get_next_mds_kv(allocator_, mds_sstable_val_))) {
        if (common::OB_ITER_END == ret) {
          mds_sstable_end_ = true;
          break;
        } else {
          MDS_LOG(WARN, "fail to get next from mds sstable iter", K(ret));
        }
      } else {
        const int64_t commit_version = mds_sstable_val_.v_.trans_version_.get_val_for_tx();
        if (commit_version <= read_version_range_.base_version_) {
          continue;
        } else if (commit_version > read_version_range_.snapshot_version_) {
          collector_->exist_new_committed_node_ = true;
          continue;
        } else {
          break;
        }
      }
    } // while
    if (FAILEDx(get_key_from_dump_kv(mds_sstable_val_, mds_sstable_key_))) {
      MDS_LOG(WARN, "fail to get key", K(ret), K_(mds_sstable_val));
    } else {
    }
    ret = (OB_ITER_END == ret ? OB_SUCCESS : ret);
  }

  return ret;
}

template <typename K, typename T>
int ObSingleTabletMdsRangeQueryIterator<K, T>::output_from_mds_table(common::ObIAllocator &allocator, mds::MdsDumpKV *&kv)
{
  int ret = common::OB_SUCCESS;

  if (OB_ISNULL(mds_table_val_)) {
    ret = OB_ERR_UNEXPECTED;
    MDS_LOG(WARN, "mds table val is null", KR(ret), KP(mds_table_val_));
  } else {
    if (OB_FAIL(ObTabletObjLoadHelper::alloc_and_new(allocator, kv))) {
    } else {
      if (OB_FAIL(convert_user_node_to_dump_kv(allocator, mds_table_key_, *mds_table_val_, *kv))) {
      }
      if (OB_FAIL(ret)) {
        ObTabletObjLoadHelper::free(allocator, kv);
      }
    }
  }
  return ret;
}

template <typename K, typename T>
int ObSingleTabletMdsRangeQueryIterator<K, T>::output_from_mds_sstable(common::ObIAllocator &allocator, mds::MdsDumpKV *&kv)
{
  int ret = common::OB_SUCCESS;

  if (OB_FAIL(ObTabletObjLoadHelper::alloc_and_new(allocator, kv))) {
  } else {
    if (OB_FAIL(kv->assign(mds_sstable_val_, allocator))) {
    }
    if (OB_FAIL(ret)) {
      ObTabletObjLoadHelper::free(allocator, kv);
    }
  }

  return ret;
}

template <typename K, typename T>
int ObSingleTabletMdsRangeQueryIterator<K, T>::convert_user_node_to_dump_kv(
    common::ObIAllocator &allocator,
    const K &user_key,
    const mds::UserMdsNode<K, T> &user_node,
    mds::MdsDumpKV &kv)
{
  int ret = common::OB_SUCCESS;
  constexpr uint8_t mds_table_id = mds::GET_MDS_TABLE_ID<mds::NormalMdsTable>::value;
  constexpr uint8_t mds_unit_id = mds::TupleTypeIdx<mds::NormalMdsTable, mds::MdsUnit<K, T>>::value;
  mds::MdsDumpKey &key = kv.k_;
  mds::MdsDumpNode &node = kv.v_;

  if (OB_FAIL(key.init(mds_table_id, mds_unit_id, user_key, allocator))) {
  } else if (OB_FAIL(node.init(mds_table_id, mds_unit_id, user_node, allocator))) {
  }

  return ret;
}

template <typename K, typename T>
int ObSingleTabletMdsRangeQueryIterator<K, T>::get_key_from_dump_kv(const mds::MdsDumpKV &kv, K &k)
{
  int ret = common::OB_SUCCESS;
  const common::ObString &key_str = kv.k_.key_;
  int64_t pos = 0;
  if (OB_FAIL(k.mds_deserialize(key_str.ptr(), key_str.length(), pos))) {
  }

  return ret;
}

template <typename K, typename T>
int ObSingleTabletMdsRangeQueryIterator<K, T>::get_next_mds_kv(
    common::ObIAllocator &allocator,
    mds::MdsDumpKV *&kv)
{
  int ret = common::OB_SUCCESS;
  int compare_result = 0;

  if (OB_UNLIKELY(!is_inited())) {
    ret = common::OB_NOT_INIT;
    MDS_LOG(WARN, "not inited", K(ret), K_(is_inited));
  }

  if (OB_FAIL(ret)) {
  } else if (!mds_table_key_.is_valid()) {
    if (OB_FAIL(advance_mds_table_iter())) {
    }
  }

  if (OB_FAIL(ret)) {
  } else if (!mds_sstable_key_.is_valid()) {
    if (OB_FAIL(advance_mds_sstable_iter())) {
    }
  }

  if (OB_FAIL(ret)) {
  } else if (mds_table_end_ && mds_sstable_end_) {
    // both iter end
    ret = common::OB_ITER_END;
  } else if (mds_table_end_) {
    // use mds sstable
    if (OB_FAIL(output_from_mds_sstable(allocator, kv))) {
    } else if (OB_FAIL(advance_mds_sstable_iter())) {
    }
  } else if (mds_sstable_end_) {
    // use mds table
    if (OB_FAIL(output_from_mds_table(allocator, kv))) {
    } else if (OB_FAIL(advance_mds_table_iter())) {
    }
  } else if (OB_UNLIKELY(!mds_table_key_.is_valid() || !mds_sstable_key_.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    MDS_LOG(WARN, "unexpected error, both mds table key and mds sstable key should be valid",
        K(ret), K_(mds_table_key), K_(mds_sstable_key));
  } else if (OB_FAIL(mds::compare_binary_key(mds_table_key_, mds_sstable_key_, compare_result))) {
  } else if (compare_result < 0) {
    // use mds table
    if (OB_FAIL(output_from_mds_table(allocator, kv))) {
    } else if (OB_FAIL(advance_mds_table_iter())) {
    }
  } else if (compare_result == 0) {
    // use mds table, both iter advance
    if (OB_FAIL(output_from_mds_table(allocator, kv))) {
    } else if (OB_FAIL(advance_mds_table_iter())) {
    } else if (OB_FAIL(advance_mds_sstable_iter())) {
    }
  } else {
    // use mds sstable
    if (OB_FAIL(output_from_mds_sstable(allocator, kv))) {
    } else if (OB_FAIL(advance_mds_sstable_iter())) {
    }
  }

  if (OB_SUCC(ret)) {
  }

  return ret;
}

} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_OB_MDS_RANGE_QUERY_ITERATOR
