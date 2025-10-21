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

#ifndef OB_STORAGE_OB_SSTABLE_MULTI_VERSION_ROW_ITERATOR_H_
#define OB_STORAGE_OB_SSTABLE_MULTI_VERSION_ROW_ITERATOR_H_

#include "ob_sstable_row_getter.h"
#include "ob_sstable_row_scanner.h"
#include "ob_sstable_row_multi_scanner.h"

namespace oceanbase {
using namespace blocksstable;
namespace storage {

class ObSSTableMultiVersionRowGetter : public ObSSTableRowScanner<>
{
public:
  ObSSTableMultiVersionRowGetter()
      : multi_version_range_(),
      range_idx_(0),
      base_rowkey_(nullptr)
  {}
  virtual ~ObSSTableMultiVersionRowGetter() {}
  virtual void reset() override;
  virtual void reuse() override;
  virtual void reclaim() override;
protected:
  virtual int inner_open(
      const ObTableIterParam &iter_param,
      ObTableAccessContext &access_ctx,
      ObITable *table,
      const void *query_range) override;
  virtual int inner_get_next_row(const blocksstable::ObDatumRow *&row) override;
protected:
  blocksstable::ObDatumRange multi_version_range_;
  int64_t range_idx_;
private:
  const blocksstable::ObDatumRowkey *base_rowkey_;
  blocksstable::ObDatumRow not_exist_row_;
};

class ObSSTableMultiVersionRowScanner : public ObSSTableMultiVersionRowGetter
{
public:
  ObSSTableMultiVersionRowScanner()
      : base_range_(nullptr)
  {}
  virtual ~ObSSTableMultiVersionRowScanner() {}
  virtual void reset() override;
  virtual void reuse() override;
  virtual void reclaim() override;
protected:
  virtual int inner_open(
      const ObTableIterParam &iter_param,
      ObTableAccessContext &access_ctx,
      ObITable *table,
      const void *query_range) override;
  virtual int inner_get_next_row(const blocksstable::ObDatumRow *&row) override
  { return ObSSTableRowScanner::inner_get_next_row(row); }
private:
  const blocksstable::ObDatumRange *base_range_;
};


class ObSSTableMultiVersionRowMultiGetter : public ObSSTableRowMultiScanner<>
{
public:
  ObSSTableMultiVersionRowMultiGetter()
      : multi_version_ranges_(),
      range_idx_(0),
      pending_row_(nullptr),
      base_rowkeys_(nullptr)
  {
    multi_version_ranges_.set_attr(ObMemAttr(MTL_ID(), "MVersionRanges"));
  }
  virtual ~ObSSTableMultiVersionRowMultiGetter() {}
  virtual void reset() override;
  virtual void reuse() override;
  virtual void reclaim() override;
protected:
  virtual int inner_open(
      const ObTableIterParam &iter_param,
      ObTableAccessContext &access_ctx,
      ObITable *table,
      const void *query_range) override;
  virtual int inner_get_next_row(const blocksstable::ObDatumRow *&row) override;
private:
  common::ObSEArray<blocksstable::ObDatumRange, 4> multi_version_ranges_;
  int64_t range_idx_;
  blocksstable::ObDatumRow not_exist_row_;
  const blocksstable::ObDatumRow *pending_row_;
  const common::ObIArray<blocksstable::ObDatumRowkey> *base_rowkeys_;
};

class ObSSTableMultiVersionRowMultiScanner : public ObSSTableRowMultiScanner<>
{
public:
  ObSSTableMultiVersionRowMultiScanner()
      : multi_version_ranges_()
  {
    multi_version_ranges_.set_attr(ObMemAttr(MTL_ID(), "MVersionRanges"));
  }
  virtual ~ObSSTableMultiVersionRowMultiScanner() {}
  virtual void reset() override;
  virtual void reuse() override;
protected:
  virtual int inner_open(
      const ObTableIterParam &iter_param,
      ObTableAccessContext &access_ctx,
      ObITable *table,
      const void *query_range) override;
private:
  common::ObSEArray<blocksstable::ObDatumRange, 4> multi_version_ranges_;
};

}
}
#endif //OB_STORAGE_OB_SSTABLE_MULTI_VERSION_ROW_ITERATOR_H_
