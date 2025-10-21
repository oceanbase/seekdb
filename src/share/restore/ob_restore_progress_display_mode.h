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

#ifndef OCENABASE_SHARE_OB_RESTORE_PROGRESS_DISPLAY_MODE_H
#define OCENABASE_SHARE_OB_RESTORE_PROGRESS_DISPLAY_MODE_H

#include <stdint.h>
#include "lib/utility/ob_print_utils.h"

namespace oceanbase
{
namespace share
{
struct ObRestoreProgressDisplayMode final
{
  OB_UNIS_VERSION(1);
public:
  enum Mode
  {
    TABLET_CNT = 0,
    BYTES = 1,
    MAX
  };

public:
  ObRestoreProgressDisplayMode() : mode_(Mode::TABLET_CNT) {}
  ~ObRestoreProgressDisplayMode() = default;
  explicit ObRestoreProgressDisplayMode(const Mode &mode) : mode_(mode) {}
  explicit ObRestoreProgressDisplayMode(const ObString &str);
  ObRestoreProgressDisplayMode &operator=(const ObRestoreProgressDisplayMode &mode);
  ObRestoreProgressDisplayMode &operator=(const Mode &mode);
  constexpr bool is_valid() const { return TABLET_CNT <= mode_ && mode_ < MAX; }
  bool is_tablet_cnt() const { return Mode::TABLET_CNT == mode_; }
  bool is_bytes() const { return Mode::BYTES == mode_; }
  const char *to_str() const;
  TO_STRING_KV("restore progress display mode", to_str());

private:
  int64_t mode_;
};
static const ObRestoreProgressDisplayMode TABLET_CNT_DISPLAY_MODE(ObRestoreProgressDisplayMode::Mode::TABLET_CNT);
static const ObRestoreProgressDisplayMode BYTES_DISPLAY_MODE(ObRestoreProgressDisplayMode::Mode::BYTES);
} //share
} //oceanbase

#endif
