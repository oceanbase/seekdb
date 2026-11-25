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

#include "rust/http_client.h"

#include <cstring>
#include <limits>
#include <new>
#include <string>
#include <utility>

namespace oceanbase::http {

std::int32_t initialize() noexcept
{
  // Client and batch lifetimes are independent of static destruction order.
  static const CURLcode result = curl_global_init(CURL_GLOBAL_ALL);
  return result;
}

Easy::Easy(rust::Box<Collector> collector) noexcept
    : handle_(curl_easy_init()), collector_(std::move(collector))
{}

Easy::~Easy()
{
  detach();
  curl_easy_cleanup(handle_);
  curl_slist_free_all(headers_);
}

std::unique_ptr<Easy> new_easy(rust::Box<Collector> collector) noexcept
{
  std::unique_ptr<Easy> easy(new (std::nothrow) Easy(std::move(collector)));
  if (easy && !easy->valid()) {
    easy.reset();
  }
  return easy;
}

std::size_t Easy::write(char *data, std::size_t size, std::size_t count, void *context) noexcept
{
  if (size != 0 && count > std::numeric_limits<std::size_t>::max() / size) {
    return 0;
  }
  // libcurl calls synchronously; Rust borrows the collector only between drive calls.
  auto &easy = *static_cast<Easy *>(context);
  return collect(*easy.collector_, rust::Slice<const std::uint8_t>(
      reinterpret_cast<const std::uint8_t *>(data), size * count));
}

std::int32_t Easy::configure(const SdkOptions &options) noexcept
{
  if (options.timeout_ms > static_cast<std::uint64_t>(std::numeric_limits<long>::max()) ||
      options.connect_timeout_ms > static_cast<std::uint64_t>(std::numeric_limits<long>::max()) ||
      options.max_redirects > static_cast<std::uint64_t>(std::numeric_limits<long>::max())) {
    return CURLE_BAD_FUNCTION_ARGUMENT;
  }
  CURLcode result = CURLE_OK;
  const auto set = [this, &result](CURLoption option, auto value) {
    if (result == CURLE_OK) {
      result = curl_easy_setopt(handle_, option, value);
    }
  };
  set(CURLOPT_NOSIGNAL, 1L);
  set(CURLOPT_TCP_NODELAY, 1L);
  set(CURLOPT_TIMEOUT_MS, static_cast<long>(options.timeout_ms));
  set(CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(options.connect_timeout_ms));
  set(CURLOPT_SSL_VERIFYPEER, options.verify_peer ? 1L : 0L);
  set(CURLOPT_SSL_VERIFYHOST, options.verify_host ? 2L : 0L);
  if (options.verify_peer) {
    set(CURLOPT_SSL_OPTIONS, static_cast<long>(CURLSSLOPT_NATIVE_CA));
  }
  set(CURLOPT_PROXY_SSL_VERIFYPEER, 1L);
  set(CURLOPT_PROXY_SSL_VERIFYHOST, 2L);
  set(CURLOPT_PROXY_SSL_OPTIONS, static_cast<long>(CURLSSLOPT_NATIVE_CA));
  set(CURLOPT_FOLLOWLOCATION, options.follow_redirects ? 1L : 0L);
  if (options.follow_redirects) {
    set(CURLOPT_MAXREDIRS, static_cast<long>(options.max_redirects));
  }
  const char *protocols = options.https_only ? "https" : "http,https";
  set(CURLOPT_PROTOCOLS_STR, protocols);
  set(CURLOPT_REDIR_PROTOCOLS_STR, protocols);
  set(CURLOPT_POST, 1L);
  set(CURLOPT_WRITEFUNCTION, &Easy::write);
  set(CURLOPT_WRITEDATA, this);
  set(CURLOPT_PRIVATE, this);
  return result;
}

std::int32_t Easy::set_text(CURLoption option, rust::Str value) noexcept
{
  if (std::memchr(value.data(), 0, value.size()) != nullptr) {
    return CURLE_BAD_FUNCTION_ARGUMENT;
  }
  try {
    const std::string text(value.data(), value.size());
    return curl_easy_setopt(handle_, option, text.c_str());
  } catch (const std::bad_alloc &) {
    return CURLE_OUT_OF_MEMORY;
  }
}

std::int32_t Easy::set_url(rust::Str url) noexcept
{
  return set_text(CURLOPT_URL, url);
}

std::int32_t Easy::accept_encoding(rust::Str encoding) noexcept
{
  return set_text(CURLOPT_ACCEPT_ENCODING, encoding);
}

std::int32_t Easy::trust_pem_file(rust::Str path) noexcept
{
  const auto result = set_text(CURLOPT_CAINFO, path);
  return result == CURLE_OK ? set_text(CURLOPT_PROXY_CAINFO, path) : result;
}

std::int32_t Easy::add_header(rust::Str header) noexcept
{
  if (std::memchr(header.data(), 0, header.size()) != nullptr) {
    return CURLE_BAD_FUNCTION_ARGUMENT;
  }
  try {
    const std::string text(header.data(), header.size());
    auto *updated = curl_slist_append(headers_, text.c_str());
    if (updated == nullptr) {
      return CURLE_OUT_OF_MEMORY;
    }
    headers_ = updated;
    return curl_easy_setopt(handle_, CURLOPT_HTTPHEADER, headers_);
  } catch (const std::bad_alloc &) {
    return CURLE_OUT_OF_MEMORY;
  }
}

std::int32_t Easy::set_body(rust::Slice<const std::uint8_t> body) noexcept
{
  if (body.size() > static_cast<std::uint64_t>(std::numeric_limits<curl_off_t>::max())) {
    return CURLE_BAD_FUNCTION_ARGUMENT;
  }
  const auto result = curl_easy_setopt(handle_, CURLOPT_POSTFIELDSIZE_LARGE,
                                       static_cast<curl_off_t>(body.size()));
  if (result != CURLE_OK) {
    return result;
  }
  return curl_easy_setopt(handle_, CURLOPT_COPYPOSTFIELDS,
                          body.empty() ? "" : reinterpret_cast<const char *>(body.data()));
}

std::int32_t Easy::perform() noexcept
{
  return curl_easy_perform(handle_);
}

std::int32_t Easy::status(std::uint32_t &status) const noexcept
{
  long value = 0;
  const auto result = curl_easy_getinfo(handle_, CURLINFO_RESPONSE_CODE, &value);
  status = static_cast<std::uint32_t>(value);
  return result;
}

Collector &Easy::collector() noexcept
{
  return *collector_;
}

std::int32_t Easy::detach() noexcept
{
  const auto result = multi_ == nullptr ? CURLM_OK : curl_multi_remove_handle(multi_, handle_);
  if (result == CURLM_OK) {
    multi_ = nullptr;
  }
  return result;
}

Multi::Multi() noexcept : handle_(curl_multi_init()) {}

Multi::~Multi()
{
  curl_multi_cleanup(handle_);
}

std::unique_ptr<Multi> new_multi() noexcept
{
  std::unique_ptr<Multi> multi(new (std::nothrow) Multi());
  if (multi && !multi->valid()) {
    multi.reset();
  }
  return multi;
}

std::int32_t Multi::add(Easy &easy, std::size_t index) noexcept
{
  const auto result = curl_multi_add_handle(handle_, easy.handle_);
  if (result == CURLM_OK) {
    easy.multi_ = handle_;
    easy.index_ = index;
  }
  return result;
}

std::int32_t Multi::drive(std::uint32_t &running) noexcept
{
  int count = 0;
  CURLMcode result;
  do {
    result = curl_multi_perform(handle_, &count);
  } while (result == CURLM_CALL_MULTI_PERFORM);
  running = static_cast<std::uint32_t>(count);
  return result;
}

std::int32_t Multi::wait(std::int32_t timeout_ms) const noexcept
{
  return curl_multi_wait(handle_, nullptr, 0, timeout_ms, nullptr);
}

Completion Multi::next_completion() noexcept
{
  int remaining = 0;
  while (auto *message = curl_multi_info_read(handle_, &remaining)) {
    if (message->msg == CURLMSG_DONE) {
      Easy *easy = nullptr;
      const auto result = curl_easy_getinfo(message->easy_handle, CURLINFO_PRIVATE, &easy);
      if (result != CURLE_OK || easy == nullptr || easy->handle_ != message->easy_handle) {
        return {true, 0, 0, CURLM_BAD_EASY_HANDLE};
      }
      return {true, easy->index_, message->data.result, CURLM_OK};
    }
  }
  return {false, 0, 0, CURLM_OK};
}

EasyErrorKind easy_error_kind(std::int32_t code) noexcept
{
  switch (static_cast<CURLcode>(code)) {
    case CURLE_URL_MALFORMAT: return EasyErrorKind::UrlMalformed;
    case CURLE_OPERATION_TIMEDOUT: return EasyErrorKind::Timeout;
    case CURLE_PEER_FAILED_VERIFICATION: return EasyErrorKind::PeerVerification;
    case CURLE_COULDNT_CONNECT: return EasyErrorKind::Connect;
    case CURLE_BAD_CONTENT_ENCODING: return EasyErrorKind::UnsupportedEncoding;
    default: return EasyErrorKind::Transport;
  }
}

rust::String easy_error_message(std::int32_t code)
{
  return rust::String(curl_easy_strerror(static_cast<CURLcode>(code)));
}

rust::String multi_error_message(std::int32_t code)
{
  return rust::String(curl_multi_strerror(static_cast<CURLMcode>(code)));
}

bool gzip_supported() noexcept
{
  const auto *version = curl_version_info(CURLVERSION_NOW);
  return version != nullptr && (version->features & CURL_VERSION_LIBZ) != 0;
}

}
