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

#ifndef JIT_CONTEXT_H
#define JIT_CONTEXT_H
#include "core/ob_orc_jit.h"
#include "expr/ob_llvm_type.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"

namespace oceanbase
{
namespace jit
{
namespace core
{
struct JitContext
{
public:
  explicit JitContext()
      : Compile(false),
        TheContext(nullptr),
        Builder(nullptr),
        TheModule(nullptr),
        TheFPM(nullptr)
  { }

  int InitializeModule(const ObDataLayout &DL);
  int compile(ObOrcJit &jit);
  int optimize();

  ObLLVMContext& get_context() { return *TheContext; }
  IRBuilder<>& get_builder() { return *Builder; }
  Module& get_module() { return *TheModule; }

public:
  bool Compile;
  
  std::unique_ptr<ObLLVMContext> TheContext;
  std::unique_ptr<IRBuilder<>> Builder;
  std::unique_ptr<Module> TheModule;
  std::unique_ptr<legacy::FunctionPassManager> TheFPM;
};

class ObDWARFContext
{
public:
  ObDWARFContext(char* DebugBuf, int64_t DebugLen)
    : MemoryRef(StringRef(DebugBuf, DebugLen), "") {}

  ~ObDWARFContext() {}

  int init();

public:
  llvm::MemoryBufferRef MemoryRef;
  std::unique_ptr<llvm::object::Binary> Bin;

  std::unique_ptr<llvm::DWARFContext> Context;
};

} // core
} // jit
} // oceanbase

#endif /* JIT_CONTEXT_H */
