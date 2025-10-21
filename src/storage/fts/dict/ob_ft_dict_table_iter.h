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

#ifndef _OCEANBASE_STORAGE_FTS_DICT_OB_FT_DICT_TABLE_ITER_H_
#define _OCEANBASE_STORAGE_FTS_DICT_OB_FT_DICT_TABLE_ITER_H_

#include "lib/mysqlclient/ob_isql_client.h"
#include "storage/fts/dict/ob_ft_dict_iterator.h"

namespace oceanbase
{
namespace storage
{
class ObFTDictTableIter : public ObIFTDictIterator
{
public:
  ObFTDictTableIter(ObISQLClient::ReadResult &result);
  ~ObFTDictTableIter() override { reset(); }

  // override
public:
  int get_key(ObString &str) override;
  int get_value() override;
  int next() override;

public:
  int init(const ObString &table_name);

private:
  void reset();

private:
  bool is_inited_;
  ObISQLClient::ReadResult &res_;
};

} //  namespace storage
} //  namespace oceanbase

#endif // _OCEANBASE_STORAGE_FTS_DICT_OB_FT_DICT_TABLE_ITER_H_
