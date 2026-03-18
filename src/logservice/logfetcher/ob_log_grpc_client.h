/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this software except in compliance with the License.
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

#ifndef OCEANBASE_LOGSERVICE_LOGFETCHER_OB_LOG_GRPC_CLIENT_H_
#define OCEANBASE_LOGSERVICE_LOGFETCHER_OB_LOG_GRPC_CLIENT_H_

#include "lib/ob_define.h"
#include "lib/net/ob_addr.h"
#include "grpc/ob_grpc_context.h"
#include "logservice/cdcservice/ob_cdc_req.h"
#include "lib/oblog/ob_log_module.h"
#include <memory>

namespace oceanbase
{
namespace logfetcher
{

// Async callback for unary RPC
// For async unary RPC, callback receives a single response
// Response type should be the proto message type
template<typename Response>
class ObGrpcAsyncCallback
{
public:
  ObGrpcAsyncCallback() : status_() {}
  virtual ~ObGrpcAsyncCallback() = default;

  // Called when response is received (successfully)
  // Response data is passed as parameter
  virtual void on_success(const Response& data) = 0;

  // Called when RPC encounters an error
  virtual void on_error(const grpc::Status& status) {
    status_ = status;
  }

  // Get final status
  const grpc::Status& get_status() const { return status_; }
  bool is_success() const { return status_.ok(); }

protected:
  grpc::Status status_;
};

} // logfetcher
} // oceanbase

#endif /* OCEANBASE_LOGSERVICE_LOGFETCHER_OB_LOG_GRPC_CLIENT_H_ */
