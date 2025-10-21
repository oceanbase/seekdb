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
 

#ifndef OB_GENERATED_SCALAR_BP_FUNC_H_
#define OB_GENERATED_SCALAR_BP_FUNC_H_

#include <stdint.h>

namespace oceanbase
{
namespace common
{
// packing 8 uint8_t once

// packing 32 uint8_t once
void scalar_fastpackwithoutmask_8_32_count(const uint8_t *__restrict__ in, uint32_t *__restrict__ _out, const uint32_t bit);
void scalar_fastunpack_8_32_count(const uint32_t *__restrict__ _in, uint8_t *__restrict__ out, const uint32_t bit);

// packing 16 uint16_t once

// packing 32 uint16_t once
void scalar_fastpackwithoutmask_16_32_count(const uint16_t *__restrict__ in, uint32_t *__restrict__ _out, const uint32_t bit);
void scalar_fastunpack_16_32_count(const uint32_t *__restrict__ _in, uint16_t *__restrict__ out, const uint32_t bit);

// packing 32 uint32_t once
void scalar_fastpackwithoutmask_32(const uint32_t *__restrict__ in, uint32_t *__restrict__ out, const uint32_t bit);
void scalar_fastunpack_32(const uint32_t*__restrict__ in, uint32_t *__restrict__ out, const uint32_t bit);

// packing 64 uint64_t once
void scalar_fastpackwithoutmask_64(const uint64_t *__restrict__ in, uint64_t *__restrict__ out, const uint32_t bit);
void scalar_fastunpack_64(const uint64_t*__restrict__ in, uint64_t *__restrict__ out, const uint32_t bit);

  
} // end namespace common
} // end namespace oceanbase
#endif /* OB_GENERATED_SCALAR_BP_FUNC_H_ */
