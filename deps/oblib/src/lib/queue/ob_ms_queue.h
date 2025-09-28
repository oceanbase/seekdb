/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#ifndef OCEANBASE_LIB_QUEUE_OB_MS_QUEUE_H_
#define OCEANBASE_LIB_QUEUE_OB_MS_QUEUE_H_
#include "lib/queue/ob_co_seq_queue.h"              // ObCoSeqQueue
#include "lib/ob_define.h"
#include "lib/queue/ob_link.h"                      // ObLink
#include "lib/utility/ob_print_utils.h"             // TO_STRING_KV

namespace oceanbase
{
namespace common
{
class ObIAllocator;
class ObMsQueue
{
public:
  typedef ObLink Task;
  struct TaskHead
  {
    Task* head_;
    Task* tail_;

    TaskHead(): head_(NULL), tail_(NULL) {}
    ~TaskHead() {}
    void add(Task* node);
    Task* pop();
  };
  struct QueueInfo
  {
    TaskHead* array_;
    int64_t len_;
    int64_t pop_;

    QueueInfo(): array_(NULL), len_(0), pop_(0) {}
    ~QueueInfo() { destroy(); }

    int destroy();
    // there may be parallel add, but one seq can only be handled by one thread.
    // NOT thread-safe
  };

public:
  ObMsQueue();
  ~ObMsQueue();
  int destroy();
  int64_t get_queue_num() const { return qcount_; }

  TO_STRING_KV(K_(inited), K_(qlen), K_(qcount));

private:
  bool inited_;
  int64_t qlen_;
  int64_t qcount_;
  QueueInfo* qinfo_;
  ObCoSeqQueue seq_queue_;
  ObIAllocator *allocator_;
  DISALLOW_COPY_AND_ASSIGN(ObMsQueue);
};
}// end namespace common
}// end namespace oceanbase

#endif /* OCEANBASE_LIB_QUEUE_OB_MS_QUEUE_H_ */

