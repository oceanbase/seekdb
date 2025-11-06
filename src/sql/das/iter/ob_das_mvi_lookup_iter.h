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

#ifndef OBDEV_SRC_SQL_DAS_ITER_OB_DAS_MVI_LOOKUP_ITER_H_
#define OBDEV_SRC_SQL_DAS_ITER_OB_DAS_MVI_LOOKUP_ITER_H_

// #include "storage/access/ob_dml_param.h"
#include "sql/das/iter/ob_das_local_lookup_iter.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

class ObDASScanCtDef;
class ObDASScanRtDef;
class ObDASMVILookupIter : public ObDASLocalLookupIter
{
public:
  ObDASMVILookupIter(): ObDASLocalLookupIter(ObDASIterType::DAS_ITER_MVI_LOOKUP) {}
  virtual ~ObDASMVILookupIter() {}

protected:
  virtual int inner_get_next_row() override;
  virtual int inner_get_next_rows(int64_t &count, int64_t capacity) override;
  virtual int do_index_lookup() override;

private:
  bool check_has_rowkey();
  int save_rowkey();
};

}  // namespace sql
}  // namespace oceanbase


#endif /* OBDEV_SRC_SQL_DAS_ITER_OB_DAS_MVI_ITER_H_ */
