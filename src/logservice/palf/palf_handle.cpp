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

#include "palf_handle.h"

namespace oceanbase
{
using namespace share;
namespace palf
{
#define CHECK_VALID if (NULL == palf_handle_impl_) { return OB_NOT_INIT; }

PalfHandle::PalfHandle() : palf_handle_impl_(NULL),
                           fs_cb_(NULL)
{
}

PalfHandle::PalfHandle(const PalfHandle& rhs)
{
  *this = rhs;
}

PalfHandle::~PalfHandle()
{
  palf_handle_impl_ = NULL;
  fs_cb_ = NULL;
}

bool PalfHandle::is_valid() const
{
  return NULL != palf_handle_impl_;
}

PalfHandle& PalfHandle::operator=(const PalfHandle &rhs)
{
  if (this == &rhs) {
    return *this;
  }
  palf_handle_impl_ = rhs.palf_handle_impl_;
  fs_cb_ = rhs.fs_cb_;
  return *this;
}

bool  PalfHandle::operator==(const PalfHandle &rhs) const
{
  return palf_handle_impl_ == rhs.palf_handle_impl_;
}

int PalfHandle::bootstrap()
{
  CHECK_VALID;
  return palf_handle_impl_->bootstrap();
}

int PalfHandle::append(const PalfAppendOptions &opts,
                       const void *buffer,
                       const int64_t nbytes,
                       const SCN &ref_scn,
                       LSN &lsn,
                       SCN &scn)
{
  int ret = OB_SUCCESS;
  CHECK_VALID;
  ret = palf_handle_impl_->submit_log(opts, static_cast<const char*>(buffer), nbytes, ref_scn, lsn, scn);
  return ret;
}

int PalfHandle::seek(const LSN &lsn, PalfBufferIterator &iter)
{
  CHECK_VALID;
  int ret = OB_SUCCESS;
  if (!lsn.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(ERROR, "invalid lsn to seek iterator", KR(ret), K(lsn));
  } else if (true == iter.is_inited()) {
    ret = iter.reuse(lsn);
  } else {
    ret = palf_handle_impl_->alloc_palf_buffer_iterator(lsn, iter);
  }
  return ret;
}

int PalfHandle::seek(const SCN &scn, PalfBufferIterator &iter)
{
  CHECK_VALID;
  return palf_handle_impl_->alloc_palf_buffer_iterator(scn, iter);
}

int PalfHandle::seek(const LSN &lsn, PalfGroupBufferIterator &iter)
{
  CHECK_VALID;
  int ret = OB_SUCCESS;
  if (!lsn.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(ERROR, "invalid lsn to seek iterator", KR(ret), K(lsn));
  } else if (true == iter.is_inited()) {
    ret = iter.reuse(lsn);
  } else {
    ret = palf_handle_impl_->alloc_palf_group_buffer_iterator(lsn, iter);
  }
  return ret;
}

int PalfHandle::seek(const SCN &scn, PalfGroupBufferIterator &iter)
{
  CHECK_VALID;
  return palf_handle_impl_->alloc_palf_group_buffer_iterator(scn, iter);
}

int PalfHandle::locate_by_scn_coarsely(const SCN &scn, LSN &result_lsn)
{
  CHECK_VALID;
  return palf_handle_impl_->locate_by_scn_coarsely(scn, result_lsn);
}

int PalfHandle::locate_by_lsn_coarsely(const LSN &lsn, SCN &result_scn)
{
  CHECK_VALID;
  return palf_handle_impl_->locate_by_lsn_coarsely(lsn, result_scn);
}

int PalfHandle::advance_base_lsn(const LSN &lsn)
{
  CHECK_VALID;
  return palf_handle_impl_->set_base_lsn(lsn);
}

int PalfHandle::get_begin_lsn(LSN &lsn) const
{
  CHECK_VALID;
  return palf_handle_impl_->get_begin_lsn(lsn);
}

int PalfHandle::get_begin_scn(SCN &scn) const
{
  CHECK_VALID;
  return palf_handle_impl_->get_begin_scn(scn);
}

int PalfHandle::get_base_lsn(LSN &lsn) const
{
  CHECK_VALID;
  return palf_handle_impl_->get_base_lsn(lsn);
}

int PalfHandle::get_base_info(const LSN &lsn,
                              PalfBaseInfo &palf_base_info)
{
  CHECK_VALID;
  return palf_handle_impl_->get_base_info(lsn, palf_base_info);
}


int PalfHandle::get_end_lsn(LSN &lsn) const
{
  int ret = OB_SUCCESS;
  CHECK_VALID;
  lsn = palf_handle_impl_->get_end_lsn();
  return ret;
}

int PalfHandle::get_max_lsn(LSN &lsn) const
{
  int ret = OB_SUCCESS;
  CHECK_VALID;
  lsn = palf_handle_impl_->get_max_lsn();
  return ret;
}

int PalfHandle::get_max_scn(SCN &scn) const
{
  int ret = OB_SUCCESS;
  CHECK_VALID;
  scn = palf_handle_impl_->get_max_scn();
  return ret;
}

int PalfHandle::get_palf_epoch(int64_t &palf_epoch) const
{
  CHECK_VALID;
  return palf_handle_impl_->get_palf_epoch(palf_epoch);
}

int PalfHandle::get_end_scn(SCN &scn) const
{
  int ret = OB_SUCCESS;
  CHECK_VALID;
  scn = palf_handle_impl_->get_end_scn();
  return ret;
}

int PalfHandle::get_access_mode_ref_scn(AccessMode &access_mode,
                                        SCN &ref_scn) const
{
  int ret = OB_SUCCESS;
  CHECK_VALID;
  ret = palf_handle_impl_->get_access_mode_ref_scn(access_mode, ref_scn);
  return ret;
}

int PalfHandle::register_file_size_cb(PalfFSCb *fs_cb)
{
  int ret = OB_SUCCESS;
  CHECK_VALID;
  if (NULL == fs_cb) {
    PALF_LOG(TRACE, "no need register_file_size_cb", K(ret));
  } else if (NULL != fs_cb_) {
    ret = OB_NOT_SUPPORTED;
    PALF_LOG(WARN, "PalfHandle has register_file_size_cb, not support regist repeatedly", K(ret), K(fs_cb_), K(fs_cb));
  } else {
    PalfFSCbNode *fs_cb_node = MTL_NEW(PalfFSCbNode, "PalfFSCbNode", fs_cb);
    if (NULL == fs_cb_node) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else if (OB_FAIL(palf_handle_impl_->register_file_size_cb(fs_cb_node))) {
      PALF_LOG(WARN, "register_file_size_cb failed", K(ret));
    } else {
      fs_cb_ = fs_cb_node;
    }
    if (OB_FAIL(ret)) {
      MTL_DELETE(PalfFSCbNode, "PalfFSCbNode", fs_cb_);
      fs_cb_ = NULL;
    }
  }
  return ret;
}

int PalfHandle::unregister_file_size_cb()
{
  int ret = OB_SUCCESS;
  CHECK_VALID;
  if (NULL == fs_cb_) {
    PALF_LOG(TRACE, "no need unregister_file_size_cb", K(fs_cb_));
  } else if (OB_FAIL(palf_handle_impl_->unregister_file_size_cb(fs_cb_))) {
    PALF_LOG(WARN, "unregister_file_size_cb failed", K(ret));
  } else {
    MTL_DELETE(PalfFSCbNode, "PalfFSCbNode", fs_cb_);
    fs_cb_ = NULL;
  }
  return ret;
}

int PalfHandle::stat(PalfStat &palf_stat) const
{
  CHECK_VALID;
  return palf_handle_impl_->stat(palf_stat);
}


int PalfHandle::diagnose(PalfDiagnoseInfo &diagnose_info) const
{
  CHECK_VALID;
  return palf_handle_impl_->diagnose(diagnose_info);
}

int PalfHandle::raw_read(const palf::LSN &lsn,
                         void *buffer,
                         const int64_t nbytes,
                         int64_t &read_size,
                         LogIOContext &io_ctx)
{
  CHECK_VALID;
  return palf_handle_impl_->raw_read(lsn, reinterpret_cast<char*>(buffer), nbytes, read_size, io_ctx);
}

int PalfHandle::get_readable_end_lsn(LSN &lsn) const
{
  int ret = OB_SUCCESS;
  CHECK_VALID;
  lsn = palf_handle_impl_->get_readable_end_lsn();
  return ret;
}

} // end namespace palf
} // end namespace oceanbase
