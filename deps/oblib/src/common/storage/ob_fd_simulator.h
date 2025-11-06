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

#ifndef OCEANBASE_STORAGE_OB_FD_SIMULATOR_
#define OCEANBASE_STORAGE_OB_FD_SIMULATOR_

#include "lib/container/ob_array.h"
#include "lib/allocator/page_arena.h"
#include "common/storage/ob_io_device.h"

namespace oceanbase
{
namespace common
{

class ObFdSimulator {
public:
  ObFdSimulator();
  virtual ~ObFdSimulator(){};

  const static int DEFAULT_ARRAY_SIZE = 100;
  const static int INVALID_SLOT_ID = -1;
  /*pointerid is a union, 
    when the slot is free, it is a id, which point next free slot
    when the slot is used, it is a pointer, which point the ctx
  */
  union PointerId {
    void*    ctx_pointer;
    int64_t  index_id;
  };

  struct FdSlot {
    PointerId pointer;
    int64_t   slot_version;
  };

  struct FirstArray {
    FdSlot    *second_array_p;
    int64_t   second_array_free_hd;
  };

  int init();
  int get_fd(void* ctx, const int device_type, const int flag, ObIOFd &fd);
  int fd_to_ctx(const ObIOFd& fd, void*& ctx);  //called when query ctx 
  int release_fd(const ObIOFd& fd);           //called when close
  void get_fd_slot_id(const ObIOFd& fd, int64_t& first_id, int64_t& second_id);
  static void get_fd_device_type(const ObIOFd& fd, int &device_type);
  static void get_fd_flag(const ObIOFd& fd, int &flag);

  bool validate_fd(const ObIOFd& fd,  bool expect);

  //for test
  void get_fd_stat(int32_t& used_cnt, int32_t& free_cnt) 
  {
    used_cnt = used_fd_cnt_;
    free_cnt = total_fd_cnt_ - used_fd_cnt_;
  }

private:
  int extend_second_array();
  void set_fd_device_type(ObIOFd& fd, int device_type);
  void set_fd_flag(ObIOFd& fd, int flag);
  
  int init_manager_array(FdSlot* second_array);
  
private:
  int32_t array_size_;
  int32_t second_array_num_;
  FirstArray *first_array_;
  common::ObArenaAllocator allocator_; 
  /*now, device is used by many source(thread), one device hold one simulator*/
  common::ObSpinLock lock_;
  int32_t used_fd_cnt_;
  int32_t total_fd_cnt_;
  bool is_init_;
};


}
}

#endif
