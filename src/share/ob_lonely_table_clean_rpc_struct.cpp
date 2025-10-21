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

#define USING_LOG_PREFIX SHARE

#include "ob_lonely_table_clean_rpc_struct.h"

namespace oceanbase
{
namespace obrpc
{

OB_SERIALIZE_MEMBER((ObForceDropLonelyLobAuxTableArg, ObDDLArg), tenant_id_, data_table_id_, aux_lob_meta_table_id_, aux_lob_piece_table_id_);

}//end namespace obrpc
}//end namespace oceanbase

