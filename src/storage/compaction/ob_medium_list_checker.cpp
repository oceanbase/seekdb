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
#define USING_LOG_PREFIX STORAGE_COMPACTION
#include "storage/compaction/ob_medium_list_checker.h"
#include "storage/compaction/ob_extra_medium_info.h"
#include "storage/compaction/ob_medium_compaction_info.h"

namespace oceanbase
{
namespace compaction
{
ERRSIM_POINT_DEF(EN_SKIP_CHECK_MEDIUM_LIST);

int ObMediumListChecker::validate_medium_info_list(
    const ObExtraMediumInfo &extra_info,
    const MediumInfoArray *medium_info_array,
    const int64_t last_major_snapshot)
{
  int ret = OB_SUCCESS;
  bool skip_validate = false;
#ifdef ERRSIM
  ret = EN_SKIP_CHECK_MEDIUM_LIST ? : OB_SUCCESS;
  if (OB_FAIL(ret)) {
    ret = OB_SUCCESS;
    skip_validate = true;
    FLOG_INFO("EN_SKIP_CHECK_MEDIUM_LIST, skip check medium info list",
              K(ret), K(last_major_snapshot), K(extra_info), KPC(medium_info_array));
  }
#endif

  if (skip_validate) {
    // do nothing
  } else if (OB_FAIL(inner_check_medium_list(extra_info, medium_info_array, last_major_snapshot))) {
    LOG_WARN("failed to inner check medium info list", K(ret));
  }
  return ret;
}

int ObMediumListChecker::inner_check_medium_list(
    const ObExtraMediumInfo &extra_info,
    const MediumInfoArray *medium_info_array,
    const int64_t last_major_snapshot)
{
  int ret = OB_SUCCESS;
  int64_t next_medium_info_idx = 0;

  if (OB_FAIL(check_extra_info(extra_info, last_major_snapshot))) {
    LOG_WARN("failed to check extra info", KR(ret), K(last_major_snapshot), K(extra_info));
  } else if (nullptr == medium_info_array || medium_info_array->empty()) {
    // do nothing
  } else if (OB_FAIL(filter_finish_medium_info(*medium_info_array, last_major_snapshot, next_medium_info_idx))) {
    LOG_WARN("failed to filter finish medium info", KR(ret), K(last_major_snapshot), K(next_medium_info_idx));
  } else if (next_medium_info_idx >= medium_info_array->count()) {
    // do nothing
  } else if (OB_FAIL(check_continue(*medium_info_array, next_medium_info_idx))) {
    LOG_WARN("failed to check medium list continue", KR(ret), K(last_major_snapshot), KPC(medium_info_array));
  } else if (OB_FAIL(check_next_schedule_medium(*medium_info_array->at(next_medium_info_idx), last_major_snapshot))) {
    LOG_WARN("failed to check next schedule medium info", KR(ret), K(last_major_snapshot), KPC(medium_info_array), K(next_medium_info_idx));
  }
  return ret;
}

int ObMediumListChecker::check_continue(
    const MediumInfoArray &medium_info_array,
    const int64_t start_check_idx)
{
  int ret = OB_SUCCESS;
  const ObMediumCompactionInfo *first_info = nullptr;
  if (medium_info_array.empty()) {
    // do nothing
  } else if (OB_UNLIKELY(start_check_idx >= medium_info_array.count()
      || nullptr == (first_info = medium_info_array.at(start_check_idx)))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(start_check_idx), K(medium_info_array), KPC(first_info));
  } else {
    int64_t prev_medium_snapshot = first_info->medium_snapshot_;
    const ObMediumCompactionInfo *info = nullptr;
    for (int64_t idx = start_check_idx + 1; OB_SUCC(ret) && idx < medium_info_array.count(); ++idx) {
      info = medium_info_array.at(idx);
      if (OB_ISNULL(info)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("medium info ist null", K(ret), KPC(info), K(idx), K(medium_info_array));
      } else if (OB_UNLIKELY(prev_medium_snapshot != info->last_medium_snapshot_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("medium info list is not continuous", K(ret), K(prev_medium_snapshot),
            "last_medium_snapshot", info->last_medium_snapshot_,
            K(medium_info_array));
      } else {
        prev_medium_snapshot = info->medium_snapshot_;
      }
    } // end of for
  }
  return ret;
}

int ObMediumListChecker::filter_finish_medium_info(
    const MediumInfoArray &medium_info_array,
    const int64_t last_major_snapshot,
    int64_t &next_medium_info_idx)
{
  int ret = OB_SUCCESS;
  next_medium_info_idx = 0;

  const ObMediumCompactionInfo *info = nullptr;
  int64_t idx = 0;
  for ( ; OB_SUCC(ret) && idx < medium_info_array.count(); ++idx) {
    info = medium_info_array.at(idx);
    if (OB_ISNULL(info)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("medium info ist null", K(ret), KPC(info), K(idx), K(medium_info_array));
    } else if (info->medium_snapshot_ > last_major_snapshot) {
      break;
    }
  } // end of for
  if (OB_SUCC(ret)) {
    next_medium_info_idx = idx;
  }
  return ret;
}

int ObMediumListChecker::check_extra_info(
  const ObExtraMediumInfo &extra_info,
  const int64_t last_major_snapshot)
{
  int ret = OB_SUCCESS;
  if (last_major_snapshot > 0 && extra_info.last_medium_scn_ > 0) {
    if (OB_UNLIKELY(extra_info.last_medium_scn_ != last_major_snapshot)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("medium list is invalid for last major sstable", K(ret), K(extra_info), K(last_major_snapshot));
    }
  }
  return ret;
}

int ObMediumListChecker::check_next_schedule_medium(
    const ObMediumCompactionInfo &next_medium_info,
    const int64_t last_major_snapshot)
{
  int ret = OB_SUCCESS;
  if (last_major_snapshot > 0 &&
      next_medium_info.medium_snapshot_ > last_major_snapshot) {
    if (OB_UNLIKELY(next_medium_info.last_medium_snapshot_ != last_major_snapshot)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("last medium snapshot in medium info is not equal to last "
                "major sstable, medium info may lost",
                KR(ret), K(next_medium_info), K(last_major_snapshot));
    }
  }
  return ret;
}

} // namespace compaction
} // namespace oceanbase
