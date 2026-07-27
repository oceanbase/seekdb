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

#ifndef OCEANBASE_STORAGE_OB_TABLET_PERSIST_H_
#define OCEANBASE_STORAGE_OB_TABLET_PERSIST_H_

#include "lib/allocator/page_arena.h"
#include "storage/compaction/ob_extra_medium_info.h"
#include "storage/meta_mem/ob_storage_meta_mem_mgr.h"
#include "storage/tablet/ob_tablet.h"
#include "storage/blockstore/ob_object_reader_writer.h"
#include "storage/tablet/ob_tablet_complex_addr.h"
#include "storage/tablet/ob_tablet_dumped_medium_info.h"
#include "storage/blocksstable/ob_object_manager.h"
#include "storage/blocksstable/ob_macro_block_id.h"

namespace oceanbase
{
namespace storage
{
struct ObBlockInfoSet;
class ObTabletMacroInfo;

struct ObSharedBlockIndex final
{
public:
  ObSharedBlockIndex()
    : shared_macro_id_(), nested_offset_(0)
  {
  }
  ObSharedBlockIndex(
      const blocksstable::MacroBlockId &shared_macro_id,
      const int64_t nested_offset)
    : shared_macro_id_(shared_macro_id), nested_offset_(nested_offset)
  {
  }
  void reset()
  {
    shared_macro_id_.reset();
    nested_offset_ = 0;
  }
  int hash(uint64_t &hash_val) const;
  bool operator ==(const ObSharedBlockIndex &other) const;
  TO_STRING_KV(K_(shared_macro_id), K_(nested_offset));
public:
  blocksstable::MacroBlockId shared_macro_id_;
  int64_t nested_offset_;
};

class ObTabletTransformArg final
{
public:
  ObTabletTransformArg();
  ~ObTabletTransformArg();
  ObTabletTransformArg(const ObTabletTransformArg &) = delete;
  ObTabletTransformArg &operator=(const ObTabletTransformArg &) = delete;
  void reset();
  bool is_valid() const;
  TO_STRING_KV(KP_(rowkey_read_info_ptr),
               K_(tablet_meta),
               K_(table_store_addr),
               K_(storage_schema_addr),
               K_(tablet_macro_info_addr),
               KP_(tablet_macro_info_ptr));
public:
  const ObRowkeyReadInfo *rowkey_read_info_ptr_;
  const ObTabletMacroInfo *tablet_macro_info_ptr_;
  ObTabletMeta tablet_meta_;
  ObMetaDiskAddr table_store_addr_;
  ObMetaDiskAddr storage_schema_addr_;
  ObMetaDiskAddr tablet_macro_info_addr_;
  ObDDLKV **ddl_kvs_;
  int64_t ddl_kv_count_;
  ObIMemtable *memtables_[MAX_MEMSTORE_CNT];
  int64_t memtable_count_;
  // If you want to add new member, make sure all member is assigned in 2 convert function.
  // ObTabletPersister::convert_tablet_to_mem_arg
  // ObTabletPersister::convert_tablet_to_disk_arg
};

class ObSSTablePersistWrapper final
{
public:
  explicit ObSSTablePersistWrapper(const blocksstable::ObSSTable *sstable) : sstable_(sstable) { }
  ~ObSSTablePersistWrapper() { reset(); }
  void reset() { sstable_ = nullptr; }
  bool is_valid() const;
  int serialize(char *buf, const int64_t buf_len, int64_t &pos) const;
  int64_t get_serialize_size() const;
  TO_STRING_KV(KPC_(sstable));
private:
  const blocksstable::ObSSTable *sstable_;
  DISALLOW_COPY_AND_ASSIGN(ObSSTablePersistWrapper);
};

class ObITabletMetaModifier
{
public:
  ObITabletMetaModifier() = default;
  virtual ~ObITabletMetaModifier() = default;
  virtual int modify_tablet_meta(ObTabletMeta &meta) = 0;
};

class ObMultiTimeStats
{
public:
  class TimeStats
  {
  public:
    explicit TimeStats(const char *owner);
    void click(const char *step_name);
    ~TimeStats() {}
    int64_t to_string(char *buf, const int64_t buf_len) const;
    int set_extra_info(const char *fmt, ...);
    int64_t get_total_time() const { return last_ts_ - start_ts_; }

  private:
    static const int64_t MAX_CLICK_COUNT = 16;
    static const int64_t MAX_EXTRA_INFO_LENGTH = 128;
    const char *owner_;
    int64_t start_ts_;
    int64_t last_ts_;
    const char *click_str_[MAX_CLICK_COUNT];
    int32_t click_[MAX_CLICK_COUNT];
    int32_t click_count_;
    char extra_info_[MAX_EXTRA_INFO_LENGTH];
    bool has_extra_info_;

    DISALLOW_COPY_AND_ASSIGN(TimeStats);
  };

  explicit ObMultiTimeStats(ObArenaAllocator *allocator);
  ~ObMultiTimeStats();
  int acquire_stats(const char *owner, TimeStats *&stats);
  int64_t to_string(char *buf, const int64_t buf_len) const;

private:
  static const int64_t MAX_STATS_CNT = 16;
  ObArenaAllocator *allocator_;
  TimeStats *stats_;
  int32_t stats_count_;

  DISALLOW_COPY_AND_ASSIGN(ObMultiTimeStats);
};

struct ObTabletPersisterParam final
{
public:
  ObTabletPersisterParam(
      const int64_t ls_epoch,
      const ObTabletID tablet_id)
    : ls_epoch_(ls_epoch), tablet_id_(tablet_id)
    {}
  ObTabletPersisterParam() = delete;

  bool is_valid() const;

  TO_STRING_KV(K_(ls_epoch), K_(tablet_id));

  int64_t ls_epoch_;
  ObTabletID tablet_id_;

  DISALLOW_COPY_AND_ASSIGN(ObTabletPersisterParam);
};

class ObTabletPersister final
{
public:
  static const int64_t SHARED_MACRO_BUCKET_CNT = 100;
  static const int64_t MAP_EXTEND_RATIO = 2;
  typedef typename common::hash::ObHashMap<ObSharedBlockIndex,
                                          int64_t,
                                          common::hash::NoPthreadDefendMode,
                                          common::hash::hash_func<ObSharedBlockIndex>,
                                          common::hash::equal_to<ObSharedBlockIndex>,
                                          common::hash::SimpleAllocer<common::hash::HashMapTypes<ObSharedBlockIndex, int64_t>::AllocType>,
                                          common::hash::NormalPointer,
                                          oceanbase::common::ObMalloc,
                                          MAP_EXTEND_RATIO> SharedMacroMap;
  typedef typename SharedMacroMap::iterator SharedMacroIterator;
private: // hide constructor
  ObTabletPersister(const ObTabletPersisterParam &param, const int64_t mem_ctx_id);
  struct ObSSTablePersistCtx final
  {
  public:
    ObSSTablePersistCtx(ObBlockInfoSet &block_info_set,
                                 ObIArray<ObObjectsWriteCtx> &sstable_meta_write_ctxs):
      is_inited_(false), total_tablet_meta_size_(0),
      normal_sstable_cnt_(0), block_info_set_(block_info_set), sstable_meta_write_ctxs_(sstable_meta_write_ctxs)
      {}
    ~ObSSTablePersistCtx() = default;
    int init(const int64_t ctx_id);
    bool is_inited() const { return is_inited_; };
    TO_STRING_KV(K_(is_inited), K_(total_tablet_meta_size),
      K_(normal_sstable_cnt), K_(tables), K_(write_infos), K_(sstable_meta_write_ctxs));
    bool is_inited_;
    int64_t total_tablet_meta_size_;
    int64_t normal_sstable_cnt_;
    SharedMacroMap shared_macro_map_;
    common::ObSArray<ObITable *> tables_;
    common::ObSArray<ObObjectWriteInfo> write_infos_;
    ObBlockInfoSet &block_info_set_;
    ObIArray<ObObjectsWriteCtx> &sstable_meta_write_ctxs_;
    DISALLOW_COPY_AND_ASSIGN(ObSSTablePersistCtx);
  };
public:
  ~ObTabletPersister();
  // Persist the old tablet itself and all internal members, and transform it into a new tablet
  // from object pool. The old tablet can be allocated by allocator or from object pool.
  static int persist_and_transform_tablet(
      const ObTabletPersisterParam &param,
      const ObTablet &old_tablet,
      ObTabletHandle &new_handle);
  static int persist_and_transform_only_tablet_meta(
      const ObTabletPersisterParam &param,
      const ObTablet &old_tablet,
      ObITabletMetaModifier &modifier,
      ObTabletHandle &new_tablet);
  // copy from old tablet
  static int copy_from_old_tablet(
      const ObTabletPersisterParam &param,
      const ObTablet &old_tablet,
      ObTabletHandle &new_handle);
  // change tablet memory footprint
  //  - degrade larger tablet objects to relatively smaller tablet objects, reducing the memory footprint.
  //  - upgrade smaller tablet objects to relatively larger tablet objects, achieving more performance.
  static int transform_tablet_memory_footprint(
      const ObTabletPersisterParam &param,
      const ObTablet &old_tablet,
      char *buf,
      const int64_t len);
  static int copy_sstable_macro_info(
      const blocksstable::ObSSTable &sstable,
      SharedMacroMap &shared_macro_map,
      ObBlockInfoSet &block_info_set);
  static int copy_shared_macro_info(
      const blocksstable::ObSSTableMacroInfo &macro_info,
      SharedMacroMap &shared_macro_map,
      ObBlockInfoSet::TabletMacroSet &meta_id_set);
  static int convert_macro_info_map(
      SharedMacroMap &shared_macro_map,
      ObBlockInfoSet::TabletMacroMap &aggregated_info_map);
  static int copy_data_macro_ids(
      const blocksstable::ObSSTableMacroInfo &macro_info,
      ObBlockInfoSet &block_info_set);
  static int transform_empty_shell(const ObTabletPersisterParam &param, const ObTablet &old_tablet, ObTabletHandle &new_handle);
private:
  void build_async_write_start_opt_(blocksstable::ObStorageObjectOpt &start_opt) const;
  static int inner_persist_and_transform_tablet(
    const ObTabletPersisterParam &param,
    const ObTablet &old_tablet,
    ObTabletHandle &new_handle);
  static int inc_ref_with_macro_iter(ObTablet &tablet, ObMacroInfoIterator &macro_iter);
  static int do_copy_ids(
      blocksstable::ObMacroIdIterator &iter,
      ObBlockInfoSet::TabletMacroSet &id_set);
  static int check_tablet_meta_ids(
      const ObIArray<blocksstable::MacroBlockId> &meta_id_arr,
      const ObTablet &tablet);
  static int acquire_tablet(
      const ObTabletPoolType &type,
      const ObTabletMapKey &key,
      const bool try_smaller_pool,
      ObTabletHandle &new_handle);
  static int convert_tablet_to_mem_arg(
      const ObTablet &tablet,
      ObTabletTransformArg &arg);
  int convert_tablet_to_disk_arg(
      const ObTablet &tablet,
      common::ObIArray<ObObjectsWriteCtx> &total_write_ctxs,
      ObTabletPoolType &type,
      ObTabletTransformArg &arg,
      int64_t &total_tablet_meta_size,
      ObBlockInfoSet &block_info_set);
  int fill_sstable_write_info_and_record(
    ObArenaAllocator &allocator,
    const ObITable *table,
    const bool check_has_padding_meta_cache,
    ObIArray<ObObjectWriteInfo> &write_info_arr,
    ObSSTablePersistCtx &sstable_persist_ctx);
  static int convert_arg_to_tablet(
      const ObTabletTransformArg &arg,
      ObTablet &tablet);
  int transform(
      const ObTabletTransformArg &arg,
      char *buf,
      const int64_t len);
  int persist_and_fill_tablet(
      const ObTablet &old_tablet,
      ObLinkedMacroBlockItemWriter &linked_writer,
      common::ObIArray<ObObjectsWriteCtx> &total_write_ctxs,
      ObTabletHandle &new_handle,
      ObTabletSpaceUsage &space_usage,
      ObTabletMacroInfo &macro_info,
      ObIArray<blocksstable::MacroBlockId> &meta_id_arr);
  int calc_tablet_space_usage_(
      const ObBlockInfoSet &block_info_set,
      ObTabletHandle &new_tablet_hdl,
      ObIArray<MacroBlockId> &meta_id_arr,
      ObTabletSpaceUsage &space_usage);
  int modify_and_fill_tablet(
      const ObTablet &old_tablet,
      ObITabletMetaModifier &modifier,
      ObTabletHandle &new_handle);
  int persist_sstable_linked_block_if_need(
      ObArenaAllocator &allocator,
      ObITable * const table,
      ObIArray<ObObjectsWriteCtx> &sstable_meta_write_ctxs);
  int fetch_and_persist_sstable(
      ObTableStoreIterator &table_iter,
      ObTabletTableStore &new_table_store,
      common::ObIArray<ObObjectsWriteCtx> &meta_write_ctxs,
      int64_t &total_tablet_meta_size,
      ObBlockInfoSet &block_info_set);
  int fetch_and_persist_normal_sstable(
    ObArenaAllocator &allocator,
    ObITable *table,
    ObSSTablePersistCtx &sstable_persist_ctx);
  int sync_write_ctx_to_total_ctx_if_failed(
    common::ObIArray<ObObjectsWriteCtx> &write_ctxs,
    common::ObIArray<ObObjectsWriteCtx> &total_write_ctxs);
  int batch_write_sstable_info(
    common::ObIArray<ObObjectWriteInfo> &write_infos,
    common::ObIArray<ObObjectsWriteCtx> &write_ctxs,
    common::ObIArray<ObMetaDiskAddr> &addrs,
    common::ObIArray<ObObjectsWriteCtx> &meta_write_ctxs,
    ObBlockInfoSet &block_info_set);
  int fetch_table_store_and_write_info(
      const ObTablet &tablet,
      ObTabletMemberWrapper<ObTabletTableStore> &wrapper,
      common::ObIArray<ObObjectWriteInfo> &write_infos,
      common::ObIArray<ObObjectsWriteCtx> &meta_write_ctxs,
      int64_t &total_tablet_meta_size,
      ObBlockInfoSet &block_info_set);
  int load_storage_schema_and_fill_write_info(
      const ObTablet &tablet,
      common::ObArenaAllocator &allocator,
      common::ObIArray<ObObjectWriteInfo> &write_infos);
  int link_write_medium_info_list(
      const ObTabletDumpedMediumInfo *medium_info_list,
      common::ObIArray<ObObjectsWriteCtx> &meta_write_ctxs,
      ObMetaDiskAddr &addr,
      int64_t &total_tablet_meta_size,
      ObBlockInfoSet::TabletMacroSet &meta_block_id_set);
  template <typename T>
  int fill_write_info(
      common::ObArenaAllocator &allocator,
      const T *t,
      common::ObIArray<ObObjectWriteInfo> &write_infos);
  int write_and_fill_args(
      const common::ObIArray<ObObjectWriteInfo> &write_infos,
      ObTabletTransformArg &arg,
      common::ObIArray<ObObjectsWriteCtx> &meta_write_ctxs,
      int64_t &total_tablet_meta_size,
      ObBlockInfoSet::TabletMacroSet &meta_block_id_set);
  int persist_aggregated_meta(
      const ObTabletMacroInfo &tablet_macro_info,
      ObTabletHandle &new_handle,
      ObTabletSpaceUsage &space_usage);
  int fill_tablet_write_info(
      common::ObArenaAllocator &allocator,
      const ObTablet *tablet,
      const ObTabletMacroInfo &tablet_macro_info,
      ObObjectWriteInfo &write_info) const;
  int load_table_store(
      common::ObArenaAllocator &allocator,
      const ObTablet &tablet,
      const ObMetaDiskAddr &addr,
      ObTabletTableStore *&table_store);
  void print_time_stats(
      const ObMultiTimeStats::TimeStats &time_stats,
      const int64_t stats_warn_threshold,
      const int64_t print_interval);
public:
  static const int64_t SSTABLE_MAX_SERIALIZE_SIZE = 1966080L;  // 1.875MB
  static const int64_t DEFAULT_CTX_ID = 0;

private:
  ObArenaAllocator allocator_;
  ObMultiTimeStats multi_stats_;
  const ObTabletPersisterParam &param_;

  DISALLOW_COPY_AND_ASSIGN(ObTabletPersister);
};

template <typename T>
int ObTabletPersister::fill_write_info(
    common::ObArenaAllocator &allocator,
    const T *t,
    common::ObIArray<ObObjectWriteInfo> &write_infos)
{
  int ret = common::OB_SUCCESS;
  int64_t size = 0;

  if (OB_ISNULL(t)) {
    ret = common::OB_ERR_UNEXPECTED;
    STORAGE_LOG(WARN, "unexpected error, tablet member is nullptr", K(ret), KP(t));
  } else if (FALSE_IT(size = t->get_serialize_size())) {
  } else if (OB_UNLIKELY(size <= 0)) {
    ret = OB_ERR_UNEXPECTED;
    STORAGE_LOG(WARN, "failed to get serialize size", K(ret), KPC(t));
  } else {
    char *buf = static_cast<char *>(allocator.alloc(size));
    int64_t pos = 0;
    if (OB_ISNULL(buf)) {
      ret = common::OB_ALLOCATE_MEMORY_FAILED;
      STORAGE_LOG(WARN, "fail to allocate memory for serialize", K(ret), K(size));
    } else if (OB_FAIL(t->serialize(buf, size, pos))) {
      STORAGE_LOG(WARN, "fail to serialize member", K(ret), KP(buf), K(size), K(pos));
    } else {
      ObObjectWriteInfo write_info;
      write_info.buffer_ = buf;
      write_info.offset_ = 0;
      write_info.size_ = size;
      write_info.io_desc_.set_wait_event(ObWaitEventIds::DB_FILE_COMPACT_WRITE);
      if (OB_FAIL(write_infos.push_back(write_info))) {
        STORAGE_LOG(WARN, "fail to push back write info", K(ret));
      }
    }
  }
  return ret;
}

} // end namespace storage
} // end namespace oceanbase

#endif /* OCEANBASE_STORAGE_OB_TABLET_PERSIST_H_ */
