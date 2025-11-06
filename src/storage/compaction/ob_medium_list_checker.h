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
#ifndef OB_STORAGE_COMPACTION_MEDIUM_LIST_CHECKER_H_
#define OB_STORAGE_COMPACTION_MEDIUM_LIST_CHECKER_H_
#include "/usr/include/stdint.h"
#include "lib/container/ob_iarray.h"
namespace oceanbase
{
namespace compaction
{
struct ObMediumCompactionInfo;
class ObExtraMediumInfo;
struct ObMediumListChecker
{
public:
  typedef common::ObIArray<compaction::ObMediumCompactionInfo*> MediumInfoArray;
  static int validate_medium_info_list(
    const ObExtraMediumInfo &extra_info,
    const MediumInfoArray *medium_info_array,
    const int64_t last_major_snapshot);
  static int check_next_schedule_medium(
    const ObMediumCompactionInfo &next_schedule_info,
    const int64_t last_major_snapshot,
    const bool force_check = true);
#ifdef OB_BUILD_SHARED_STORAGE
  static int check_next_schedule_medium_for_ss(
    const ObMediumCompactionInfo &next_schedule_info,
    const int64_t last_major_snapshot);
#endif

private:
  static int check_continue(
    const MediumInfoArray &medium_info_array,
    const int64_t start_check_idx = 0);
  static int inner_check_medium_list(
    const ObExtraMediumInfo &extra_info,
    const MediumInfoArray *medium_info_array,
    const int64_t last_major_snapshot);
#ifdef OB_BUILD_SHARED_STORAGE
  static int check_continue_for_ss(
    const MediumInfoArray &medium_info_array,
    const int64_t start_check_idx = 0);
  static int inner_check_medium_list_for_ss(
    const ObExtraMediumInfo &extra_info,
    const MediumInfoArray *medium_info_array,
    const int64_t last_major_snapshot);
#endif
  static int check_extra_info(
    const ObExtraMediumInfo &extra_info,
    const int64_t last_major_snapshot);
  static int filter_finish_medium_info(
    const MediumInfoArray &medium_info_array,
    const int64_t last_major_snapshot,
    int64_t &next_medium_info_idx);
};

} // namespace compaction
} // namespace oceanbase

#endif // OB_STORAGE_COMPACTION_MEDIUM_LIST_CHECKER_H_
