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

#ifndef OBORCOBJECTCACHE_H
#define OBORCOBJECTCACHE_H

#include "llvm/ExecutionEngine/ObjectCache.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/ADT/StringRef.h"

#include <string>
#include <memory>
#include <utility>
#include <vector>
#include <map>
#include <cstdlib>
#include <cstring>

namespace oceanbase {
namespace jit {
namespace core {

class ObOrcObjectCache: public llvm::ObjectCache {
  friend class ObOrcJit;
public:
  ObOrcObjectCache() = default;
  ObOrcObjectCache(const ObOrcObjectCache&) = delete;
  ObOrcObjectCache& operator=(const ObOrcObjectCache&) = delete;
  ObOrcObjectCache(ObOrcObjectCache&&) = delete;

  ~ObOrcObjectCache() {
    if (mem.addr != nullptr) {
      std::free(mem.addr);
      mem.addr = nullptr;
      mem.sz = 0;
    }
  }

  virtual void notifyObjectCompiled(const llvm::Module* M, llvm::MemoryBufferRef ObjRef)
      override {
    mem.sz = ObjRef.getBufferSize();
    mem.addr = (char *) std::malloc(mem.sz);
    std::memcpy(mem.addr, ObjRef.getBufferStart(), ObjRef.getBufferSize());
  }

  virtual std::unique_ptr<llvm::MemoryBuffer> getObject(const llvm::Module* M) override {
    return llvm::MemoryBuffer::getMemBufferCopy(
             llvm::StringRef(mem.addr, mem.sz));
  }

private:
  struct MemStruct {
    char* addr;
    size_t sz;

    MemStruct() : addr(nullptr), sz(0) {
    }
    ~MemStruct() {
    }
  };
  MemStruct mem;
};
}
}
}

#endif /* OBORCOBJECTCACHE_H */
