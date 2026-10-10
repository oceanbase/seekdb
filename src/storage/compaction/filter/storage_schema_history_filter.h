/* Copyright (c) 2025 OceanBase. Licensed under the Apache License, Version 2.0. */
#ifndef OCEANBASE_COMPACTION_STORAGE_SCHEMA_HISTORY_FILTER_H_
#define OCEANBASE_COMPACTION_STORAGE_SCHEMA_HISTORY_FILTER_H_

#include "storage/compaction/ob_i_compaction_filter.h"
#include "storage/instance_meta/storage_schema_history.h"
#include "lib/container/ob_array.h"

namespace oceanbase {
namespace compaction {

// Local physical reclamation, never a replicated KV DELETE. Only use on a
// minor containing the oldest schema SSTable and after local restore finishes.
// The temporary mark set combines opaque ownership roots at the protected
// cutoff with the local files/objects which can still load exact definitions.
class StorageSchemaHistoryFilter final : public ObICompactionFilter
{
public:
  int init(storage::InstanceMetaStore &store, const share::SCN &cutoff, int64_t deadline);
  int filter(const blocksstable::ObDatumRow &row, ObFilterRet &result) override;
  CompactionFilterType get_filter_type() const override { return STORAGE_SCHEMA_HISTORY; }
  int64_t layout_count() const { return references_.count(); }
private:
  using Reference = storage::StorageSchemaHistory::PhysicalReference;
  share::SCN cutoff_;
  common::ObArray<Reference> references_;
};

} // namespace compaction
} // namespace oceanbase
#endif
