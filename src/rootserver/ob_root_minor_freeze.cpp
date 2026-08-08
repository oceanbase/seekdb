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

#define USING_LOG_PREFIX RS

#include "ob_root_minor_freeze.h"
#include "rootserver/ob_rootserver_local_runtime.h"

namespace oceanbase
{
using namespace common;
using namespace share;

namespace rootserver
{

ObRootMinorFreeze::ObRootMinorFreeze()
  : inited_(false), stopped_(false)
{}

ObRootMinorFreeze::~ObRootMinorFreeze()
{
  destroy();
}

int ObRootMinorFreeze::init()
{
  int ret = OB_SUCCESS;
  if (inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("minor freeze service is already initialized", KR(ret));
  } else {
    inited_ = true;
    stopped_ = false;
  }
  return ret;
}

void ObRootMinorFreeze::start()
{
  ATOMIC_STORE(&stopped_, false);
}

void ObRootMinorFreeze::stop()
{
  ATOMIC_STORE(&stopped_, true);
}

int ObRootMinorFreeze::destroy()
{
  inited_ = false;
  return OB_SUCCESS;
}

int ObRootMinorFreeze::check_cancel() const
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("minor freeze service is not initialized", KR(ret));
  } else if (ATOMIC_LOAD(&stopped_)) {
    ret = OB_CANCELED;
    LOG_WARN("rs is stopped", K(ret));
  }
  return ret;
}

int ObRootMinorFreeze::try_minor_freeze(const obcall::ObMinorFreezeArg &arg) const
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_cancel())) {
    LOG_WARN("minor freeze canceled", K(ret));
  } else {
    obcall::Int64 result;
    if (OB_FAIL(rootserver_local_runtime()->minor_freeze(arg, result))) {
      LOG_WARN("local minor freeze failed", K(ret), K(arg));
    } else if (OB_FAIL(static_cast<int>(result))) {
      LOG_WARN("local minor freeze returned error", K(ret), K(arg));
    }
  }

  return ret;
}

} // namespace rootserver
} // namespace oceanbase
