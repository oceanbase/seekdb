/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef OCEANBASE_DATA_PLANE_API_ACCESS_OB_TABLE_SCAN_ACCESS_H_
#define OCEANBASE_DATA_PLANE_API_ACCESS_OB_TABLE_SCAN_ACCESS_H_

#include <stdint.h>

#include "common/object/ob_object.h"

namespace oceanbase
{
namespace common
{
class ObNewRowIterator;
class ObIAllocator;
}
namespace blocksstable
{
class ObDatumRow;
}
namespace storage
{
class ObTableScanParam;
}
namespace data_plane
{

class ObISparseRetrievalBlockSource;

// Query-neutral description of the statistics needed by a sparse-vector
// block bound.  SQL may obtain these values from its compiled scan spec, while
// the data-plane implementation remains free to use skip indexes or another
// physical statistics source.
struct ObSparseVectorBlockColumnSpec
{
  ObSparseVectorBlockColumnSpec()
    : store_index_(0), statistic_type_(0), projector_(0)
  {}
  int32_t store_index_;
  uint8_t statistic_type_;
  int32_t projector_;
  TO_STRING_KV(K_(store_index), K_(statistic_type), K_(projector));
};

struct ObSparseVectorBlockSourceSpec
{
  ObSparseVectorBlockSourceSpec()
    : columns_(nullptr),
      column_count_(0),
      min_domain_id_index_(0),
      max_domain_id_index_(0),
      score_index_(0),
      domain_id_rowkey_index_(1),
      dimension_rowkey_index_(0),
      domain_id_meta_(),
      dimension_meta_(),
      query_value_(0.0)
  {}
  bool is_valid() const
  {
    return nullptr != columns_ && column_count_ > 0
        && min_domain_id_index_ >= 0 && min_domain_id_index_ < column_count_
        && max_domain_id_index_ >= 0 && max_domain_id_index_ < column_count_
        && score_index_ >= 0 && score_index_ < column_count_
        && domain_id_rowkey_index_ >= 0 && dimension_rowkey_index_ >= 0
        && domain_id_meta_.is_valid() && dimension_meta_.is_valid();
  }
  const ObSparseVectorBlockColumnSpec *columns_;
  int64_t column_count_;
  int32_t min_domain_id_index_;
  int32_t max_domain_id_index_;
  int32_t score_index_;
  int32_t domain_id_rowkey_index_;
  int32_t dimension_rowkey_index_;
  common::ObObjMeta domain_id_meta_;
  common::ObObjMeta dimension_meta_;
  double query_value_;
  TO_STRING_KV(KP_(columns), K_(column_count), K_(min_domain_id_index),
      K_(max_domain_id_index), K_(score_index), K_(domain_id_rowkey_index),
      K_(dimension_rowkey_index), K_(domain_id_meta), K_(dimension_meta),
      K_(query_value));
};

// The concrete table-scan iterator stays private to storage.  Query only
// operates on the common row-iterator handle through these capabilities.
int table_scan_next_datum_row(
    common::ObNewRowIterator *iterator,
    blocksstable::ObDatumRow *&row);
int table_scan_rescan(
    common::ObNewRowIterator *iterator,
    storage::ObTableScanParam &scan_param);

// On success source is allocator-owned and must be destroyed through its
// public port.  On failure source remains null and the scan param stays owned
// by its caller.
int create_sparse_vector_block_source(
    common::ObIAllocator &allocator,
    storage::ObTableScanParam &scan_param,
    const ObSparseVectorBlockSourceSpec &spec,
    ObISparseRetrievalBlockSource *&source);

} // namespace data_plane
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_API_ACCESS_OB_TABLE_SCAN_ACCESS_H_
