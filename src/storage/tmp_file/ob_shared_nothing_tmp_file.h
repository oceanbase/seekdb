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

#pragma once

#include "storage/tmp_file/ob_tmp_file_cache.h"
#include "storage/tmp_file/ob_tmp_file_global.h"
#include "storage/tmp_file/ob_tmp_file_types.h"
#include "storage/tmp_file/ob_tmp_file_write_buffer_index_cache.h"
#include "storage/blocksstable/ob_macro_block_id.h"
#include "lib/lock/ob_spin_rwlock.h"
#include "lib/lock/ob_spin_lock.h"
#include "lib/lock/ob_tc_rwlock.h"
#include "storage/tmp_file/ob_tmp_file_meta_tree.h"

namespace oceanbase
{
namespace tmp_file
{
class ObTmpFileIOCtx;
class ObTmpFileBlockPageBitmap;
class ObTmpFileBlockManager;
class ObTmpFileEvictionManager;
class ObTmpFileFlushPriorityManager;
class ObTmpFileFlushInfo;
class ObTmpFileFlushTask;
class ObTmpFilePageCacheController;
class ObTmpFileTreeFlushContext;
class ObTmpFileDataFlushContext;

//for virtual table show
class ObSNTmpFileInfo final : public ObTmpFileInfo
{
public:
  ObSNTmpFileInfo() :
    ObTmpFileInfo(),
    meta_tree_epoch_(0),
    meta_tree_level_cnt_(0),
    meta_size_(0),
    cached_meta_page_num_(0),
    write_back_meta_page_num_(0),
    all_type_page_flush_cnt_(0) {}
  virtual ~ObSNTmpFileInfo() { reset(); }
  virtual void reset() override;
public:
  int64_t meta_tree_epoch_;
  int64_t meta_tree_level_cnt_;
  int64_t meta_size_;
  int64_t cached_meta_page_num_;
  int64_t write_back_meta_page_num_;
  int64_t all_type_page_flush_cnt_;
  INHERIT_TO_STRING_KV("ObTmpFileInfo", ObTmpFileInfo,
                       K(meta_tree_epoch_),
                       K(meta_tree_level_cnt_), K(meta_size_), K(cached_meta_page_num_),
                       K(write_back_meta_page_num_), K(all_type_page_flush_cnt_));
};

class ObSharedNothingTmpFile final
{
public:
  int delete_file();
  int seal();
  int aio_pread(ObTmpFileIOCtx &io_ctx);
  int aio_write(ObTmpFileIOCtx &io_ctx);
  // truncate offset is open interval

  struct ObTmpFileNode : public ObDLinkBase<ObTmpFileNode>
  {
    ObTmpFileNode(ObSharedNothingTmpFile &file) : file_(file) {}
    ObSharedNothingTmpFile &file_;
  };

  bool can_remove();
  bool is_deleting();
  bool is_sealed();
  bool is_flushing();

  int64_t get_file_size();
  int64_t cal_wbp_begin_offset();
  void update_read_offset(int64_t read_offset);
  int64_t get_dirty_data_page_size_with_lock();

  OB_INLINE int64_t get_fd() const { return fd_; }
  OB_INLINE int64_t get_dir_id() const { return dir_id_; }
  OB_INLINE void inc_ref_cnt() { ATOMIC_INC(&ref_cnt_); }
  OB_INLINE void dec_ref_cnt() { ATOMIC_AAF(&ref_cnt_, -1); }
  OB_INLINE void dec_ref_cnt(int64_t &ref_cnt)
  {
    ref_cnt = ATOMIC_AAF(&ref_cnt_, -1);
  }
  OB_INLINE int64_t get_ref_cnt() const { return ATOMIC_LOAD(&ref_cnt_); }
  OB_INLINE void set_data_page_flush_level(int64_t data_page_flush_level)
  {
    data_page_flush_level_ = data_page_flush_level;
  }
  OB_INLINE int64_t get_data_page_flush_level() const { return data_page_flush_level_; }
  OB_INLINE ObTmpFileNode *get_data_flush_node() { return &data_flush_node_; }
  int reinsert_data_flush_node();
  int remove_data_flush_node();

  // for virtual table
  void set_read_stats_vars(const ObTmpFileIOCtx &ctx, const int64_t read_size);
  void set_write_stats_vars(const ObTmpFileIOCtx &ctx);

  struct InnerFlushInfo
  {
  public:
    InnerFlushInfo();
    ~InnerFlushInfo() { reset(); }
    void reset();
    bool has_data() const { return flush_data_page_num_ > 0; }
    bool has_meta() const { return !flush_meta_page_array_.empty(); }
    int init_by_tmp_file_flush_info(const ObTmpFileFlushInfo& flush_info);
    TO_STRING_KV(K(flush_data_page_disk_begin_id_), K(flush_data_page_num_), K(flush_virtual_page_id_), K(file_size_),
                 K(flush_meta_page_array_));
  public:
    bool update_meta_data_done_;
    // information for updating data
    int64_t flush_data_page_disk_begin_id_;  // record begin page id in the macro block, for updating meta tree item
    int64_t flush_data_page_num_;
    int64_t flush_virtual_page_id_;          // record virtual_page_id while copying data, pass to meta tree while inserting items
    int64_t file_size_;                      // if file_size > 0, it means the last page is in flushing
    // information for updating meta tree
    ObArray<ObTmpFileTreeIOInfo> flush_meta_page_array_;
  };

  struct InnerFlushContext
  {
    InnerFlushContext();
    void reset();
    int update_finished_continuous_flush_info_num(const bool is_meta, const int64_t end_pos);
    bool is_all_finished() const { return is_data_finished() && is_meta_finished(); }
    bool is_data_finished() const
    {
      return data_flush_infos_.size() == data_finished_continuous_flush_info_num_;
    }
    bool is_meta_finished() const
    {
      return meta_flush_infos_.size() == meta_finished_continuous_flush_info_num_;
    }
    bool is_flushing() const
    {
      return !data_flush_infos_.empty() || !meta_flush_infos_.empty();
    }
    TO_STRING_KV(K(flush_seq_), K(data_flush_infos_.size()), K(meta_flush_infos_.size()),
                 K(data_finished_continuous_flush_info_num_),
                 K(meta_finished_continuous_flush_info_num_),
                 K(need_to_wait_for_the_previous_data_flush_req_to_complete_),
                 K(need_to_wait_for_the_previous_meta_flush_req_to_complete_));
    int64_t flush_seq_;
    int64_t data_finished_continuous_flush_info_num_;
    int64_t meta_finished_continuous_flush_info_num_;
    ObArray<InnerFlushInfo> data_flush_infos_;
    ObArray<InnerFlushInfo> meta_flush_infos_;
    bool need_to_wait_for_the_previous_data_flush_req_to_complete_;
    bool need_to_wait_for_the_previous_meta_flush_req_to_complete_;
  };

  ObSharedNothingTmpFile();
  ~ObSharedNothingTmpFile();
  int init(const int64_t fd, const int64_t dir_id,
           ObTmpFileBlockManager *block_manager,
           ObIAllocator *callback_allocator,
           ObIAllocator *wbp_index_cache_allocator,
           ObIAllocator *wbp_index_cache_bkt_allocator,
           ObTmpFilePageCacheController *page_cache_controller,
           const char* label);
  void reset();
  int release_resource();
  int evict_data_pages(const int64_t expected_evict_page_num,
                       int64_t &actual_evict_page_num,
                       int64_t &remain_flushed_page_num);
  int evict_meta_pages(const int64_t expected_evict_page_num,
                       int64_t &actual_evict_page_num);
  // truncate offset is open interval
  int truncate(const int64_t truncate_offset);

  // ATTENTION!!!
  // Currently, K(tmp_file) is used to print the ObSharedNothingTmpFile structure without holding
  // the file lock. Before adding the print field, make sure it is thread-safe.
  TO_STRING_KV(K(is_inited_), K(dir_id_), K(fd_),
                       K(is_deleting_), K(is_sealed_),
                       K(ref_cnt_), K(truncated_offset_), K(read_offset_),
                       K(file_size_),
                       K(cached_page_nums_), K(write_back_data_page_num_),
                       K(begin_page_id_), K(begin_page_virtual_id_),
                       K(end_page_id_),
                       K(data_page_flush_level_),
                       KP(data_flush_node_.get_next()),
                       KP(wbp_), KP(flush_prio_mgr_), KP(&page_idx_cache_), KP(callback_allocator_),
                       K(trace_id_), K(birth_ts_), K(label_),
                       K(write_req_cnt_), K(unaligned_write_req_cnt_),
                       K(write_persisted_tail_page_cnt_), K(lack_page_cnt_), K(last_modify_ts_),
                       K(read_req_cnt_), K(unaligned_read_req_cnt_),
                       K(total_truncated_page_read_cnt_), K(total_uncached_page_read_cnt_),
                       K(total_kv_cache_page_read_cnt_), K(total_wbp_page_read_cnt_),
                       K(truncated_page_read_hits_), K(uncached_page_read_hits_),
                       K(kv_cache_page_read_hits_), K(wbp_page_read_hits_),
                       K(total_read_size_), K(last_access_ts_),
                       K(flushed_data_page_num_),
                       K(flushed_page_id_), K(flushed_page_virtual_id_),
                       K(meta_page_flush_level_), KP(meta_flush_node_.get_next()),
                       K(is_in_data_eviction_list_), K(is_in_meta_eviction_list_),
                       KP(data_eviction_node_.get_next()),
                       KP(meta_eviction_node_.get_next()));

  // for flush
  int update_meta_after_flush(const int64_t info_idx, const bool is_meta, bool &reset_ctx);
  int generate_data_flush_info(ObTmpFileFlushTask &flush_task,
                               ObTmpFileFlushInfo &info,
                               ObTmpFileDataFlushContext &data_flush_context,
                               const int64_t flush_sequence,
                               const bool need_flush_tail);
  int generate_meta_flush_info(ObTmpFileFlushTask &flush_task,
                               ObTmpFileFlushInfo &info,
                               ObTmpFileTreeFlushContext &meta_flush_context,
                               const int64_t flush_sequence,
                               const bool need_flush_tail);
  int insert_meta_tree_item(const ObTmpFileFlushInfo &info, int64_t block_index);
  int copy_finish();

  int remove_meta_flush_node();
  int reinsert_meta_flush_node();
  bool is_in_meta_flush_list();
  OB_INLINE ObTmpFileNode *get_meta_flush_node() { return &meta_flush_node_; }
  OB_INLINE ObTmpFileNode &get_data_eviction_node() { return data_eviction_node_; }
  OB_INLINE ObTmpFileNode &get_meta_eviction_node() { return meta_eviction_node_; }
  OB_INLINE const ObTmpFileNode &get_data_eviction_node() const { return data_eviction_node_; }
  OB_INLINE const ObTmpFileNode &get_meta_eviction_node() const { return meta_eviction_node_; }

  OB_INLINE void set_meta_page_flush_level(int64_t meta_page_flush_level)
  {
    meta_page_flush_level_ = meta_page_flush_level;
  }
  OB_INLINE int64_t get_meta_page_flush_level() const { return meta_page_flush_level_; }
  void get_dirty_meta_page_num(int64_t &non_rightmost_dirty_page_num, int64_t &rightmost_dirty_page_num) const;
  void get_dirty_meta_page_num_with_lock(int64_t &non_rightmost_dirty_page_num, int64_t &rightmost_dirty_page_num);
  int copy_info_for_virtual_table(ObTmpFileInfo &tmp_file_info);

private:
  int init_io_state_(const int64_t dir_id,
                   const int64_t fd,
                   ObTmpWriteBufferPool *wbp,
                   ObTmpFileFlushPriorityManager *flush_prio_mgr,
                   ObIAllocator *callback_allocator,
                   ObIAllocator *wbp_index_cache_allocator,
                   ObIAllocator *wbp_index_cache_bkt_allocator,
                   const char* label);
  void reset_io_state_();

  int truncate_io_(const int64_t truncate_offset);

  int inner_read_truncated_part_(ObTmpFileIOCtx &io_ctx);
  int inner_read_from_wbp_(ObTmpFileIOCtx &io_ctx);

  int inner_write_(ObTmpFileIOCtx &io_ctx);
  int inner_fill_tail_page_(ObTmpFileIOCtx &io_ctx);
  int inner_write_continuous_pages_(ObTmpFileIOCtx &io_ctx);
  int alloc_and_write_pages_(const ObTmpFileIOCtx &io_ctx,
                             ObArray<uint32_t> &alloced_page_id,
                             int64_t &actual_write_size);

  int truncate_cached_pages_(const int64_t truncate_offset, const int64_t wbp_begin_offset);

  // for virtual table
  void inner_set_read_stats_vars_(const ObTmpFileIOCtx &ctx, const int64_t read_size);
  void inner_set_write_stats_vars_(const ObTmpFileIOCtx &ctx);

  int64_t cal_wbp_begin_offset_() const;
  int reinsert_data_flush_node_();
  int insert_or_update_data_flush_node_();

  OB_INLINE bool has_unfinished_page_() const { return file_size_ % ObTmpFileGlobal::ALLOC_PAGE_SIZE != 0; }
  OB_INLINE int64_t get_page_begin_offset_(const int64_t offset) const
  {
    return common::lower_align(offset, ObTmpFileGlobal::ALLOC_PAGE_SIZE);
  }
  OB_INLINE int64_t get_page_end_offset_(const int64_t offset) const
  {
    return common::upper_align(offset, ObTmpFileGlobal::ALLOC_PAGE_SIZE);
  }
  OB_INLINE int64_t get_page_begin_offset_by_virtual_id_(const int64_t virtual_page_id) const
  {
    return virtual_page_id * ObTmpFileGlobal::ALLOC_PAGE_SIZE;
  }
  OB_INLINE int64_t get_page_end_offset_by_virtual_id_(const int64_t virtual_page_id) const
  {
    return (virtual_page_id + 1) * ObTmpFileGlobal::ALLOC_PAGE_SIZE;
  }
  OB_INLINE int64_t get_page_virtual_id_(const int64_t offset, const bool is_open_interval) const
  {
    return is_open_interval ?
           common::upper_align(offset, ObTmpFileGlobal::ALLOC_PAGE_SIZE) / ObTmpFileGlobal::ALLOC_PAGE_SIZE - 1 :
           offset / ObTmpFileGlobal::ALLOC_PAGE_SIZE;
  }
  OB_INLINE int64_t get_end_page_virtual_id_() const
  {
    return cached_page_nums_ == 0 ?
           ObTmpFileGlobal::INVALID_VIRTUAL_PAGE_ID :
           get_page_virtual_id_(file_size_, true /*is_open_interval*/);
  }
  OB_INLINE int64_t get_offset_in_page_(const int64_t offset) const
  {
    return offset % ObTmpFileGlobal::ALLOC_PAGE_SIZE;
  }

  OB_INLINE int64_t get_offset_in_block_(int64_t file_offset) const
  {
    int64_t ret = 0;
    ret = file_offset % ObTmpFileGlobal::SN_BLOCK_SIZE;
    return ret;
  }

  bool is_inited_;

  int64_t dir_id_;
  int64_t fd_;
  bool is_deleting_;
  bool is_sealed_;
  int64_t ref_cnt_;
  int64_t truncated_offset_;      // read data befor truncated_offset will be set as 0
  int64_t read_offset_;           // read offset is on the entire file
  int64_t file_size_;             // has written size of this file
  int64_t cached_page_nums_;      // page nums in write buffer pool
  int64_t write_back_data_page_num_;
  uint32_t begin_page_id_;        // the first page index in write buffer pool
  int64_t begin_page_virtual_id_;
  uint32_t end_page_id_;          // the last page index in write buffer pool
  int64_t diag_log_print_cnt_;    // print diagnosis log every PRINT_LOG_FILE_SIZE
  int64_t data_page_flush_level_;
  ObTmpFileNode data_flush_node_;
  common::TCRWLock meta_lock_; // handle conflicts between writing and reading meta tree and meta data of file
  ObSpinLock stat_lock_;       // for updating vtable write info/read info
  ObSpinLock multi_write_lock_; // handle conflicts between multiple writes
  ObSpinLock last_page_lock_; // handle conflicts between writing and evicting for last page
  ObTmpWriteBufferPool *wbp_;
  ObTmpFileFlushPriorityManager *flush_prio_mgr_;
  ObTmpFileWBPIndexCache page_idx_cache_;
  ObIAllocator *callback_allocator_;
  /********for virtual table begin********/
  // common info
  common::ObCurTraceId::TraceId trace_id_;
  int64_t birth_ts_;
  ObFixedLengthString<ObTmpFileGlobal::TMP_FILE_MAX_LABEL_SIZE + 1> label_;
  // write info
  int64_t write_req_cnt_;
  int64_t unaligned_write_req_cnt_;
  int64_t write_persisted_tail_page_cnt_;
  int64_t lack_page_cnt_;
  int64_t last_modify_ts_;
  // read info
  int64_t read_req_cnt_;
  int64_t unaligned_read_req_cnt_;
  int64_t total_truncated_page_read_cnt_;
  int64_t total_kv_cache_page_read_cnt_;
  int64_t total_uncached_page_read_cnt_;
  int64_t total_wbp_page_read_cnt_;
  int64_t truncated_page_read_hits_;
  int64_t kv_cache_page_read_hits_;
  int64_t uncached_page_read_hits_;
  int64_t wbp_page_read_hits_;
  int64_t total_read_size_;
  int64_t last_access_ts_;
  /********for virtual table end********/

  int inner_read_from_disk_(const int64_t expected_read_disk_size, ObTmpFileIOCtx &io_ctx);
  int inner_direct_read_from_block_(const int64_t block_index,
                                    const int64_t begin_read_offset_in_block, const int64_t end_read_offset_in_block,
                                    ObTmpFileIOCtx &io_ctx);
  int inner_cached_read_from_block_(const int64_t block_index,
                                    const int64_t begin_read_offset_in_block, const int64_t end_read_offset_in_block,
                                    ObTmpFileIOCtx &io_ctx,
                                    ObTmpFileInfo::ObTmpFileReadInfo &read_stat);
  int inner_cached_read_from_block_with_prefetch_(const int64_t block_index,
                                                  const int64_t begin_read_offset_in_block,
                                                  const int64_t end_offset_in_block,
                                                  const int64_t user_read_size,
                                                  ObTmpFileIOCtx &io_ctx,
                                                  ObTmpFileInfo::ObTmpFileReadInfo &read_stat);
  int collect_pages_in_block_(const int64_t block_index,
                              const int64_t begin_page_idx_in_block,
                              const int64_t end_page_idx_in_block,
                              ObTmpFileBlockPageBitmap &bitmap,
                              ObIArray<ObTmpPageValueHandle> &page_value_handles);
  int inner_read_continuous_cached_pages_(const int64_t begin_read_offset_in_block,
                                          const int64_t end_read_offset_in_block,
                                          const ObArray<ObTmpPageValueHandle> &page_value_handles,
                                          const int64_t start_array_idx,
                                          ObTmpFileIOCtx &io_ctx);
  int inner_read_continuous_uncached_pages_(const int64_t block_index,
                                            const int64_t begin_io_read_offset,
                                            const int64_t end_io_read_offset,
                                            const int64_t user_read_size,
                                            ObTmpFileIOCtx &io_ctx);

  int swap_page_to_disk_(const ObTmpFileIOCtx &io_ctx);
  int load_disk_tail_page_and_rewrite_(ObTmpFileIOCtx &io_ctx);
  int append_write_memory_tail_page_(ObTmpFileIOCtx &io_ctx);

  int truncate_the_first_wbp_page_();
  int truncate_persistent_pages_(const int64_t truncate_offset);

  int collect_flush_data_page_id_(ObTmpFileFlushTask &flush_task, ObTmpFileFlushInfo &info,
      ObTmpFileDataFlushContext &data_flush_context,
      const uint32_t copy_begin_page_id, const int64_t copy_begin_page_virtual_id,
      const uint32_t copy_end_page_id, const int64_t flush_sequence, const bool need_flush_tail);
  int cal_end_position_(ObArray<InnerFlushInfo> &flush_infos,
                        const int64_t start_pos,
                        int64_t &end_pos,
                        int64_t &flushed_data_page_num);
  int cal_next_flush_page_id_from_flush_ctx_or_file_(const ObTmpFileDataFlushContext &data_flush_context,
                                                     uint32_t &next_flush_page_id,
                                                     int64_t &next_flush_page_virtual_id);
  int64_t get_dirty_data_page_size_() const;
  int get_next_flush_page_id_(uint32_t& next_flush_page_id, int64_t& next_flush_page_virtual_id) const;
  int get_flush_end_page_id_(uint32_t& flush_end_page_id, const bool need_flush_tail) const;
  int get_physical_page_id_in_wbp_(const int64_t virtual_page_id, uint32_t& page_id) const ;
  int remove_useless_page_in_data_flush_infos_(const int64_t start_pos,
                                               const int64_t end_pos,
                                               const int64_t flushed_data_page_num,
                                               int64_t &new_start_pos,
                                               int64_t &new_flushed_data_page_num);
  int update_file_meta_after_flush_(const int64_t start_pos,
                                    const int64_t end_pos,
                                    const int64_t flushed_data_page_num);
  int update_meta_tree_after_flush_(const int64_t start_pos, const int64_t end_pos);

  int generate_data_flush_info_(ObTmpFileFlushTask &flush_task,
                                ObTmpFileFlushInfo &info,
                                ObTmpFileDataFlushContext &data_flush_context,
                                const int64_t flush_sequence,
                                const bool need_flush_tail);
  int generate_meta_flush_info_(ObTmpFileFlushTask &flush_task,
                                ObTmpFileFlushInfo &info,
                                ObTmpFileTreeFlushContext &meta_flush_context,
                                const int64_t flush_sequence,
                                const bool need_flush_tail);
  int reinsert_meta_flush_node_();
  int insert_or_update_meta_flush_node_();
  bool is_flushing_();

  int inner_seal_();
  int inner_delete_file_();

  ObTmpFileBlockManager *tmp_file_block_manager_;
  ObTmpFilePageCacheController *page_cache_controller_;
  ObTmpFileEvictionManager *eviction_mgr_;
  bool is_in_data_eviction_list_;
  bool is_in_meta_eviction_list_;
  int64_t meta_page_flush_level_;
  int64_t flushed_data_page_num_; // equal to the page num between [begin_page_id_, flushed_page_id_] in wbp
  uint32_t flushed_page_id_;      // the last flushed page index in write buffer pool.
                                  // specifically, if flushed_page_id_ == end_page_id_,
                                  // it means the last page has been flushed. However,
                                  // the last page might be appending written after flushing.
                                  // thus, in this case, whether "flushed_page_id_" page in wbp is flushed
                                  // needs to be checked with page status (with some get functions).
  int64_t flushed_page_virtual_id_;
  ObSharedNothingTmpFileMetaTree meta_tree_;
  ObTmpFileNode meta_flush_node_;
  ObTmpFileNode data_eviction_node_;
  ObTmpFileNode meta_eviction_node_;
  SpinRWLock truncate_lock_; // handle conflicts between truncate and flushing
  InnerFlushContext inner_flush_ctx_; // file-level flush context

};

}  // end namespace tmp_file
}  // end namespace oceanbase
