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

#include "logservice/wal_adapter/handle.h"
#include "share/rc/ob_server_runtime.h"

namespace oceanbase
{
using namespace share;
namespace wal
{
#define CHECK_VALID if (NULL == wal_handle_impl_) { return OB_NOT_INIT; }

Handle::Handle() : wal_handle_impl_(NULL),
                           fs_cb_(NULL)
{
}

Handle::Handle(const Handle& rhs)
{
  *this = rhs;
}

Handle::~Handle()
{
  wal_handle_impl_ = NULL;
  fs_cb_ = NULL;
}

bool Handle::is_valid() const
{
  return NULL != wal_handle_impl_;
}

Handle& Handle::operator=(const Handle &rhs)
{
  if (this == &rhs) {
    return *this;
  }
  wal_handle_impl_ = rhs.wal_handle_impl_;
  fs_cb_ = rhs.fs_cb_;
  return *this;
}

bool  Handle::operator==(const Handle &rhs) const
{
  return wal_handle_impl_ == rhs.wal_handle_impl_;
}

int Handle::append(const AppendOptions &opts,
                       const void *buffer,
                       const int64_t nbytes,
                       const SCN &ref_scn,
                       LSN &lsn,
                       SCN &scn)
{
  int ret = OB_SUCCESS;
  CHECK_VALID;
  ret = wal_handle_impl_->submit_log(opts, static_cast<const char*>(buffer), nbytes, ref_scn, lsn, scn);
  return ret;
}

int Handle::append_imported_groups(const ImportedGroups &groups)
{
  int ret = OB_SUCCESS;
  CHECK_VALID;
  ret = wal_handle_impl_->submit_imported_groups(groups);
  return ret;
}

int Handle::seek(const LSN &lsn, EntryIterator &iter)
{
  CHECK_VALID;
  int ret = OB_SUCCESS;
  if (!lsn.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    WAL_LOG(ERROR, "invalid lsn to seek iterator", KR(ret), K(lsn));
  } else if (true == iter.is_inited()) {
    ret = iter.reuse(lsn);
  } else {
    ret = wal_handle_impl_->alloc_wal_buffer_iterator(lsn, iter);
  }
  return ret;
}

int Handle::seek(const SCN &scn, EntryIterator &iter)
{
  CHECK_VALID;
  return wal_handle_impl_->alloc_wal_buffer_iterator(scn, iter);
}

int Handle::seek(const LSN &lsn, GroupIterator &iter)
{
  CHECK_VALID;
  int ret = OB_SUCCESS;
  if (!lsn.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    WAL_LOG(ERROR, "invalid lsn to seek iterator", KR(ret), K(lsn));
  } else if (true == iter.is_inited()) {
    ret = iter.reuse(lsn);
  } else {
    ret = wal_handle_impl_->alloc_wal_group_buffer_iterator(lsn, iter);
  }
  return ret;
}

int Handle::seek(const SCN &scn, GroupIterator &iter)
{
  CHECK_VALID;
  return wal_handle_impl_->alloc_wal_group_buffer_iterator(scn, iter);
}

int Handle::locate_by_scn_coarsely(const SCN &scn, LSN &result_lsn)
{
  CHECK_VALID;
  return wal_handle_impl_->locate_by_scn_coarsely(scn, result_lsn);
}

int Handle::locate_by_lsn_coarsely(const LSN &lsn, SCN &result_scn)
{
  CHECK_VALID;
  return wal_handle_impl_->locate_by_lsn_coarsely(lsn, result_scn);
}

int Handle::advance_base_lsn(const LSN &lsn)
{
  CHECK_VALID;
  return wal_handle_impl_->set_base_lsn(lsn);
}

int Handle::get_begin_lsn(LSN &lsn) const
{
  CHECK_VALID;
  return wal_handle_impl_->get_begin_lsn(lsn);
}

int Handle::get_base_lsn(LSN &lsn) const
{
  CHECK_VALID;
  return wal_handle_impl_->get_base_lsn(lsn);
}

int Handle::get_base_info(const LSN &lsn,
                              BaseInfo &wal_base_info)
{
  CHECK_VALID;
  return wal_handle_impl_->get_base_info(lsn, wal_base_info);
}


int Handle::get_end_lsn(LSN &lsn) const
{
  int ret = OB_SUCCESS;
  CHECK_VALID;
  lsn = wal_handle_impl_->get_end_lsn();
  return ret;
}

int Handle::get_max_lsn(LSN &lsn) const
{
  int ret = OB_SUCCESS;
  CHECK_VALID;
  lsn = wal_handle_impl_->get_max_lsn();
  return ret;
}

int Handle::get_max_scn(SCN &scn) const
{
  int ret = OB_SUCCESS;
  CHECK_VALID;
  scn = wal_handle_impl_->get_max_scn();
  return ret;
}

int Handle::get_end_scn(SCN &scn) const
{
  int ret = OB_SUCCESS;
  CHECK_VALID;
  scn = wal_handle_impl_->get_end_scn();
  return ret;
}

int Handle::register_file_size_cb(FsyncCallback *fs_cb)
{
  int ret = OB_SUCCESS;
  CHECK_VALID;
  if (NULL == fs_cb) {
  } else if (NULL != fs_cb_) {
    ret = OB_NOT_SUPPORTED;
    WAL_LOG(WARN, "Handle has register_file_size_cb, not support regist repeatedly", K(ret), K(fs_cb_), K(fs_cb));
  } else {
    FsyncCallbackNode *fs_cb_node = SERVER_NEW(FsyncCallbackNode, "WalFsyncCb", fs_cb);
    if (NULL == fs_cb_node) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else if (OB_FAIL(wal_handle_impl_->register_file_size_cb(fs_cb_node))) {
    } else {
      fs_cb_ = fs_cb_node;
    }
    if (OB_FAIL(ret)) {
      SERVER_DELETE(FsyncCallbackNode, "WalFsyncCb", fs_cb_);
      fs_cb_ = NULL;
    }
  }
  return ret;
}

int Handle::unregister_file_size_cb()
{
  int ret = OB_SUCCESS;
  CHECK_VALID;
  if (NULL == fs_cb_) {
  } else if (OB_FAIL(wal_handle_impl_->unregister_file_size_cb(fs_cb_))) {
  } else {
      SERVER_DELETE(FsyncCallbackNode, "WalFsyncCb", fs_cb_);
    fs_cb_ = NULL;
  }
  return ret;
}

int Handle::stat(Stat &wal_stat) const
{
  CHECK_VALID;
  return wal_handle_impl_->stat(wal_stat);
}


int Handle::diagnose(DiagnoseInfo &diagnose_info) const
{
  CHECK_VALID;
  return wal_handle_impl_->diagnose(diagnose_info);
}

} // end namespace wal
} // end namespace oceanbase
