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

#ifndef OCEANBASE_DATUM_ROW_ITERATOR_
#define OCEANBASE_DATUM_ROW_ITERATOR_

#include "src/storage/blocksstable/ob_datum_row.h"

namespace oceanbase
{
namespace blocksstable
{

class ObDatumRowIterator
{
public:
  typedef common::ObReserveArenaAllocator<1024> ObStorageReserveAllocator;
public:
  ObDatumRowIterator() {}
  virtual ~ObDatumRowIterator() {}
  /**
   * get the next datum row and move the cursor
   *
   * @param row [out]
   *
   * @return OB_ITER_END if end of iteration
   */
  virtual int get_next_row(ObDatumRow *&row) = 0;
  virtual int get_next_rows(ObDatumRow *&rows, int64_t &row_count)
  {
    int ret = OB_SUCCESS;
    if (OB_FAIL(get_next_row(rows))) {
    } else {
      row_count = 1;
    }
    return ret;
  }
  virtual void reset() {}
  TO_STRING_EMPTY();
};

/// wrap one datum row as an iterator
class ObSingleDatumRowIteratorWrapper: public ObDatumRowIterator
{
public:
  ObSingleDatumRowIteratorWrapper();
  ObSingleDatumRowIteratorWrapper(ObDatumRow *row);
  virtual ~ObSingleDatumRowIteratorWrapper() {}

  void set_row(ObDatumRow *row) { row_ = row; }
  virtual int get_next_row(ObDatumRow *&row);
	virtual void reset() { iter_end_ = false; }
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObSingleDatumRowIteratorWrapper);
private:
  // data members
  ObDatumRow *row_;
  bool iter_end_;
};

inline ObSingleDatumRowIteratorWrapper::ObSingleDatumRowIteratorWrapper()
    :row_(NULL),
     iter_end_(false)
{}

inline ObSingleDatumRowIteratorWrapper::ObSingleDatumRowIteratorWrapper(ObDatumRow *row)
    :row_(row),
     iter_end_(false)
{}

inline int ObSingleDatumRowIteratorWrapper::get_next_row(ObDatumRow *&row)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(row_)) {
    ret = OB_NOT_INIT;
  } else if (iter_end_) {
    ret = OB_ITER_END;
  } else {
    row = row_;
    iter_end_ = true;
  }
  return ret;
}

}
}

#endif //OCEANBASE_DATUM_ROW_ITERATOR_
