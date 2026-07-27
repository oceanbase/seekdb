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

#include "ob_gti_source.h"
#include "ob_trans_id_service.h"
#include "share/ob_server_struct.h"
#include "share/rc/ob_module_provider.h"

namespace oceanbase
{
using namespace common;
using namespace share;

namespace transaction
{

const int64_t ObGtiSource::TRANS_ID_RANGE_SIZE;

int ObGtiSource::init()
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    TRANS_LOG(WARN, "init twice", KR(ret));
  } else {
    is_inited_ = true;
    TRANS_LOG(INFO, "gti source init success", KP(this));
  }

  return ret;
}

int ObGtiSource::start()
{
  int ret = OB_SUCCESS;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "ObGtiSource is not inited", KR(ret));
  } else if (is_running_) {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(ERROR, "ObGtiSource is already running", KR(ret));
  } else {
    is_running_ = true;
    TRANS_LOG(INFO, "ObGtiSource start success");
  }
  return ret;
}

void ObGtiSource::stop()
{
  int ret = OB_SUCCESS;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "ObGtiSource is not inited", KR(ret));
  } else {
    is_running_ = false;
    TRANS_LOG(INFO, "ObGtiSource stop success");
  }
}

void ObGtiSource::wait()
{
  int ret = OB_SUCCESS;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "ObGtiSource is not inited", KR(ret));
  } else if (is_running_) {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(ERROR, "ObGtiSource is running", KR(ret));
  } else {
    TRANS_LOG(INFO, "ObGtiSource wait success");
  }
}

void ObGtiSource::destroy()
{
  if (is_inited_) {
    if (is_running_) {
      stop();
      wait();
    }
    is_inited_ = false;
  }
  reset();
  TRANS_LOG(INFO, "ObGtiSource destroyed");
}

void ObGtiSource::reset()
{
  is_inited_ = false;
  is_running_ = false;
  next_id_ = 0;
  end_id_ = 0;
}

int ObGtiSource::get_trans_id(int64_t &trans_id)
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "ObGtiSource is not inited", KR(ret));
  } else if (!is_running_) {
    ret = OB_NOT_RUNNING;
    TRANS_LOG(WARN, "ObGtiSource is not running", KR(ret));
  } else {
    while (OB_SUCC(ret)) {
      const int64_t tmp_end_id = ATOMIC_LOAD(&end_id_);
      const int64_t tmp_next_id = ATOMIC_LOAD(&next_id_);
      int64_t tmp_trans_id = 0;
      if (tmp_next_id < tmp_end_id &&
          (tmp_trans_id = ATOMIC_FAA(&next_id_, 1)) < tmp_end_id) {
        trans_id = tmp_trans_id;
        break;
      } else if (OB_FAIL(refill_trans_id_range_())) {
        if (OB_EAGAIN != ret) {
          TRANS_LOG(WARN, "refill trans id range failed", KR(ret));
        }
      }
    }
  }
  return ret;
}

int ObGtiSource::refill_trans_id_range_()
{
  int ret = OB_SUCCESS;
  ObLatchWGuard guard(lock_, ObLatchIds::GTI_SOURCE_LOCK);
  const int64_t cur_next_id = ATOMIC_LOAD(&next_id_);
  const int64_t cur_end_id = ATOMIC_LOAD(&end_id_);
  if (cur_next_id < cur_end_id) {
    // Another thread has already refilled the local range.
  } else {
    int64_t tmp_trans_id = 0;
    int64_t tmp_end_id = 0;
    if (OB_ISNULL(share::g_mp) || OB_ISNULL(share::g_mp->trans_id_service())) {
      ret = OB_ERR_UNEXPECTED;
      TRANS_LOG(WARN, "trans id service is null", KR(ret), KP(share::g_mp));
    } else if (OB_FAIL(share::g_mp->trans_id_service()->alloc_trans_id_range(
        TRANS_ID_RANGE_SIZE, tmp_trans_id, tmp_end_id))) {
      if (OB_EAGAIN != ret) {
        TRANS_LOG(WARN, "alloc trans id range failed", KR(ret), K(TRANS_ID_RANGE_SIZE));
      }
    } else if (OB_UNLIKELY(tmp_trans_id >= tmp_end_id)) {
      ret = OB_ERR_UNEXPECTED;
      TRANS_LOG(WARN, "invalid trans id range", KR(ret), K(tmp_trans_id), K(tmp_end_id));
    } else {
      ATOMIC_STORE(&next_id_, tmp_trans_id);
      ATOMIC_STORE(&end_id_, tmp_end_id);
      TRANS_LOG(INFO, "refill trans id range", K(tmp_trans_id), K(tmp_end_id));
    }
  }
  return ret;
}

}
}
