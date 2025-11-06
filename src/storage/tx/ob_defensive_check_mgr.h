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

#ifndef OB_DEFENSIVE_CHECK_MGR_H_
#define OB_DEFENSIVE_CHECK_MGR_H_
#include "storage/blocksstable/ob_fuse_row_cache.h"
#include "share/scn.h"

namespace oceanbase
{
namespace transaction
{

struct ObDefensiveCheckRecordExtend
{
  ObDefensiveCheckRecordExtend() { reset(); }
  ~ObDefensiveCheckRecordExtend() { reset(); }
  void reset()
  {
    fist_access_table_start_scn_.set_min();
    total_table_handle_cnt_ = 0;
    start_access_table_idx_ = INT64_MAX;
    end_access_table_idx_ = INT64_MAX;
    use_fuse_cache_data_ = false;
    is_all_data_from_memtable_ = false;
    query_flag_.reset();
  }
  TO_STRING_KV(K_(fist_access_table_start_scn),
               K_(total_table_handle_cnt),
               K_(start_access_table_idx),
               K_(end_access_table_idx),
               K_(use_fuse_cache_data),
               K_(is_all_data_from_memtable),
               K_(query_flag));
public:
  share::SCN fist_access_table_start_scn_;
  int64_t total_table_handle_cnt_;
  int64_t start_access_table_idx_;
  int64_t end_access_table_idx_;
  bool use_fuse_cache_data_;
  bool is_all_data_from_memtable_;
  ObQueryFlag query_flag_;
};

class SingleRowDefensiveRecord
{
public:
  SingleRowDefensiveRecord() : generate_ts_(0) {}
  ~SingleRowDefensiveRecord() { reset(); }
  void reset();
  void destroy() { reset(); }
  int deep_copy(const blocksstable::ObDatumRow &row,
                const blocksstable::ObDatumRowkey &rowkey,
                const ObDefensiveCheckRecordExtend &extend_info,
                const ObTabletID &tablet_id);

  TO_STRING_KV(K_(row), K_(generate_ts), K_(rowkey), K_(tablet_id), K_(extend_info));

  blocksstable::ObDatumRow row_;
  int64_t generate_ts_;
  ObArenaAllocator allocator_;
  blocksstable::ObDatumRowkey rowkey_;
  ObTabletID tablet_id_;
  ObDefensiveCheckRecordExtend extend_info_;
};

typedef common::ObSEArray<SingleRowDefensiveRecord *, 12> ObSingleRowDefensiveRecordArray;

class ObSingleTabletDefensiveCheckInfo : public share::ObLightHashLink<ObSingleTabletDefensiveCheckInfo>
{
public:
  ObSingleTabletDefensiveCheckInfo() { }
  ~ObSingleTabletDefensiveCheckInfo() { reset(); }
  int init(const ObTransID &tx_id);
  void reset();
  void destroy() { reset(); }
  bool contain(const ObTransID &tx_id) { return tx_id_ == tx_id; }
  int add_record(SingleRowDefensiveRecord *record);
  ObSingleRowDefensiveRecordArray &get_record_arr() { return record_arr_; }
  const ObTransID &get_tx_id() const { return tx_id_; }
private:
  ObTransID tx_id_;
  ObSingleRowDefensiveRecordArray record_arr_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObSingleTabletDefensiveCheckInfo);
};

class ObSingleTabletDefensiveCheckInfoAlloc
{
public:
  static const int64_t OP_LOCAL_NUM = 128;
  ObSingleTabletDefensiveCheckInfo * alloc_value()
  {
    return NULL;
  }

  void free_value(ObSingleTabletDefensiveCheckInfo* info)
  {
    if (NULL != info) {
      op_free(info);
      info = NULL;
    }
  }
};

typedef share::ObLightHashMap<ObTransID,
                       ObSingleTabletDefensiveCheckInfo,
                       ObSingleTabletDefensiveCheckInfoAlloc,
                       common::SpinRWLock, 2 << 16 /*bucket_num*/> ObTxDefensiveCheckInfoMap;

class ObDefensiveCheckMgr
{
public:
  ObDefensiveCheckMgr() : is_inited_(false)  { }
  ~ObDefensiveCheckMgr() { destroy(); }
  void reset();
  void destroy() { reset(); }
private:
  static int64_t max_record_cnt_;
  typedef ObSmallSpinLockGuard<common::ObByteLock> Guard;
  bool is_inited_;
  common::ObByteLock lock_;
  ObTxDefensiveCheckInfoMap map_;
};

} /* namespace transaction*/
} /* namespace oceanbase */

#endif /* OB_DEFENSIVE_CHECK_MGR_H_ */
