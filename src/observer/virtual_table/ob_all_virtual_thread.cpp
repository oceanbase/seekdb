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

#include "ob_all_virtual_thread.h"
#include "lib/file/file_directory_utils.h"
#include "lib/thread/protected_stack_allocator.h"
#include "lib/resource/ob_affinity_ctrl.h"

#ifdef __APPLE__
#include <sys/uio.h>
static ssize_t process_vm_readv(pid_t pid, const struct iovec *local_iov, unsigned long liovcnt,
                                const struct iovec *remote_iov, unsigned long riovcnt, unsigned long flags) {
  (void)pid;
  (void)local_iov;
  (void)liovcnt;
  (void)remote_iov;
  (void)riovcnt;
  (void)flags;
  return -1;
}
#endif

#define GET_OTHER_TSI_ADDR(var_name, addr) \
const int64_t var_name##_offset = ((int64_t)addr - (int64_t)pthread_self()); \
decltype(*addr) var_name = *(decltype(addr))(thread_base + var_name##_offset);

namespace oceanbase
{
using namespace lib;
namespace observer
{
ObAllVirtualThread::ObAllVirtualThread() : is_inited_(false), is_config_cgroup_(false)
{
}

ObAllVirtualThread::~ObAllVirtualThread()
{
  reset();
}

int ObAllVirtualThread::inner_open()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ObServerConfig::get_instance().self_addr_.ip_to_string(ip_buf_, sizeof(ip_buf_))
              == false)) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "ip_to_string() fail", K(ret));
  }
  return ret;
}

void ObAllVirtualThread::reset()
{
  is_inited_ = false;
}

int ObAllVirtualThread::inner_get_next_row(common::ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    const char *cgroup_path = "cgroup";
    if (OB_FAIL(FileDirectoryUtils::is_exists(cgroup_path, is_config_cgroup_))) {
      SERVER_LOG(WARN, "fail check file exist", K(cgroup_path), K(ret));
    }
    #ifdef OB_BUILD_EMBED_MODE
    ret = OB_NOT_SUPPORTED;
    return ret;
    #endif
    #if defined(__APPLE__) || defined(__ANDROID__)
    ret = OB_NOT_SUPPORTED;
    return ret;
    #endif
    #ifdef _WIN32
    ret = OB_NOT_SUPPORTED;
    return ret;
    #endif
    #ifndef _WIN32
    const int64_t col_count = output_column_ids_.count();
    pid_t pid = getpid();
    StackMgr::Guard guard(g_stack_mgr);
    for (oceanbase::lib::ObStackHeader* header = *guard; OB_NOT_NULL(header); header = guard.next()) {
      char* thread_base = (char*)(header->pth_);
      if (OB_NOT_NULL(thread_base)) {
        GET_OTHER_TSI_ADDR(tid, &get_tid_cache());
        {
          char path[64];
          IGNORE_RETURN snprintf(path, 64, "/proc/self/task/%ld", tid);
          if (-1 == access(path, F_OK)) {
            continue;
          }
        }
        
        if (!true
            && false) {
          continue;
        }
        GET_OTHER_TSI_ADDR(wait_addr, &ObLatch::current_wait);
        for (int64_t i = 0; i < col_count && OB_SUCC(ret); ++i) {
          const uint64_t col_id = output_column_ids_.at(i);
          ObObj *cells = cur_row_.cells_;
          switch (col_id) {
            case TID: {
              cells[i].set_int(tid);
              break;
            }
            case TNAME: {
              GET_OTHER_TSI_ADDR(tname, &(ob_get_tname()[0]));
              MEMCPY(tname_, thread_base + tname_offset, sizeof(tname_));
              cells[i].set_varchar(tname_);
              cells[i].set_collation_type(
                  ObCharset::get_default_collation(ObCharset::get_default_charset()));
              break;
            }
            case LATCH_WAIT: {
              if (OB_ISNULL(wait_addr)) {
                cells[i].set_varchar("");
              } else {
                IGNORE_RETURN snprintf(wait_addr_, 16, "%p", wait_addr);
                cells[i].set_varchar(wait_addr_);
              }
              cells[i].set_collation_type(
                  ObCharset::get_default_collation(ObCharset::get_default_charset()));
              break;
            }
            case LATCH_HOLD: {
              GET_OTHER_TSI_ADDR(locks_addr, &(ObLatch::current_locks[0]));
              GET_OTHER_TSI_ADDR(slot_cnt, &ObLatch::max_lock_slot_idx)
              const int64_t cnt = std::min(ARRAYSIZEOF(ObLatch::current_locks), (int64_t)slot_cnt);
              decltype(&locks_addr) locks = (decltype(&locks_addr))(thread_base + locks_addr_offset);
              locks_addr_[0] = 0;
              for (int64_t i = 0, j = 0; i < cnt; ++i) {
                int64_t idx = (slot_cnt + i) % ARRAYSIZEOF(ObLatch::current_locks);
                if (OB_NOT_NULL(locks[idx]) && j < 256) {
                  uint32_t val = 0;
                  struct iovec local_iov = {&val, sizeof(val)};
                  struct iovec remote_iov = {locks[idx], sizeof(val)};
                  ssize_t n = process_vm_readv(pid, &local_iov, 1, &remote_iov, 1, 0);
                  if (n != sizeof(val)) {
                  } else if (0 != val) {
                    j += snprintf(locks_addr_ + j, 256 - j, "%p ", locks[idx]);
                  }
                }
              }
              cells[i].set_varchar(locks_addr_);
              cells[i].set_collation_type(
                  ObCharset::get_default_collation(ObCharset::get_default_charset()));
              break;
            }
            case TRACE_ID: {
              GET_OTHER_TSI_ADDR(trace_id, ObCurTraceId::get_trace_id());
              IGNORE_RETURN trace_id.to_string(trace_id_buf_, sizeof(trace_id_buf_));
              cells[i].set_varchar(trace_id_buf_);
              cells[i].set_collation_type(
                  ObCharset::get_default_collation(ObCharset::get_default_charset()));
              break;
            }
            case CGROUP_PATH: {
              if (!is_config_cgroup_) {
                cells[i].set_varchar("");
              } else {
                int64_t pid = getpid();
                snprintf(cgroup_path_buf_, PATH_BUFSIZE, "/proc/%ld/task/%ld/cgroup", pid, tid);
                cells[i].set_varchar(cgroup_path_buf_);
              }
              break;
            }
            case NUMA_NODE: {
              GET_OTHER_TSI_ADDR(numa_node, &ObAffinityCtrl::get_tls_node());
              int64_t numa_node_display = -1;
              if (numa_node == OB_NUMA_SHARED_INDEX) {
              } else {
                numa_node_display = numa_node;
              }
              cells[i].set_int(numa_node_display);
              break;
            }
            default: {
              ret = OB_ERR_UNEXPECTED;
              SERVER_LOG(WARN, "unexpected column id", K(col_id), K(i), K(ret));
              break;
            }
          }
        }
        if (OB_SUCC(ret)) {
          if (OB_FAIL(scanner_.add_row(cur_row_))) {
            SERVER_LOG(WARN, "fail to add row", K(ret), K(cur_row_));
            if (OB_SIZE_OVERFLOW == ret) {
              ret = OB_SUCCESS;
            }
          }
        }
      }
    }
    if (OB_SUCC(ret)) {
      scanner_it_ = scanner_.begin();
      is_inited_ = true;
    }
    #endif /* !_WIN32 */
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(scanner_it_.get_next_row(cur_row_))) {
      if (OB_ITER_END != ret) {
        SERVER_LOG(WARN, "fail to get next row", K(ret));
      }
    } else if (OB_FAIL(read_real_cgroup_path())){
      SERVER_LOG(WARN, "fail to get cgroup path real path", K(ret));
    } else {
      row = &cur_row_;
    }
  }
  return ret;
}

int ObAllVirtualThread::read_real_cgroup_path()
{
  int ret = OB_SUCCESS;
  const int64_t col_count = output_column_ids_.count();
  for (int64_t i = 0; i < col_count && OB_SUCC(ret); ++i) {
    const uint64_t col_id = output_column_ids_.at(i);
    ObObj *cells = cur_row_.cells_;
    if (col_id == CGROUP_PATH) {
      char path[PATH_BUFSIZE];
      snprintf(path, cells[i].get_val_len() + 1, "%s", cells[i].get_varchar().ptr());
      FILE *file = fopen(path, "r");
      if (NULL == file) {
        cells[i].set_varchar("");
      } else {
        bool is_find = false;
        int min_len = 2;
        int discard_len = 1;
        char read_buff[PATH_BUFSIZE];
        cgroup_path_buf_[0] = '\0';
        while (fgets(read_buff, sizeof(read_buff), file) != NULL && !is_find) {
          const char* match_begin =  strstr(read_buff, ":/");
          const char* match_cpu =  strstr(read_buff, "cpu");
          if (match_begin != NULL && match_cpu != NULL) {
            is_find = true;
            match_begin += discard_len;
            snprintf(cgroup_path_buf_, PATH_BUFSIZE, "%s", match_begin);
          }
        }
        if (is_find) {
          int cgroup_path_len = strlen(cgroup_path_buf_);
          if (min_len < cgroup_path_len) {
            if (cgroup_path_buf_[cgroup_path_len - 1] == '\n') {
              cgroup_path_buf_[cgroup_path_len - 1] = '\0';
            }
            cells[i].set_varchar(cgroup_path_buf_);
          } else {
            cells[i].set_varchar("");
          }
        } else {
          cells[i].set_varchar("");
        }
      }
      cells[i].set_collation_type(
          ObCharset::get_default_collation(ObCharset::get_default_charset()));
      if (NULL != file) {
        fclose(file);
      }
    }
  }
  return ret;
}

} // namespace observer
} // namespace oceanbase
