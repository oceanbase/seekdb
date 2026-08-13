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

#ifndef OCEANBASE_DATA_PLANE_ACCESS_OB_DML_EXECUTION_H_
#define OCEANBASE_DATA_PLANE_ACCESS_OB_DML_EXECUTION_H_

#include <stdint.h>

namespace oceanbase
{
namespace common
{
class ObTimeZoneInfo;
}
namespace data_plane
{

// Query-owned values needed to prepare one table write.  Storage-specific
// context, schema and allocator objects are deliberately not represented here.
struct ObDmlWriteSpec
{
  ObDmlWriteSpec()
    : timeout_(-1),
      schema_version_(-1),
      sql_mode_(0),
      tz_info_(nullptr),
      branch_id_(0),
      is_total_quantity_log_(false),
      prelock_(false),
      is_batch_stmt_(false),
      is_main_table_in_fts_ddl_(false),
      check_schema_version_(true),
      access_vector_id_as_master_table_(false)
  {}

  int64_t timeout_;
  int64_t schema_version_;
  uint64_t sql_mode_;
  const common::ObTimeZoneInfo *tz_info_;
  int16_t branch_id_;
  bool is_total_quantity_log_;
  bool prelock_;
  bool is_batch_stmt_;
  bool is_main_table_in_fts_ddl_;
  bool check_schema_version_;
  bool access_vector_id_as_master_table_;
};

// Narrow protocol implemented by the data plane.  destroy() is separate from
// the virtual destructor because implementations are allocator-owned.
class ObIDmlExecutionState
{
public:
  virtual ~ObIDmlExecutionState() {}
  virtual void destroy() = 0;
  virtual int64_t timeout() const = 0;
  virtual void set_skip_flush_redo(bool skip) = 0;
};

// Move-only lifetime handle for a prepared write.  There is intentionally no
// implementation/native-handle escape hatch: SQL can only use the behaviours
// declared by ObIDmlExecutionState.
class ObDmlExecution
{
public:

  ObDmlExecution() : state_(nullptr) {}

  ~ObDmlExecution() { reset(); }

  ObDmlExecution(ObDmlExecution &&other) : state_(other.state_)
  {
    other.state_ = nullptr;
  }

  ObDmlExecution &operator=(ObDmlExecution &&other)
  {
    if (this != &other) {
      reset();
      state_ = other.state_;
      other.state_ = nullptr;
    }
    return *this;
  }

  void reset()
  {
    if (nullptr != state_) {
      state_->destroy();
    }
    state_ = nullptr;
  }

  bool is_valid() const { return nullptr != state_; }
  int64_t timeout() const { return nullptr == state_ ? -1 : state_->timeout(); }
  void set_skip_flush_redo(const bool skip)
  {
    if (nullptr != state_) {
      state_->set_skip_flush_redo(skip);
    }
  }

private:
  ObDmlExecution(const ObDmlExecution &) = delete;
  ObDmlExecution &operator=(const ObDmlExecution &) = delete;

  void bind(ObIDmlExecutionState *state)
  {
    reset();
    state_ = state;
  }

  ObIDmlExecutionState *state() const { return state_; }
  friend class ObIDmlService;

private:
  ObIDmlExecutionState *state_;
};

} // namespace data_plane
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_ACCESS_OB_DML_EXECUTION_H_
