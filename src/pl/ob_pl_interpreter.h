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

#ifndef OCEANBASE_SRC_PL_OB_PL_INTERPRETER_H_
#define OCEANBASE_SRC_PL_OB_PL_INTERPRETER_H_

#include "ob_pl_stmt.h"

namespace oceanbase
{
namespace pl
{
class ObPLExecState; // defined in ob_pl.h

// Tree-walking interpreter for PL routines. On this branch it is THE dispatch
// layer for PL: ObPLExecState::execute() calls it unconditionally. It walks the
// resolved ObPLStmt tree directly and drives the routine through the same ObSPI
// (spi_*) runtime the generated code used, so the front end (parser/resolver)
// and the runtime are shared with the legacy codegen path.
//
// WIP: execute() currently resolves the routine, walks + logs the ObPLStmt tree,
// and returns OB_NOT_SUPPORTED. Statement dispatch (real execution) is being
// added incrementally; until then the server cannot run PL.
class ObPLInterpreter
{
public:
  explicit ObPLInterpreter(ObPLExecState &state) : state_(state) {}
  ~ObPLInterpreter() {}

  // Entry point: walk and execute the routine's ObPLStmt body.
  int execute();

private:
  ObPLExecState &state_;
  DISALLOW_COPY_AND_ASSIGN(ObPLInterpreter);
};

} // namespace pl
} // namespace oceanbase

#endif // OCEANBASE_SRC_PL_OB_PL_INTERPRETER_H_
