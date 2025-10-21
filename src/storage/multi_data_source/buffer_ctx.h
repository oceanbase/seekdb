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
#ifndef SHARE_STORAGE_MULTI_DATA_SOURCE_BUFFER_CTX_H
#define SHARE_STORAGE_MULTI_DATA_SOURCE_BUFFER_CTX_H

#include "lib/ob_define.h"
#include "lib/oblog/ob_log_module.h"
#include "runtime_utility/common_define.h"
#include "mds_writer.h"
#include "runtime_utility/mds_tenant_service.h"

namespace oceanbase
{
namespace share
{
class SCN;
}

namespace storage
{
namespace mds
{

class BufferCtx
{
public:
  BufferCtx() : binding_type_id_(INVALID_VALUE),is_incomplete_replay_(false) {}
  virtual ~BufferCtx() {}
  void set_binding_type_id(const int64_t type_id) { binding_type_id_ = type_id; }
  int64_t get_binding_type_id() const { return binding_type_id_; }
  void set_incomplete_replay(const bool incomplete_replay) { is_incomplete_replay_ = incomplete_replay; }
  bool is_incomplete_replay() const { return is_incomplete_replay_; }
  // Allows the user to override the method
  virtual const MdsWriter get_writer() const = 0;
  virtual void on_redo(const share::SCN &redo_scn) {}
  virtual void before_prepare() {}
  virtual void on_prepare(const share::SCN &prepare_version) {}
  virtual void on_commit(const share::SCN &commit_version, const share::SCN &commit_scn) {}
  virtual void on_abort(const share::SCN &abort_scn) {}
  // When restoring the transaction context, a deep copy of BufferCtx needs to be made from the transaction state table to the transaction context
  virtual int deep_copy(BufferCtx &) const { return common::OB_SUCCESS; }
  virtual int64_t get_impl_binging_type_id() { return 0; }// Need to know the type of subclass object during deserialization and deep copy at the transaction layer
  virtual int64_t to_string(char*, const int64_t buf_len) const = 0;
  // Persist and restore along with transaction status
  virtual int serialize(char*, const int64_t, int64_t&) const = 0;
  virtual int deserialize(const char*, const int64_t, int64_t&) = 0;
  virtual int64_t get_serialize_size(void) const = 0;
private:
  int64_t binding_type_id_;
  bool is_incomplete_replay_;
};
// This structure is embedded in the transaction context, corresponding one-to-one with BufferNode of multiple data sources, and is persisted and restored along with the transaction status
// Multi-datasource framework is responsible for maintaining the state within this structure
// The transaction layer needs to call the corresponding interface of this structure at the specified event node
class BufferCtxNode
{
public:
  BufferCtxNode() : ctx_(nullptr) {}
  void set_ctx(BufferCtx *ctx) { MDS_ASSERT(ctx_ == nullptr); ctx_ = ctx; }// It is not expected that an overwrite will occur
  const BufferCtx *get_ctx() const { return ctx_; }
  void destroy_ctx();
  void on_redo(const share::SCN &redo_scn) { ctx_->on_redo(redo_scn); }
  void before_prepare() { ctx_->before_prepare(); }
  void on_prepare(const share::SCN &prepare_version) { ctx_->on_prepare(prepare_version); }
  void on_commit(const share::SCN &commit_version, const share::SCN &commit_scn) {
    ctx_->on_commit(commit_version, commit_scn);
  }
  void on_abort(const share::SCN &abort_scn) { ctx_->on_abort(abort_scn); }
  // Persist and restore along with transaction status
  int serialize(char*, const int64_t, int64_t&) const;// To encode the actual ctx type into binary
  int deserialize(const char*,
                  const int64_t,
                  int64_t&,
                  ObIAllocator &allocator = MTL(ObTenantMdsService*)->get_buffer_ctx_allocator());// To be determined based on the actual ctx type at compile time through subclass reflection
  int64_t get_serialize_size(void) const;
  TO_STRING_KV(KP(this), KP_(ctx), KPC_(ctx));
private:
  BufferCtx *ctx_;
};

}
}
}
#endif
