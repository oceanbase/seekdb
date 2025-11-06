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

#ifndef MOCK_OB_STORE_ROW_ITERATOR_H_
#define MOCK_OB_STORE_ROW_ITERATOR_H_

#include "storage/access/ob_store_row_iterator.h"

namespace oceanbase
{
namespace storage
{

class MockObStoreRowIterator : public ObStoreRowIterator
{
public:
  MockObStoreRowIterator() {}
  virtual ~MockObStoreRowIterator() {}
  MOCK_METHOD1(get_next_row, int(const ObStoreRow *&row));
  MOCK_METHOD0(reset, void());
};

}
}



#endif /* MOCK_OB_STORE_ROW_ITERATOR_H_ */

