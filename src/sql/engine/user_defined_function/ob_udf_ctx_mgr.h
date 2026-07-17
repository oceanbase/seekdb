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

#ifndef OB_UDF_CTX_MGR_H_
#define OB_UDF_CTX_MGR_H_

#include "sql/engine/user_defined_function/ob_user_defined_function.h"

namespace oceanbase
{
namespace sql
{

class ObExprDllUdf;

/*
 * Use expr's id as the execution period, expr obtains the key for its own execution ctx.
 * Currently, the same column points to the same raw expr, the same aggregation (e.g., sum) points to
 * the same raw expr, but this is not done for ordinary expressions.
 * ATTENTION: If optimization for sharing the same expr among ordinary expressions is done later, udf's expr must
 * ensure that expr cannot be shared.
 *
 * */
class ObUdfCtxMgr
{
private:
  static const int64_t BUKET_NUM = 100;
public:
  ObUdfCtxMgr() : allocator_(common::ObModIds::OB_SQL_UDF), ctxs_() {}
  ~ObUdfCtxMgr();
  int try_init_map();
  common::ObIAllocator &get_allocator() { return allocator_; }
  int reset();
private:
  common::ObArenaAllocator allocator_;
  common::hash::ObHashMap<uint64_t, ObNormalUdfExeUnit *, common::hash::NoPthreadDefendMode> ctxs_;
private:
  //disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObUdfCtxMgr);
};

}
}

#endif
