/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef OCEANBASE_QUERY_API_ENGINE_BASIC_OB_ENCODED_SORT_ROW_H_
#define OCEANBASE_QUERY_API_ENGINE_BASIC_OB_ENCODED_SORT_ROW_H_

#include <cstdint>
#include "common/datum/ob_datum.h"
#include "lib/container/ob_iarray.h"

namespace oceanbase
{
namespace common
{
class ObIAllocator;
}
namespace query
{

// Neutral wire/layout view used by direct-load to hand encoded keys to the
// query sort implementation.  Query's internal StoredRow has the same layout;
// the adapter verifies that invariant before invoking the sorter.
struct ObEncodedSortRow
{
  ObEncodedSortRow() : cnt_(0), row_size_(0) {}

  common::ObDatum *cells()
  {
    return reinterpret_cast<common::ObDatum *>(payload_);
  }
  const common::ObDatum *cells() const
  {
    return reinterpret_cast<const common::ObDatum *>(payload_);
  }
  TO_STRING_KV(K_(cnt), K_(row_size));

  uint32_t cnt_;
  uint32_t row_size_;
  char payload_[0];
} __attribute__((packed));

int sort_encoded_rows(common::ObIArray<ObEncodedSortRow *> &rows,
                      common::ObIAllocator &allocator,
                      bool &can_encode);

} // namespace query
} // namespace oceanbase

#endif // OCEANBASE_QUERY_API_ENGINE_BASIC_OB_ENCODED_SORT_ROW_H_
