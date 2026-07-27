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

#ifndef OCEANBASE_STORAGE_OB_TABLET_HANDLE
#define OCEANBASE_STORAGE_OB_TABLET_HANDLE

#include "storage/meta_mem/ob_meta_obj_struct.h"
#include "storage/tablet/ob_table_store_util.h"
#include "storage/tablet/ob_tablet_table_store_iterator.h"
#include "share/ob_ddl_common.h"

namespace oceanbase
{
namespace storage
{
enum class WashTabletPriority : int8_t
{
  WTP_HIGH = 0,
  WTP_LOW  = 1,
  WTP_MAX
};
class ObTablet;

class ObTabletHandle : public ObMetaObjGuard<ObTablet>
{
private:
  typedef ObMetaObjGuard<ObTablet> Base;
public:
  ObTabletHandle();
  ObTabletHandle(const ObTabletHandle &other);
  virtual ~ObTabletHandle();
  ObTabletHandle &operator = (const ObTabletHandle &other);
  virtual void set_obj(ObMetaObj<ObTablet> &obj) override;
  virtual void set_obj(ObTablet *obj, common::ObIAllocator *allocator, ObStorageMetaMemMgr *t3m) override;
  virtual void reset() override;
  virtual bool need_hold_time_check() const override { return true; }
  void set_wash_priority(const WashTabletPriority priority) { wash_priority_ = priority; }
  common::ObArenaAllocator *get_allocator() { return static_cast<common::ObArenaAllocator *>(allocator_); }
  void disallow_copy_and_assign() { allow_copy_and_assign_ = false; }
  bool is_tmp_tablet() const { return !allow_copy_and_assign_; }
  char *get_buf() { return reinterpret_cast<char *>(obj_); }
  int64_t get_buf_len() const { return get_buf_header().buf_len_; }
  DECLARE_VIRTUAL_TO_STRING;
private:
  int64_t calc_wash_score(const WashTabletPriority priority) const;
  ObMetaObjBufferHeader &get_buf_header() const
  {
    return ObMetaObjBufferHelper::get_buffer_header(reinterpret_cast<char *>(obj_));
  }
private:
  WashTabletPriority wash_priority_;
  bool allow_copy_and_assign_;
};

class ObTabletTableIterator final
{
  friend class ObTablet;
  friend class ObLSTabletService;
  typedef ObSEArray<share::ObForkTabletInfo, 1> ForkTabletInfoArray;
  typedef ObSEArray<ObTabletHandle, 1> ForkTabletHandleArray;
  struct ForkCtx final
  {
    ForkCtx() : fork_infos_(), fork_tablet_handles_() {}
    ~ForkCtx() { reset(); }
    void reset()
    {
      fork_infos_.reset();
      fork_tablet_handles_.reset();
    }
    TO_STRING_KV(K_(fork_infos), K_(fork_tablet_handles));
    ForkTabletInfoArray fork_infos_;
    ForkTabletHandleArray fork_tablet_handles_;
    DISALLOW_COPY_AND_ASSIGN(ForkCtx);
  };
public:
  ObTabletTableIterator()
      : tablet_handle_(),
        table_store_iter_(),
        fork_ctx_(nullptr)
  {}
  explicit ObTabletTableIterator(const bool is_reverse)
      : tablet_handle_(),
        table_store_iter_(is_reverse),
        fork_ctx_(nullptr)
  {}
  int assign(const ObTabletTableIterator& other);
  ~ObTabletTableIterator() { reset(); }
  void reset()
  {
    table_store_iter_.reset();
    tablet_handle_.reset();
    destroy_fork_ctx_();
  }
  bool is_valid() const { return tablet_handle_.is_valid() || table_store_iter_.is_valid(); }
  ObTableStoreIterator *table_iter();
  const ObTableStoreIterator *table_iter() const;
  const ObTablet *get_tablet() const { return tablet_handle_.get_obj(); }
  ObTablet *get_tablet() { return tablet_handle_.get_obj(); }
  const ObTabletHandle &get_tablet_handle() { return tablet_handle_; }
  const ObTabletHandle *get_tablet_handle_ptr() const { return &tablet_handle_; }
  const ObIArray<share::ObForkTabletInfo> *get_fork_infos() const
  {
    return (nullptr == fork_ctx_ || fork_ctx_->fork_infos_.empty()) ? nullptr : &fork_ctx_->fork_infos_;
  }
  int set_tablet_handle(const ObTabletHandle &tablet_handle);
  int add_fork_tablet_handle(const ObTabletHandle &tablet_handle, share::ObForkTabletInfo &fork_info);
  int refresh_read_tables_from_tablet(
      const int64_t snapshot_version,
      const bool allow_no_ready_read,
      const bool major_sstable_only);
  int get_mds_sstables_from_tablet(const int64_t snapshot_version);
  int get_read_tables_from_tablet(
      const int64_t snapshot_version,
      const bool allow_no_ready_read,
      const bool major_sstable_only,
      ObIArray<ObITable *> &tables);
  TO_STRING_KV(K_(tablet_handle), K_(table_store_iter), KPC_(fork_ctx));
private:
  void destroy_fork_ctx_()
  {
    if (nullptr != fork_ctx_) {
      fork_ctx_->~ForkCtx();
      ob_free(fork_ctx_);
      fork_ctx_ = nullptr;
    }
  }

  int build_fork_ctx_()
  {
    int ret = OB_SUCCESS;
    if (nullptr == fork_ctx_) {
      void *buf = ob_malloc(sizeof(ForkCtx), ObMemAttr("ForkTblIter"));
      if (OB_ISNULL(buf)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        fork_ctx_ = new (buf) ForkCtx();
      }
    }
    return ret;
  }

  ObTabletHandle tablet_handle_;
  ObTableStoreIterator table_store_iter_;
  ForkCtx *fork_ctx_;
  DISALLOW_COPY_AND_ASSIGN(ObTabletTableIterator);
};

struct ObGetTableParam final
{
public:
  ObGetTableParam() : frozen_version_(-1), sample_info_(), tablet_iter_(), refreshed_merge_(nullptr) {}
  ~ObGetTableParam() { reset(); }
  bool is_valid() const { return tablet_iter_.is_valid(); }
  void reset()
  {
    frozen_version_ = -1;
    sample_info_.reset();
    tablet_iter_.reset();
    refreshed_merge_ = nullptr;
  }
  TO_STRING_KV(K_(frozen_version), K_(sample_info), K_(tablet_iter));
public:
  int64_t frozen_version_;
  common::SampleInfo sample_info_;
  ObTabletTableIterator tablet_iter_;

  // when tablet has been refreshed, to notify other ObMultipleMerge in ObTableScanIterator re-inited
  // before rescan.
  void *refreshed_merge_;

  DISALLOW_COPY_AND_ASSIGN(ObGetTableParam);
};

} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_OB_TABLET_HANDLE
