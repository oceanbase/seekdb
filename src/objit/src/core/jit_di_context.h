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

#ifndef JIT_CONTEXT_DI_H_
#define JIT_CONTEXT_DI_H_

#include "expr/ob_llvm_type.h"

namespace oceanbase
{
namespace jit
{
namespace core
{
struct JitDIContext
{
public:
  JitDIContext(ObIRModule &module)
    : dbuilder_(module),
      cu_(NULL),
      file_(NULL),
      sp_(NULL)
  {}

  ObDIBuilder dbuilder_;
  ObDICompileUnit *cu_;
  ObDIFile *file_;
  ObDISubprogram *sp_;
};

}  // core
}  // jit
}  // oceanbase

#endif /* JIT_DI_CONTEXT_H_ */
