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

#define USING_LOG_PREFIX SHARE
#include "share/backup/ob_archive_mode.h"

namespace oceanbase
{
namespace share 
{

/**
 * ------------------------------ObArchiveMode---------------------
 */
OB_SERIALIZE_MEMBER(ObArchiveMode, mode_);

static const char *OB_ARCHIVE_MODE_STR[] = {"INVALID", "ARCHIVELOG", "NOARCHIVELOG"};

bool ObArchiveMode::is_valid() const
{
  return Mode::INVALID < mode_ && Mode::MAX_MODE > mode_;
}

const char* ObArchiveMode::to_str() const
{
  STATIC_ASSERT(ARRAYSIZEOF(OB_ARCHIVE_MODE_STR) == MAX_MODE, "array size mismatch");
  const char *str = OB_ARCHIVE_MODE_STR[0];
  if (OB_UNLIKELY(mode_ >= ARRAYSIZEOF(OB_ARCHIVE_MODE_STR)
                  || mode_ < Mode::INVALID)) {
    LOG_ERROR_RET(OB_ERR_UNEXPECTED, "fatal error, unknown archive mode", K_(mode));
  } else {
    str = OB_ARCHIVE_MODE_STR[mode_];
  }
  return str;
}

ObArchiveMode::ObArchiveMode(const ObString &str)
{
  mode_ = Mode::INVALID;
  if (str.empty()) {
  } else {
    for (int64_t i = 0; i < ARRAYSIZEOF(OB_ARCHIVE_MODE_STR); i++) {
      if (0 == str.case_compare(OB_ARCHIVE_MODE_STR[i])) {
        mode_ = i;
        break;
      }
    }
  }

  if (Mode::INVALID == mode_) {
    LOG_WARN_RET(OB_INVALID_ARGUMENT, "invalid archive mode", K_(mode), K(str));
  }
}

}
}
