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

#define USING_LOG_PREFIX JIT

#include "core/jit_context.h"
#include "ob_llvm_type.h"
namespace oceanbase {
namespace jit {

using namespace ::oceanbase::common;

void ObIRObj::reset() {
  type_ = nullptr;
  cs_level_ = nullptr;
  cs_type_ = nullptr;
  scale_ = nullptr;
  val_len_ = nullptr;
  v_ = nullptr;
}

//const value set

ObIRValuePtr ObIRObj::get_ir_value_element(core::JitContext &jc,
                                           const ObIRValuePtr obj,
                                           int64_t idx)
{
  ObIRValuePtr ret = NULL;
  ObIRValuePtr indices[] = {get_const(jc.get_context(), 32, 0),
                            get_const(jc.get_context(), 32, idx)};
  ObIRValuePtr value_p = jc.get_builder().CreateGEP(ObIRType::getInt64Ty(jc.get_context()), obj,
                                               makeArrayRef(indices),
                                               "obj_elem_p");
  ret = jc.get_builder().CreateLoad(ObIRType::getInt64Ty(jc.get_context()), value_p, "value_elem");
  return ret;
}



} //jit end
} //oceanbase end
