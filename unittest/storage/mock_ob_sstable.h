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

#ifndef MOCK_OB_SSTABLE_H_
#define MOCK_OB_SSTABLE_H_

namespace oceanbase
{
namespace storage
{
class MockObMacroBlockIterator : public ObMacroBlockIterator
{
public:
  MockObMacroBlockIterator() {}
  virtual ~MockObMacroBlockIterator() {}
  MOCK_METHOD1(get_next_macro_block, int(ObMacroBlockDesc &block_desc));
};


class MockObSSTable : public ObSSTable
{
public:
  MockObSSTable() {}
  virtual ~MockObSSTable() {}

  MOCK_CONST_METHOD3(get_macro_range, int(const int64_t index, int64_t &macro_block_id,
                                          common::ObStoreRange &range));
  MOCK_METHOD2(open, int(
                   const share::schema::ObTableSchema &schema,
                   const int64_t data_version));
  MOCK_METHOD1(append, int(const ObStoreRow &row));
  MOCK_METHOD1(append, int(const int64_t macro_block_id));
  MOCK_METHOD0(close, int());
};

}
}






#endif /* MOCK_OB_SSTABLE_H_ */
