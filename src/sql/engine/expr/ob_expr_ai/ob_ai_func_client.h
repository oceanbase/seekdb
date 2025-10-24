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

#ifndef OB_AI_FUNC_CLIENT_H_
#define OB_AI_FUNC_CLIENT_H_

#include <atomic>
#include <curl/curl.h>
#include "ob_ai_func.h"

namespace oceanbase
{
namespace common
{
class ObAIFuncClient: public ObAIFuncHandle
{
public:
  ObAIFuncClient();
  virtual ~ObAIFuncClient();
  int init(common::ObIAllocator &allocator, ObString &url, ObArray<ObString> &headers);
  void clean_up();
  void reset();
  void set_timeout_sec(int64_t timeout_sec) { timeout_sec_ = timeout_sec; }
  // ai function interface
  virtual int send_post(common::ObIAllocator &allocator,
                        ObString &url,
                        ObArray<ObString> &headers,
                        ObJsonObject *data,
                        ObJsonObject *&response) override;
  virtual int send_post_batch(common::ObIAllocator &allocator,
                              ObString &url,
                              ObArray<ObString> &headers,
                              ObArray<ObJsonObject *> &data_array,
                              ObArray<ObJsonObject *> &responses) override;
  // embedding service interface
  int send_post_batch_no_wait(ObArray<ObJsonObject *> &data_array);
  bool check_batch_finished();
  int get_batch_result(ObArray<ObJsonObject *> &responses);
private:
  int error_handle(CURLcode res);
  int send_post(ObJsonObject *data, ObJsonObject *&response);
  int send_post_batch(ObArray<ObJsonObject *> &data_array, ObArray<ObJsonObject *> &responses);
  int init_easy_handle(CURL *curl, ObJsonObject *data, ObStringBuffer &response_buf);
  static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp);
  bool is_retryable_status_code(int64_t http_code);
  bool is_timeout();
private:
  static const int64_t CURL_MAX_TIMEOUT_SEC;
  common::ObIAllocator *allocator_;
  char *url_;
  struct curl_slist *header_list_;
  CURLM *curlm_;
  CURL *curl_;
  ObArray<CURL *> curl_handles_;
  ObArray<ObStringBuffer *> response_buffers_;
  // atomic boolean value, used to check if the batch task is finished
  std::atomic<bool> is_finished_;
  int64_t max_retry_times_;
  int64_t abs_timeout_ts_;
  int64_t timeout_sec_;
  DISALLOW_COPY_AND_ASSIGN(ObAIFuncClient);
};

} // namespace common
} // namespace oceanbase

#endif /* OB_AI_FUNC_CLIENT_H_ */
