/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef OCEANBASE_DATA_PLANE_API_BLOCKSSTABLE_OB_DATUM_ROW_ITERATOR_H_
#define OCEANBASE_DATA_PLANE_API_BLOCKSSTABLE_OB_DATUM_ROW_ITERATOR_H_

#include <cstdint>
#include "lib/ob_errno.h"
#include "lib/utility/ob_macro_utils.h"
#include "lib/utility/ob_print_utils.h"
#include "share/allocator/ob_reserve_arena.h"

namespace oceanbase
{
namespace blocksstable
{

class ObDatumRow;

class ObDatumRowIterator
{
public:
  typedef common::ObReserveArenaAllocator<1024> ObStorageReserveAllocator;
  ObDatumRowIterator() {}
  virtual ~ObDatumRowIterator() {}
  virtual int get_next_row(ObDatumRow *&row) = 0;
  virtual int get_next_rows(ObDatumRow *&rows, int64_t &row_count)
  {
    int ret = common::OB_SUCCESS;
    if (OB_FAIL(get_next_row(rows))) {
    } else {
      row_count = 1;
    }
    return ret;
  }
  virtual void reset() {}
  TO_STRING_EMPTY();
};

class ObSingleDatumRowIteratorWrapper : public ObDatumRowIterator
{
public:
  ObSingleDatumRowIteratorWrapper() : row_(nullptr), iter_end_(false) {}
  explicit ObSingleDatumRowIteratorWrapper(ObDatumRow *row)
      : row_(row), iter_end_(false) {}
  virtual ~ObSingleDatumRowIteratorWrapper() {}

  void set_row(ObDatumRow *row) { row_ = row; }
  int get_next_row(ObDatumRow *&row) override
  {
    int ret = common::OB_SUCCESS;
    if (OB_ISNULL(row_)) {
      ret = common::OB_NOT_INIT;
    } else if (iter_end_) {
      ret = common::OB_ITER_END;
    } else {
      row = row_;
      iter_end_ = true;
    }
    return ret;
  }
  void reset() override { iter_end_ = false; }

private:
  DISALLOW_COPY_AND_ASSIGN(ObSingleDatumRowIteratorWrapper);
  ObDatumRow *row_;
  bool iter_end_;
};

} // namespace blocksstable
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_API_BLOCKSSTABLE_OB_DATUM_ROW_ITERATOR_H_
