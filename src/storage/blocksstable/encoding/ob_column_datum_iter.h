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
#ifndef OCEANBASE_ENCODING_OB_COLUMN_DATUM_ITER_H_
#define OCEANBASE_ENCODING_OB_COLUMN_DATUM_ITER_H_

#include "storage/blocksstable/encoding/ob_encoding_hash_util.h"
#include "storage/blocksstable/encoding/ob_encoding_util.h"

namespace oceanbase
{
namespace blocksstable
{

class ObIDatumIter
{
public:
  virtual ~ObIDatumIter() = default;
  virtual int get_next(const ObDatum *&datum) = 0;
  virtual int64_t size() const = 0;
  virtual void reset() = 0;
  bool empty() const { return 0 == size(); }
};

class ObColumnDatumIter final : public ObIDatumIter
{
public:
  explicit ObColumnDatumIter(const ObColDatums &col_datums)
      : col_datums_(col_datums), idx_(0)
  {}
  int get_next(const ObDatum *&datum) override;
  int64_t size() const override { return col_datums_.count(); }
  void reset() override { idx_ = 0; }

private:
  const ObColDatums &col_datums_;
  int64_t idx_;
};

class ObEncodingHashTableDatumIter final : public ObIDatumIter
{
public:
  explicit ObEncodingHashTableDatumIter(const ObEncodingHashTable &hash_table)
      : hash_table_(hash_table), iter_(hash_table_.begin())
  {}
  int get_next(const ObDatum *&datum) override;
  int64_t size() const override { return hash_table_.size(); }
  void reset() override { iter_ = hash_table_.begin(); }

private:
  const ObEncodingHashTable &hash_table_;
  ObEncodingHashTable::ConstIterator iter_;
};

} // namespace blocksstable
} // namespace oceanbase

#endif // OCEANBASE_ENCODING_OB_COLUMN_DATUM_ITER_H_
