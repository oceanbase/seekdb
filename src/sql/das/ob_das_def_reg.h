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

#ifndef DEV_SRC_SQL_DAS_OB_DAS_DEF_REG_H_
#define DEV_SRC_SQL_DAS_OB_DAS_DEF_REG_H_
#include <type_traits>
#include "sql/das/ob_das_define.h"

namespace oceanbase
{
namespace sql
{
namespace das_reg
{
template <int>
struct ObDASOpTypeTraits
{

  constexpr static bool registered_ = false;
  //attached_=false means this computation is bound to other operations for execution
  //and does not have its own operator.
  constexpr static bool attached_ = false;
  typedef char DASOp;
  typedef char DASCtDef;
  typedef char DASRtDef;
};

template <typename T>
struct ObDASOpTraits
{
  constexpr static int type_ = 0;
};
}  // namespace das_reg

#define REGISTER_DAS_OP(type, op, ctdef, rtdef)                                                 \
  namespace das_reg {                                                                           \
  template<>                                                                                    \
  struct ObDASOpTypeTraits<type>                                                                \
  {                                                                                             \
    constexpr static bool registered_ = true;                                                   \
    constexpr static bool attached_ = false;                                                    \
    typedef op DASOp;                                                                           \
    typedef ctdef DASCtDef;                                                                     \
    typedef rtdef DASRtDef;                                                                     \
  };                                                                                            \
  template <> struct ObDASOpTraits<op> { constexpr static int type_ = type; };                  \
  }

class ObDASScanOp;
struct ObDASScanCtDef;
struct ObDASScanRtDef;
REGISTER_DAS_OP(DAS_OP_TABLE_SCAN, ObDASScanOp, ObDASScanCtDef, ObDASScanRtDef);

class ObDASInsertOp;
struct ObDASInsCtDef;
struct ObDASInsRtDef;
REGISTER_DAS_OP(DAS_OP_TABLE_INSERT, ObDASInsertOp, ObDASInsCtDef, ObDASInsRtDef);

class ObDASDeleteOp;
struct ObDASDelCtDef;
struct ObDASDelRtDef;
REGISTER_DAS_OP(DAS_OP_TABLE_DELETE, ObDASDeleteOp, ObDASDelCtDef, ObDASDelRtDef);

class ObDASUpdateOp;
struct ObDASUpdCtDef;
struct ObDASUpdRtDef;
REGISTER_DAS_OP(DAS_OP_TABLE_UPDATE, ObDASUpdateOp, ObDASUpdCtDef, ObDASUpdRtDef);

class ObDASLockOp;
struct ObDASLockCtDef;
struct ObDASLockRtDef;
REGISTER_DAS_OP(DAS_OP_TABLE_LOCK, ObDASLockOp, ObDASLockCtDef, ObDASLockRtDef);

class ObDASGroupScanOp;
struct ObDASScanCtDef;
struct ObDASScanRtDef;
REGISTER_DAS_OP(DAS_OP_TABLE_BATCH_SCAN, ObDASGroupScanOp, ObDASScanCtDef, ObDASScanRtDef);

class ObDASSplitRangesOp;
class ObDASEmptyCtDef;
class ObDASEmptyRtDef;
REGISTER_DAS_OP(DAS_OP_SPLIT_MULTI_RANGES, ObDASSplitRangesOp, ObDASEmptyCtDef, ObDASEmptyRtDef);

class ObDASRangesCostOp;
REGISTER_DAS_OP(DAS_OP_GET_RANGES_COST, ObDASRangesCostOp, ObDASEmptyCtDef, ObDASEmptyRtDef);

#undef REGISTER_DAS_OP

class ObDASEmptyOp;
#define REGISTER_DAS_ATTACH_OP(type, ctdef, rtdef)                                              \
  namespace das_reg {                                                                           \
  template<>                                                                                    \
  struct ObDASOpTypeTraits<type>                                                                \
  {                                                                                             \
    constexpr static bool registered_ = true;                                                   \
    constexpr static bool attached_ = true;                                                     \
    typedef ObDASEmptyOp DASOp;                                                                 \
    typedef ctdef DASCtDef;                                                                     \
    typedef rtdef DASRtDef;                                                                     \
  };                                                                                            \
  template <> struct ObDASOpTraits<ctdef> { constexpr static int type_ = type; };               \
  }

struct ObDASTableLookupCtDef;
struct ObDASTableLookupRtDef;
REGISTER_DAS_ATTACH_OP(DAS_OP_TABLE_LOOKUP, ObDASTableLookupCtDef, ObDASTableLookupRtDef);

struct ObDASIRScanCtDef;
struct ObDASIRScanRtDef;
REGISTER_DAS_ATTACH_OP(DAS_OP_IR_SCAN, ObDASIRScanCtDef, ObDASIRScanRtDef);

struct ObDASVecAuxScanCtDef;
struct ObDASVecAuxScanRtDef;
REGISTER_DAS_ATTACH_OP(DAS_OP_VEC_SCAN, ObDASVecAuxScanCtDef, ObDASVecAuxScanRtDef);

struct ObDASIRAuxLookupCtDef;
struct ObDASIRAuxLookupRtDef;
REGISTER_DAS_ATTACH_OP(DAS_OP_IR_AUX_LOOKUP, ObDASIRAuxLookupCtDef, ObDASIRAuxLookupRtDef);

struct ObDASSortCtDef;
struct ObDASSortRtDef;
REGISTER_DAS_ATTACH_OP(DAS_OP_SORT, ObDASSortCtDef, ObDASSortRtDef);

struct ObDASDocIdMergeCtDef;
struct ObDASDocIdMergeRtDef;
REGISTER_DAS_ATTACH_OP(DAS_OP_DOC_ID_MERGE, ObDASDocIdMergeCtDef, ObDASDocIdMergeRtDef);

struct ObDASVIdMergeCtDef;
struct ObDASVIdMergeRtDef;
REGISTER_DAS_ATTACH_OP(DAS_OP_VID_MERGE, ObDASVIdMergeCtDef, ObDASVIdMergeRtDef);

struct ObDASFuncLookupCtDef;
struct ObDASFuncLookupRtDef;
REGISTER_DAS_ATTACH_OP(DAS_OP_FUNC_LOOKUP, ObDASFuncLookupCtDef, ObDASFuncLookupRtDef);

struct ObDASIndexMergeCtDef;
struct ObDASIndexMergeRtDef;
REGISTER_DAS_ATTACH_OP(DAS_OP_INDEX_MERGE, ObDASIndexMergeCtDef, ObDASIndexMergeRtDef);

struct ObDASIndexProjLookupCtDef;
struct ObDASIndexProjLookupRtDef;
REGISTER_DAS_ATTACH_OP(DAS_OP_INDEX_PROJ_LOOKUP, ObDASIndexProjLookupCtDef, ObDASIndexProjLookupRtDef);

struct ObDASDomainIdMergeCtDef;
struct ObDASDomainIdMergeRtDef;
REGISTER_DAS_ATTACH_OP(DAS_OP_DOMAIN_ID_MERGE, ObDASDomainIdMergeCtDef, ObDASDomainIdMergeRtDef);

struct ObDASIREsMatchCtDef;
struct ObDASIREsMatchRtDef;
REGISTER_DAS_ATTACH_OP(DAS_OP_IR_ES_MATCH, ObDASIREsMatchCtDef, ObDASIREsMatchRtDef);

struct ObDASIREsScoreCtDef;
struct ObDASIREsScoreRtDef;
REGISTER_DAS_ATTACH_OP(DAS_OP_IR_ES_SCORE, ObDASIREsScoreCtDef, ObDASIREsScoreRtDef);
#undef REGISTER_DAS_ATTACH_OP
}  // namespace sql
}  // namespace oceanbase

#endif /* DEV_SRC_SQL_DAS_OB_DAS_DEF_REG_H_ */
