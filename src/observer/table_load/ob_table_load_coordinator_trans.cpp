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

#define USING_LOG_PREFIX SERVER

#include "observer/table_load/ob_table_load_coordinator_trans.h"
#include "observer/table_load/ob_table_load_coordinator.h"
#include "observer/table_load/ob_table_load_trans_bucket_writer.h"
#include "observer/table_load/ob_table_load_table_ctx.h"

namespace oceanbase
{
namespace observer
{
using namespace common;
using namespace lib;
using namespace table;

ObTableLoadCoordinatorTrans::ObTableLoadCoordinatorTrans(ObTableLoadTransCtx *trans_ctx, int32_t default_session_id)
  : trans_ctx_(trans_ctx),
    default_session_id_(default_session_id),
    trans_bucket_writer_(nullptr),
    ref_count_(0),
    is_dirty_(false),
    is_inited_(false)
{
}

ObTableLoadCoordinatorTrans::~ObTableLoadCoordinatorTrans()
{
  if (nullptr != trans_bucket_writer_) {
    trans_bucket_writer_->~ObTableLoadTransBucketWriter();
    trans_ctx_->allocator_.free(trans_bucket_writer_);
    trans_bucket_writer_ = nullptr;
  }
}

int ObTableLoadCoordinatorTrans::init()
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObTableLoadCoordinatorTrans init twice", KR(ret), KP(this));
  } else {
    if (OB_ISNULL(trans_bucket_writer_ =
                    OB_NEWx(ObTableLoadTransBucketWriter, (&trans_ctx_->allocator_), trans_ctx_))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to new ObTableLoadTransBucketWriter", KR(ret));
    } else if (OB_FAIL(trans_bucket_writer_->init())) {
      LOG_WARN("fail to init trans bucket writer", KR(ret));
    } else if (OB_FAIL(set_trans_status_inited())) {
      LOG_WARN("fail to set trans status inited", KR(ret));
    } else {
      is_inited_ = true;
    }
  }
  return ret;
}

int ObTableLoadCoordinatorTrans::advance_trans_status(ObTableLoadTransStatusType trans_status)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(trans_ctx_->advance_trans_status(trans_status))) {
    LOG_WARN("fail to advance trans status", KR(ret), K(trans_status));
  } else {
    table_load_trans_status_to_string(trans_status,
                                      trans_ctx_->ctx_->job_stat_->coordinator_.trans_status_);
  }
  return ret;
}

int ObTableLoadCoordinatorTrans::set_trans_status_error(int error_code)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(trans_ctx_->set_trans_status_error(error_code))) {
    LOG_WARN("fail to set trans status error", KR(ret));
  } else {
    table_load_trans_status_to_string(ObTableLoadTransStatusType::ERROR,
                                      trans_ctx_->ctx_->job_stat_->coordinator_.trans_status_);
  }
  return ret;
}

int ObTableLoadCoordinatorTrans::set_trans_status_abort()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(trans_ctx_->set_trans_status_abort())) {
    LOG_WARN("fail to set trans status abort", KR(ret));
  } else {
    table_load_trans_status_to_string(ObTableLoadTransStatusType::ABORT,
                                      trans_ctx_->ctx_->job_stat_->coordinator_.trans_status_);
  }
  return ret;
}

}  // namespace observer
}  // namespace oceanbase
