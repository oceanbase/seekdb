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

#include "palf_handle_impl_guard.h"
#include "palf_env_impl.h"

namespace oceanbase
{
using namespace common;
namespace palf
{

IPalfHandleImplGuard::IPalfHandleImplGuard() : palf_id_(),
                                               palf_handle_impl_(NULL),
                                               palf_env_impl_(NULL)
{
}

IPalfHandleImplGuard::~IPalfHandleImplGuard()
{
  reset();
}

bool IPalfHandleImplGuard::is_valid() const
{
  return true == is_valid_palf_id(palf_id_) && NULL != palf_handle_impl_ && NULL != palf_env_impl_;
}

void IPalfHandleImplGuard::reset()
{ 
  if (NULL != palf_handle_impl_ && NULL != palf_env_impl_) {
    palf_env_impl_->revert_palf_handle_impl(palf_handle_impl_); 
  } 
  palf_handle_impl_ = NULL;  
  palf_env_impl_ = NULL;
  palf_id_ = -1;
};

} // end namespace palf
} // end namespace oceanbase
