// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
#ifndef SEEKDB_OBSERVER_PLUGIN_MEMORY_TABLE_H_
#define SEEKDB_OBSERVER_PLUGIN_MEMORY_TABLE_H_

#include "observer/virtual_table/ob_virtual_table_iterator.h"
#include "share/plugin/ob_plugin_loader.h"

namespace oceanbase { namespace observer {

// One owned, read-only snapshot per scan. No polling thread, catalog writes,
// plugin callbacks, module leases or runtime pointers retained between rows.
class PluginMemoryTable : public common::ObVirtualTableIterator
{
public:
  PluginMemoryTable() = default;
  ~PluginMemoryTable() override = default;
  void reset() override;
  int inner_open() override;
  int inner_close() override;
  int inner_get_next_row(common::ObNewRow *&row) override;

protected:
  virtual int read_snapshot(std::vector<share::plugin::ObPluginStatusSnapshot> &statuses);

private:
  void clear_snapshot() noexcept;
  enum Column {
    PLUGIN_ID = common::OB_APP_MIN_COLUMN_ID,
    GENERATION, RUNTIME_INCARNATION, RUNTIME_STATE, LEASE_COUNT,
    USED_BYTES, PEAK_BYTES, LIVE_ALLOCATIONS, PEAK_ALLOCATIONS,
    ALLOCATION_FAILURES, INVALID_FREES, BYTE_LIMIT, ALLOCATION_LIMIT, SAMPLE_TIME
  };
  std::vector<share::plugin::ObPluginStatusSnapshot> snapshot_;
  size_t position_ = 0;
  int64_t sample_time_ = 0;
  bool started_ = false;
  int snapshot_error_ = common::OB_SUCCESS;
  DISALLOW_COPY_AND_ASSIGN(PluginMemoryTable);
};

} }
#endif
