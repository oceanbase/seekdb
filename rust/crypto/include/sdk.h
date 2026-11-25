// Copyright (c) 2026 OceanBase.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <cstdint>
#include "rust/cxx.h"

namespace oceanbase::crypto {

struct OperationResult;
struct CipherInfo;
struct CalendarResult;

OperationResult sdk_digest(std::uint32_t algorithm, rust::Slice<const std::uint8_t> input,
                           rust::Slice<std::uint8_t> output) noexcept;
OperationResult sdk_hmac(std::uint32_t algorithm, rust::Slice<const std::uint8_t> key,
                         rust::Slice<const std::uint8_t> input, rust::Slice<std::uint8_t> output) noexcept;
OperationResult sdk_random(rust::Slice<std::uint8_t> output) noexcept;
CipherInfo sdk_cipher_info(std::uint32_t mode) noexcept;
OperationResult sdk_encrypt(std::uint32_t mode, rust::Slice<const std::uint8_t> key,
                            rust::Slice<const std::uint8_t> iv, rust::Slice<const std::uint8_t> aad,
                            rust::Slice<const std::uint8_t> input, rust::Slice<std::uint8_t> output,
                            rust::Slice<std::uint8_t> tag) noexcept;
OperationResult sdk_decrypt(std::uint32_t mode, rust::Slice<const std::uint8_t> key,
                            rust::Slice<const std::uint8_t> iv, rust::Slice<const std::uint8_t> aad,
                            rust::Slice<const std::uint8_t> input, rust::Slice<const std::uint8_t> tag,
                            rust::Slice<std::uint8_t> output) noexcept;
OperationResult sdk_des(rust::Slice<const std::uint8_t> password, bool has_password,
                        rust::Slice<std::uint8_t> buffer, bool encrypt) noexcept;
OperationResult sdk_des_crypt(rust::Slice<const std::uint8_t> password,
                              rust::Slice<const std::uint8_t> salt, rust::Slice<std::uint8_t> output) noexcept;
CalendarResult sdk_certificate_expiry(rust::Slice<const std::uint8_t> pem) noexcept;

}
