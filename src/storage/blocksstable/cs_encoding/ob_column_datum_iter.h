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

#include "ob_column_encoding_struct.h"
#include "ob_dict_encoding_hash_table.h"
#include "storage/blocksstable/encoding/ob_encoding_util.h"
#include "storage/blocksstable/encoding/ob_encoding_hash_util.h"

namespace oceanbase
{
namespace blocksstable
{
class ObIDatumIter
{
public:
  ObIDatumIter() {}
  virtual ~ObIDatumIter() {}
  virtual int get_next(const ObDatum *&datum) = 0;
  virtual int64_t size() const = 0;
  bool empty() const { return 0 == size(); }
  virtual void reset() = 0;
};

class ObColumnDatumIter : public ObIDatumIter
{
public:
  explicit ObColumnDatumIter(const ObColDatums &col_datums)
    : col_datums_(col_datums), idx_(0) { }
  ~ObColumnDatumIter() {}
  ObColumnDatumIter(const ObColumnDatumIter&) = delete;
  ObColumnDatumIter &operator=(const ObColumnDatumIter&) = delete;
  int get_next(const ObDatum *&datum) override;
  int64_t size() const override { return col_datums_.count(); }
  virtual void reset() override { idx_ = 0; }

private:
  const ObColDatums &col_datums_;
  int64_t idx_;
};

class ObDictDatumIter : public ObIDatumIter
{
public:
  explicit ObDictDatumIter(const ObDictEncodingHashTable &ht)
    : ht_(ht), iter_(ht_.begin()) { }
  ~ObDictDatumIter() {}
  ObDictDatumIter(const ObDictDatumIter&) = delete;
  ObDictDatumIter &operator=(const ObDictDatumIter&) = delete;

  int get_next(const ObDatum *&datum) override;
  int64_t size() const override { return ht_.distinct_node_cnt(); }
  virtual void reset() override {  iter_ = ht_.begin(); }

private:
  const ObDictEncodingHashTable &ht_;
  ObDictEncodingHashTable::ConstIterator iter_;
};

class ObEncodingHashTableDatumIter final : public ObIDatumIter
{
public:
  explicit ObEncodingHashTableDatumIter(const ObEncodingHashTable &ht)
    : ht_(ht), iter_(ht_.begin())
  {}
  virtual ~ObEncodingHashTableDatumIter() {}
  virtual int get_next(const ObDatum *&datum) override;
  virtual int64_t size() const override
  {
    return ht_.size();
  }
  virtual void reset() override
  {
    iter_ = ht_.begin();
  }
private:
  const ObEncodingHashTable &ht_;
  ObEncodingHashTable::ConstIterator iter_;
  DISALLOW_COPY_AND_ASSIGN(ObEncodingHashTableDatumIter);
};


}  // namespace blocksstable
}  // namespace oceanbase

#endif  // OCEANBASE_ENCODING_OB_COLUMN_DATUM_ITER_H_
