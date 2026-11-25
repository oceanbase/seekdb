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

#include <cstddef>
#include <cstdint>
#include <memory>
#include <curl/curl.h>
#include "rust/cxx.h"

namespace oceanbase::http {

struct Collector;
struct SdkOptions;
struct Completion;
enum class EasyErrorKind : std::uint8_t;

class Multi;

class Easy final {
public:
  explicit Easy(rust::Box<Collector> collector) noexcept;
  ~Easy();
  Easy(const Easy &) = delete;
  Easy &operator=(const Easy &) = delete;

  std::int32_t configure(const SdkOptions &options) noexcept;
  std::int32_t set_url(rust::Str url) noexcept;
  std::int32_t accept_encoding(rust::Str encoding) noexcept;
  std::int32_t add_header(rust::Str header) noexcept;
  std::int32_t set_body(rust::Slice<const std::uint8_t> body) noexcept;
  std::int32_t perform() noexcept;
  std::int32_t status(std::uint32_t &status) const noexcept;
  Collector &collector() noexcept;
  std::int32_t detach() noexcept;
  std::int32_t trust_pem_file(rust::Str path) noexcept;
  bool valid() const noexcept { return handle_ != nullptr; }

private:
  friend class Multi;
  static std::size_t write(char *data, std::size_t size, std::size_t count, void *context) noexcept;
  std::int32_t set_text(CURLoption option, rust::Str value) noexcept;

  CURL *handle_;
  CURLM *multi_ = nullptr;
  curl_slist *headers_ = nullptr;
  rust::Box<Collector> collector_;
  std::size_t index_ = 0;
};

class Multi final {
public:
  Multi() noexcept;
  ~Multi();
  Multi(const Multi &) = delete;
  Multi &operator=(const Multi &) = delete;

  std::int32_t add(Easy &easy, std::size_t index) noexcept;
  std::int32_t drive(std::uint32_t &running) noexcept;
  std::int32_t wait(std::int32_t timeout_ms) const noexcept;
  Completion next_completion() noexcept;
  bool valid() const noexcept { return handle_ != nullptr; }

private:
  CURLM *handle_;
};

std::int32_t initialize() noexcept;
std::unique_ptr<Easy> new_easy(rust::Box<Collector> collector) noexcept;
std::unique_ptr<Multi> new_multi() noexcept;
EasyErrorKind easy_error_kind(std::int32_t code) noexcept;
rust::String easy_error_message(std::int32_t code);
rust::String multi_error_message(std::int32_t code);
bool gzip_supported() noexcept;

}
