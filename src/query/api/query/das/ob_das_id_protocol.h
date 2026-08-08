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

#ifndef OCEANBASE_QUERY_DAS_OB_DAS_ID_PROTOCOL_H_
#define OCEANBASE_QUERY_DAS_OB_DAS_ID_PROTOCOL_H_

#include "lib/ob_define.h"
#include "lib/utility/ob_print_utils.h"
#include "lib/utility/ob_unify_serialize.h"
#include "common/datum/ob_datum.h"
#include "common/object/ob_object.h"

namespace oceanbase
{
namespace sql
{

// Value object shared by query's DAS layer and data-plane retrieval.  Keeping
// it here avoids either side depending on the other's iterator definitions.
class ObDocIdExt final
{
public:
  ObDocIdExt();
  ObDocIdExt(const ObDocIdExt &other);
  ~ObDocIdExt() = default;
  void reset();

  int hash(uint64_t &hash_val) const;
  const common::ObDatum &get_datum() const;
  int from_datum(const common::ObDatum &datum);
  int from_obj(const common::ObObj &obj);

  ObDocIdExt &operator=(const ObDocIdExt &other);
  bool operator==(const ObDocIdExt &other) const;
  bool operator!=(const ObDocIdExt &other) const;

  TO_STRING_KV(KP_(buf), K_(datum));
private:
  static const int64_t OB_DOC_ID_EXT_SIZE = 40;
  char buf_[OB_DOC_ID_EXT_SIZE];
  common::ObDatum datum_;
};

class ObDASIDRequest
{
  OB_UNIS_VERSION(1);
public:
  ObDASIDRequest() : range_(0) {}
  ~ObDASIDRequest() {}
  int init(const int64_t range)
  {
    int ret = common::OB_SUCCESS;
    if (range <= 0) {
      ret = common::OB_INVALID_ARGUMENT;
    } else {
      range_ = range;
    }
    return ret;
  }
  bool is_valid() const { return range_ > 0; }
  int64_t get_range() const { return range_; }
  TO_STRING_KV(K_(range));
private:
  int64_t range_;
};

} // namespace sql

namespace obcall
{

class ObDASIDRpcResult
{
  OB_UNIS_VERSION(1);
public:
  ObDASIDRpcResult() : status_(0), start_id_(0), end_id_(0) {}
  virtual ~ObDASIDRpcResult() {}
  int init(const int status, const int64_t start_id, const int64_t end_id)
  {
    int ret = common::OB_SUCCESS;
    if (common::OB_SUCCESS == status && (start_id <= 0 || end_id <= 0)) {
      ret = common::OB_INVALID_ARGUMENT;
    } else {
      status_ = status;
      start_id_ = start_id;
      end_id_ = end_id;
    }
    return ret;
  }
  int get_status() const { return status_; }
  int64_t get_start_id() const { return start_id_; }
  int64_t get_end_id() const { return end_id_; }
  bool is_valid() const
  {
    return common::OB_SUCCESS != status_ || (start_id_ > 0 && end_id_ > 0);
  }
  TO_STRING_KV(K_(status), K_(start_id), K_(end_id));
private:
  int status_;
  int64_t start_id_;
  int64_t end_id_;
};

} // namespace obcall
} // namespace oceanbase

#endif // OCEANBASE_QUERY_DAS_OB_DAS_ID_PROTOCOL_H_
