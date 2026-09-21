// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
#include "observer/virtual_table/plugin_memory_table.h"
#include "sql/session/ob_sql_session_info.h"
#include "lib/time/ob_time_utility.h"
#include <new>
#if defined(SEEKDB_WITH_EXPERIMENTAL_PLUGINS)
#include "observer/ob_server_plugin_runtime.h"
#include "share/ob_server_struct.h"
#endif

namespace oceanbase { namespace observer {
using namespace common;
using share::plugin::ObPluginState;

namespace {
const char *plugin_memory_state_name(ObPluginState state)
{
  switch (state) {
    case ObPluginState::DISCOVERED: return "DISCOVERED";
    case ObPluginState::VALIDATED: return "VALIDATED";
    case ObPluginState::LOADED: return "LOADED";
    case ObPluginState::INITIALIZING: return "INITIALIZING";
    case ObPluginState::ACTIVE: return "ACTIVE";
    case ObPluginState::QUIESCING: return "QUIESCING";
    case ObPluginState::STOPPED: return "STOPPED";
    case ObPluginState::FAILED: return "FAILED";
    case ObPluginState::BLOCKED: return "BLOCKED";
  }
  return "UNKNOWN";
}
}

void PluginMemoryTable::clear_snapshot() noexcept
{
  // Release retained strings/capacity, not just the logical row count.
  std::vector<share::plugin::ObPluginStatusSnapshot>().swap(snapshot_);
  position_ = 0;
  sample_time_ = 0;
  started_ = false;
  snapshot_error_ = OB_SUCCESS;
}

void PluginMemoryTable::reset()
{
  clear_snapshot();
  ObVirtualTableIterator::reset();
}

int PluginMemoryTable::inner_open()
{
  clear_snapshot();
  return OB_SUCCESS;
}

int PluginMemoryTable::inner_close()
{
  clear_snapshot();
  return OB_SUCCESS;
}

int PluginMemoryTable::read_snapshot(std::vector<share::plugin::ObPluginStatusSnapshot> &statuses)
{
  statuses.clear();
#if defined(SEEKDB_WITH_EXPERIMENTAL_PLUGINS)
  // Normal SQL request admission/drain keeps GCTX and its runtime alive here.
  // After this call, the scan owns only copied values, including strings.
  auto *runtime = GCTX.plugin_runtime_;
  return nullptr == runtime ? OB_NOT_INIT : runtime->list_plugin_status(statuses);
#else
  return OB_SUCCESS; // Table exists but there are no loaded plugin generations.
#endif
}

int PluginMemoryTable::inner_get_next_row(ObNewRow *&row)
{
  row = nullptr;
  if (nullptr == session_ || nullptr == allocator_ || nullptr == cur_row_.cells_
      || cur_row_.count_ < output_column_ids_.count()) return OB_NOT_INIT;
  // Process-wide usage is not a tenant-local catalog row. Require PROCESS in
  // addition to the normal SQL object privileges, before collecting any data.
  if (!session_->has_user_process_privilege()) return OB_ERR_NO_PRIVILEGE;
  if (!started_) {
    started_ = true;
    try {
      snapshot_error_ = read_snapshot(snapshot_);
    } catch (const std::bad_alloc &) {
      snapshot_error_ = OB_ALLOCATE_MEMORY_FAILED;
    } catch (...) {
      snapshot_error_ = OB_ERR_UNEXPECTED;
    }
    if (snapshot_error_ != OB_SUCCESS) snapshot_.clear();
    else sample_time_ = ObTimeUtility::current_time();
  }
  if (snapshot_error_ != OB_SUCCESS) return snapshot_error_;
  if (position_ >= snapshot_.size()) return OB_ITER_END;
  const auto &status = snapshot_[position_];
  const auto &memory = status.host_memory_;
  if (status.plugin_id_.size() > 256 || status.runtime_incarnation_.size() > 256) return OB_SIZE_OVERFLOW;
  for (int64_t i = 0; i < output_column_ids_.count(); ++i) {
    auto &cell = cur_row_.cells_[i];
    switch (output_column_ids_.at(i)) {
      case PLUGIN_ID: cell.set_varchar(ObString(static_cast<int32_t>(status.plugin_id_.size()), status.plugin_id_.data())); break;
      case GENERATION: cell.set_uint64(status.generation_); break;
      case RUNTIME_INCARNATION: cell.set_varchar(ObString(static_cast<int32_t>(status.runtime_incarnation_.size()), status.runtime_incarnation_.data())); break;
      case RUNTIME_STATE: cell.set_varchar(plugin_memory_state_name(status.state_)); break;
      case LEASE_COUNT: cell.set_int(status.lease_count_); break;
      case USED_BYTES: cell.set_uint64(memory.bytes_); break;
      case PEAK_BYTES: cell.set_uint64(memory.peak_bytes_); break;
      case LIVE_ALLOCATIONS: cell.set_uint64(memory.allocations_); break;
      case PEAK_ALLOCATIONS: cell.set_uint64(memory.peak_allocations_); break;
      case ALLOCATION_FAILURES: cell.set_uint64(memory.allocation_failures_); break;
      case INVALID_FREES: cell.set_uint64(memory.invalid_frees_); break;
      case BYTE_LIMIT: cell.set_uint64(memory.byte_limit_); break;
      case ALLOCATION_LIMIT: cell.set_uint64(memory.allocation_limit_); break;
      case SAMPLE_TIME: cell.set_timestamp(sample_time_); break;
      default: return OB_ERR_UNEXPECTED;
    }
    if (cell.is_varchar()) {
      cell.set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
    }
  }
  ++position_;
  row = &cur_row_;
  return OB_SUCCESS;
}

} }
