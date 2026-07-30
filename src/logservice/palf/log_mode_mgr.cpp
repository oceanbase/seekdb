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

#define USING_LOG_PREFIX PALF
#include "log_mode_mgr.h"

namespace oceanbase
{
using namespace common;
using namespace share;

namespace palf
{
LogModeMgr::LogModeMgr()
    : is_inited_(false),
      self_(),
      applied_mode_meta_()
{}

int LogModeMgr::init(const common::ObAddr &self, const LogModeMeta &log_mode_meta)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
  } else if (!self.is_valid() || !log_mode_meta.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    self_ = self;
    applied_mode_meta_ = log_mode_meta;
    is_inited_ = true;
  }
  return ret;
}

void LogModeMgr::destroy()
{
  is_inited_ = false;
  self_.reset();
  applied_mode_meta_.reset();
}

int LogModeMgr::get_access_mode(AccessMode &access_mode) const
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else {
    access_mode = applied_mode_meta_.access_mode_;
  }
  return ret;
}

int LogModeMgr::get_access_mode_ref_scn(AccessMode &access_mode,
                                        SCN &ref_scn) const
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else {
    access_mode = applied_mode_meta_.access_mode_;
    ref_scn = applied_mode_meta_.ref_scn_;
  }
  return ret;
}

bool LogModeMgr::can_append() const
{
  return applied_mode_meta_.access_mode_ == AccessMode::APPEND;
}

} // namespace palf
} // namespace oceanbase
