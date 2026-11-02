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

#define USING_LOG_PREFIX SQL_DAS
#include "ob_das_id_rpc.h"
#include "ob_das_id_service.h"
#include "share/location_cache/ob_location_service.h"

namespace oceanbase
{
using namespace common;
using namespace share;
namespace sql
{

OB_SERIALIZE_MEMBER(ObDASIDRequest, range_);

int ObDASIDRequest::init(const int64_t range)
{
  int ret = OB_SUCCESS;
  if (!true || 0 >= range) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(range));
  } else {
    range_ = range;
  }
  return ret;
}

bool ObDASIDRequest::is_valid() const
{
  return true && range_ > 0;
}

ObDASIDRequestRpc::ObDASIDRequestRpc()
  : is_inited_(false),
    is_running_(false),
    self_(),
    local_id_counter_(1)  // Start from 1 to satisfy ObDASIDRpcResult validation (start_id > 0)
{
}

int ObDASIDRequestRpc::init(const common::ObAddr &self,
                            ObDASIDCache *id_cache)
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("das id request rpc inited twice", KR(ret));
  } else if (!self.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(self));
  } else {
    self_ = self;
    is_inited_ = true;
  }
  return ret;
}

void ObDASIDRequestRpc::destroy()
{
  if (is_inited_) {
    self_.reset();
    local_id_counter_ = 1;  // Reset to 1 for next init
    is_inited_ = false;
  }
}

int ObDASIDRequestRpc::fetch_new_range(const ObDASIDRequest &msg,
                                       obcall::ObDASIDRpcResult &res,
                                       const int64_t timeout,
                                       const bool force_renew)
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("das id request rpc not inited", KR(ret));
  } else if (!msg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid request", KR(ret), K(msg));
  } else {
    // Use local auto-increment counter instead of RPC
    // Counter resets to 1 on restart, no persistence needed
    const int64_t range = msg.get_range();
    const int64_t start_id = ATOMIC_FAA(&local_id_counter_, range);
    const int64_t end_id = start_id + range;
    
    
    if (OB_FAIL(res.init(OB_SUCCESS, start_id, end_id))) {
      LOG_WARN("init das id result failed", KR(ret), K(start_id), K(end_id));
    } else {
      LOG_TRACE("fetch new DAS ID range from local counter", K(msg), K(res));
    }
  }
  return ret;
}
} // namespace sql

namespace obcall
{

OB_SERIALIZE_MEMBER(ObDASIDRpcResult, status_, start_id_, end_id_);

int ObDASIDRpcResult::init(const int status,
                           const int64_t start_id,
                           const int64_t end_id)
{
  int ret = OB_SUCCESS;
  if (!true ||
      (OB_SUCCESS == status && (0 >= start_id || 0 >= end_id))) {
    // when status is OB_SUCCESS, RPC should have succeeded with valid start id and end id
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(start_id), K(end_id));
  } else {
    status_ = status;
    start_id_ = start_id;
    end_id_ = end_id;
  }
  return ret;
}

bool ObDASIDRpcResult::is_valid() const
{
  // when status is not OB_SUCCESS,
  // RPC has failed due to some error on the server side, e.g. NOT_MASTER
  // otherwise, RPC should have succeeded with valid start id and end id
  return true &&
         (OB_SUCCESS != status_ || (start_id_ > 0 && end_id_ > 0));
}

} // namespace obcall
} // namespace oceanbase
