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

#include "standby/restore/ob_restore_helper_ctx.h"
#include "lib/allocator/ob_allocator.h"
#include "lib/ob_define.h"
#include "lib/utility/ob_macro_utils.h"
#include "lib/utility/ob_print_utils.h"
#include "lib/oblog/ob_log_module.h"

namespace oceanbase
{
namespace restore
{
ObRestoreHelperLSViewCtx::ObRestoreHelperLSViewCtx()
  : ls_meta_fetched_(false),
    ls_view_reader_()
{
}

ObRestoreHelperLSViewCtx::~ObRestoreHelperLSViewCtx()
{
  reset();
}

ObRestoreHelperCtxType ObRestoreHelperLSViewCtx::get_type() const
{
  return ObRestoreHelperCtxType::RESTORE_HELPER_CTX_LS_VIEW;
}

bool ObRestoreHelperLSViewCtx::is_valid() const
{
  return ObRestoreHelperCtxType::RESTORE_HELPER_CTX_LS_VIEW == get_type();
}

void ObRestoreHelperLSViewCtx::reset()
{
  ls_meta_fetched_ = false;
  int ret = OB_SUCCESS;
  if (ls_view_reader_) {
    ls_view_reader_->cancel();
    if (OB_FAIL(ObRestoreHelperCtxUtil::close_reader(ls_view_reader_))) {
      LOG_ERROR("stream finished with error", K(ret));
    }
  }
}

void ObRestoreHelperLSViewCtx::destroy()
{
  this->~ObRestoreHelperLSViewCtx();
}

ObRestoreHelperTabletInfoCtx::ObRestoreHelperTabletInfoCtx()
  : tablet_info_reader_()
{
}

ObRestoreHelperTabletInfoCtx::~ObRestoreHelperTabletInfoCtx()
{
  reset();
}

ObRestoreHelperCtxType ObRestoreHelperTabletInfoCtx::get_type() const
{
  return ObRestoreHelperCtxType::RESTORE_HELPER_CTX_TABLET_INFO;
}

bool ObRestoreHelperTabletInfoCtx::is_valid() const
{
  return tablet_info_reader_ != nullptr;
}

void ObRestoreHelperTabletInfoCtx::reset()
{
  int ret = OB_SUCCESS;
  if (tablet_info_reader_) {
    tablet_info_reader_->cancel();
    if (OB_FAIL(ObRestoreHelperCtxUtil::close_reader(tablet_info_reader_))) {
      LOG_ERROR("stream finished with error", K(ret));
    }
  }
}

void ObRestoreHelperTabletInfoCtx::destroy()
{
  this->~ObRestoreHelperTabletInfoCtx();
}

ObRestoreHelperSSTableInfoCtx::ObRestoreHelperSSTableInfoCtx()
  : sstable_info_reader_(),
    pending_sstable_count_(0),
    cur_tablet_id_()
{
}

ObRestoreHelperSSTableInfoCtx::~ObRestoreHelperSSTableInfoCtx()
{
  reset();
}

ObRestoreHelperCtxType ObRestoreHelperSSTableInfoCtx::get_type() const
{
  return ObRestoreHelperCtxType::RESTORE_HELPER_CTX_SSTABLE_INFO;
}

bool ObRestoreHelperSSTableInfoCtx::is_valid() const
{
  return ObRestoreHelperCtxType::RESTORE_HELPER_CTX_SSTABLE_INFO == get_type()
         && sstable_info_reader_;
}

void ObRestoreHelperSSTableInfoCtx::reset()
{
  pending_sstable_count_ = 0;
  cur_tablet_id_.reset();
  int ret = OB_SUCCESS;
  if (sstable_info_reader_) {
    sstable_info_reader_->cancel();
    if (OB_FAIL(ObRestoreHelperCtxUtil::close_reader(sstable_info_reader_))) {
      LOG_ERROR("stream finished with error", K(ret));
    }
  }
}

void ObRestoreHelperSSTableInfoCtx::destroy()
{
  this->~ObRestoreHelperSSTableInfoCtx();
}

ObRestoreHelperSSTableMacroRangeCtx::ObRestoreHelperSSTableMacroRangeCtx()
  : macro_info_reader_()
{
}

ObRestoreHelperSSTableMacroRangeCtx::~ObRestoreHelperSSTableMacroRangeCtx()
{
  reset();
}

ObRestoreHelperCtxType ObRestoreHelperSSTableMacroRangeCtx::get_type() const
{
  return ObRestoreHelperCtxType::RESTORE_HELPER_CTX_SSTABLE_MACRO_RANGE;
}

bool ObRestoreHelperSSTableMacroRangeCtx::is_valid() const
{
  return macro_info_reader_ != nullptr;
}

void ObRestoreHelperSSTableMacroRangeCtx::reset()
{
  int ret = OB_SUCCESS;
  if (macro_info_reader_) {
    macro_info_reader_->cancel();
    if (OB_FAIL(ObRestoreHelperCtxUtil::close_reader(macro_info_reader_))) {
      LOG_ERROR("stream finished with error", K(ret));
    }
  }
}

void ObRestoreHelperSSTableMacroRangeCtx::destroy()
{
  this->~ObRestoreHelperSSTableMacroRangeCtx();
}

ObRestoreHelperMacroBlockCtx::ObRestoreHelperMacroBlockCtx()
  : ObIRestoreHelperCtx(),
    macro_block_reader_(),
    data_version_(0),
    copy_table_key_(),
    data_buffer_("MacroBlockCtx")
{
}

ObRestoreHelperMacroBlockCtx::~ObRestoreHelperMacroBlockCtx()
{
  reset();
}

ObRestoreHelperCtxType ObRestoreHelperMacroBlockCtx::get_type() const
{
  return ObRestoreHelperCtxType::RESTORE_HELPER_CTX_MACRO_BLOCK;
}

bool ObRestoreHelperMacroBlockCtx::is_valid() const
{
  return macro_block_reader_ != nullptr;
}

void ObRestoreHelperMacroBlockCtx::reset()
{
  int ret = OB_SUCCESS;
  if (macro_block_reader_) {
    macro_block_reader_->cancel();
    if (OB_FAIL(ObRestoreHelperCtxUtil::close_reader(macro_block_reader_))) {
      LOG_ERROR("stream finished with error", K(ret));
    }
  }
  data_buffer_.reset();
}

void ObRestoreHelperMacroBlockCtx::destroy()
{
  this->~ObRestoreHelperMacroBlockCtx();
}

int ObRestoreHelperCtxUtil::create_ctx(
    const ObRestoreHelperCtxType ctx_type,
    common::ObIAllocator &allocator,
    ObIRestoreHelperCtx *&ctx)
{
  int ret = OB_SUCCESS;
  void *buf = nullptr;
  ctx = nullptr;
  if (OB_UNLIKELY(ctx_type <= ObRestoreHelperCtxType::RESTORE_HELPER_CTX_NONE
                      || ctx_type >= ObRestoreHelperCtxType::RESTORE_HELPER_CTX_MAX)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid ctx type", K(ret), "ctx_type", static_cast<int64_t>(ctx_type));
  } else {
    switch (ctx_type) {
      case ObRestoreHelperCtxType::RESTORE_HELPER_CTX_LS_VIEW: {
        if (OB_ISNULL(buf = allocator.alloc(sizeof(ObRestoreHelperLSViewCtx)))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("failed to alloc ls view ctx", K(ret), "ctx_type", static_cast<int64_t>(ctx_type));
        } else {
          ctx = new (buf) ObRestoreHelperLSViewCtx();
        }
        break;
      }
      case ObRestoreHelperCtxType::RESTORE_HELPER_CTX_TABLET_INFO: {
        if (OB_ISNULL(buf = allocator.alloc(sizeof(ObRestoreHelperTabletInfoCtx)))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("failed to alloc tablet info ctx", K(ret), "ctx_type", static_cast<int64_t>(ctx_type));
        } else {
          ctx = new (buf) ObRestoreHelperTabletInfoCtx();
        }
        break;
      }
      case ObRestoreHelperCtxType::RESTORE_HELPER_CTX_SSTABLE_INFO: {
        if (OB_ISNULL(buf = allocator.alloc(sizeof(ObRestoreHelperSSTableInfoCtx)))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("failed to alloc sstable info ctx", K(ret), "ctx_type", static_cast<int64_t>(ctx_type));
        } else {
          ctx = new (buf) ObRestoreHelperSSTableInfoCtx();
        }
        break;
      }
      case ObRestoreHelperCtxType::RESTORE_HELPER_CTX_SSTABLE_MACRO_RANGE: {
        if (OB_ISNULL(buf = allocator.alloc(sizeof(ObRestoreHelperSSTableMacroRangeCtx)))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("failed to alloc sstable macro range ctx", K(ret), "ctx_type", static_cast<int64_t>(ctx_type));
        } else {
          ctx = new (buf) ObRestoreHelperSSTableMacroRangeCtx();
        }
        break;
      }
      case ObRestoreHelperCtxType::RESTORE_HELPER_CTX_MACRO_BLOCK: {
        if (OB_ISNULL(buf = allocator.alloc(sizeof(ObRestoreHelperMacroBlockCtx)))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("failed to alloc macro block ctx", K(ret), "ctx_type", static_cast<int64_t>(ctx_type));
        } else {
          ctx = new (buf) ObRestoreHelperMacroBlockCtx();
        }
        break;
      }
      default: {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid ctx type", K(ret), "ctx_type", static_cast<int64_t>(ctx_type));
        break;
      }
    }
  }
  return ret;
}

} // namespace restore
} // namespace oceanbase
