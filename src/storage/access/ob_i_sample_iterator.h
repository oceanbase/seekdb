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

#ifndef OCEANBASE_STORAGE_OB_I_SAMPLE_ITERATOR_H
#define OCEANBASE_STORAGE_OB_I_SAMPLE_ITERATOR_H

#include "ob_store_row_iterator.h"

namespace oceanbase
{
namespace storage
{

class ObISampleIterator : public ObQueryRowIterator
{
public:
  explicit ObISampleIterator(const common::SampleInfo &sample_info);
  virtual ~ObISampleIterator();
  virtual void reuse() = 0;
protected:
  bool return_this_sample(const int64_t num) const;
protected:
  const common::SampleInfo *sample_info_;
};

}
}

#endif /* OCEANBASE_STORAGE_OB_I_SAMPLE_ITERATOR_H */
