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
