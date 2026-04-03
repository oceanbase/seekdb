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

#ifndef OCEANBASE_ARCHIVE_OB_ARCHIVE_UTIL_H_
#define OCEANBASE_ARCHIVE_OB_ARCHIVE_UTIL_H_

#include "logservice/palf/lsn.h"

namespace oceanbase
{
namespace archive
{
using oceanbase::palf::LSN;

#define GET_LS_TASK_CTX(mgr, id)                             \
    ObArchiveLSGuard guard(mgr);                                \
    ObLSArchiveTask *ls_archive_task = NULL;                 \
    if (OB_FAIL(mgr->get_ls_guard(id, guard))) {               \
      ARCHIVE_LOG(WARN, "get ls guard failed", K(ret), K(id));    \
    } else if (OB_ISNULL(ls_archive_task = guard.get_ls_task())) {  \
      ret = OB_ERR_UNEXPECTED;                                              \
      ARCHIVE_LOG(ERROR, "ls task is NULL", K(ret), K(id));       \
    }                                                                       \
    if (OB_SUCC(ret))

int64_t cal_archive_file_id(const LSN &lsn, const int64_t N);
// Not support concurrent
struct SimpleQueue
{
  common::ObLink *head_;
  common::ObLink *tail_;

public:
  SimpleQueue() : head_(NULL), tail_(NULL) {}
  ~SimpleQueue() { head_ = NULL; tail_ = NULL;}

public:
  bool is_empty()
  {
    return NULL == head_ && NULL == tail_;
  }

  int push(common::ObLink *p)
  {
    int ret = common::OB_SUCCESS;
    if (OB_ISNULL(p)) {
      ret = common::OB_INVALID_ARGUMENT;
    } else if ((NULL == head_ && NULL != tail_) || (NULL != head_ && NULL == tail_)) {
      ret = common::OB_ERR_UNEXPECTED;
    } else if (NULL == head_) {
      p->next_ = NULL;
      head_ = p;
      tail_ = p;
    } else {
      common::ObLink *tmp = tail_;
      tail_ = p;
      tmp->next_ = tail_;
      tail_->next_ = NULL;
    }
    return ret;
  }

  int pop(common::ObLink *&p)
  {
    int ret = common::OB_SUCCESS;
    p = NULL;
    if ((NULL == head_ && NULL != tail_) || (NULL != head_ && NULL == tail_)) {
      ret = common::OB_ERR_UNEXPECTED;
    } else if (NULL == head_) {
    } else if (head_ == tail_) {
      p = head_;
      head_ = NULL;
      tail_ = NULL;
    } else {
      p = head_;
      head_ = head_->next_;
    }
    return ret;
  }

  int top(common::ObLink *&p)
  {
    int ret = common::OB_SUCCESS;
    p = NULL;
    if ((NULL == head_ && NULL != tail_) || (NULL != head_ && NULL == tail_)) {
      ret = common::OB_ERR_UNEXPECTED;
    } else if (NULL == head_) {
    } else {
      p = head_;
    }
    return ret;
  }
};

} // namespace archive
} // namespace oceanbase

#endif /* OCEANBASE_ARCHIVE_OB_ARCHIVE_UTIL_H_ */
