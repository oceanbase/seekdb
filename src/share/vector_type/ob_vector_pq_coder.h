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

#ifndef OCEANBASE_LIB_OB_VECTOR_PQ_CODER_H_
#define OCEANBASE_LIB_OB_VECTOR_PQ_CODER_H_

#include "common/ob_target_specific.h"

namespace oceanbase
{
namespace common
{

// pq coder
template<typename T>
struct PQDecoder {
  const T* code_;
  PQDecoder(const uint8_t* code) : code_((T*)code) {};
  inline uint64_t decode() { return (uint64_t)(*code_++); }
};

struct PQDecoderGeneric {
  int64_t nbits_;
  union {
    const uint8_t* code8_;
    const uint16_t* code16_;
    const uint32_t* code32_;
  };
  PQDecoderGeneric(const uint8_t* code, int64_t nbits) : nbits_(nbits), code8_(code) {};
  inline uint64_t decode() {
    if (nbits_ <= 8) return (uint64_t)(*code8_++);
    else if (nbits_ <= 16) return (uint64_t)(*code16_++);
    return (uint64_t)(*code32_++);
  }
};

template<typename T>
struct PQEncoder {
  T* code_;
  PQEncoder(uint8_t* code) : code_((T*)code) {};
  inline void encode(uint64_t code) {
    *code_ = (T)code;
    code_++;
  }
};

struct PQEncoderGeneric {
  int64_t nbits_;
  union {
    uint8_t* code8_;
    uint16_t* code16_;
    uint32_t* code32_;
  };
  PQEncoderGeneric(uint8_t* code, int64_t nbits) : nbits_(nbits), code8_(code) {};
  inline void encode(uint64_t code) {
    if (nbits_ <= 8) {
      *code8_ = (uint8_t)code;
      code8_++;
    } else if (nbits_ <= 16) {
      *code16_ = (uint16_t)code;
      code16_++;
    } else {
      *code32_ = (uint32_t)code;
      code32_++;
    }
  }
};

}  // namespace common
}  // namespace oceanbase
#endif
