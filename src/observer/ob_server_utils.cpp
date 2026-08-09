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

#include "observer/ob_server_utils.h"
#include "share/ob_server_struct.h"
#include "share/ob_share_util.h"
#include "storage/ob_file_system_router.h"
#ifndef _WIN32
#include <sys/utsname.h>
#endif
#include "share/ob_version.h"

namespace oceanbase
{
using namespace common;

namespace observer
{

int ObServerUtils::get_server_ip(ObIAllocator *allocator, ObString &ipstr)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(allocator)) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "invalid alloctor pointer is NULL", K(ret));
  } else if (OB_FAIL(share::ObShareUtil::get_server_ip(GCTX.self_addr(), *allocator, ipstr))) {
  }
  return ret;
}

int ObServerUtils::get_log_disk_info_in_config(int64_t& log_disk_size,
                                               int64_t& log_disk_percentage,
                                               int64_t& total_log_disk_size)
{
  int ret = OB_SUCCESS;
  const int64_t DEFAULT_LOG_DISK_SIZE = MAX(2LL << 30, lib::get_memory_budget());
  int64_t suggested_clog_disk_size = 0 == GCONF.log_disk_size ? DEFAULT_LOG_DISK_SIZE : GCONF.log_disk_size;
  int64_t suggested_clog_disk_percentage = GCONF.log_disk_percentage;
  int64_t data_default_disk_percentage = 0;
  int64_t clog_default_disk_percentage = 0;
  int64_t data_disk_total_size = 0;
  int64_t clog_disk_total_size = 0;
  bool same_filesystem = false;
  const char* data_dir = OB_FILE_SYSTEM_ROUTER.get_sstable_dir();
  const char* clog_dir = OB_FILE_SYSTEM_ROUTER.get_clog_dir();
  if (OB_FAIL(calculate_disk_layout_defaults(data_disk_total_size,
                                             data_default_disk_percentage,
                                             clog_disk_total_size,
                                             clog_default_disk_percentage,
                                             same_filesystem))) {
  } else if (OB_FAIL(decide_disk_size(clog_disk_total_size,
                                      suggested_clog_disk_size,
                                      suggested_clog_disk_percentage,
                                      clog_default_disk_percentage,
                                      log_disk_size,
                                      log_disk_percentage))) {
  } else {
    total_log_disk_size = clog_disk_total_size;
    LOG_INFO("get_log_disk_info_in_config", K(suggested_clog_disk_size),
             K(suggested_clog_disk_percentage), K(log_disk_size),
             K(log_disk_percentage), K(total_log_disk_size));
  }
  return ret;
}

int ObServerUtils::get_data_disk_info_in_config(int64_t& data_disk_size,
                                                int64_t& data_disk_percentage)
{
  int ret = OB_SUCCESS;
  int64_t suggested_data_disk_size = GCONF.datafile_size;
  int64_t suggested_data_disk_percentage = GCONF.datafile_disk_percentage;
  int64_t data_default_disk_percentage = 0;
  int64_t clog_default_disk_percentage = 0;
  int64_t data_disk_total_size = 0;
  int64_t clog_disk_total_size = 0;
  bool same_filesystem = false;
  const char* data_dir = OB_FILE_SYSTEM_ROUTER.get_sstable_dir();
  const char* clog_dir = OB_FILE_SYSTEM_ROUTER.get_clog_dir();
  if (OB_FAIL(calculate_disk_layout_defaults(data_disk_total_size,
                                             data_default_disk_percentage,
                                             clog_disk_total_size,
                                             clog_default_disk_percentage,
                                             same_filesystem))) {
  } else if (OB_FAIL(decide_disk_size(data_disk_total_size,
                                      suggested_data_disk_size,
                                      suggested_data_disk_percentage,
                                      data_default_disk_percentage,
                                      data_disk_size,
                                      data_disk_percentage))) {
  } else {
    LOG_INFO("get_data_disk_info_in_config", K(suggested_data_disk_size),
             K(suggested_data_disk_percentage), K(data_disk_size),
             K(data_disk_percentage));
  }
  return ret;
}

int ObServerUtils::cal_all_part_disk_size(const int64_t suggested_data_disk_size,
                                          const int64_t suggested_clog_disk_size,
                                          const int64_t suggested_data_disk_percentage,
                                          const int64_t suggested_clog_disk_percentage,
                                          int64_t& data_disk_size,
                                          int64_t& log_disk_size,
                                          int64_t& data_disk_percentage,
                                          int64_t& log_disk_percentage)
{
  int ret = OB_SUCCESS;
  int64_t total_log_disk_space = 0;
  if (OB_FAIL(get_data_disk_info_in_config(data_disk_size, data_disk_percentage))) {
  } else if (OB_FAIL(get_log_disk_info_in_config(log_disk_size, log_disk_percentage, total_log_disk_space))) {
  } else {
    LOG_INFO("cal_all_part_disk_size success", K(suggested_data_disk_size), K(suggested_clog_disk_size),
             K(suggested_data_disk_percentage), K(suggested_clog_disk_percentage), K(data_disk_size),
             K(log_disk_size), K(data_disk_percentage), K(log_disk_percentage));
  }
  return ret;
}

const char *ObServerUtils::build_syslog_file_info()
{
  int ret = OB_SUCCESS;
  const static int64_t max_info_len = 512;
  static char info[max_info_len];

#ifdef _WIN32
  const char *os_sysname = "Windows";
  const char *os_release = "10";
  const char *os_machine = "x86_64";
#else
  struct utsname uts;
  if (0 != ::uname(&uts)) {
    ret = OB_ERR_SYS;
    LOG_WARN("call uname failed");
  }
  const char *os_sysname = uts.sysname;
  const char *os_release = uts.release;
  const char *os_machine = uts.machine;
#endif

  int gmtoff_hour = 0;
  int gmtoff_minute = 0;
  if (OB_SUCC(ret)) {
    time_t t = time(NULL);
    struct tm lt;
#ifdef _WIN32
    if (0 != localtime_s(&lt, &t)) {
      ret = OB_ERR_SYS;
      LOG_WARN("call localtime failed");
    } else {
      TIME_ZONE_INFORMATION tzi;
      GetTimeZoneInformation(&tzi);
      long bias_seconds = -tzi.Bias * 60L;
      gmtoff_hour = static_cast<int>((std::abs(bias_seconds) / 3600) * (bias_seconds < 0 ? -1 : 1));
      gmtoff_minute = static_cast<int>(std::abs(bias_seconds) % 3600 / 60);
    }
#else
    if (NULL == localtime_r(&t, &lt)) {
      ret = OB_ERR_SYS;
      LOG_WARN("call localtime failed");
    } else {
      gmtoff_hour = (std::abs(lt.tm_gmtoff) / 3600) * (lt.tm_gmtoff < 0 ? -1 : 1);
      gmtoff_minute = std::abs(lt.tm_gmtoff) % 3600 / 60;
    }
#endif
  }

  if (OB_SUCC(ret)) {
    int n = snprintf(info, max_info_len,
                     "observer version: %s, revision: %s, "
                     "sysname: %s, os release: %s, machine: %s, tz GMT offset: %02d:%02d",
                     PACKAGE_STRING, build_version(),
                     os_sysname, os_release, os_machine, gmtoff_hour, gmtoff_minute);
    if (n <= 0) {
      ret = OB_ERR_SYS;
      LOG_WARN("snprintf failed");
    }
  }

  return OB_SUCCESS == ret ? info : nullptr;
}

/*
 * calc actual_extend_size, following the rules:
 *  1. if datafile_next less than 32M, actual_extend_size equal to min(32M, datafile_maxsize * 10%)
 *  2. if datafile_next large than 32M, actual_extend_size equal to min(datafile_next, max_extend_file)
*/
int ObServerUtils::calc_auto_extend_size(int64_t &cur_datafile_size, int64_t &actual_extend_size)
{
  int ret = OB_SUCCESS;

  const int64_t datafile_maxsize = GCONF.datafile_maxsize;
  const int64_t datafile_next = GCONF.datafile_next;
  const int64_t datafile_size =
    OB_STORAGE_OBJECT_MGR.get_total_macro_block_count() * OB_STORAGE_OBJECT_MGR.get_macro_block_size();

  if (OB_UNLIKELY(datafile_maxsize <= 0) ||
      OB_UNLIKELY(datafile_size) <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid argument",
      K(ret), K(datafile_maxsize), K(datafile_size));
  } else {
    // attention: max_extend_file maybe equal to zero in the following situations:
    // 1. alter datafile_size as A, alter datafile_maxsize as B, and A < B
    // 2. auto extend to size to C ( A < C < B )
    // 3. alter datafile_maxsize as D ( A < D < C )
    int64_t extend_size = 0;
    int64_t max_extend_file = datafile_maxsize - datafile_size;
    if (0 == datafile_next) {
      const int64_t datafile_next_scale_limit = 1L * 1024 * 1024 * 1024; // 1G
      extend_size = MIN(datafile_size, datafile_next_scale_limit);
    } else {
      const int64_t datafile_next_minsize = 32 * 1024 * 1024; // 32M
      if (datafile_next < datafile_next_minsize) {
        int64_t min_extend_size = datafile_maxsize * 10 / 100;
        extend_size =
          min_extend_size < datafile_next_minsize ? min_extend_size : datafile_next_minsize;
      } else {
        extend_size = datafile_next;
      }
    }
    actual_extend_size = MIN(extend_size, max_extend_file);

    if (actual_extend_size <= 0) {
      ret = OB_SERVER_OUTOF_DISK_SPACE;
      if (REACH_TIME_INTERVAL(300 * 1000 * 1000L)) { // 5 min
        LOG_INFO("No more disk space to extend, is full",
          K(ret), K(datafile_maxsize), K(datafile_size));
      }
    } else {
      actual_extend_size += datafile_size; // suggest block file size
      cur_datafile_size = datafile_size;
    }
  }
  return ret;
}

int ObServerUtils::calculate_disk_layout_defaults(int64_t &data_disk_total_size,
                                                  int64_t &data_disk_default_percentage,
                                                  int64_t &clog_disk_total_size,
                                                  int64_t &clog_disk_default_percentage,
                                                  bool &same_filesystem)
{
  int ret = OB_SUCCESS;

  const int64_t DEFAULT_DISK_PERCENTAGE_ON_SEPARATE_FILESYSTEM = 90;
  const int64_t DEFAULT_DATA_DISK_PERCENTAGE_ON_SAME_FILESYSTEM = 60;
  const int64_t DEFAULT_CLOG_DISK_PERCENTAGE_ON_SAME_FILESYSTEM = 30;

  // We use sstable_dir as the data disk directory to identify whether the log and data are located
  // on the same file system, and the storage module will ensure that sstable_dir and slog_dir are
  // located on the same file system;
  const char* data_dir = OB_FILE_SYSTEM_ROUTER.get_sstable_dir();
  const char* clog_dir = OB_FILE_SYSTEM_ROUTER.get_clog_dir();

  struct statvfs data_statvfs;
  struct statvfs clog_statvfs;
  if (OB_SUCC(ret)) {
    if (OB_UNLIKELY(0 != statvfs(data_dir, &data_statvfs))) {
      LOG_ERROR("Failed to get data disk space ", KR(ret), K(data_dir), K(errno));
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_UNLIKELY(0 != statvfs(clog_dir, &clog_statvfs))) {
      LOG_ERROR("Failed to get clog disk space ", KR(ret), K(clog_dir), K(errno));
      ret = OB_ERR_UNEXPECTED;
    }
  }

  if (OB_SUCC(ret)) {
#ifdef _WIN32
    auto get_volume_serial = [](const char *path) -> DWORD {
      char root[MAX_PATH];
      if (!GetVolumePathNameA(path, root, MAX_PATH)) return 0;
      DWORD serial = 0;
      GetVolumeInformationA(root, NULL, 0, &serial, NULL, NULL, NULL, 0);
      return serial;
    };
    same_filesystem = (get_volume_serial(data_dir) == get_volume_serial(clog_dir));
#else
    same_filesystem = (data_statvfs.f_fsid == clog_statvfs.f_fsid);
#endif
    if (same_filesystem) {
      data_disk_default_percentage = DEFAULT_DATA_DISK_PERCENTAGE_ON_SAME_FILESYSTEM;
      clog_disk_default_percentage = DEFAULT_CLOG_DISK_PERCENTAGE_ON_SAME_FILESYSTEM;
    } else {
      data_disk_default_percentage = DEFAULT_DISK_PERCENTAGE_ON_SEPARATE_FILESYSTEM;
      clog_disk_default_percentage = DEFAULT_DISK_PERCENTAGE_ON_SEPARATE_FILESYSTEM;
    }
    data_disk_total_size = (data_statvfs.f_blocks + data_statvfs.f_bavail - data_statvfs.f_bfree) * data_statvfs.f_bsize;
    clog_disk_total_size = (clog_statvfs.f_blocks + clog_statvfs.f_bavail - clog_statvfs.f_bfree) * clog_statvfs.f_bsize;
    LOG_INFO("calculate disk layout defaults succeeded",
        K(data_dir), K(clog_dir),
        K(same_filesystem), K(data_disk_total_size), K(data_disk_default_percentage),
        K(clog_disk_total_size), K(clog_disk_default_percentage));
  }

  return ret;
}

int ObServerUtils::decide_disk_size(const int64_t total_space,
                                    const int64_t suggested_disk_size,
                                    const int64_t suggested_disk_percentage,
                                    const int64_t default_disk_percentage,
                                    int64_t& disk_size,
                                    int64_t& disk_percentage)
{
  int ret = OB_SUCCESS;

  if (suggested_disk_size <= 0) {
    int64_t disk_percentage = 0;
    if (suggested_disk_percentage <= 0) {
      disk_percentage = default_disk_percentage;
    } else {
      disk_percentage = suggested_disk_percentage;
    }
    disk_size = total_space * disk_percentage / 100;
  } else {
    disk_size = suggested_disk_size;
  }
  if (disk_size > total_space) {
    LOG_WARN("disk_size is greater than total disk space", KR(OB_SERVER_OUTOF_DISK_SPACE),
          K(suggested_disk_size), K(suggested_disk_percentage),
          K(default_disk_percentage),
          K(total_space), K(disk_size));
  }

  LOG_INFO("decide disk size finished",
        K(suggested_disk_size), K(suggested_disk_percentage),
        K(default_disk_percentage),
        K(total_space), K(disk_size));
  return ret;
}

} // namespace observer
} // namespace oceanbase
