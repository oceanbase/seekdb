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

#ifndef OCEANBASE_COMMON_OB_OBJECT_H_
#define OCEANBASE_COMMON_OB_OBJECT_H_

#include "common/datum/datum_parameter_array.h"
#include "common/datum/datum_param_store.h"
#include "lib/ob_define.h"
#include "common/object/ob_type_metadata.h"
#include "common/enumset/enumset_value.h"
#include "common/lob/lob_types.h"
#include "common/datum/parameter_flags.h"
#include "common/timezone/ob_time_def.h"
#include "lib/string/ob_string.h"
#include "common/ob_action_flag.h"
#include "common/object/ob_obj_type.h"
#include "common/object/ob_value_print_params.h"
#include "common/ob_accuracy.h"
#include "lib/checksum/ob_crc64.h"
#include "common/number/ob_number_v2.h"
#include "lib/charset/ob_charset.h"
#include "common/timezone/ob_timezone_info.h"
#include "lib/hash/ob_hashutils.h"
#include "lib/hash_func/ob_hash_func.h"
#include "lib/charset/ob_dtoa.h"
#include "common/wide_integer/ob_wide_integer.h"

namespace oceanbase
{
namespace tests
{
namespace common
{
class ObjTest;
class ObSqlString;
}
}
namespace sql
{
class ObExecContext;
}
namespace common
{

struct ObCompareCtx;
class ObSqlCollectionInfo;
enum class ObDocIDType : uint16_t
{
  INVALID = 0,
  TABLET_SEQUENCE,
  HIDDEN_INC_PK,
};

class ObDocId final
{
public:
  ObDocId();
  ObDocId(const uint64_t tablet_id, const uint64_t seq_id);
  ~ObDocId() = default;

  void reset();
  bool is_valid() const;
  int hash(uint64_t &hash_val) const;
  ObString get_string() const;
  int from_string(const ObString &doc_id);

  bool operator ==(const ObDocId &other) const;
  bool operator !=(const ObDocId &other) const;

  TO_STRING_KV(K_(tablet_id), K_(seq_id));
public:
  static const int64_t OB_DOC_ID_HASH = 10;
  uint64_t tablet_id_;
  uint64_t seq_id_;
};

class ObCenterId final
{
public:
  ObCenterId();
  ObCenterId(const uint64_t tablet_id, const uint64_t center_id);
  ~ObCenterId() = default;

  bool is_valid() const;


  TO_STRING_KV(K_(tablet_id), K_(center_id));
public:
  uint64_t tablet_id_;
  uint64_t center_id_;
};

class ObPqCenterId final
{
public:
  ObPqCenterId();
  ObPqCenterId(const uint64_t tablet_id, const uint32_t m_id, const uint32_t center_id);
  ~ObPqCenterId() = default;

  bool is_valid() const;


  TO_STRING_KV(K_(tablet_id), K_(center_id), K_(m_id));
public:
  uint64_t tablet_id_;
  uint32_t m_id_;
  uint32_t center_id_;
};

} // namespace common
} // namespace oceanbase

#endif // OCEANBASE_COMMON_OB_OBJECT_H_
