
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

#ifndef _OCEANBASE_STORAGE_FTS_DICT_OB_FT_DICT_ITERATOR_H_
#define _OCEANBASE_STORAGE_FTS_DICT_OB_FT_DICT_ITERATOR_H_

#include "lib/string/ob_string.h"

namespace oceanbase
{
namespace storage
{
// TOOD: will make it a template with data, for now there's no data
class ObIFTDictIterator
{
public:
  ObIFTDictIterator() {}
  virtual ~ObIFTDictIterator() {}

  // vaild until it returns OB_ITER_END.
  virtual int next() = 0;
  // get key
  virtual int get_key(ObString &str) = 0;
  // get value by template, current no use
  virtual int get_value() = 0;
};

} //  namespace storage
} //  namespace oceanbase

#endif // _OCEANBASE_STORAGE_FTS_DICT_OB_FT_DICT_ITERATOR_H_
