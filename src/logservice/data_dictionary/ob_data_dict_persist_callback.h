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

#ifndef OCEANBASE_DICT_SERVICE_DATA_DICTIONARY_PERSIST_CB_
#define OCEANBASE_DICT_SERVICE_DATA_DICTIONARY_PERSIST_CB_

#include "logservice/ob_append_callback.h"   // AppendCb
#include "lib/queue/ob_link_queue.h"         // ObSimpleLinkQueue

namespace oceanbase
{
namespace datadict
{

class ObDataDictPersistCallback : public logservice::AppendCb, public common::QLink
{
public:
  ObDataDictPersistCallback() : is_callback_invoked_(false), is_success_(false) {}
  ~ObDataDictPersistCallback() { reset(); }
  void reset()
  {
    ATOMIC_SET(&is_callback_invoked_, false);
    ATOMIC_SET(&is_success_, false);
  }
public:
  virtual int on_success() override
  {
    ATOMIC_SET(&is_success_, true);
    MEM_BARRIER();
    ATOMIC_SET(&is_callback_invoked_, true);
    return OB_SUCCESS;
  }
  virtual int on_failure() override
  {
    ATOMIC_SET(&is_callback_invoked_, true);
    return OB_SUCCESS;
  }
  const char *get_cb_name() const override { return "DataDictPersistCallback"; }
public:
  TO_STRING_KV(K_(is_callback_invoked), K_(is_success));
  OB_INLINE bool is_invoked() const { return ATOMIC_LOAD(&is_callback_invoked_); }
  OB_INLINE bool is_success() const { return ATOMIC_LOAD(&is_success_); }
private:
  bool is_callback_invoked_;
  bool is_success_;
};

} // datadict
} // oceanbase
#endif
