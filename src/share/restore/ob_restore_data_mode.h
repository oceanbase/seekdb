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

#ifndef OCENABASE_SHARE_OB_RESTORE_DATA_MODE_H
#define OCENABASE_SHARE_OB_RESTORE_DATA_MODE_H

#include <stdint.h>
#include "lib/utility/ob_print_utils.h"

namespace oceanbase
{
namespace share
{
class ObRestoreDataMode final
{
  OB_UNIS_VERSION(1);
public:
  enum Mode
  {
    // tenant restoring from whole data, default retore mode
    NORMAL = 0,
    // just restore clog, minor and major macro blocks are in remote reference state
    REMOTE = 1,
    RESTORE_DATA_MODE_MAX
  };

public:
  ObRestoreDataMode() : mode_(Mode::NORMAL) {}
  ~ObRestoreDataMode() = default;
  explicit ObRestoreDataMode(const Mode &mode) : mode_(mode) {}
  explicit ObRestoreDataMode(const ObString &str);
  ObRestoreDataMode &operator=(const ObRestoreDataMode &restore_mode);
  ObRestoreDataMode &operator=(const Mode &mode);
  constexpr bool is_valid() const { return mode_ >= Mode::NORMAL && mode_ < Mode::RESTORE_DATA_MODE_MAX;}
  void set_invalid() { mode_ = Mode::RESTORE_DATA_MODE_MAX; }
  bool is_remote_mode() const { return Mode::REMOTE == mode_; }
  bool is_normal_mode() const { return Mode::NORMAL == mode_; }
  bool is_same_mode(const ObRestoreDataMode &other) const { return mode_ == other.mode_; }
  void reset() { mode_ = Mode::NORMAL; }
  const char *to_str() const;
  TO_STRING_KV("restore data mode", to_str());

public:
  int64_t mode_;
};

static const ObRestoreDataMode NORMAL_RESTORE_DATA_MODE(ObRestoreDataMode::Mode::NORMAL);
static const ObRestoreDataMode REMOTE_RESTORE_DATA_MODE(ObRestoreDataMode::Mode::REMOTE);

} //share
} //oceanbase

#endif
