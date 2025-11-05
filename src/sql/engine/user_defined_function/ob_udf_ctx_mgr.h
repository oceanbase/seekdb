/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
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
