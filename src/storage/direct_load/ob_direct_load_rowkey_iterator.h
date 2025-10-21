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
#pragma once

#include "lib/container/ob_heap.h"
#include "storage/blocksstable/ob_datum_rowkey.h"
#include "storage/direct_load/ob_direct_load_multiple_datum_rowkey.h"

namespace oceanbase
{
namespace blocksstable
{
class ObStorageDatumUtils;
class ObSSTableSecMetaIterator;
} // namespace blocksstable
namespace storage
{

template <class Rowkey>
class ObIDirectLoadRowkeyIterator
{
public:
  virtual ~ObIDirectLoadRowkeyIterator() = default;
  virtual int get_next_rowkey(const Rowkey *&rowkey) = 0;
  TO_STRING_EMPTY();
};

template <class Rowkey>
class ObDirectLoadRowkeyEmptyIterator : public ObIDirectLoadRowkeyIterator<Rowkey>
{
public:
  ObDirectLoadRowkeyEmptyIterator() = default;
  virtual ~ObDirectLoadRowkeyEmptyIterator() = default;
  int get_next_rowkey(const Rowkey *&rowkey) override { return OB_ITER_END; }
};

typedef ObIDirectLoadRowkeyIterator<blocksstable::ObDatumRowkey> ObIDirectLoadDatumRowkeyIterator;
typedef ObIDirectLoadRowkeyIterator<ObDirectLoadMultipleDatumRowkey>
  ObIDirectLoadMultipleDatumRowkeyIterator;
typedef ObDirectLoadRowkeyEmptyIterator<blocksstable::ObDatumRowkey>
  ObDirectLoadDatumRowkeyEmptyIterator;

class ObDirectLoadDatumRowkeyArrayIterator : public ObIDirectLoadDatumRowkeyIterator
{
public:
  ObDirectLoadDatumRowkeyArrayIterator();
  virtual ~ObDirectLoadDatumRowkeyArrayIterator();
  int init(const common::ObIArray<blocksstable::ObDatumRowkey> &rowkey_array);
  int get_next_rowkey(const blocksstable::ObDatumRowkey *&rowkey) override;
private:
  const common::ObIArray<blocksstable::ObDatumRowkey> *rowkey_array_;
  int64_t pos_;
  bool is_inited_;
};

class ObDirectLoadMacroBlockEndKeyIterator : public ObIDirectLoadDatumRowkeyIterator
{
public:
  ObDirectLoadMacroBlockEndKeyIterator();
  virtual ~ObDirectLoadMacroBlockEndKeyIterator();
  int init(blocksstable::ObSSTableSecMetaIterator *macro_meta_iter);
  int get_next_rowkey(const blocksstable::ObDatumRowkey *&rowkey) override;
private:
  blocksstable::ObSSTableSecMetaIterator *macro_meta_iter_;
  blocksstable::ObDatumRowkey rowkey_;
  bool is_inited_;
};

template <class Rowkey>
class ObDirectLoadRowkeyIteratorGuard
{
  typedef ObIDirectLoadRowkeyIterator<Rowkey> RowkeyIterator;
public:
  ObDirectLoadRowkeyIteratorGuard()
  {
    all_iters_.set_tenant_id(MTL_ID());
    iters_.set_tenant_id(MTL_ID());
  }
  ~ObDirectLoadRowkeyIteratorGuard() { reset(); }
  void reset()
  {
    for (int64_t i = 0; i < all_iters_.count(); ++i) {
      RowkeyIterator *iter = all_iters_.at(i);
      iter->~RowkeyIterator();
    }
    all_iters_.reset();
    iters_.reset();
  }
  void reuse() { iters_.reuse(); }
  int push_back(RowkeyIterator *iter)
  {
    int ret = OB_SUCCESS;
    if (OB_ISNULL(iter)) {
      ret = OB_INVALID_ARGUMENT;
      STORAGE_LOG(WARN, "invalid args", KR(ret), KP(iter));
    } else {
      if (OB_FAIL(iters_.push_back(iter))) {
        STORAGE_LOG(WARN, "fail to push back", KR(ret));
      } else if (OB_FAIL(all_iters_.push_back(iter))) {
        STORAGE_LOG(WARN, "fail to push back", KR(ret));
        iters_.pop_back();
      }
    }
    return ret;
  }
  int add(RowkeyIterator *iter) { return push_back(iter); }
  ObArray<RowkeyIterator *> &get_iters() { return iters_; }
private:
  ObArray<RowkeyIterator *> all_iters_;
  ObArray<RowkeyIterator *> iters_;
};

typedef ObDirectLoadRowkeyIteratorGuard<blocksstable::ObDatumRowkey>
  ObDirectLoadDatumRowkeyIteratorGuard;
typedef ObDirectLoadRowkeyIteratorGuard<ObDirectLoadMultipleDatumRowkey>
  ObDirectLoadMultipleDatumRowkeyIteratorGuard;

} // namespace storage
} // namespace oceanbase
