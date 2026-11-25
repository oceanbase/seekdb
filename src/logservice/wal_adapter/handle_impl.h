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

#ifndef OCEANBASE_LOGSERVICE_WAL_ADAPTER_HANDLE_IMPL_
#define OCEANBASE_LOGSERVICE_WAL_ADAPTER_HANDLE_IMPL_

#include <memory>
#include <optional>
#include <vector>
#include "common/ob_role.h"
#include "lib/lock/ob_tc_rwlock.h"
#include "share/log/types/base_info.h"
#include "callback_wrapper.h"
#include "iterator.h"
#include "rust/wal.h"

namespace oceanbase
{
namespace wal
{
using share::SCN;
using ImportedGroups = std::vector<wal_rs::ImportedGroup>;

struct Stat
{
  OB_UNIS_VERSION(1);
public:
  Stat();
  ~Stat() { reset(); }
  bool is_valid() const;
  void reset();
  share::SCN begin_scn_;
  LSN base_lsn_;
  LSN end_lsn_;
  share::SCN end_scn_;
  TO_STRING_KV(K_(begin_scn), K_(base_lsn), K_(end_lsn), K_(end_scn));
};

class HandleImpl
{
public:
  HandleImpl();
  ~HandleImpl();
  int init(::rust::Box<wal_rs::Stream> stream);
  bool check_can_be_used() const;
  void destroy();
  int submit_log(const char *buf, const int64_t buf_len,
      const share::SCN &ref_scn, LSN &lsn, share::SCN &scn);
  int submit_imported_groups(const ImportedGroups &groups);
  int set_base_lsn(const LSN &lsn);
  int locate_by_scn_coarsely(const share::SCN &scn, LSN &result_lsn);
  int locate_by_lsn_coarsely(const LSN &lsn, share::SCN &result_scn);
  int alloc_wal_buffer_iterator(const LSN &offset, EntryIterator &iterator);
  int alloc_wal_buffer_iterator(const SCN &scn, EntryIterator &iterator);
  int alloc_wal_group_buffer_iterator(const LSN &offset,
      GroupIterator &iterator);
  int alloc_wal_group_buffer_iterator(const share::SCN &scn,
      GroupIterator &iterator);
  int register_file_size_cb(wal::FsyncCallbackNode *fs_cb);
  int unregister_file_size_cb(wal::FsyncCallbackNode *fs_cb);
  int get_base_lsn(LSN &lsn) const;
  int get_base_info(const LSN &base_lsn, BaseInfo &base_info);
  const LSN get_end_lsn() const;
  const share::SCN get_end_scn() const;
  int stat(Stat &wal_stat);
  TO_STRING_KV(K_(is_inited));

private:
  typedef common::RWLock RWLock;
  typedef RWLock::RLockGuard RLockGuard;
  typedef RWLock::WLockGuard WLockGuard;
  template<typename IteratorType>
  int alloc_iterator_from_scn_(const SCN &scn, IteratorType &iterator);

private:
  mutable RWLock lock_;
  std::optional<::rust::Box<wal_rs::Stream>> stream_;
  wal::FsyncCallbackWrapper fs_cb_wrapper_;
  bool is_inited_;
};
using HandleImplPtr = std::shared_ptr<HandleImpl>;
} // namespace wal
} // namespace oceanbase
#endif
