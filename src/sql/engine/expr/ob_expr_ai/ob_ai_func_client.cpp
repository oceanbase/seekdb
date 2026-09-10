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

#define USING_LOG_PREFIX SQL_ENG
#include "ob_ai_func_client.h"
#include "lib/lock/ob_thread_cond.h"
#include <algorithm>

namespace oceanbase
{
namespace common
{

namespace
{

static const int64_t AI_HTTP_DEFAULT_MAX_CONCURRENCY = 8;

#ifndef __EMSCRIPTEN__
struct ObAIHttpUrlLimit
{
  ObAIHttpUrlLimit()
      : endpoint_url_(), active_count_(0),
        concurrency_limit_(AI_HTTP_DEFAULT_MAX_CONCURRENCY)
  {}

  TO_STRING_KV(K_(endpoint_url), K_(active_count),
               K_(concurrency_limit));

  ObString endpoint_url_;
  int64_t active_count_;
  int64_t concurrency_limit_;
};

// AI expressions using the same complete request URL share one concurrency
// window. API keys are intentionally not part of the key: changing credentials
// must not multiply the configured service capacity. HTTP handles and response
// buffers remain owned by the Rust HTTP client and batch objects.
class ObAIHttpConcurrencyLimiter
{
public:
  ObAIHttpConcurrencyLimiter()
      : key_allocator_(ObModIds::OB_SQL_EXECUTOR), cond_(), url_limits_(),
        is_inited_(false)
  {
    const int ret = cond_.init(ObWaitEventIds::DEFAULT_COND_WAIT);
    if (OB_SUCCESS != ret) {
      LOG_ERROR("failed to initialize AI HTTP concurrency limiter", K(ret));
    } else {
      is_inited_ = true;
    }
  }

  ~ObAIHttpConcurrencyLimiter()
  {
    if (is_inited_) {
      cond_.destroy();
      is_inited_ = false;
    }
  }

  int acquire(const ObString &endpoint_url, const int64_t configured_limit,
              const int64_t requested_slots, int64_t &acquired_slots,
              const bool allow_partial = true)
  {
    int ret = OB_SUCCESS;
    ObAIHttpUrlLimit *url_limit = NULL;
    acquired_slots = 0;
    const int64_t now = ObTimeUtility::current_time();
    const int64_t remain_us = THIS_WORKER.is_timeout_ts_valid()
        ? THIS_WORKER.get_timeout_remain() : 60 * 1000 * 1000;
    const int64_t deadline_us = now + std::max<int64_t>(1, remain_us);
    if (!is_inited_) {
      ret = OB_NOT_INIT;
      LOG_WARN("AI HTTP concurrency limiter is not initialized", K(ret));
    } else if (endpoint_url.empty() || configured_limit <= 0
               || requested_slots <= 0) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid AI HTTP concurrency arguments", K(ret),
               K(endpoint_url), K(configured_limit), K(requested_slots));
    } else if (!allow_partial && requested_slots > configured_limit) {
      ret = OB_SIZE_OVERFLOW;
      LOG_WARN("batch exceeds the configured AI HTTP concurrency limit",
               K(ret), K(endpoint_url), K(configured_limit),
               K(requested_slots));
    }
    if (OB_FAIL(ret)) {
      return ret;
    }
    ObThreadCondGuard guard(cond_);
    if (OB_FAIL(find_url_limit_(
            endpoint_url, configured_limit, true, url_limit))) {
      LOG_WARN("failed to find AI HTTP URL limit", K(ret), K(endpoint_url));
    }
    const int64_t required_available = allow_partial ? 1 : requested_slots;
    while (OB_SUCC(ret)
           && url_limit->concurrency_limit_ - url_limit->active_count_
                  < required_available
           && ObTimeUtility::current_time() < deadline_us) {
      cond_.wait_us(std::max<int64_t>(
          1, deadline_us - ObTimeUtility::current_time()));
      url_limit = NULL;
      if (OB_FAIL(find_url_limit_(
              endpoint_url, configured_limit, false, url_limit))) {
        LOG_WARN("AI HTTP URL limit disappeared while waiting", K(ret),
                 K(endpoint_url));
      }
    }
    if (OB_FAIL(ret)) {
    } else if (url_limit->concurrency_limit_ - url_limit->active_count_
                   < required_available) {
      ret = OB_TIMEOUT;
      LOG_WARN("timed out waiting for AI HTTP concurrency slot", K(ret),
               K(endpoint_url), K(url_limit->active_count_),
               K(url_limit->concurrency_limit_));
    } else {
      acquired_slots = allow_partial
          ? std::min<int64_t>(requested_slots,
                url_limit->concurrency_limit_ - url_limit->active_count_)
          : requested_slots;
      url_limit->active_count_ += acquired_slots;
    }
    return ret;
  }

  void release(const ObString &endpoint_url, const int64_t released_slots)
  {
    ObAIHttpUrlLimit *url_limit = NULL;
    if (!is_inited_ || endpoint_url.empty() || released_slots <= 0) {
      return;
    }
    ObThreadCondGuard guard(cond_);
    if (OB_SUCCESS != find_url_limit_(endpoint_url, 0, false, url_limit)
        || OB_ISNULL(url_limit)) {
      const int ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("failed to release AI HTTP concurrency slot",
                K(ret), K(endpoint_url));
      return;
    }
    url_limit->active_count_ = std::max<int64_t>(
        0, url_limit->active_count_ - released_slots);
    cond_.broadcast();
  }

private:
  int find_url_limit_(const ObString &endpoint_url,
                      const int64_t configured_limit, const bool create,
                      ObAIHttpUrlLimit *&url_limit)
  {
    int ret = OB_SUCCESS;
    url_limit = NULL;
    for (int64_t i = 0;
         OB_ISNULL(url_limit) && i < url_limits_.count(); ++i) {
      if (endpoint_url == url_limits_.at(i).endpoint_url_) {
        url_limit = &url_limits_.at(i);
      }
    }
    if (OB_NOT_NULL(url_limit) && configured_limit > 0
        && configured_limit != url_limit->concurrency_limit_) {
      const int64_t old_limit = url_limit->concurrency_limit_;
      url_limit->concurrency_limit_ = configured_limit;
      LOG_INFO("updated configured AI HTTP concurrency limit",
               K(endpoint_url), K(old_limit), K(configured_limit));
    }
    if (OB_ISNULL(url_limit) && create) {
      ObAIHttpUrlLimit new_limit;
      new_limit.concurrency_limit_ = configured_limit;
      if (OB_FAIL(ob_write_string(
              key_allocator_, endpoint_url, new_limit.endpoint_url_, true))) {
        LOG_WARN("failed to copy AI endpoint URL", K(ret), K(endpoint_url));
      } else if (OB_FAIL(url_limits_.push_back(new_limit))) {
        LOG_WARN("failed to create AI HTTP URL limit", K(ret),
                 K(endpoint_url));
      } else {
        url_limit = &url_limits_.at(url_limits_.count() - 1);
      }
    } else if (OB_ISNULL(url_limit)) {
      ret = OB_ENTRY_NOT_EXIST;
    }
    return ret;
  }

  ObArenaAllocator key_allocator_;
  ObThreadCond cond_;
  ObSEArray<ObAIHttpUrlLimit, 8> url_limits_;
  bool is_inited_;
};

ObAIHttpConcurrencyLimiter &get_ai_http_concurrency_limiter()
{
  static ObAIHttpConcurrencyLimiter limiter;
  return limiter;
}

#endif
} // namespace

const int64_t ObAIFuncClient::CURL_MAX_TIMEOUT_SEC = INT_MAX / 1000;

ObAIFuncClient::ObAIFuncClient() : allocator_(nullptr)
{
  is_finished_.store(false);
  abs_timeout_ts_ = 0;
  max_retry_times_ = 3;
  timeout_sec_ = 60;
  max_concurrency_ = AI_HTTP_DEFAULT_MAX_CONCURRENCY;
  acquired_batch_slots_ = 0;
}

ObAIFuncClient::~ObAIFuncClient()
{
  reset();
}

#ifdef __EMSCRIPTEN__
void ObAIFuncClient::clean_up()
{
  is_finished_.store(true);
}

void ObAIFuncClient::reset()
{
  clean_up();
}

int ObAIFuncClient::init(common::ObIAllocator &, const ObString &, ObArray<ObString> &)
{
  return OB_NOT_SUPPORTED;
}

int ObAIFuncClient::send_post(common::ObIAllocator &, const ObString &,
                              ObArray<ObString> &, ObIJsonBase *, ObIJsonBase *&)
{
  return OB_NOT_SUPPORTED;
}

int ObAIFuncClient::send_post_batch(common::ObIAllocator &, const ObString &,
                                    ObArray<ObString> &, ObArray<ObIJsonBase *> &,
                                    ObArray<ObIJsonBase *> &)
{
  return OB_NOT_SUPPORTED;
}

int ObAIFuncClient::send_post_batch_no_wait(ObArray<ObIJsonBase *> &)
{
  return OB_NOT_SUPPORTED;
}

bool ObAIFuncClient::check_batch_finished()
{
  return true;
}

int ObAIFuncClient::get_batch_result(ObArray<ObIJsonBase *> &)
{
  return OB_NOT_SUPPORTED;
}
#else
namespace
{
oceanbase::http::ClientConfig make_client_config(const int64_t timeout_sec)
{
  oceanbase::http::ClientConfig config;
  config.timeout_ms = static_cast<uint64_t>(timeout_sec) * 1000;
  config.connect_timeout_ms = 10000;
  config.https_only = false;
  config.follow_redirects = false;
  config.max_redirects = 0;
  config.verify_peer = true;
  config.verify_host = true;
  config.discard_body = false;
  config.accept_gzip = false;
  return config;
}

ObString response_body(const rust::Slice<const uint8_t> &body)
{
  return ObString(body.size(), reinterpret_cast<const char *>(body.data()));
}
} // namespace

void ObAIFuncClient::reset()
{
  clean_up();
  http_client_.reset();
  if (!endpoint_url_.empty() && OB_NOT_NULL(allocator_)) {
    allocator_->free(endpoint_url_.ptr());
  }
  endpoint_url_.reset();
  allocator_ = nullptr;
}

int ObAIFuncClient::init(common::ObIAllocator &allocator, const common::ObString &url,
                         ObArray<ObString> &headers)
{
  int ret = OB_SUCCESS;
  const int64_t remain_timeout_us = THIS_WORKER.is_timeout_ts_valid()
      ? THIS_WORKER.get_timeout_remain() : timeout_sec_ * 1000000;
  if (OB_ISNULL(url) || headers.empty()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    reset();
    allocator_ = &allocator;
    if (OB_FAIL(ob_write_string(allocator, url, endpoint_url_, true))) {
      LOG_WARN("failed to copy AI endpoint URL", K(ret));
    }
  }
  if (OB_SUCC(ret)) {
    timeout_sec_ = std::min(std::max(static_cast<int64_t>(1), remain_timeout_us / 1000000),
                            static_cast<int64_t>(CURL_MAX_TIMEOUT_SEC));
    abs_timeout_ts_ = remain_timeout_us + ObTimeUtility::current_time();
    http_client_.emplace(oceanbase::http::new_http_client(
        rust::Str(url.ptr(), url.length()), make_client_config(timeout_sec_)));
    for (uint32_t i = 0; OB_SUCC(ret) && i < headers.count(); ++i) {
      const auto code = oceanbase::http::http_client_add_header(
          *http_client_.value(), rust::Str(headers.at(i).ptr(), headers.at(i).length()));
      if (oceanbase::http::Code::Ok != code) {
        ret = OB_INVALID_ARGUMENT;
      } else {
        LOG_DEBUG("ai_function request header appended", K(i));
      }
    }
  }
  return ret;
}

int ObAIFuncClient::error_handle(const oceanbase::http::Result &result)
{
  int ret = OB_SUCCESS;
  if (oceanbase::http::Code::UrlMalformed == result.code) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("url malformat", K(result.transport_code));
    char message[] = "error url format";
    FORWARD_USER_ERROR(ret, message);
  } else {
    ret = OB_CURL_ERROR;
    LOG_WARN("curl error, error code is ", K(result.transport_code));
    char message[1024];
    snprintf(message, sizeof(message), "curl error, curlerror code is %d", result.transport_code);
    FORWARD_USER_ERROR(ret, message);
  }
  return ret;
}

int ObAIFuncClient::send_post(ObIJsonBase *data, ObIJsonBase *&response)
{
  int ret = OB_SUCCESS;
  int64_t acquired_slots = 0;
  ObJsonBuffer json(allocator_);
  if (OB_ISNULL(data) || !http_client_.has_value()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(data->print(json, false))) {
  } else if (OB_FAIL(get_ai_http_concurrency_limiter().acquire(
                 endpoint_url_, max_concurrency_, 1, acquired_slots))) {
    LOG_WARN("failed to acquire AI HTTP concurrency slot", K(ret));
  }
  if (OB_SUCC(ret)) {
    const ObString body = json.string();
    std::optional<rust::Box<oceanbase::http::HttpReply>> reply;
    std::optional<oceanbase::http::Result> last_result;
    for (int64_t i = 0; OB_SUCC(ret) && i <= max_retry_times_; ++i) {
      reply.emplace(oceanbase::http::http_post(
          *http_client_.value(), rust::Slice<const uint8_t>(
              reinterpret_cast<const uint8_t *>(body.ptr()), body.length())));
      last_result = oceanbase::http::http_reply_result(*reply.value());
      if (is_timeout()) {
        ret = OB_TIMEOUT;
      } else if (oceanbase::http::Code::Ok != last_result->code) {
        continue;
      } else if (is_retryable_status_code(last_result->http_status)) {
        LOG_WARN("retryable http status code", K(last_result->http_status), K(i));
        const int64_t delay_ms = 1000 * (1 << i) + rand() % 1000;
        ob_usleep(delay_ms * 1000);
      } else {
        break;
      }
    }
    if (OB_FAIL(ret)) {
    } else if (oceanbase::http::Code::Ok != last_result->code) {
      ret = error_handle(last_result.value());
    } else if (last_result->http_status / 100 != 2) {
      ret = OB_CURL_ERROR;
      char message[1024];
      snprintf(message, sizeof(message), "http status code: %u", last_result->http_status);
      FORWARD_USER_ERROR(ret, message);
    } else {
      ObIJsonBase *tree = nullptr;
      const auto body_bytes = oceanbase::http::http_reply_body(*reply.value());
      const ObString response_string = response_body(body_bytes);
      if (OB_FAIL(ObJsonBaseFactory::get_json_base(
              allocator_, response_string, ObJsonInType::JSON_TEXT, tree))) {
        ret = OB_ERR_INVALID_JSON_TEXT;
      } else {
        // Preserve valid JSON for diagnostics even when its shape is unexpected.
        response = tree;
        if (OB_ISNULL(tree) || tree->json_type() != ObJsonNodeType::J_OBJECT) {
          ret = OB_INVALID_DATA;
        }
      }
    }
  }
  if (acquired_slots > 0) {
    get_ai_http_concurrency_limiter().release(endpoint_url_, acquired_slots);
  }
  return ret;
}

int ObAIFuncClient::send_post(common::ObIAllocator &allocator, const common::ObString &url,
                              ObArray<ObString> &headers, ObIJsonBase *data,
                              ObIJsonBase *&response)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(data)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    reset();
    if (OB_FAIL(init(allocator, url, headers))) {
    } else if (OB_FAIL(send_post(data, response))) {
    }
  }
  return ret;
}

int ObAIFuncClient::add_http_request(ObIJsonBase *data)
{
  int ret = OB_SUCCESS;
  ObJsonBuffer json(allocator_);
  if (OB_ISNULL(data) || !http_batch_.has_value()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(data->print(json, false))) {
  } else {
    const ObString body = json.string();
    const auto result = oceanbase::http::http_batch_add(
        *http_batch_.value(), rust::Slice<const uint8_t>(
            reinterpret_cast<const uint8_t *>(body.ptr()), body.length()));
    if (oceanbase::http::Code::Ok != result.code) {
      ret = OB_CURL_ERROR;
      LOG_WARN("failed to add HTTP request", K(result.transport_code));
    }
  }
  return ret;
}

int ObAIFuncClient::send_post_batch(ObArray<ObIJsonBase *> &data_array,
                                    ObArray<ObIJsonBase *> &responses)
{
  int ret = OB_SUCCESS;
  if (data_array.empty() || !http_client_.has_value()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    clean_up();
    http_batch_.emplace(oceanbase::http::new_http_batch(*http_client_.value()));
    for (int64_t i = 0; OB_SUCC(ret) && i < data_array.count(); ++i) {
      if (OB_FAIL(add_http_request(data_array.at(i)))) {
      }
    }
    if (OB_SUCC(ret)) {
      auto progress = oceanbase::http::http_batch_perform(*http_batch_.value());
      if (oceanbase::http::Code::Ok != progress.code) {
        ret = OB_CURL_ERROR;
      }
      while (OB_SUCC(ret) && progress.running > 0) {
        const auto wait_result = oceanbase::http::http_batch_wait(*http_batch_.value(), 1000);
        if (oceanbase::http::Code::Ok != wait_result.code) {
          ret = OB_CURL_ERROR;
          break;
        }
        progress = oceanbase::http::http_batch_perform(*http_batch_.value());
        if (oceanbase::http::Code::Ok != progress.code) {
          ret = OB_CURL_ERROR;
        }
      }
      is_finished_.store(OB_SUCC(ret));
    }
    if (OB_SUCC(ret)) {
      ret = get_batch_result(responses);
    }
    clean_up();
  }
  return ret;
}

int ObAIFuncClient::send_post_batch(common::ObIAllocator &allocator,
                                    const common::ObString &url, ObArray<ObString> &headers,
                                    ObArray<ObIJsonBase *> &data_array,
                                    ObArray<ObIJsonBase *> &responses)
{
  int ret = OB_SUCCESS;
  if (data_array.empty()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(init(allocator, url, headers))) {
  }
  for (int64_t offset = 0; OB_SUCC(ret) && offset < data_array.count();) {
    int64_t acquired_slots = 0;
    ObArray<ObIJsonBase *> batch;
    if (OB_FAIL(get_ai_http_concurrency_limiter().acquire(
            endpoint_url_, max_concurrency_, data_array.count() - offset,
            acquired_slots))) {
      LOG_WARN("failed to acquire AI HTTP batch concurrency slots", K(ret));
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < acquired_slots; ++i) {
      if (OB_FAIL(batch.push_back(data_array.at(offset + i)))) {
        LOG_WARN("failed to build AI HTTP batch", K(ret));
      }
    }
    if (OB_SUCC(ret)) {
      const int64_t response_count_before_attempt = responses.count();
      for (int64_t i = 0; i <= max_retry_times_; ++i) {
        ret = send_post_batch(batch, responses);
        if (is_timeout()) {
          ret = OB_TIMEOUT;
        } else if (OB_CURL_ERROR == ret && i < max_retry_times_) {
          while (responses.count() > response_count_before_attempt) {
            responses.pop_back();
          }
          ret = OB_SUCCESS;
          const int64_t delay_ms = 1000 * (1 << i) + rand() % 1000;
          ob_usleep(delay_ms * 1000);
        } else {
          break;
        }
      }
    }
    if (acquired_slots > 0) {
      get_ai_http_concurrency_limiter().release(endpoint_url_, acquired_slots);
    }
    if (OB_SUCC(ret)) {
      offset += acquired_slots;
    }
  }
  return ret;
}

int ObAIFuncClient::send_post_batch_no_wait(ObArray<ObIJsonBase *> &data_array)
{
  int ret = OB_SUCCESS;
  if (data_array.empty() || !http_client_.has_value()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    clean_up();
    if (OB_FAIL(get_ai_http_concurrency_limiter().acquire(
            endpoint_url_, max_concurrency_, data_array.count(),
            acquired_batch_slots_, false))) {
      LOG_WARN("failed to acquire no-wait AI HTTP batch concurrency slots", K(ret));
    } else {
      http_batch_.emplace(oceanbase::http::new_http_batch(*http_client_.value()));
      for (int64_t i = 0; OB_SUCC(ret) && i < data_array.count(); ++i) {
        if (OB_FAIL(add_http_request(data_array.at(i)))) {
        }
      }
      if (OB_SUCC(ret)) {
        const auto progress = oceanbase::http::http_batch_perform(*http_batch_.value());
        if (oceanbase::http::Code::Ok != progress.code) {
          ret = OB_CURL_ERROR;
        } else {
          is_finished_.store(progress.running == 0);
          if (is_finished_.load()) {
            release_batch_slots();
          }
        }
      }
      if (OB_FAIL(ret)) {
        clean_up();
      }
    }
  }
  return ret;
}

bool ObAIFuncClient::check_batch_finished()
{
  if (!is_finished_.load() && http_batch_.has_value()) {
    const auto progress = oceanbase::http::http_batch_perform(*http_batch_.value());
    if (oceanbase::http::Code::Ok != progress.code) {
      const int ret = OB_CURL_ERROR;
      LOG_WARN("failed to drive HTTP batch", K(progress.transport_code));
      // Destroy in-flight requests before making their concurrency slots reusable.
      clean_up();
      is_finished_.store(true);
    } else {
      is_finished_.store(progress.running == 0);
      if (is_finished_.load()) {
        release_batch_slots();
      }
    }
  }
  return is_finished_.load();
}

int ObAIFuncClient::get_batch_result(ObArray<ObIJsonBase *> &responses)
{
  int ret = OB_SUCCESS;
  if (!is_finished_.load() || !http_batch_.has_value()) {
    ret = OB_NOT_SUPPORTED;
  } else {
    const size_t count = oceanbase::http::http_batch_size(*http_batch_.value());
    for (size_t i = 0; OB_SUCC(ret) && i < count; ++i) {
      const auto result = oceanbase::http::http_batch_result(*http_batch_.value(), i);
      if (oceanbase::http::Code::Ok != result.code || result.http_status != 200) {
        ret = OB_CURL_ERROR;
      }
    }
    for (size_t i = 0; OB_SUCC(ret) && i < count; ++i) {
      ObIJsonBase *tree = nullptr;
      const auto body = oceanbase::http::http_batch_body(*http_batch_.value(), i);
      const ObString response_string = response_body(body);
      if (OB_FAIL(ObJsonBaseFactory::get_json_base(
              allocator_, response_string, ObJsonInType::JSON_TEXT, tree))) {
        ret = OB_ERR_INVALID_JSON_TEXT;
      } else if (OB_FAIL(responses.push_back(static_cast<ObIJsonBase *>(tree)))) {
      }
    }
  }
  return ret;
}

void ObAIFuncClient::release_batch_slots()
{
  if (acquired_batch_slots_ > 0) {
    get_ai_http_concurrency_limiter().release(endpoint_url_, acquired_batch_slots_);
    acquired_batch_slots_ = 0;
  }
}

void ObAIFuncClient::clean_up()
{
  http_batch_.reset();
  release_batch_slots();
  is_finished_.store(false);
}
#endif

bool ObAIFuncClient::is_retryable_status_code(int64_t http_code)
{
  return http_code == 429 || http_code == 500 || http_code == 502 ||
         http_code == 503 || http_code == 504;
}

bool ObAIFuncClient::is_timeout()
{
  return ObTimeUtility::current_time() > abs_timeout_ts_;
}

} // namespace common
} // namespace oceanbase
