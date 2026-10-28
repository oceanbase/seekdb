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

#ifndef OCEANBASE_STORAGE_RESTORE_HELPER_CTX_
#define OCEANBASE_STORAGE_RESTORE_HELPER_CTX_

#include "lib/container/ob_array.h"
#include "standby/restore/ob_standby_restore_rpc.h"
#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#ifndef CONST
#define CONST const
#define _OB_UNDEF_CONST
#endif
#ifndef OPTIONAL
#define OPTIONAL
#define _OB_UNDEF_OPTIONAL
#endif
#include <mswsock.h>
#ifdef _OB_UNDEF_CONST
#undef CONST
#undef _OB_UNDEF_CONST
#endif
#ifdef _OB_UNDEF_OPTIONAL
#undef OPTIONAL
#undef _OB_UNDEF_OPTIONAL
#endif
#undef ERROR
#undef DELETE
#endif
#include "standby/rpc_transport.h"
#include "storage/tablet/ob_tablet_common.h"
#include "ob_standby_restore_storage_struct.h"
#include "storage/ob_i_table.h"
#include "storage/blocksstable/ob_data_buffer.h"
#include "lib/oblog/ob_log_module.h"
#include <memory>

namespace oceanbase
{
namespace common
{
class ObIAllocator;
}

namespace restore
{

enum class ObRestoreHelperCtxType
{
  RESTORE_HELPER_CTX_NONE = 0,
  RESTORE_HELPER_CTX_LS_VIEW = 1,
  RESTORE_HELPER_CTX_TABLET_INFO = 2,
  RESTORE_HELPER_CTX_SSTABLE_INFO = 3,
  RESTORE_HELPER_CTX_SSTABLE_MACRO_RANGE =4,
  RESTORE_HELPER_CTX_MACRO_BLOCK =5,
  RESTORE_HELPER_CTX_MAX
};

struct ObIRestoreHelperCtx
{
public:
  ObIRestoreHelperCtx() = default;
  virtual ~ObIRestoreHelperCtx() = default;
  virtual ObRestoreHelperCtxType get_type() const = 0;
  virtual bool is_valid() const = 0;
  virtual void reset() = 0;
  virtual void destroy() = 0;
};

struct ObRestoreHelperLSViewCtx final : public ObIRestoreHelperCtx
{
public:
  ObRestoreHelperLSViewCtx();
  virtual ~ObRestoreHelperLSViewCtx();
  virtual ObRestoreHelperCtxType get_type() const override;
  virtual bool is_valid() const override;
  virtual void reset() override;
  virtual void destroy() override;
  bool ls_meta_fetched_;
  std::unique_ptr<rpc::Reader> ls_view_reader_;
};

struct ObRestoreHelperTabletInfoCtx final : public ObIRestoreHelperCtx
{
public:
  ObRestoreHelperTabletInfoCtx();
  virtual ~ObRestoreHelperTabletInfoCtx();
  virtual ObRestoreHelperCtxType get_type() const override;
  virtual bool is_valid() const override;
  virtual void reset() override;
  virtual void destroy() override;
  std::unique_ptr<rpc::Reader> tablet_info_reader_;
};

struct ObRestoreHelperSSTableInfoCtx final : public ObIRestoreHelperCtx
{
public:
  ObRestoreHelperSSTableInfoCtx();
  virtual ~ObRestoreHelperSSTableInfoCtx();
  virtual ObRestoreHelperCtxType get_type() const override;
  virtual bool is_valid() const override;
  virtual void reset() override;
  virtual void destroy() override;

  // Stream reader related members
  std::unique_ptr<rpc::Reader> sstable_info_reader_;
  int64_t pending_sstable_count_;
  common::ObTabletID cur_tablet_id_;
};

struct ObRestoreHelperSSTableMacroRangeCtx final : public ObIRestoreHelperCtx
{
public:
  ObRestoreHelperSSTableMacroRangeCtx();
  virtual ~ObRestoreHelperSSTableMacroRangeCtx();
  virtual ObRestoreHelperCtxType get_type() const override;
  virtual bool is_valid() const override;
  virtual void reset() override;
  virtual void destroy() override;

  std::unique_ptr<rpc::Reader> macro_info_reader_;
};

struct ObRestoreHelperMacroBlockCtx final : public ObIRestoreHelperCtx
{
public:
  ObRestoreHelperMacroBlockCtx();
  virtual ~ObRestoreHelperMacroBlockCtx();
  virtual ObRestoreHelperCtxType get_type() const override;
  virtual bool is_valid() const override;
  virtual void reset() override;
  virtual void destroy() override;

  std::unique_ptr<rpc::Reader> macro_block_reader_;
  int64_t data_version_;
  storage::ObITable::TableKey copy_table_key_;
  blocksstable::ObSelfBufferWriter data_buffer_;
};

class ObRestoreHelperCtxUtil
{
public:
  static int close_reader(std::unique_ptr<rpc::Reader> &reader)
  {
    int ret = OB_SUCCESS;
    if (!reader) {
      ret = OB_INVALID_ARGUMENT;
      STORAGE_LOG(WARN, "reader is nullptr", KR(ret));
    } else {
      ret = reader->finish();
      reader.reset();
      if (OB_FAIL(ret)) {
        STORAGE_LOG(ERROR, "RPC stream finished with error", KR(ret));
      }
    }
    return ret;
  }

  static int create_ctx(
      const ObRestoreHelperCtxType ctx_type,
      common::ObIAllocator &allocator,
      ObIRestoreHelperCtx *&ctx);
};

} // namespace restore
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_RESTORE_HELPER_CTX_
