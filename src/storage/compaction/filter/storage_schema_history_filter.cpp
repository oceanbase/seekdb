/* Copyright (c) 2025 OceanBase. Licensed under the Apache License, Version 2.0. */
#define USING_LOG_PREFIX STORAGE_COMPACTION
#include "storage/compaction/filter/storage_schema_history_filter.h"
#include "storage/blocksstable/ob_datum_row.h"
#include <algorithm>

namespace oceanbase {
namespace compaction {
using namespace common;
using namespace storage;

int StorageSchemaHistoryFilter::init(InstanceMetaStore &store, const share::SCN &cutoff,
    int64_t deadline)
{
  if (cutoff_.is_valid()) { return OB_INIT_TWICE; }
  if (!cutoff.is_valid() || cutoff.is_min() || cutoff.is_max() || cutoff.is_base_scn()) {
    return OB_INVALID_ARGUMENT;
  }
  InstanceMetaStore::Transaction tx;
  ObArray<Reference> roots;
  int ret = StorageSchemaHistory::begin_read_at(store, tx, cutoff, deadline);
  if (ret == OB_SUCCESS) {
    // Keys are opaque ownership tokens supplied by the upper layer. Storage
    // interprets only the referenced G; it does not decode Namespace/table IDs.
    ret = store.scan(tx, MetaCollection::TABLE_STORAGE_LAYOUTS, {},
        [&](const ObString &, const ObString &value, bool &) {
      int64_t id = 0, pos = 0;
      int rc = value.length() == sizeof(id) ? OB_SUCCESS : OB_CHECKSUM_ERROR;
      if (rc == OB_SUCCESS) { rc = serialization::decode_i64(value.ptr(), value.length(), pos, &id); }
      if (rc == OB_SUCCESS && id <= 0) { rc = OB_CHECKSUM_ERROR; }
      if (rc == OB_SUCCESS) {
        Reference root;
        root.layout_id = id;
        rc = roots.push_back(root);
      }
      return rc;
    });
  }
  StorageSchemaHistory history(store, tx);
  for (int64_t i = 0; ret == OB_SUCCESS && i < roots.count(); ++i) {
    ret = history.read_version(roots.at(i).layout_id, roots.at(i).minimum_version);
  }
  if (ret == OB_SUCCESS) { ret = StorageSchemaHistory::collect_physical_references(references_, deadline); }
  for (int64_t i = 0; ret == OB_SUCCESS && i < roots.count(); ++i) {
    ret = references_.push_back(roots.at(i));
  }
  if (tx.is_active()) {
    const int end = store.commit(tx);
    if (ret == OB_SUCCESS) { ret = end; }
  }
  if (ret == OB_SUCCESS) {
    std::sort(references_.begin(), references_.end(), [](const Reference &a, const Reference &b) {
      return a.layout_id < b.layout_id
          || (a.layout_id == b.layout_id && a.minimum_version < b.minimum_version);
    });
    int64_t kept = 0;
    for (int64_t i = 0; i < references_.count(); ++i) {
      if (kept == 0 || references_.at(i).layout_id != references_.at(kept - 1).layout_id) {
        references_.at(kept++) = references_.at(i);
      }
    }
    while (references_.count() > kept) { references_.pop_back(); }
    cutoff_ = cutoff;
  } else {
    references_.reuse();
  }
  return ret;
}

int StorageSchemaHistoryFilter::filter(const blocksstable::ObDatumRow &row, ObFilterRet &result)
{
  result = FILTER_RET_NOT_CHANGE;
  if (!cutoff_.is_valid()) { return OB_NOT_INIT; }
  // Schema KV has two user rowkey columns and the two native MVCC columns.
  // Publications after capture and uncommitted rows cannot be reclaimed from
  // this mark set, even if their G did not exist at the cutoff.
  if (row.is_uncommitted_row() || row.is_ghost_row()) { return OB_SUCCESS; }
  if (row.count_ < 4) { return OB_ERR_UNEXPECTED; }
  if (row.storage_datums_[0].get_int() != static_cast<int64_t>(MetaCollection::STORAGE_LAYOUTS)) {
    return OB_SUCCESS;
  }
  const int64_t encoded_scn = row.storage_datums_[2].get_int();
  if (encoded_scn >= 0 || encoded_scn == INT64_MIN) { return OB_STATE_NOT_MATCH; }
  if (-encoded_scn > cutoff_.get_val_for_tx()) { return OB_SUCCESS; }
  const ObString key = row.storage_datums_[1].get_string();
  int64_t id = 0, version = 0, chunk = 0, pos = 0;
  int ret = key.length() == 24 ? OB_SUCCESS : OB_CHECKSUM_ERROR;
  if (ret == OB_SUCCESS) { ret = serialization::decode_i64(key.ptr(), key.length(), pos, &id); }
  if (ret == OB_SUCCESS) { ret = serialization::decode_i64(key.ptr(), key.length(), pos, &version); }
  if (ret == OB_SUCCESS) { ret = serialization::decode_i64(key.ptr(), key.length(), pos, &chunk); }
  if (ret == OB_SUCCESS && (id <= 0 || version < -1 || chunk < 0 || (version == -1 && chunk != 0))) {
    ret = OB_CHECKSUM_ERROR;
  }
  if (ret == OB_SUCCESS) {
    const auto reference = std::lower_bound(references_.begin(), references_.end(),
        static_cast<uint64_t>(id), [](const Reference &entry, uint64_t target) {
      return entry.layout_id < target;
    });
    if (reference == references_.end() || reference->layout_id != static_cast<uint64_t>(id)
        || (version >= 0 && version < reference->minimum_version)) {
      result = FILTER_RET_REMOVE;
    }
  }
  return ret;
}

} // namespace compaction
} // namespace oceanbase
