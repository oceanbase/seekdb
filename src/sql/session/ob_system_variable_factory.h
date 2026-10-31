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

#ifndef OCEANBASE_SQL_SESSION_OB_SYSTEM_VARIABLE_FACTORY_
#define OCEANBASE_SQL_SESSION_OB_SYSTEM_VARIABLE_FACTORY_
#include "common/datum/datum_value.h"
#include "sql/session/ob_system_variable.h"
#include "share/system_variable/ob_system_variable_init.h"
#include "lib/allocator/page_arena.h"
#include "lib/string/ob_string.h"
#include "lib/container/ob_array.h"

namespace oceanbase
{
namespace sql
{
class ObSysVarAutoIncrementIncrement : public ObIntSysVar
{
public:
  ObSysVarAutoIncrementIncrement() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_AUTO_INCREMENT_INCREMENT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(0); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(0); }
};
class ObSysVarAutoIncrementOffset : public ObIntSysVar
{
public:
  ObSysVarAutoIncrementOffset() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_AUTO_INCREMENT_OFFSET; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(1); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(1); }
};
class ObSysVarAutocommit : public ObBoolSysVar
{
public:
  ObSysVarAutocommit() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_AUTOCOMMIT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(2); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(2); }
};
class ObSysVarCharacterSetClient : public ObCharsetSysVar
{
public:
  ObSysVarCharacterSetClient() : ObCharsetSysVar(ObSysVarOnCheckFuncs::check_and_convert_charset_not_null, NULL, ObSysVarToObjFuncs::to_obj_charset, ObSysVarToStrFuncs::to_str_charset, ObSysVarGetMetaTypeFuncs::get_meta_type_varchar) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_CHARACTER_SET_CLIENT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(3); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(3); }
};
class ObSysVarCharacterSetConnection : public ObCharsetSysVar
{
public:
  ObSysVarCharacterSetConnection() : ObCharsetSysVar(ObSysVarOnCheckFuncs::check_and_convert_charset_not_null, NULL, ObSysVarToObjFuncs::to_obj_charset, ObSysVarToStrFuncs::to_str_charset, ObSysVarGetMetaTypeFuncs::get_meta_type_varchar) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_CHARACTER_SET_CONNECTION; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(4); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(4); }
};
class ObSysVarCharacterSetDatabase : public ObCharsetSysVar
{
public:
  ObSysVarCharacterSetDatabase() : ObCharsetSysVar(ObSysVarOnCheckFuncs::check_and_convert_charset_not_null, NULL, ObSysVarToObjFuncs::to_obj_charset, ObSysVarToStrFuncs::to_str_charset, ObSysVarGetMetaTypeFuncs::get_meta_type_varchar) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_CHARACTER_SET_DATABASE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(5); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(5); }
};
class ObSysVarCharacterSetResults : public ObCharsetSysVar
{
public:
  ObSysVarCharacterSetResults() : ObCharsetSysVar(ObSysVarOnCheckFuncs::check_and_convert_charset, NULL, ObSysVarToObjFuncs::to_obj_charset, ObSysVarToStrFuncs::to_str_charset, ObSysVarGetMetaTypeFuncs::get_meta_type_varchar) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_CHARACTER_SET_RESULTS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(6); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(6); }
};
class ObSysVarCharacterSetServer : public ObCharsetSysVar
{
public:
  ObSysVarCharacterSetServer() : ObCharsetSysVar(ObSysVarOnCheckFuncs::check_and_convert_charset_not_null, NULL, ObSysVarToObjFuncs::to_obj_charset, ObSysVarToStrFuncs::to_str_charset, ObSysVarGetMetaTypeFuncs::get_meta_type_varchar) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_CHARACTER_SET_SERVER; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(7); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(7); }
};
class ObSysVarCharacterSetSystem : public ObCharsetSysVar
{
public:
  ObSysVarCharacterSetSystem() : ObCharsetSysVar(NULL, NULL, ObSysVarToObjFuncs::to_obj_charset, ObSysVarToStrFuncs::to_str_charset, ObSysVarGetMetaTypeFuncs::get_meta_type_varchar) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_CHARACTER_SET_SYSTEM; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(8); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(8); }
};
class ObSysVarCollationConnection : public ObCharsetSysVar
{
public:
  ObSysVarCollationConnection() : ObCharsetSysVar(ObSysVarOnCheckFuncs::check_and_convert_collation_not_null, NULL, ObSysVarToObjFuncs::to_obj_collation, ObSysVarToStrFuncs::to_str_collation, ObSysVarGetMetaTypeFuncs::get_meta_type_varchar) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_COLLATION_CONNECTION; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(9); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(9); }
};
class ObSysVarCollationDatabase : public ObCharsetSysVar
{
public:
  ObSysVarCollationDatabase() : ObCharsetSysVar(ObSysVarOnCheckFuncs::check_and_convert_collation_not_null, NULL, ObSysVarToObjFuncs::to_obj_collation, ObSysVarToStrFuncs::to_str_collation, ObSysVarGetMetaTypeFuncs::get_meta_type_varchar) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_COLLATION_DATABASE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(10); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(10); }
};
class ObSysVarCollationServer : public ObCharsetSysVar
{
public:
  ObSysVarCollationServer() : ObCharsetSysVar(ObSysVarOnCheckFuncs::check_and_convert_collation_not_null, NULL, ObSysVarToObjFuncs::to_obj_collation, ObSysVarToStrFuncs::to_str_collation, ObSysVarGetMetaTypeFuncs::get_meta_type_varchar) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_COLLATION_SERVER; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(11); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(11); }
};
class ObSysVarInteractiveTimeout : public ObIntSysVar
{
public:
  ObSysVarInteractiveTimeout() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INTERACTIVE_TIMEOUT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(12); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(12); }
};
class ObSysVarLastInsertId : public ObSessionSpecialIntSysVar
{
public:
  ObSysVarLastInsertId() : ObSessionSpecialIntSysVar(NULL, ObSysVarSessionSpecialUpdateFuncs::update_last_insert_id, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_LAST_INSERT_ID; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(13); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(13); }
};
class ObSysVarMaxAllowedPacket : public ObIntSysVar
{
public:
  ObSysVarMaxAllowedPacket() : ObIntSysVar(ObSysVarOnCheckFuncs::check_and_convert_max_allowed_packet, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_MAX_ALLOWED_PACKET; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(14); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(14); }
};
class ObSysVarSqlMode : public ObSqlModeVar
{
public:
  ObSysVarSqlMode() : ObSqlModeVar(ObSysVarOnCheckFuncs::check_and_convert_sql_mode, NULL, ObSysVarToObjFuncs::to_obj_sql_mode, ObSysVarToStrFuncs::to_str_sql_mode, ObSysVarGetMetaTypeFuncs::get_meta_type_varchar) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SQL_MODE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(15); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(15); }
};
class ObSysVarTimeZone : public ObTimeZoneSysVar
{
public:
  ObSysVarTimeZone() : ObTimeZoneSysVar(ObSysVarOnCheckFuncs::check_and_convert_time_zone, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_TIME_ZONE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(16); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(16); }
};
class ObSysVarTxIsolation : public ObSessionSpecialVarcharSysVar
{
public:
  ObSysVarTxIsolation() : ObSessionSpecialVarcharSysVar(ObSysVarOnCheckFuncs::check_and_convert_tx_isolation, ObSysVarOnUpdateFuncs::update_tx_isolation, ObSysVarSessionSpecialUpdateFuncs::update_tx_isolation, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_TX_ISOLATION; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(17); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(17); }
};
class ObSysVarVersionComment : public ObVarcharSysVar
{
public:
  ObSysVarVersionComment() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_VERSION_COMMENT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(18); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(18); }
};
class ObSysVarWaitTimeout : public ObIntSysVar
{
public:
  ObSysVarWaitTimeout() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_WAIT_TIMEOUT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(19); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(19); }
};
class ObSysVarBinlogRowImage : public ObEnumSysVar
{
public:
  const static char * BINLOG_ROW_IMAGE_NAMES[];
public:
  ObSysVarBinlogRowImage() : ObEnumSysVar(BINLOG_ROW_IMAGE_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_BINLOG_ROW_IMAGE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(20); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(20); }
};
class ObSysVarCharacterSetFilesystem : public ObCharsetSysVar
{
public:
  ObSysVarCharacterSetFilesystem() : ObCharsetSysVar(ObSysVarOnCheckFuncs::check_and_convert_charset_not_null, NULL, ObSysVarToObjFuncs::to_obj_charset, ObSysVarToStrFuncs::to_str_charset, ObSysVarGetMetaTypeFuncs::get_meta_type_varchar) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_CHARACTER_SET_FILESYSTEM; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(21); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(21); }
};
class ObSysVarConnectTimeout : public ObIntSysVar
{
public:
  ObSysVarConnectTimeout() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_CONNECT_TIMEOUT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(22); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(22); }
};
class ObSysVarDatadir : public ObVarcharSysVar
{
public:
  ObSysVarDatadir() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_DATADIR; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(23); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(23); }
};
class ObSysVarDebugSync : public ObVarcharSysVar
{
public:
  ObSysVarDebugSync() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_DEBUG_SYNC; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(24); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(24); }
};
class ObSysVarDivPrecisionIncrement : public ObIntSysVar
{
public:
  ObSysVarDivPrecisionIncrement() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_DIV_PRECISION_INCREMENT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(25); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(25); }
};
class ObSysVarExplicitDefaultsForTimestamp : public ObBoolSysVar
{
public:
  ObSysVarExplicitDefaultsForTimestamp() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_EXPLICIT_DEFAULTS_FOR_TIMESTAMP; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(26); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(26); }
};
class ObSysVarGroupConcatMaxLen : public ObIntSysVar
{
public:
  ObSysVarGroupConcatMaxLen() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_GROUP_CONCAT_MAX_LEN; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(27); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(27); }
};
class ObSysVarIdentity : public ObSessionSpecialIntSysVar
{
public:
  ObSysVarIdentity() : ObSessionSpecialIntSysVar(NULL, ObSysVarSessionSpecialUpdateFuncs::update_identity, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_IDENTITY; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(28); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(28); }
};
class ObSysVarLowerCaseTableNames : public ObIntSysVar
{
public:
  ObSysVarLowerCaseTableNames() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_LOWER_CASE_TABLE_NAMES; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(29); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(29); }
};
class ObSysVarNetReadTimeout : public ObIntSysVar
{
public:
  ObSysVarNetReadTimeout() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_NET_READ_TIMEOUT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(30); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(30); }
};
class ObSysVarNetWriteTimeout : public ObIntSysVar
{
public:
  ObSysVarNetWriteTimeout() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_NET_WRITE_TIMEOUT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(31); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(31); }
};
class ObSysVarReadOnly : public ObBoolSysVar
{
public:
  ObSysVarReadOnly() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_READ_ONLY; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(32); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(32); }
};
class ObSysVarSqlAutoIsNull : public ObBoolSysVar
{
public:
  ObSysVarSqlAutoIsNull() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SQL_AUTO_IS_NULL; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(33); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(33); }
};
class ObSysVarSqlSelectLimit : public ObIntSysVar
{
public:
  ObSysVarSqlSelectLimit() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SQL_SELECT_LIMIT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(34); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(34); }
};
class ObSysVarTimestamp : public ObNumericSysVar
{
public:
  ObSysVarTimestamp() : ObNumericSysVar(ObSysVarOnCheckFuncs::check_and_convert_max_min_timestamp, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_TIMESTAMP; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(35); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(35); }
};
class ObSysVarTxReadOnly : public ObSessionSpecialBoolSysVar
{
public:
  ObSysVarTxReadOnly() : ObSessionSpecialBoolSysVar(ObSysVarOnCheckFuncs::check_and_convert_tx_read_only, ObSysVarOnUpdateFuncs::update_tx_read_only_no_scope, ObSysVarSessionSpecialUpdateFuncs::update_tx_read_only, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_TX_READ_ONLY; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(36); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(36); }
};
class ObSysVarVersion : public ObVarcharSysVar
{
public:
  ObSysVarVersion() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_VERSION; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(37); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(37); }
};
class ObSysVarSqlWarnings : public ObBoolSysVar
{
public:
  ObSysVarSqlWarnings() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SQL_WARNINGS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(38); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(38); }
};
class ObSysVarMaxUserConnections : public ObIntSysVar
{
public:
  ObSysVarMaxUserConnections() : ObIntSysVar(ObSysVarOnCheckFuncs::check_and_convert_max_user_connections, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_MAX_USER_CONNECTIONS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(39); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(39); }
};
class ObSysVarInitConnect : public ObVarcharSysVar
{
public:
  ObSysVarInitConnect() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INIT_CONNECT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(40); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(40); }
};
class ObSysVarLicense : public ObVarcharSysVar
{
public:
  ObSysVarLicense() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_LICENSE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(41); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(41); }
};
class ObSysVarNetBufferLength : public ObIntSysVar
{
public:
  ObSysVarNetBufferLength() : ObIntSysVar(ObSysVarOnCheckFuncs::check_and_convert_net_buffer_length, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_NET_BUFFER_LENGTH; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(42); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(42); }
};
class ObSysVarSystemTimeZone : public ObVarcharSysVar
{
public:
  ObSysVarSystemTimeZone() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SYSTEM_TIME_ZONE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(43); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(43); }
};
class ObSysVarQueryCacheSize : public ObIntSysVar
{
public:
  ObSysVarQueryCacheSize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_QUERY_CACHE_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(44); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(44); }
};
class ObSysVarQueryCacheType : public ObEnumSysVar
{
public:
  const static char * QUERY_CACHE_TYPE_NAMES[];
public:
  ObSysVarQueryCacheType() : ObEnumSysVar(QUERY_CACHE_TYPE_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_QUERY_CACHE_TYPE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(45); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(45); }
};
class ObSysVarSqlQuoteShowCreate : public ObBoolSysVar
{
public:
  ObSysVarSqlQuoteShowCreate() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SQL_QUOTE_SHOW_CREATE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(46); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(46); }
};
class ObSysVarMaxSpRecursionDepth : public ObIntSysVar
{
public:
  ObSysVarMaxSpRecursionDepth() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_MAX_SP_RECURSION_DEPTH; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(47); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(47); }
};
class ObSysVarSqlSafeUpdates : public ObBoolSysVar
{
public:
  ObSysVarSqlSafeUpdates() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SQL_SAFE_UPDATES; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(48); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(48); }
};
class ObSysVarConcurrentInsert : public ObVarcharSysVar
{
public:
  ObSysVarConcurrentInsert() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_CONCURRENT_INSERT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(49); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(49); }
};
class ObSysVarDefaultAuthenticationPlugin : public ObVarcharSysVar
{
public:
  ObSysVarDefaultAuthenticationPlugin() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_DEFAULT_AUTHENTICATION_PLUGIN; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(50); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(50); }
};
class ObSysVarDisabledStorageEngines : public ObVarcharSysVar
{
public:
  ObSysVarDisabledStorageEngines() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_DISABLED_STORAGE_ENGINES; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(51); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(51); }
};
class ObSysVarErrorCount : public ObIntSysVar
{
public:
  ObSysVarErrorCount() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_ERROR_COUNT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(52); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(52); }
};
class ObSysVarGeneralLog : public ObBoolSysVar
{
public:
  ObSysVarGeneralLog() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_GENERAL_LOG; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(53); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(53); }
};
class ObSysVarHaveOpenssl : public ObVarcharSysVar
{
public:
  ObSysVarHaveOpenssl() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_HAVE_OPENSSL; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(54); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(54); }
};
class ObSysVarHaveProfiling : public ObVarcharSysVar
{
public:
  ObSysVarHaveProfiling() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_HAVE_PROFILING; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(55); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(55); }
};
class ObSysVarHaveSsl : public ObVarcharSysVar
{
public:
  ObSysVarHaveSsl() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_HAVE_SSL; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(56); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(56); }
};
class ObSysVarHostname : public ObVarcharSysVar
{
public:
  ObSysVarHostname() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_HOSTNAME; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(57); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(57); }
};
class ObSysVarLcMessages : public ObVarcharSysVar
{
public:
  ObSysVarLcMessages() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_LC_MESSAGES; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(58); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(58); }
};
class ObSysVarLocalInfile : public ObBoolSysVar
{
public:
  ObSysVarLocalInfile() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_LOCAL_INFILE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(59); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(59); }
};
class ObSysVarLockWaitTimeout : public ObIntSysVar
{
public:
  ObSysVarLockWaitTimeout() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_LOCK_WAIT_TIMEOUT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(60); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(60); }
};
class ObSysVarLongQueryTime : public ObNumericSysVar
{
public:
  ObSysVarLongQueryTime() : ObNumericSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_LONG_QUERY_TIME; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(61); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(61); }
};
class ObSysVarMaxConnections : public ObIntSysVar
{
public:
  ObSysVarMaxConnections() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_MAX_CONNECTIONS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(62); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(62); }
};
class ObSysVarMaxExecutionTime : public ObIntSysVar
{
public:
  ObSysVarMaxExecutionTime() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_MAX_EXECUTION_TIME; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(63); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(63); }
};
class ObSysVarProtocolVersion : public ObIntSysVar
{
public:
  ObSysVarProtocolVersion() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_PROTOCOL_VERSION; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(64); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(64); }
};
class ObSysVarServerId : public ObIntSysVar
{
public:
  ObSysVarServerId() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SERVER_ID; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(65); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(65); }
};
class ObSysVarSslCa : public ObVarcharSysVar
{
public:
  ObSysVarSslCa() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SSL_CA; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(66); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(66); }
};
class ObSysVarSslCapath : public ObVarcharSysVar
{
public:
  ObSysVarSslCapath() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SSL_CAPATH; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(67); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(67); }
};
class ObSysVarSslCert : public ObVarcharSysVar
{
public:
  ObSysVarSslCert() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SSL_CERT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(68); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(68); }
};
class ObSysVarSslCipher : public ObVarcharSysVar
{
public:
  ObSysVarSslCipher() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SSL_CIPHER; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(69); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(69); }
};
class ObSysVarSslCrl : public ObVarcharSysVar
{
public:
  ObSysVarSslCrl() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SSL_CRL; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(70); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(70); }
};
class ObSysVarSslCrlpath : public ObVarcharSysVar
{
public:
  ObSysVarSslCrlpath() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SSL_CRLPATH; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(71); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(71); }
};
class ObSysVarSslKey : public ObVarcharSysVar
{
public:
  ObSysVarSslKey() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SSL_KEY; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(72); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(72); }
};
class ObSysVarTimeFormat : public ObVarcharSysVar
{
public:
  ObSysVarTimeFormat() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_TIME_FORMAT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(73); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(73); }
};
class ObSysVarTlsVersion : public ObVarcharSysVar
{
public:
  ObSysVarTlsVersion() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_TLS_VERSION; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(74); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(74); }
};
class ObSysVarTmpTableSize : public ObIntSysVar
{
public:
  ObSysVarTmpTableSize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_TMP_TABLE_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(75); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(75); }
};
class ObSysVarTmpdir : public ObVarcharSysVar
{
public:
  ObSysVarTmpdir() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_TMPDIR; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(76); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(76); }
};
class ObSysVarUniqueChecks : public ObBoolSysVar
{
public:
  ObSysVarUniqueChecks() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_UNIQUE_CHECKS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(77); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(77); }
};
class ObSysVarVersionCompileMachine : public ObVarcharSysVar
{
public:
  ObSysVarVersionCompileMachine() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_VERSION_COMPILE_MACHINE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(78); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(78); }
};
class ObSysVarVersionCompileOs : public ObVarcharSysVar
{
public:
  ObSysVarVersionCompileOs() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_VERSION_COMPILE_OS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(79); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(79); }
};
class ObSysVarWarningCount : public ObIntSysVar
{
public:
  ObSysVarWarningCount() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_WARNING_COUNT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(80); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(80); }
};
class ObSysVarSessionTrackSchema : public ObBoolSysVar
{
public:
  ObSysVarSessionTrackSchema() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SESSION_TRACK_SCHEMA; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(81); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(81); }
};
class ObSysVarSessionTrackSystemVariables : public ObVarcharSysVar
{
public:
  ObSysVarSessionTrackSystemVariables() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SESSION_TRACK_SYSTEM_VARIABLES; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(82); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(82); }
};
class ObSysVarSessionTrackStateChange : public ObBoolSysVar
{
public:
  ObSysVarSessionTrackStateChange() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SESSION_TRACK_STATE_CHANGE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(83); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(83); }
};
class ObSysVarHaveQueryCache : public ObVarcharSysVar
{
public:
  ObSysVarHaveQueryCache() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_HAVE_QUERY_CACHE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(84); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(84); }
};
class ObSysVarQueryCacheLimit : public ObIntSysVar
{
public:
  ObSysVarQueryCacheLimit() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_QUERY_CACHE_LIMIT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(85); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(85); }
};
class ObSysVarQueryCacheMinResUnit : public ObIntSysVar
{
public:
  ObSysVarQueryCacheMinResUnit() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_QUERY_CACHE_MIN_RES_UNIT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(86); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(86); }
};
class ObSysVarQueryCacheWlockInvalidate : public ObBoolSysVar
{
public:
  ObSysVarQueryCacheWlockInvalidate() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_QUERY_CACHE_WLOCK_INVALIDATE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(87); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(87); }
};
class ObSysVarBinlogFormat : public ObEnumSysVar
{
public:
  const static char * BINLOG_FORMAT_NAMES[];
public:
  ObSysVarBinlogFormat() : ObEnumSysVar(BINLOG_FORMAT_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_BINLOG_FORMAT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(88); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(88); }
};
class ObSysVarBinlogChecksum : public ObVarcharSysVar
{
public:
  ObSysVarBinlogChecksum() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_BINLOG_CHECKSUM; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(89); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(89); }
};
class ObSysVarBinlogRowsQueryLogEvents : public ObBoolSysVar
{
public:
  ObSysVarBinlogRowsQueryLogEvents() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_BINLOG_ROWS_QUERY_LOG_EVENTS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(90); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(90); }
};
class ObSysVarLogBin : public ObBoolSysVar
{
public:
  ObSysVarLogBin() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_LOG_BIN; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(91); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(91); }
};
class ObSysVarServerUuid : public ObVarcharSysVar
{
public:
  ObSysVarServerUuid() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SERVER_UUID; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(92); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(92); }
};
class ObSysVarDefaultStorageEngine : public ObVarcharSysVar
{
public:
  ObSysVarDefaultStorageEngine() : ObVarcharSysVar(ObSysVarOnCheckFuncs::check_and_convert_default_storage_engine, NULL, ObSysVarToObjFuncs::to_obj_default_storage_engine, ObSysVarToStrFuncs::to_str_default_storage_engine, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_DEFAULT_STORAGE_ENGINE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(93); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(93); }
};
class ObSysVarCteMaxRecursionDepth : public ObIntSysVar
{
public:
  ObSysVarCteMaxRecursionDepth() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_CTE_MAX_RECURSION_DEPTH; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(94); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(94); }
};
class ObSysVarRegexpStackLimit : public ObIntSysVar
{
public:
  ObSysVarRegexpStackLimit() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_REGEXP_STACK_LIMIT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(95); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(95); }
};
class ObSysVarRegexpTimeLimit : public ObIntSysVar
{
public:
  ObSysVarRegexpTimeLimit() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_REGEXP_TIME_LIMIT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(96); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(96); }
};
class ObSysVarProfiling : public ObEnumSysVar
{
public:
  const static char * PROFILING_NAMES[];
public:
  ObSysVarProfiling() : ObEnumSysVar(PROFILING_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_PROFILING; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(97); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(97); }
};
class ObSysVarProfilingHistorySize : public ObIntSysVar
{
public:
  ObSysVarProfilingHistorySize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_PROFILING_HISTORY_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(98); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(98); }
};
class ObSysVarObLogLevel : public ObVarcharSysVar
{
public:
  ObSysVarObLogLevel() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_OB_LOG_LEVEL; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(99); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(99); }
};
class ObSysVarObQueryTimeout : public ObIntSysVar
{
public:
  ObSysVarObQueryTimeout() : ObIntSysVar(ObSysVarOnCheckFuncs::check_and_convert_timeout_too_large, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_OB_QUERY_TIMEOUT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(100); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(100); }
};
class ObSysVarObReadConsistency : public ObEnumSysVar
{
public:
  const static char * OB_READ_CONSISTENCY_NAMES[];
public:
  ObSysVarObReadConsistency() : ObEnumSysVar(OB_READ_CONSISTENCY_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_OB_READ_CONSISTENCY; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(101); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(101); }
};
class ObSysVarObEnableTransformation : public ObBoolSysVar
{
public:
  ObSysVarObEnableTransformation() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_OB_ENABLE_TRANSFORMATION; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(102); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(102); }
};
class ObSysVarObTrxTimeout : public ObIntSysVar
{
public:
  ObSysVarObTrxTimeout() : ObIntSysVar(ObSysVarOnCheckFuncs::check_and_convert_timeout_too_large, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_OB_TRX_TIMEOUT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(103); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(103); }
};
class ObSysVarObEnablePlanCache : public ObBoolSysVar
{
public:
  ObSysVarObEnablePlanCache() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_OB_ENABLE_PLAN_CACHE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(104); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(104); }
};
class ObSysVarObEnableIndexDirectSelect : public ObBoolSysVar
{
public:
  ObSysVarObEnableIndexDirectSelect() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_OB_ENABLE_INDEX_DIRECT_SELECT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(105); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(105); }
};
class ObSysVarObEnableAggregationPushdown : public ObBoolSysVar
{
public:
  ObSysVarObEnableAggregationPushdown() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_OB_ENABLE_AGGREGATION_PUSHDOWN; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(106); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(106); }
};
class ObSysVarObGlobalDebugSync : public ObVarcharSysVar
{
public:
  ObSysVarObGlobalDebugSync() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_OB_GLOBAL_DEBUG_SYNC; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(107); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(107); }
};
class ObSysVarObEnableShowTrace : public ObBoolSysVar
{
public:
  ObSysVarObEnableShowTrace() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_OB_ENABLE_SHOW_TRACE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(108); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(108); }
};
class ObSysVarObPlanCachePercentage : public ObIntSysVar
{
public:
  ObSysVarObPlanCachePercentage() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_OB_PLAN_CACHE_PERCENTAGE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(109); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(109); }
};
class ObSysVarObPlanCacheEvictHighPercentage : public ObIntSysVar
{
public:
  ObSysVarObPlanCacheEvictHighPercentage() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_OB_PLAN_CACHE_EVICT_HIGH_PERCENTAGE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(110); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(110); }
};
class ObSysVarObPlanCacheEvictLowPercentage : public ObIntSysVar
{
public:
  ObSysVarObPlanCacheEvictLowPercentage() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_OB_PLAN_CACHE_EVICT_LOW_PERCENTAGE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(111); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(111); }
};
class ObSysVarRecyclebin : public ObBoolSysVar
{
public:
  ObSysVarRecyclebin() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_RECYCLEBIN; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(112); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(112); }
};
class ObSysVarIsResultAccurate : public ObBoolSysVar
{
public:
  ObSysVarIsResultAccurate() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_IS_RESULT_ACCURATE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(113); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(113); }
};
class ObSysVarErrorOnOverlapTime : public ObBoolSysVar
{
public:
  ObSysVarErrorOnOverlapTime() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_ERROR_ON_OVERLAP_TIME; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(114); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(114); }
};
class ObSysVarObSqlWorkAreaPercentage : public ObIntSysVar
{
public:
  ObSysVarObSqlWorkAreaPercentage() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_OB_SQL_WORK_AREA_PERCENTAGE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(115); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(115); }
};
class ObSysVarForeignKeyChecks : public ObBoolSysVar
{
public:
  ObSysVarForeignKeyChecks() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_FOREIGN_KEY_CHECKS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(116); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(116); }
};
class ObSysVarObTcpInvitedNodes : public ObVarcharSysVar
{
public:
  ObSysVarObTcpInvitedNodes() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_OB_TCP_INVITED_NODES; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(117); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(117); }
};
class ObSysVarAutoIncrementCacheSize : public ObIntSysVar
{
public:
  ObSysVarAutoIncrementCacheSize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_AUTO_INCREMENT_CACHE_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(118); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(118); }
};
class ObSysVarParallelServersTarget : public ObIntSysVar
{
public:
  ObSysVarParallelServersTarget() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_PARALLEL_SERVERS_TARGET; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(119); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(119); }
};
class ObSysVarObTrxIdleTimeout : public ObIntSysVar
{
public:
  ObSysVarObTrxIdleTimeout() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_OB_TRX_IDLE_TIMEOUT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(120); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(120); }
};
class ObSysVarBlockEncryptionMode : public ObEnumSysVar
{
public:
  const static char * BLOCK_ENCRYPTION_MODE_NAMES[];
public:
  ObSysVarBlockEncryptionMode() : ObEnumSysVar(BLOCK_ENCRYPTION_MODE_NAMES, ObSysVarOnCheckFuncs::check_and_convert_block_encryption_mode, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_BLOCK_ENCRYPTION_MODE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(121); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(121); }
};
class ObSysVarNljBatchingEnabled : public ObBoolSysVar
{
public:
  ObSysVarNljBatchingEnabled() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR__NLJ_BATCHING_ENABLED; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(122); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(122); }
};
class ObSysVarTransactionIsolation : public ObSessionSpecialVarcharSysVar
{
public:
  ObSysVarTransactionIsolation() : ObSessionSpecialVarcharSysVar(ObSysVarOnCheckFuncs::check_and_convert_tx_isolation, ObSysVarOnUpdateFuncs::update_tx_isolation, ObSysVarSessionSpecialUpdateFuncs::update_tx_isolation, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_TRANSACTION_ISOLATION; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(123); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(123); }
};
class ObSysVarObTrxLockTimeout : public ObIntSysVar
{
public:
  ObSysVarObTrxLockTimeout() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_OB_TRX_LOCK_TIMEOUT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(124); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(124); }
};
class ObSysVarValidatePasswordCheckUserName : public ObEnumSysVar
{
public:
  const static char * VALIDATE_PASSWORD_CHECK_USER_NAME_NAMES[];
public:
  ObSysVarValidatePasswordCheckUserName() : ObEnumSysVar(VALIDATE_PASSWORD_CHECK_USER_NAME_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_VALIDATE_PASSWORD_CHECK_USER_NAME; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(125); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(125); }
};
class ObSysVarValidatePasswordLength : public ObIntSysVar
{
public:
  ObSysVarValidatePasswordLength() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_VALIDATE_PASSWORD_LENGTH; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(126); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(126); }
};
class ObSysVarValidatePasswordMixedCaseCount : public ObIntSysVar
{
public:
  ObSysVarValidatePasswordMixedCaseCount() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_VALIDATE_PASSWORD_MIXED_CASE_COUNT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(127); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(127); }
};
class ObSysVarValidatePasswordNumberCount : public ObIntSysVar
{
public:
  ObSysVarValidatePasswordNumberCount() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_VALIDATE_PASSWORD_NUMBER_COUNT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(128); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(128); }
};
class ObSysVarValidatePasswordPolicy : public ObEnumSysVar
{
public:
  const static char * VALIDATE_PASSWORD_POLICY_NAMES[];
public:
  ObSysVarValidatePasswordPolicy() : ObEnumSysVar(VALIDATE_PASSWORD_POLICY_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_VALIDATE_PASSWORD_POLICY; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(129); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(129); }
};
class ObSysVarValidatePasswordSpecialCharCount : public ObIntSysVar
{
public:
  ObSysVarValidatePasswordSpecialCharCount() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_VALIDATE_PASSWORD_SPECIAL_CHAR_COUNT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(130); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(130); }
};
class ObSysVarDefaultPasswordLifetime : public ObIntSysVar
{
public:
  ObSysVarDefaultPasswordLifetime() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_DEFAULT_PASSWORD_LIFETIME; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(131); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(131); }
};
class ObSysVarEnableParallelDml : public ObBoolSysVar
{
public:
  ObSysVarEnableParallelDml() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR__ENABLE_PARALLEL_DML; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(132); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(132); }
};
class ObSysVarSecureFilePriv : public ObVarcharSysVar
{
public:
  ObSysVarSecureFilePriv() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SECURE_FILE_PRIV; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(133); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(133); }
};
class ObSysVarEnableParallelQuery : public ObBoolSysVar
{
public:
  ObSysVarEnableParallelQuery() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR__ENABLE_PARALLEL_QUERY; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(134); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(134); }
};
class ObSysVarForceParallelQueryDop : public ObIntSysVar
{
public:
  ObSysVarForceParallelQueryDop() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR__FORCE_PARALLEL_QUERY_DOP; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(135); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(135); }
};
class ObSysVarForceParallelDmlDop : public ObIntSysVar
{
public:
  ObSysVarForceParallelDmlDop() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR__FORCE_PARALLEL_DML_DOP; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(136); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(136); }
};
class ObSysVarObPlBlockTimeout : public ObIntSysVar
{
public:
  ObSysVarObPlBlockTimeout() : ObIntSysVar(ObSysVarOnCheckFuncs::check_and_convert_timeout_too_large, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_OB_PL_BLOCK_TIMEOUT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(137); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(137); }
};
class ObSysVarTransactionReadOnly : public ObSessionSpecialBoolSysVar
{
public:
  ObSysVarTransactionReadOnly() : ObSessionSpecialBoolSysVar(ObSysVarOnCheckFuncs::check_and_convert_tx_read_only, ObSysVarOnUpdateFuncs::update_tx_read_only_no_scope, ObSysVarSessionSpecialUpdateFuncs::update_tx_read_only, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_TRANSACTION_READ_ONLY; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(138); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(138); }
};
class ObSysVarPerformanceSchema : public ObBoolSysVar
{
public:
  ObSysVarPerformanceSchema() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_PERFORMANCE_SCHEMA; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(139); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(139); }
};
class ObSysVarEnableParallelDdl : public ObBoolSysVar
{
public:
  ObSysVarEnableParallelDdl() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR__ENABLE_PARALLEL_DDL; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(140); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(140); }
};
class ObSysVarForceParallelDdlDop : public ObIntSysVar
{
public:
  ObSysVarForceParallelDdlDop() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR__FORCE_PARALLEL_DDL_DOP; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(141); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(141); }
};
class ObSysVarCursorSharing : public ObEnumSysVar
{
public:
  const static char * CURSOR_SHARING_NAMES[];
public:
  ObSysVarCursorSharing() : ObEnumSysVar(CURSOR_SHARING_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_CURSOR_SHARING; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(142); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(142); }
};
class ObSysVarAggregationOptimizationSettings : public ObIntSysVar
{
public:
  ObSysVarAggregationOptimizationSettings() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR__AGGREGATION_OPTIMIZATION_SETTINGS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(143); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(143); }
};
class ObSysVarPxSharedHashJoin : public ObBoolSysVar
{
public:
  ObSysVarPxSharedHashJoin() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR__PX_SHARED_HASH_JOIN; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(144); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(144); }
};
class ObSysVarSqlNotes : public ObBoolSysVar
{
public:
  ObSysVarSqlNotes() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SQL_NOTES; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(145); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(145); }
};
class ObSysVarInnodbStrictMode : public ObBoolSysVar
{
public:
  ObSysVarInnodbStrictMode() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_STRICT_MODE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(146); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(146); }
};
class ObSysVarWindowfuncOptimizationSettings : public ObIntSysVar
{
public:
  ObSysVarWindowfuncOptimizationSettings() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR__WINDOWFUNC_OPTIMIZATION_SETTINGS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(147); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(147); }
};
class ObSysVarLogRowValueOptions : public ObVarcharSysVar
{
public:
  ObSysVarLogRowValueOptions() : ObVarcharSysVar(ObSysVarOnCheckFuncs::check_log_row_value_option_is_valid, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_LOG_ROW_VALUE_OPTIONS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(148); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(148); }
};
class ObSysVarObMaxReadStaleTime : public ObIntSysVar
{
public:
  ObSysVarObMaxReadStaleTime() : ObIntSysVar(ObSysVarOnCheckFuncs::check_and_convert_timeout_too_large, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_OB_MAX_READ_STALE_TIME; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(149); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(149); }
};
class ObSysVarOptimizerGatherStatsOnLoad : public ObBoolSysVar
{
public:
  ObSysVarOptimizerGatherStatsOnLoad() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR__OPTIMIZER_GATHER_STATS_ON_LOAD; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(150); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(150); }
};
class ObSysVarShowDdlInCompatMode : public ObBoolSysVar
{
public:
  ObSysVarShowDdlInCompatMode() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR__SHOW_DDL_IN_COMPAT_MODE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(151); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(151); }
};
class ObSysVarParallelDegreePolicy : public ObEnumSysVar
{
public:
  const static char * PARALLEL_DEGREE_POLICY_NAMES[];
public:
  ObSysVarParallelDegreePolicy() : ObEnumSysVar(PARALLEL_DEGREE_POLICY_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_PARALLEL_DEGREE_POLICY; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(152); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(152); }
};
class ObSysVarParallelDegreeLimit : public ObIntSysVar
{
public:
  ObSysVarParallelDegreeLimit() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_PARALLEL_DEGREE_LIMIT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(153); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(153); }
};
class ObSysVarParallelMinScanTimeThreshold : public ObIntSysVar
{
public:
  ObSysVarParallelMinScanTimeThreshold() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_PARALLEL_MIN_SCAN_TIME_THRESHOLD; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(154); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(154); }
};
class ObSysVarOptimizerDynamicSampling : public ObIntSysVar
{
public:
  ObSysVarOptimizerDynamicSampling() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_OPTIMIZER_DYNAMIC_SAMPLING; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(155); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(155); }
};
class ObSysVarRuntimeFilterType : public ObVarcharSysVar
{
public:
  ObSysVarRuntimeFilterType() : ObVarcharSysVar(ObSysVarOnCheckFuncs::check_runtime_filter_type_is_valid, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_RUNTIME_FILTER_TYPE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(156); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(156); }
};
class ObSysVarRuntimeFilterWaitTimeMs : public ObIntSysVar
{
public:
  ObSysVarRuntimeFilterWaitTimeMs() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_RUNTIME_FILTER_WAIT_TIME_MS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(157); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(157); }
};
class ObSysVarRuntimeFilterMaxInNum : public ObIntSysVar
{
public:
  ObSysVarRuntimeFilterMaxInNum() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_RUNTIME_FILTER_MAX_IN_NUM; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(158); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(158); }
};
class ObSysVarRuntimeBloomFilterMaxSize : public ObIntSysVar
{
public:
  ObSysVarRuntimeBloomFilterMaxSize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_RUNTIME_BLOOM_FILTER_MAX_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(159); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(159); }
};
class ObSysVarAutomaticSpPrivileges : public ObIntSysVar
{
public:
  ObSysVarAutomaticSpPrivileges() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_AUTOMATIC_SP_PRIVILEGES; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(160); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(160); }
};
class ObSysVarObEnablePlCache : public ObBoolSysVar
{
public:
  ObSysVarObEnablePlCache() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_OB_ENABLE_PL_CACHE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(161); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(161); }
};
class ObSysVarObDefaultLobInrowThreshold : public ObIntSysVar
{
public:
  ObSysVarObDefaultLobInrowThreshold() : ObIntSysVar(ObSysVarOnCheckFuncs::check_default_lob_inrow_threshold, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_OB_DEFAULT_LOB_INROW_THRESHOLD; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(162); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(162); }
};
class ObSysVarEnableStorageCardinalityEstimation : public ObBoolSysVar
{
public:
  ObSysVarEnableStorageCardinalityEstimation() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR__ENABLE_STORAGE_CARDINALITY_ESTIMATION; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(163); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(163); }
};
class ObSysVarLcTimeNames : public ObVarcharSysVar
{
public:
  ObSysVarLcTimeNames() : ObVarcharSysVar(ObSysVarOnCheckFuncs::check_locale_type_is_valid, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_LC_TIME_NAMES; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(164); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(164); }
};
class ObSysVarActivateAllRolesOnLogin : public ObBoolSysVar
{
public:
  ObSysVarActivateAllRolesOnLogin() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_ACTIVATE_ALL_ROLES_ON_LOGIN; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(165); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(165); }
};
class ObSysVarInnodbStatsPersistent : public ObEnumSysVar
{
public:
  const static char * INNODB_STATS_PERSISTENT_NAMES[];
public:
  ObSysVarInnodbStatsPersistent() : ObEnumSysVar(INNODB_STATS_PERSISTENT_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_STATS_PERSISTENT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(166); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(166); }
};
class ObSysVarDebug : public ObVarcharSysVar
{
public:
  ObSysVarDebug() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_DEBUG; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(167); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(167); }
};
class ObSysVarInnodbChangeBufferingDebug : public ObIntSysVar
{
public:
  ObSysVarInnodbChangeBufferingDebug() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_CHANGE_BUFFERING_DEBUG; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(168); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(168); }
};
class ObSysVarInnodbDisableResizeBufferPoolDebug : public ObBoolSysVar
{
public:
  ObSysVarInnodbDisableResizeBufferPoolDebug() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_DISABLE_RESIZE_BUFFER_POOL_DEBUG; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(169); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(169); }
};
class ObSysVarInnodbFilMakePageDirtyDebug : public ObIntSysVar
{
public:
  ObSysVarInnodbFilMakePageDirtyDebug() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_FIL_MAKE_PAGE_DIRTY_DEBUG; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(170); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(170); }
};
class ObSysVarInnodbLimitOptimisticInsertDebug : public ObIntSysVar
{
public:
  ObSysVarInnodbLimitOptimisticInsertDebug() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_LIMIT_OPTIMISTIC_INSERT_DEBUG; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(171); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(171); }
};
class ObSysVarInnodbMergeThresholdSetAllDebug : public ObIntSysVar
{
public:
  ObSysVarInnodbMergeThresholdSetAllDebug() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_MERGE_THRESHOLD_SET_ALL_DEBUG; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(172); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(172); }
};
class ObSysVarInnodbSavedPageNumberDebug : public ObIntSysVar
{
public:
  ObSysVarInnodbSavedPageNumberDebug() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_SAVED_PAGE_NUMBER_DEBUG; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(173); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(173); }
};
class ObSysVarInnodbTrxPurgeViewUpdateOnlyDebug : public ObBoolSysVar
{
public:
  ObSysVarInnodbTrxPurgeViewUpdateOnlyDebug() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_TRX_PURGE_VIEW_UPDATE_ONLY_DEBUG; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(174); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(174); }
};
class ObSysVarInnodbTrxRsegNSlotsDebug : public ObIntSysVar
{
public:
  ObSysVarInnodbTrxRsegNSlotsDebug() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_TRX_RSEG_N_SLOTS_DEBUG; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(175); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(175); }
};
class ObSysVarStoredProgramCache : public ObIntSysVar
{
public:
  ObSysVarStoredProgramCache() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_STORED_PROGRAM_CACHE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(176); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(176); }
};
class ObSysVarCardinalityEstimationModel : public ObEnumSysVar
{
public:
  const static char * CARDINALITY_ESTIMATION_MODEL_NAMES[];
public:
  ObSysVarCardinalityEstimationModel() : ObEnumSysVar(CARDINALITY_ESTIMATION_MODEL_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_CARDINALITY_ESTIMATION_MODEL; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(177); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(177); }
};
class ObSysVarFlush : public ObEnumSysVar
{
public:
  const static char * FLUSH_NAMES[];
public:
  ObSysVarFlush() : ObEnumSysVar(FLUSH_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_FLUSH; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(178); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(178); }
};
class ObSysVarFlushTime : public ObIntSysVar
{
public:
  ObSysVarFlushTime() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_FLUSH_TIME; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(179); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(179); }
};
class ObSysVarInnodbAdaptiveFlushing : public ObEnumSysVar
{
public:
  const static char * INNODB_ADAPTIVE_FLUSHING_NAMES[];
public:
  ObSysVarInnodbAdaptiveFlushing() : ObEnumSysVar(INNODB_ADAPTIVE_FLUSHING_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_ADAPTIVE_FLUSHING; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(180); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(180); }
};
class ObSysVarInnodbAdaptiveFlushingLwm : public ObIntSysVar
{
public:
  ObSysVarInnodbAdaptiveFlushingLwm() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_ADAPTIVE_FLUSHING_LWM; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(181); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(181); }
};
class ObSysVarInnodbAdaptiveHashIndex : public ObEnumSysVar
{
public:
  const static char * INNODB_ADAPTIVE_HASH_INDEX_NAMES[];
public:
  ObSysVarInnodbAdaptiveHashIndex() : ObEnumSysVar(INNODB_ADAPTIVE_HASH_INDEX_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_ADAPTIVE_HASH_INDEX; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(182); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(182); }
};
class ObSysVarInnodbAdaptiveHashIndexParts : public ObIntSysVar
{
public:
  ObSysVarInnodbAdaptiveHashIndexParts() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_ADAPTIVE_HASH_INDEX_PARTS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(183); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(183); }
};
class ObSysVarInnodbAdaptiveMaxSleepDelay : public ObIntSysVar
{
public:
  ObSysVarInnodbAdaptiveMaxSleepDelay() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_ADAPTIVE_MAX_SLEEP_DELAY; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(184); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(184); }
};
class ObSysVarInnodbAutoextendIncrement : public ObIntSysVar
{
public:
  ObSysVarInnodbAutoextendIncrement() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_AUTOEXTEND_INCREMENT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(185); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(185); }
};
class ObSysVarInnodbBackgroundDropListEmpty : public ObEnumSysVar
{
public:
  const static char * INNODB_BACKGROUND_DROP_LIST_EMPTY_NAMES[];
public:
  ObSysVarInnodbBackgroundDropListEmpty() : ObEnumSysVar(INNODB_BACKGROUND_DROP_LIST_EMPTY_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_BACKGROUND_DROP_LIST_EMPTY; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(186); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(186); }
};
class ObSysVarInnodbBufferPoolDumpAtShutdown : public ObEnumSysVar
{
public:
  const static char * INNODB_BUFFER_POOL_DUMP_AT_SHUTDOWN_NAMES[];
public:
  ObSysVarInnodbBufferPoolDumpAtShutdown() : ObEnumSysVar(INNODB_BUFFER_POOL_DUMP_AT_SHUTDOWN_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_BUFFER_POOL_DUMP_AT_SHUTDOWN; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(187); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(187); }
};
class ObSysVarInnodbBufferPoolDumpNow : public ObEnumSysVar
{
public:
  const static char * INNODB_BUFFER_POOL_DUMP_NOW_NAMES[];
public:
  ObSysVarInnodbBufferPoolDumpNow() : ObEnumSysVar(INNODB_BUFFER_POOL_DUMP_NOW_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_BUFFER_POOL_DUMP_NOW; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(188); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(188); }
};
class ObSysVarInnodbBufferPoolDumpPct : public ObIntSysVar
{
public:
  ObSysVarInnodbBufferPoolDumpPct() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_BUFFER_POOL_DUMP_PCT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(189); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(189); }
};
class ObSysVarInnodbBufferPoolFilename : public ObVarcharSysVar
{
public:
  ObSysVarInnodbBufferPoolFilename() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_BUFFER_POOL_FILENAME; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(190); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(190); }
};
class ObSysVarInnodbBufferPoolLoadAbort : public ObEnumSysVar
{
public:
  const static char * INNODB_BUFFER_POOL_LOAD_ABORT_NAMES[];
public:
  ObSysVarInnodbBufferPoolLoadAbort() : ObEnumSysVar(INNODB_BUFFER_POOL_LOAD_ABORT_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_BUFFER_POOL_LOAD_ABORT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(191); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(191); }
};
class ObSysVarInnodbBufferPoolLoadNow : public ObEnumSysVar
{
public:
  const static char * INNODB_BUFFER_POOL_LOAD_NOW_NAMES[];
public:
  ObSysVarInnodbBufferPoolLoadNow() : ObEnumSysVar(INNODB_BUFFER_POOL_LOAD_NOW_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_BUFFER_POOL_LOAD_NOW; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(192); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(192); }
};
class ObSysVarInnodbBufferPoolSize : public ObIntSysVar
{
public:
  ObSysVarInnodbBufferPoolSize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_BUFFER_POOL_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(193); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(193); }
};
class ObSysVarInnodbChangeBufferMaxSize : public ObIntSysVar
{
public:
  ObSysVarInnodbChangeBufferMaxSize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_CHANGE_BUFFER_MAX_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(194); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(194); }
};
class ObSysVarInnodbChangeBuffering : public ObEnumSysVar
{
public:
  const static char * INNODB_CHANGE_BUFFERING_NAMES[];
public:
  ObSysVarInnodbChangeBuffering() : ObEnumSysVar(INNODB_CHANGE_BUFFERING_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_CHANGE_BUFFERING; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(195); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(195); }
};
class ObSysVarInnodbChecksumAlgorithm : public ObEnumSysVar
{
public:
  const static char * INNODB_CHECKSUM_ALGORITHM_NAMES[];
public:
  ObSysVarInnodbChecksumAlgorithm() : ObEnumSysVar(INNODB_CHECKSUM_ALGORITHM_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_CHECKSUM_ALGORITHM; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(196); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(196); }
};
class ObSysVarInnodbCmpPerIndexEnabled : public ObEnumSysVar
{
public:
  const static char * INNODB_CMP_PER_INDEX_ENABLED_NAMES[];
public:
  ObSysVarInnodbCmpPerIndexEnabled() : ObEnumSysVar(INNODB_CMP_PER_INDEX_ENABLED_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_CMP_PER_INDEX_ENABLED; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(197); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(197); }
};
class ObSysVarInnodbCommitConcurrency : public ObIntSysVar
{
public:
  ObSysVarInnodbCommitConcurrency() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_COMMIT_CONCURRENCY; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(198); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(198); }
};
class ObSysVarInnodbCompressionFailureThresholdPct : public ObIntSysVar
{
public:
  ObSysVarInnodbCompressionFailureThresholdPct() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_COMPRESSION_FAILURE_THRESHOLD_PCT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(199); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(199); }
};
class ObSysVarInnodbCompressionLevel : public ObIntSysVar
{
public:
  ObSysVarInnodbCompressionLevel() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_COMPRESSION_LEVEL; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(200); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(200); }
};
class ObSysVarInnodbCompressionPadPctMax : public ObIntSysVar
{
public:
  ObSysVarInnodbCompressionPadPctMax() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_COMPRESSION_PAD_PCT_MAX; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(201); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(201); }
};
class ObSysVarInnodbConcurrencyTickets : public ObIntSysVar
{
public:
  ObSysVarInnodbConcurrencyTickets() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_CONCURRENCY_TICKETS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(202); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(202); }
};
class ObSysVarInnodbDefaultRowFormat : public ObEnumSysVar
{
public:
  const static char * INNODB_DEFAULT_ROW_FORMAT_NAMES[];
public:
  ObSysVarInnodbDefaultRowFormat() : ObEnumSysVar(INNODB_DEFAULT_ROW_FORMAT_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_DEFAULT_ROW_FORMAT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(203); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(203); }
};
class ObSysVarInnodbDisableSortFileCache : public ObEnumSysVar
{
public:
  const static char * INNODB_DISABLE_SORT_FILE_CACHE_NAMES[];
public:
  ObSysVarInnodbDisableSortFileCache() : ObEnumSysVar(INNODB_DISABLE_SORT_FILE_CACHE_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_DISABLE_SORT_FILE_CACHE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(204); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(204); }
};
class ObSysVarInnodbFileFormat : public ObEnumSysVar
{
public:
  const static char * INNODB_FILE_FORMAT_NAMES[];
public:
  ObSysVarInnodbFileFormat() : ObEnumSysVar(INNODB_FILE_FORMAT_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_FILE_FORMAT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(205); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(205); }
};
class ObSysVarInnodbFileFormatMax : public ObEnumSysVar
{
public:
  const static char * INNODB_FILE_FORMAT_MAX_NAMES[];
public:
  ObSysVarInnodbFileFormatMax() : ObEnumSysVar(INNODB_FILE_FORMAT_MAX_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_FILE_FORMAT_MAX; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(206); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(206); }
};
class ObSysVarInnodbFilePerTable : public ObEnumSysVar
{
public:
  const static char * INNODB_FILE_PER_TABLE_NAMES[];
public:
  ObSysVarInnodbFilePerTable() : ObEnumSysVar(INNODB_FILE_PER_TABLE_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_FILE_PER_TABLE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(207); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(207); }
};
class ObSysVarInnodbFillFactor : public ObIntSysVar
{
public:
  ObSysVarInnodbFillFactor() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_FILL_FACTOR; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(208); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(208); }
};
class ObSysVarInnodbFlushNeighbors : public ObEnumSysVar
{
public:
  const static char * INNODB_FLUSH_NEIGHBORS_NAMES[];
public:
  ObSysVarInnodbFlushNeighbors() : ObEnumSysVar(INNODB_FLUSH_NEIGHBORS_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_FLUSH_NEIGHBORS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(209); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(209); }
};
class ObSysVarInnodbFlushSync : public ObEnumSysVar
{
public:
  const static char * INNODB_FLUSH_SYNC_NAMES[];
public:
  ObSysVarInnodbFlushSync() : ObEnumSysVar(INNODB_FLUSH_SYNC_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_FLUSH_SYNC; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(210); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(210); }
};
class ObSysVarInnodbFlushingAvgLoops : public ObIntSysVar
{
public:
  ObSysVarInnodbFlushingAvgLoops() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_FLUSHING_AVG_LOOPS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(211); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(211); }
};
class ObSysVarInnodbLruScanDepth : public ObIntSysVar
{
public:
  ObSysVarInnodbLruScanDepth() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_LRU_SCAN_DEPTH; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(212); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(212); }
};
class ObSysVarInnodbMaxDirtyPagesPct : public ObNumericSysVar
{
public:
  ObSysVarInnodbMaxDirtyPagesPct() : ObNumericSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_MAX_DIRTY_PAGES_PCT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(213); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(213); }
};
class ObSysVarInnodbMaxDirtyPagesPctLwm : public ObNumericSysVar
{
public:
  ObSysVarInnodbMaxDirtyPagesPctLwm() : ObNumericSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_MAX_DIRTY_PAGES_PCT_LWM; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(214); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(214); }
};
class ObSysVarInnodbMaxPurgeLag : public ObIntSysVar
{
public:
  ObSysVarInnodbMaxPurgeLag() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_MAX_PURGE_LAG; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(215); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(215); }
};
class ObSysVarInnodbMaxPurgeLagDelay : public ObIntSysVar
{
public:
  ObSysVarInnodbMaxPurgeLagDelay() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_MAX_PURGE_LAG_DELAY; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(216); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(216); }
};
class ObSysVarHaveSymlink : public ObEnumSysVar
{
public:
  const static char * HAVE_SYMLINK_NAMES[];
public:
  ObSysVarHaveSymlink() : ObEnumSysVar(HAVE_SYMLINK_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_HAVE_SYMLINK; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(217); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(217); }
};
class ObSysVarIgnoreBuiltinInnodb : public ObEnumSysVar
{
public:
  const static char * IGNORE_BUILTIN_INNODB_NAMES[];
public:
  ObSysVarIgnoreBuiltinInnodb() : ObEnumSysVar(IGNORE_BUILTIN_INNODB_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_IGNORE_BUILTIN_INNODB; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(218); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(218); }
};
class ObSysVarInnodbBufferPoolChunkSize : public ObIntSysVar
{
public:
  ObSysVarInnodbBufferPoolChunkSize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_BUFFER_POOL_CHUNK_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(219); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(219); }
};
class ObSysVarInnodbBufferPoolInstances : public ObIntSysVar
{
public:
  ObSysVarInnodbBufferPoolInstances() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_BUFFER_POOL_INSTANCES; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(220); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(220); }
};
class ObSysVarInnodbBufferPoolLoadAtStartup : public ObEnumSysVar
{
public:
  const static char * INNODB_BUFFER_POOL_LOAD_AT_STARTUP_NAMES[];
public:
  ObSysVarInnodbBufferPoolLoadAtStartup() : ObEnumSysVar(INNODB_BUFFER_POOL_LOAD_AT_STARTUP_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_BUFFER_POOL_LOAD_AT_STARTUP; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(221); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(221); }
};
class ObSysVarInnodbChecksums : public ObEnumSysVar
{
public:
  const static char * INNODB_CHECKSUMS_NAMES[];
public:
  ObSysVarInnodbChecksums() : ObEnumSysVar(INNODB_CHECKSUMS_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_CHECKSUMS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(222); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(222); }
};
class ObSysVarInnodbDoublewrite : public ObEnumSysVar
{
public:
  const static char * INNODB_DOUBLEWRITE_NAMES[];
public:
  ObSysVarInnodbDoublewrite() : ObEnumSysVar(INNODB_DOUBLEWRITE_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_DOUBLEWRITE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(223); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(223); }
};
class ObSysVarInnodbFileFormatCheck : public ObEnumSysVar
{
public:
  const static char * INNODB_FILE_FORMAT_CHECK_NAMES[];
public:
  ObSysVarInnodbFileFormatCheck() : ObEnumSysVar(INNODB_FILE_FORMAT_CHECK_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_FILE_FORMAT_CHECK; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(224); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(224); }
};
class ObSysVarInnodbFlushMethod : public ObEnumSysVar
{
public:
  const static char * INNODB_FLUSH_METHOD_NAMES[];
public:
  ObSysVarInnodbFlushMethod() : ObEnumSysVar(INNODB_FLUSH_METHOD_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_FLUSH_METHOD; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(225); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(225); }
};
class ObSysVarInnodbForceLoadCorrupted : public ObEnumSysVar
{
public:
  const static char * INNODB_FORCE_LOAD_CORRUPTED_NAMES[];
public:
  ObSysVarInnodbForceLoadCorrupted() : ObEnumSysVar(INNODB_FORCE_LOAD_CORRUPTED_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_FORCE_LOAD_CORRUPTED; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(226); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(226); }
};
class ObSysVarInnodbPageSize : public ObEnumSysVar
{
public:
  const static char * INNODB_PAGE_SIZE_NAMES[];
public:
  ObSysVarInnodbPageSize() : ObEnumSysVar(INNODB_PAGE_SIZE_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_PAGE_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(227); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(227); }
};
class ObSysVarInnodbVersion : public ObEnumSysVar
{
public:
  const static char * INNODB_VERSION_NAMES[];
public:
  ObSysVarInnodbVersion() : ObEnumSysVar(INNODB_VERSION_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_VERSION; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(228); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(228); }
};
class ObSysVarMyisamMmapSize : public ObIntSysVar
{
public:
  ObSysVarMyisamMmapSize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_MYISAM_MMAP_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(229); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(229); }
};
class ObSysVarTableOpenCacheInstances : public ObIntSysVar
{
public:
  ObSysVarTableOpenCacheInstances() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_TABLE_OPEN_CACHE_INSTANCES; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(230); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(230); }
};
class ObSysVarGtidExecuted : public ObVarcharSysVar
{
public:
  ObSysVarGtidExecuted() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_GTID_EXECUTED; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(231); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(231); }
};
class ObSysVarGtidOwned : public ObVarcharSysVar
{
public:
  ObSysVarGtidOwned() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_GTID_OWNED; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(232); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(232); }
};
class ObSysVarInnodbRollbackOnTimeout : public ObBoolSysVar
{
public:
  ObSysVarInnodbRollbackOnTimeout() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_ROLLBACK_ON_TIMEOUT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(233); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(233); }
};
class ObSysVarCompletionType : public ObEnumSysVar
{
public:
  const static char * COMPLETION_TYPE_NAMES[];
public:
  ObSysVarCompletionType() : ObEnumSysVar(COMPLETION_TYPE_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_COMPLETION_TYPE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(234); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(234); }
};
class ObSysVarEnforceGtidConsistency : public ObEnumSysVar
{
public:
  const static char * ENFORCE_GTID_CONSISTENCY_NAMES[];
public:
  ObSysVarEnforceGtidConsistency() : ObEnumSysVar(ENFORCE_GTID_CONSISTENCY_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_ENFORCE_GTID_CONSISTENCY; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(235); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(235); }
};
class ObSysVarGtidExecutedCompressionPeriod : public ObIntSysVar
{
public:
  ObSysVarGtidExecutedCompressionPeriod() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_GTID_EXECUTED_COMPRESSION_PERIOD; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(236); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(236); }
};
class ObSysVarGtidMode : public ObEnumSysVar
{
public:
  const static char * GTID_MODE_NAMES[];
public:
  ObSysVarGtidMode() : ObEnumSysVar(GTID_MODE_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_GTID_MODE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(237); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(237); }
};
class ObSysVarGtidNext : public ObEnumSysVar
{
public:
  const static char * GTID_NEXT_NAMES[];
public:
  ObSysVarGtidNext() : ObEnumSysVar(GTID_NEXT_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_GTID_NEXT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(238); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(238); }
};
class ObSysVarGtidPurged : public ObVarcharSysVar
{
public:
  ObSysVarGtidPurged() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_GTID_PURGED; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(239); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(239); }
};
class ObSysVarInnodbApiBkCommitInterval : public ObIntSysVar
{
public:
  ObSysVarInnodbApiBkCommitInterval() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_API_BK_COMMIT_INTERVAL; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(240); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(240); }
};
class ObSysVarInnodbApiTrxLevel : public ObIntSysVar
{
public:
  ObSysVarInnodbApiTrxLevel() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_API_TRX_LEVEL; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(241); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(241); }
};
class ObSysVarSessionTrackGtids : public ObEnumSysVar
{
public:
  const static char * SESSION_TRACK_GTIDS_NAMES[];
public:
  ObSysVarSessionTrackGtids() : ObEnumSysVar(SESSION_TRACK_GTIDS_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SESSION_TRACK_GTIDS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(242); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(242); }
};
class ObSysVarSessionTrackTransactionInfo : public ObEnumSysVar
{
public:
  const static char * SESSION_TRACK_TRANSACTION_INFO_NAMES[];
public:
  ObSysVarSessionTrackTransactionInfo() : ObEnumSysVar(SESSION_TRACK_TRANSACTION_INFO_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SESSION_TRACK_TRANSACTION_INFO; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(243); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(243); }
};
class ObSysVarTransactionAllocBlockSize : public ObIntSysVar
{
public:
  ObSysVarTransactionAllocBlockSize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_TRANSACTION_ALLOC_BLOCK_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(244); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(244); }
};
class ObSysVarTransactionAllowBatching : public ObBoolSysVar
{
public:
  ObSysVarTransactionAllowBatching() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_TRANSACTION_ALLOW_BATCHING; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(245); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(245); }
};
class ObSysVarTransactionPreallocSize : public ObIntSysVar
{
public:
  ObSysVarTransactionPreallocSize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_TRANSACTION_PREALLOC_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(246); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(246); }
};
class ObSysVarTransactionWriteSetExtraction : public ObEnumSysVar
{
public:
  const static char * TRANSACTION_WRITE_SET_EXTRACTION_NAMES[];
public:
  ObSysVarTransactionWriteSetExtraction() : ObEnumSysVar(TRANSACTION_WRITE_SET_EXTRACTION_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_TRANSACTION_WRITE_SET_EXTRACTION; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(247); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(247); }
};
class ObSysVarInformationSchemaStatsExpiry : public ObIntSysVar
{
public:
  ObSysVarInformationSchemaStatsExpiry() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INFORMATION_SCHEMA_STATS_EXPIRY; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(248); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(248); }
};
class ObSysVarGroupReplicationAllowLocalDisjointGtidsJoin : public ObBoolSysVar
{
public:
  ObSysVarGroupReplicationAllowLocalDisjointGtidsJoin() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_GROUP_REPLICATION_ALLOW_LOCAL_DISJOINT_GTIDS_JOIN; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(249); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(249); }
};
class ObSysVarGroupReplicationAllowLocalLowerVersionJoin : public ObBoolSysVar
{
public:
  ObSysVarGroupReplicationAllowLocalLowerVersionJoin() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_GROUP_REPLICATION_ALLOW_LOCAL_LOWER_VERSION_JOIN; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(250); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(250); }
};
class ObSysVarGroupReplicationAutoIncrementIncrement : public ObIntSysVar
{
public:
  ObSysVarGroupReplicationAutoIncrementIncrement() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_GROUP_REPLICATION_AUTO_INCREMENT_INCREMENT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(251); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(251); }
};
class ObSysVarGroupReplicationBootstrapGroup : public ObBoolSysVar
{
public:
  ObSysVarGroupReplicationBootstrapGroup() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_GROUP_REPLICATION_BOOTSTRAP_GROUP; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(252); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(252); }
};
class ObSysVarGroupReplicationComponentsStopTimeout : public ObIntSysVar
{
public:
  ObSysVarGroupReplicationComponentsStopTimeout() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_GROUP_REPLICATION_COMPONENTS_STOP_TIMEOUT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(253); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(253); }
};
class ObSysVarGroupReplicationCompressionThreshold : public ObIntSysVar
{
public:
  ObSysVarGroupReplicationCompressionThreshold() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_GROUP_REPLICATION_COMPRESSION_THRESHOLD; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(254); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(254); }
};
class ObSysVarGroupReplicationEnforceUpdateEverywhereChecks : public ObBoolSysVar
{
public:
  ObSysVarGroupReplicationEnforceUpdateEverywhereChecks() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_GROUP_REPLICATION_ENFORCE_UPDATE_EVERYWHERE_CHECKS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(255); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(255); }
};
class ObSysVarGroupReplicationExitStateAction : public ObEnumSysVar
{
public:
  const static char * GROUP_REPLICATION_EXIT_STATE_ACTION_NAMES[];
public:
  ObSysVarGroupReplicationExitStateAction() : ObEnumSysVar(GROUP_REPLICATION_EXIT_STATE_ACTION_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_GROUP_REPLICATION_EXIT_STATE_ACTION; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(256); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(256); }
};
class ObSysVarGroupReplicationFlowControlApplierThreshold : public ObIntSysVar
{
public:
  ObSysVarGroupReplicationFlowControlApplierThreshold() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_GROUP_REPLICATION_FLOW_CONTROL_APPLIER_THRESHOLD; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(257); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(257); }
};
class ObSysVarGroupReplicationFlowControlCertifierThreshold : public ObIntSysVar
{
public:
  ObSysVarGroupReplicationFlowControlCertifierThreshold() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_GROUP_REPLICATION_FLOW_CONTROL_CERTIFIER_THRESHOLD; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(258); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(258); }
};
class ObSysVarGroupReplicationFlowControlMode : public ObEnumSysVar
{
public:
  const static char * GROUP_REPLICATION_FLOW_CONTROL_MODE_NAMES[];
public:
  ObSysVarGroupReplicationFlowControlMode() : ObEnumSysVar(GROUP_REPLICATION_FLOW_CONTROL_MODE_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_GROUP_REPLICATION_FLOW_CONTROL_MODE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(259); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(259); }
};
class ObSysVarGroupReplicationForceMembers : public ObVarcharSysVar
{
public:
  ObSysVarGroupReplicationForceMembers() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_GROUP_REPLICATION_FORCE_MEMBERS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(260); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(260); }
};
class ObSysVarGroupReplicationGroupName : public ObVarcharSysVar
{
public:
  ObSysVarGroupReplicationGroupName() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_GROUP_REPLICATION_GROUP_NAME; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(261); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(261); }
};
class ObSysVarGroupReplicationGtidAssignmentBlockSize : public ObVarcharSysVar
{
public:
  ObSysVarGroupReplicationGtidAssignmentBlockSize() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_GROUP_REPLICATION_GTID_ASSIGNMENT_BLOCK_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(262); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(262); }
};
class ObSysVarGroupReplicationIpWhitelist : public ObVarcharSysVar
{
public:
  ObSysVarGroupReplicationIpWhitelist() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_GROUP_REPLICATION_IP_WHITELIST; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(263); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(263); }
};
class ObSysVarGroupReplicationLocalAddress : public ObVarcharSysVar
{
public:
  ObSysVarGroupReplicationLocalAddress() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_GROUP_REPLICATION_LOCAL_ADDRESS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(264); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(264); }
};
class ObSysVarGroupReplicationMemberWeight : public ObIntSysVar
{
public:
  ObSysVarGroupReplicationMemberWeight() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_GROUP_REPLICATION_MEMBER_WEIGHT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(265); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(265); }
};
class ObSysVarGroupReplicationPollSpinLoops : public ObIntSysVar
{
public:
  ObSysVarGroupReplicationPollSpinLoops() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_GROUP_REPLICATION_POLL_SPIN_LOOPS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(266); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(266); }
};
class ObSysVarGroupReplicationRecoveryCompleteAt : public ObEnumSysVar
{
public:
  const static char * GROUP_REPLICATION_RECOVERY_COMPLETE_AT_NAMES[];
public:
  ObSysVarGroupReplicationRecoveryCompleteAt() : ObEnumSysVar(GROUP_REPLICATION_RECOVERY_COMPLETE_AT_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_GROUP_REPLICATION_RECOVERY_COMPLETE_AT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(267); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(267); }
};
class ObSysVarGroupReplicationRecoveryReconnectInterval : public ObIntSysVar
{
public:
  ObSysVarGroupReplicationRecoveryReconnectInterval() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_GROUP_REPLICATION_RECOVERY_RECONNECT_INTERVAL; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(268); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(268); }
};
class ObSysVarGroupReplicationRecoveryRetryCount : public ObIntSysVar
{
public:
  ObSysVarGroupReplicationRecoveryRetryCount() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_GROUP_REPLICATION_RECOVERY_RETRY_COUNT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(269); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(269); }
};
class ObSysVarGroupReplicationRecoverySslCa : public ObVarcharSysVar
{
public:
  ObSysVarGroupReplicationRecoverySslCa() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_GROUP_REPLICATION_RECOVERY_SSL_CA; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(270); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(270); }
};
class ObSysVarGroupReplicationRecoverySslCapath : public ObVarcharSysVar
{
public:
  ObSysVarGroupReplicationRecoverySslCapath() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_GROUP_REPLICATION_RECOVERY_SSL_CAPATH; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(271); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(271); }
};
class ObSysVarGroupReplicationRecoverySslCert : public ObVarcharSysVar
{
public:
  ObSysVarGroupReplicationRecoverySslCert() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_GROUP_REPLICATION_RECOVERY_SSL_CERT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(272); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(272); }
};
class ObSysVarGroupReplicationRecoverySslCipher : public ObVarcharSysVar
{
public:
  ObSysVarGroupReplicationRecoverySslCipher() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_GROUP_REPLICATION_RECOVERY_SSL_CIPHER; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(273); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(273); }
};
class ObSysVarGroupReplicationRecoverySslCrl : public ObVarcharSysVar
{
public:
  ObSysVarGroupReplicationRecoverySslCrl() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_GROUP_REPLICATION_RECOVERY_SSL_CRL; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(274); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(274); }
};
class ObSysVarGroupReplicationRecoverySslCrlpath : public ObVarcharSysVar
{
public:
  ObSysVarGroupReplicationRecoverySslCrlpath() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_GROUP_REPLICATION_RECOVERY_SSL_CRLPATH; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(275); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(275); }
};
class ObSysVarGroupReplicationRecoverySslKey : public ObVarcharSysVar
{
public:
  ObSysVarGroupReplicationRecoverySslKey() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_GROUP_REPLICATION_RECOVERY_SSL_KEY; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(276); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(276); }
};
class ObSysVarGroupReplicationRecoverySslVerifyServerCert : public ObBoolSysVar
{
public:
  ObSysVarGroupReplicationRecoverySslVerifyServerCert() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_GROUP_REPLICATION_RECOVERY_SSL_VERIFY_SERVER_CERT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(277); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(277); }
};
class ObSysVarGroupReplicationRecoveryUseSsl : public ObBoolSysVar
{
public:
  ObSysVarGroupReplicationRecoveryUseSsl() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_GROUP_REPLICATION_RECOVERY_USE_SSL; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(278); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(278); }
};
class ObSysVarGroupReplicationSinglePrimaryMode : public ObBoolSysVar
{
public:
  ObSysVarGroupReplicationSinglePrimaryMode() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_GROUP_REPLICATION_SINGLE_PRIMARY_MODE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(279); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(279); }
};
class ObSysVarGroupReplicationSslMode : public ObEnumSysVar
{
public:
  const static char * GROUP_REPLICATION_SSL_MODE_NAMES[];
public:
  ObSysVarGroupReplicationSslMode() : ObEnumSysVar(GROUP_REPLICATION_SSL_MODE_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_GROUP_REPLICATION_SSL_MODE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(280); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(280); }
};
class ObSysVarGroupReplicationStartOnBoot : public ObBoolSysVar
{
public:
  ObSysVarGroupReplicationStartOnBoot() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_GROUP_REPLICATION_START_ON_BOOT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(281); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(281); }
};
class ObSysVarGroupReplicationTransactionSizeLimit : public ObIntSysVar
{
public:
  ObSysVarGroupReplicationTransactionSizeLimit() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_GROUP_REPLICATION_TRANSACTION_SIZE_LIMIT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(282); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(282); }
};
class ObSysVarGroupReplicationUnreachableMajorityTimeout : public ObIntSysVar
{
public:
  ObSysVarGroupReplicationUnreachableMajorityTimeout() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_GROUP_REPLICATION_UNREACHABLE_MAJORITY_TIMEOUT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(283); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(283); }
};
class ObSysVarInnodbReplicationDelay : public ObIntSysVar
{
public:
  ObSysVarInnodbReplicationDelay() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_REPLICATION_DELAY; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(284); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(284); }
};
class ObSysVarMasterInfoRepository : public ObVarcharSysVar
{
public:
  ObSysVarMasterInfoRepository() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_MASTER_INFO_REPOSITORY; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(285); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(285); }
};
class ObSysVarMasterVerifyChecksum : public ObBoolSysVar
{
public:
  ObSysVarMasterVerifyChecksum() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_MASTER_VERIFY_CHECKSUM; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(286); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(286); }
};
class ObSysVarPseudoSlaveMode : public ObBoolSysVar
{
public:
  ObSysVarPseudoSlaveMode() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_PSEUDO_SLAVE_MODE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(287); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(287); }
};
class ObSysVarPseudoThreadId : public ObIntSysVar
{
public:
  ObSysVarPseudoThreadId() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_PSEUDO_THREAD_ID; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(288); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(288); }
};
class ObSysVarRbrExecMode : public ObEnumSysVar
{
public:
  const static char * RBR_EXEC_MODE_NAMES[];
public:
  ObSysVarRbrExecMode() : ObEnumSysVar(RBR_EXEC_MODE_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_RBR_EXEC_MODE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(289); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(289); }
};
class ObSysVarReplicationOptimizeForStaticPluginConfig : public ObBoolSysVar
{
public:
  ObSysVarReplicationOptimizeForStaticPluginConfig() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_REPLICATION_OPTIMIZE_FOR_STATIC_PLUGIN_CONFIG; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(290); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(290); }
};
class ObSysVarReplicationSenderObserveCommitOnly : public ObBoolSysVar
{
public:
  ObSysVarReplicationSenderObserveCommitOnly() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_REPLICATION_SENDER_OBSERVE_COMMIT_ONLY; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(291); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(291); }
};
class ObSysVarRplSemiSyncMasterEnabled : public ObBoolSysVar
{
public:
  ObSysVarRplSemiSyncMasterEnabled() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_RPL_SEMI_SYNC_MASTER_ENABLED; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(292); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(292); }
};
class ObSysVarRplSemiSyncMasterTimeout : public ObIntSysVar
{
public:
  ObSysVarRplSemiSyncMasterTimeout() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_RPL_SEMI_SYNC_MASTER_TIMEOUT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(293); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(293); }
};
class ObSysVarRplSemiSyncMasterTraceLevel : public ObIntSysVar
{
public:
  ObSysVarRplSemiSyncMasterTraceLevel() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_RPL_SEMI_SYNC_MASTER_TRACE_LEVEL; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(294); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(294); }
};
class ObSysVarRplSemiSyncMasterWaitForSlaveCount : public ObIntSysVar
{
public:
  ObSysVarRplSemiSyncMasterWaitForSlaveCount() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_RPL_SEMI_SYNC_MASTER_WAIT_FOR_SLAVE_COUNT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(295); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(295); }
};
class ObSysVarRplSemiSyncMasterWaitNoSlave : public ObBoolSysVar
{
public:
  ObSysVarRplSemiSyncMasterWaitNoSlave() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_RPL_SEMI_SYNC_MASTER_WAIT_NO_SLAVE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(296); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(296); }
};
class ObSysVarRplSemiSyncMasterWaitPoint : public ObEnumSysVar
{
public:
  const static char * RPL_SEMI_SYNC_MASTER_WAIT_POINT_NAMES[];
public:
  ObSysVarRplSemiSyncMasterWaitPoint() : ObEnumSysVar(RPL_SEMI_SYNC_MASTER_WAIT_POINT_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_RPL_SEMI_SYNC_MASTER_WAIT_POINT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(297); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(297); }
};
class ObSysVarRplSemiSyncSlaveEnabled : public ObBoolSysVar
{
public:
  ObSysVarRplSemiSyncSlaveEnabled() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_RPL_SEMI_SYNC_SLAVE_ENABLED; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(298); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(298); }
};
class ObSysVarRplSemiSyncSlaveTraceLevel : public ObIntSysVar
{
public:
  ObSysVarRplSemiSyncSlaveTraceLevel() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_RPL_SEMI_SYNC_SLAVE_TRACE_LEVEL; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(299); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(299); }
};
class ObSysVarRplStopSlaveTimeout : public ObIntSysVar
{
public:
  ObSysVarRplStopSlaveTimeout() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_RPL_STOP_SLAVE_TIMEOUT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(300); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(300); }
};
class ObSysVarSlaveAllowBatching : public ObBoolSysVar
{
public:
  ObSysVarSlaveAllowBatching() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SLAVE_ALLOW_BATCHING; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(301); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(301); }
};
class ObSysVarSlaveCheckpointGroup : public ObIntSysVar
{
public:
  ObSysVarSlaveCheckpointGroup() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SLAVE_CHECKPOINT_GROUP; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(302); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(302); }
};
class ObSysVarSlaveCheckpointPeriod : public ObIntSysVar
{
public:
  ObSysVarSlaveCheckpointPeriod() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SLAVE_CHECKPOINT_PERIOD; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(303); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(303); }
};
class ObSysVarSlaveCompressedProtocol : public ObBoolSysVar
{
public:
  ObSysVarSlaveCompressedProtocol() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SLAVE_COMPRESSED_PROTOCOL; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(304); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(304); }
};
class ObSysVarSlaveExecMode : public ObEnumSysVar
{
public:
  const static char * SLAVE_EXEC_MODE_NAMES[];
public:
  ObSysVarSlaveExecMode() : ObEnumSysVar(SLAVE_EXEC_MODE_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SLAVE_EXEC_MODE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(305); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(305); }
};
class ObSysVarSlaveMaxAllowedPacket : public ObIntSysVar
{
public:
  ObSysVarSlaveMaxAllowedPacket() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SLAVE_MAX_ALLOWED_PACKET; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(306); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(306); }
};
class ObSysVarSlaveNetTimeout : public ObIntSysVar
{
public:
  ObSysVarSlaveNetTimeout() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SLAVE_NET_TIMEOUT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(307); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(307); }
};
class ObSysVarSlaveParallelType : public ObEnumSysVar
{
public:
  const static char * SLAVE_PARALLEL_TYPE_NAMES[];
public:
  ObSysVarSlaveParallelType() : ObEnumSysVar(SLAVE_PARALLEL_TYPE_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SLAVE_PARALLEL_TYPE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(308); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(308); }
};
class ObSysVarSlaveParallelWorkers : public ObIntSysVar
{
public:
  ObSysVarSlaveParallelWorkers() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SLAVE_PARALLEL_WORKERS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(309); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(309); }
};
class ObSysVarSlavePendingJobsSizeMax : public ObIntSysVar
{
public:
  ObSysVarSlavePendingJobsSizeMax() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SLAVE_PENDING_JOBS_SIZE_MAX; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(310); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(310); }
};
class ObSysVarSlavePreserveCommitOrder : public ObBoolSysVar
{
public:
  ObSysVarSlavePreserveCommitOrder() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SLAVE_PRESERVE_COMMIT_ORDER; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(311); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(311); }
};
class ObSysVarSlaveSqlVerifyChecksum : public ObBoolSysVar
{
public:
  ObSysVarSlaveSqlVerifyChecksum() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SLAVE_SQL_VERIFY_CHECKSUM; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(312); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(312); }
};
class ObSysVarSlaveTransactionRetries : public ObIntSysVar
{
public:
  ObSysVarSlaveTransactionRetries() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SLAVE_TRANSACTION_RETRIES; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(313); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(313); }
};
class ObSysVarSqlSlaveSkipCounter : public ObIntSysVar
{
public:
  ObSysVarSqlSlaveSkipCounter() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SQL_SLAVE_SKIP_COUNTER; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(314); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(314); }
};
class ObSysVarInnodbForceRecovery : public ObIntSysVar
{
public:
  ObSysVarInnodbForceRecovery() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_FORCE_RECOVERY; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(315); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(315); }
};
class ObSysVarSkipSlaveStart : public ObBoolSysVar
{
public:
  ObSysVarSkipSlaveStart() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SKIP_SLAVE_START; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(316); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(316); }
};
class ObSysVarSlaveLoadTmpdir : public ObVarcharSysVar
{
public:
  ObSysVarSlaveLoadTmpdir() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SLAVE_LOAD_TMPDIR; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(317); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(317); }
};
class ObSysVarSlaveSkipErrors : public ObVarcharSysVar
{
public:
  ObSysVarSlaveSkipErrors() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SLAVE_SKIP_ERRORS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(318); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(318); }
};
class ObSysVarInnodbSyncDebug : public ObBoolSysVar
{
public:
  ObSysVarInnodbSyncDebug() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_SYNC_DEBUG; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(319); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(319); }
};
class ObSysVarDefaultCollationForUtf8mb4 : public ObCharsetSysVar
{
public:
  ObSysVarDefaultCollationForUtf8mb4() : ObCharsetSysVar(ObSysVarOnCheckFuncs::check_default_value_for_utf8mb4, NULL, ObSysVarToObjFuncs::to_obj_collation, ObSysVarToStrFuncs::to_str_collation, ObSysVarGetMetaTypeFuncs::get_meta_type_varchar) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_DEFAULT_COLLATION_FOR_UTF8MB4; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(320); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(320); }
};
class ObSysVarInsertId : public ObIntSysVar
{
public:
  ObSysVarInsertId() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INSERT_ID; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(321); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(321); }
};
class ObSysVarJoinBufferSize : public ObIntSysVar
{
public:
  ObSysVarJoinBufferSize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_JOIN_BUFFER_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(322); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(322); }
};
class ObSysVarMaxJoinSize : public ObIntSysVar
{
public:
  ObSysVarMaxJoinSize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_MAX_JOIN_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(323); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(323); }
};
class ObSysVarMaxLengthForSortData : public ObIntSysVar
{
public:
  ObSysVarMaxLengthForSortData() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_MAX_LENGTH_FOR_SORT_DATA; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(324); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(324); }
};
class ObSysVarMaxPreparedStmtCount : public ObIntSysVar
{
public:
  ObSysVarMaxPreparedStmtCount() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_MAX_PREPARED_STMT_COUNT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(325); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(325); }
};
class ObSysVarMaxSortLength : public ObIntSysVar
{
public:
  ObSysVarMaxSortLength() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_MAX_SORT_LENGTH; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(326); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(326); }
};
class ObSysVarMinExaminedRowLimit : public ObIntSysVar
{
public:
  ObSysVarMinExaminedRowLimit() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_MIN_EXAMINED_ROW_LIMIT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(327); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(327); }
};
class ObSysVarMultiRangeCount : public ObIntSysVar
{
public:
  ObSysVarMultiRangeCount() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_MULTI_RANGE_COUNT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(328); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(328); }
};
class ObSysVarMysqlxConnectTimeout : public ObIntSysVar
{
public:
  ObSysVarMysqlxConnectTimeout() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_MYSQLX_CONNECT_TIMEOUT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(329); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(329); }
};
class ObSysVarMysqlxIdleWorkerThreadTimeout : public ObIntSysVar
{
public:
  ObSysVarMysqlxIdleWorkerThreadTimeout() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_MYSQLX_IDLE_WORKER_THREAD_TIMEOUT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(330); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(330); }
};
class ObSysVarMysqlxMaxAllowedPacket : public ObIntSysVar
{
public:
  ObSysVarMysqlxMaxAllowedPacket() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_MYSQLX_MAX_ALLOWED_PACKET; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(331); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(331); }
};
class ObSysVarMysqlxMaxConnections : public ObIntSysVar
{
public:
  ObSysVarMysqlxMaxConnections() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_MYSQLX_MAX_CONNECTIONS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(332); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(332); }
};
class ObSysVarMysqlxMinWorkerThreads : public ObIntSysVar
{
public:
  ObSysVarMysqlxMinWorkerThreads() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_MYSQLX_MIN_WORKER_THREADS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(333); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(333); }
};
class ObSysVarPerformanceSchemaShowProcesslist : public ObBoolSysVar
{
public:
  ObSysVarPerformanceSchemaShowProcesslist() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_PERFORMANCE_SCHEMA_SHOW_PROCESSLIST; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(334); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(334); }
};
class ObSysVarQueryAllocBlockSize : public ObIntSysVar
{
public:
  ObSysVarQueryAllocBlockSize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_QUERY_ALLOC_BLOCK_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(335); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(335); }
};
class ObSysVarQueryPreallocSize : public ObIntSysVar
{
public:
  ObSysVarQueryPreallocSize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_QUERY_PREALLOC_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(336); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(336); }
};
class ObSysVarSlowQueryLog : public ObIntSysVar
{
public:
  ObSysVarSlowQueryLog() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SLOW_QUERY_LOG; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(337); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(337); }
};
class ObSysVarSlowQueryLogFile : public ObVarcharSysVar
{
public:
  ObSysVarSlowQueryLogFile() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SLOW_QUERY_LOG_FILE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(338); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(338); }
};
class ObSysVarSortBufferSize : public ObIntSysVar
{
public:
  ObSysVarSortBufferSize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SORT_BUFFER_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(339); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(339); }
};
class ObSysVarSqlBufferResult : public ObIntSysVar
{
public:
  ObSysVarSqlBufferResult() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SQL_BUFFER_RESULT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(340); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(340); }
};
class ObSysVarBinlogCacheSize : public ObIntSysVar
{
public:
  ObSysVarBinlogCacheSize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_BINLOG_CACHE_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(341); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(341); }
};
class ObSysVarBinlogDirectNonTransactionalUpdates : public ObIntSysVar
{
public:
  ObSysVarBinlogDirectNonTransactionalUpdates() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_BINLOG_DIRECT_NON_TRANSACTIONAL_UPDATES; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(342); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(342); }
};
class ObSysVarBinlogErrorAction : public ObEnumSysVar
{
public:
  const static char * BINLOG_ERROR_ACTION_NAMES[];
public:
  ObSysVarBinlogErrorAction() : ObEnumSysVar(BINLOG_ERROR_ACTION_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_BINLOG_ERROR_ACTION; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(343); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(343); }
};
class ObSysVarBinlogGroupCommitSyncDelay : public ObIntSysVar
{
public:
  ObSysVarBinlogGroupCommitSyncDelay() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_BINLOG_GROUP_COMMIT_SYNC_DELAY; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(344); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(344); }
};
class ObSysVarBinlogGroupCommitSyncNoDelayCount : public ObIntSysVar
{
public:
  ObSysVarBinlogGroupCommitSyncNoDelayCount() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_BINLOG_GROUP_COMMIT_SYNC_NO_DELAY_COUNT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(345); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(345); }
};
class ObSysVarBinlogMaxFlushQueueTime : public ObIntSysVar
{
public:
  ObSysVarBinlogMaxFlushQueueTime() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_BINLOG_MAX_FLUSH_QUEUE_TIME; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(346); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(346); }
};
class ObSysVarBinlogOrderCommits : public ObIntSysVar
{
public:
  ObSysVarBinlogOrderCommits() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_BINLOG_ORDER_COMMITS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(347); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(347); }
};
class ObSysVarBinlogStmtCacheSize : public ObIntSysVar
{
public:
  ObSysVarBinlogStmtCacheSize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_BINLOG_STMT_CACHE_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(348); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(348); }
};
class ObSysVarBinlogTransactionDependencyHistorySize : public ObIntSysVar
{
public:
  ObSysVarBinlogTransactionDependencyHistorySize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_BINLOG_TRANSACTION_DEPENDENCY_HISTORY_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(349); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(349); }
};
class ObSysVarBinlogTransactionDependencyTracking : public ObEnumSysVar
{
public:
  const static char * BINLOG_TRANSACTION_DEPENDENCY_TRACKING_NAMES[];
public:
  ObSysVarBinlogTransactionDependencyTracking() : ObEnumSysVar(BINLOG_TRANSACTION_DEPENDENCY_TRACKING_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_BINLOG_TRANSACTION_DEPENDENCY_TRACKING; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(350); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(350); }
};
class ObSysVarExpireLogsDays : public ObIntSysVar
{
public:
  ObSysVarExpireLogsDays() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_EXPIRE_LOGS_DAYS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(351); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(351); }
};
class ObSysVarInnodbFlushLogAtTimeout : public ObIntSysVar
{
public:
  ObSysVarInnodbFlushLogAtTimeout() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_FLUSH_LOG_AT_TIMEOUT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(352); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(352); }
};
class ObSysVarInnodbFlushLogAtTrxCommit : public ObIntSysVar
{
public:
  ObSysVarInnodbFlushLogAtTrxCommit() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_FLUSH_LOG_AT_TRX_COMMIT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(353); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(353); }
};
class ObSysVarInnodbLogCheckpointNow : public ObBoolSysVar
{
public:
  ObSysVarInnodbLogCheckpointNow() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_LOG_CHECKPOINT_NOW; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(354); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(354); }
};
class ObSysVarInnodbLogChecksums : public ObIntSysVar
{
public:
  ObSysVarInnodbLogChecksums() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_LOG_CHECKSUMS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(355); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(355); }
};
class ObSysVarInnodbLogCompressedPages : public ObIntSysVar
{
public:
  ObSysVarInnodbLogCompressedPages() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_LOG_COMPRESSED_PAGES; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(356); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(356); }
};
class ObSysVarInnodbLogWriteAheadSize : public ObIntSysVar
{
public:
  ObSysVarInnodbLogWriteAheadSize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_LOG_WRITE_AHEAD_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(357); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(357); }
};
class ObSysVarInnodbMaxUndoLogSize : public ObIntSysVar
{
public:
  ObSysVarInnodbMaxUndoLogSize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_MAX_UNDO_LOG_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(358); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(358); }
};
class ObSysVarInnodbOnlineAlterLogMaxSize : public ObIntSysVar
{
public:
  ObSysVarInnodbOnlineAlterLogMaxSize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_ONLINE_ALTER_LOG_MAX_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(359); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(359); }
};
class ObSysVarInnodbUndoLogTruncate : public ObIntSysVar
{
public:
  ObSysVarInnodbUndoLogTruncate() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_UNDO_LOG_TRUNCATE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(360); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(360); }
};
class ObSysVarInnodbUndoLogs : public ObIntSysVar
{
public:
  ObSysVarInnodbUndoLogs() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_UNDO_LOGS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(361); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(361); }
};
class ObSysVarLogBinTrustFunctionCreators : public ObIntSysVar
{
public:
  ObSysVarLogBinTrustFunctionCreators() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_LOG_BIN_TRUST_FUNCTION_CREATORS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(362); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(362); }
};
class ObSysVarLogBinUseV1RowEvents : public ObIntSysVar
{
public:
  ObSysVarLogBinUseV1RowEvents() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_LOG_BIN_USE_V1_ROW_EVENTS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(363); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(363); }
};
class ObSysVarLogBuiltinAsIdentifiedByPassword : public ObIntSysVar
{
public:
  ObSysVarLogBuiltinAsIdentifiedByPassword() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_LOG_BUILTIN_AS_IDENTIFIED_BY_PASSWORD; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(364); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(364); }
};
class ObSysVarMaxBinlogCacheSize : public ObIntSysVar
{
public:
  ObSysVarMaxBinlogCacheSize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_MAX_BINLOG_CACHE_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(365); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(365); }
};
class ObSysVarMaxBinlogSize : public ObIntSysVar
{
public:
  ObSysVarMaxBinlogSize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_MAX_BINLOG_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(366); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(366); }
};
class ObSysVarMaxBinlogStmtCacheSize : public ObIntSysVar
{
public:
  ObSysVarMaxBinlogStmtCacheSize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_MAX_BINLOG_STMT_CACHE_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(367); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(367); }
};
class ObSysVarMaxRelayLogSize : public ObIntSysVar
{
public:
  ObSysVarMaxRelayLogSize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_MAX_RELAY_LOG_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(368); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(368); }
};
class ObSysVarRelayLogInfoRepository : public ObVarcharSysVar
{
public:
  ObSysVarRelayLogInfoRepository() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_RELAY_LOG_INFO_REPOSITORY; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(369); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(369); }
};
class ObSysVarRelayLogPurge : public ObIntSysVar
{
public:
  ObSysVarRelayLogPurge() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_RELAY_LOG_PURGE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(370); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(370); }
};
class ObSysVarSyncBinlog : public ObIntSysVar
{
public:
  ObSysVarSyncBinlog() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SYNC_BINLOG; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(371); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(371); }
};
class ObSysVarSyncRelayLog : public ObIntSysVar
{
public:
  ObSysVarSyncRelayLog() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SYNC_RELAY_LOG; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(372); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(372); }
};
class ObSysVarSyncRelayLogInfo : public ObIntSysVar
{
public:
  ObSysVarSyncRelayLogInfo() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SYNC_RELAY_LOG_INFO; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(373); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(373); }
};
class ObSysVarInnodbDeadlockDetect : public ObIntSysVar
{
public:
  ObSysVarInnodbDeadlockDetect() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_DEADLOCK_DETECT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(374); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(374); }
};
class ObSysVarInnodbLockWaitTimeout : public ObIntSysVar
{
public:
  ObSysVarInnodbLockWaitTimeout() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_LOCK_WAIT_TIMEOUT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(375); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(375); }
};
class ObSysVarInnodbPrintAllDeadlocks : public ObIntSysVar
{
public:
  ObSysVarInnodbPrintAllDeadlocks() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_PRINT_ALL_DEADLOCKS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(376); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(376); }
};
class ObSysVarInnodbTableLocks : public ObIntSysVar
{
public:
  ObSysVarInnodbTableLocks() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_TABLE_LOCKS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(377); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(377); }
};
class ObSysVarMaxWriteLockCount : public ObIntSysVar
{
public:
  ObSysVarMaxWriteLockCount() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_MAX_WRITE_LOCK_COUNT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(378); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(378); }
};
class ObSysVarObEnableRoleIds : public ObVarcharSysVar
{
public:
  ObSysVarObEnableRoleIds() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR__OB_ENABLE_ROLE_IDS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(379); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(379); }
};
class ObSysVarInnodbReadOnly : public ObBoolSysVar
{
public:
  ObSysVarInnodbReadOnly() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_READ_ONLY; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(380); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(380); }
};
class ObSysVarInnodbApiDisableRowlock : public ObBoolSysVar
{
public:
  ObSysVarInnodbApiDisableRowlock() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_API_DISABLE_ROWLOCK; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(381); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(381); }
};
class ObSysVarInnodbAutoincLockMode : public ObIntSysVar
{
public:
  ObSysVarInnodbAutoincLockMode() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_AUTOINC_LOCK_MODE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(382); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(382); }
};
class ObSysVarSkipExternalLocking : public ObBoolSysVar
{
public:
  ObSysVarSkipExternalLocking() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SKIP_EXTERNAL_LOCKING; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(383); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(383); }
};
class ObSysVarSuperReadOnly : public ObBoolSysVar
{
public:
  ObSysVarSuperReadOnly() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SUPER_READ_ONLY; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(384); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(384); }
};
class ObSysVarLowPriorityUpdates : public ObBoolSysVar
{
public:
  ObSysVarLowPriorityUpdates() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_LOW_PRIORITY_UPDATES; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(385); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(385); }
};
class ObSysVarMaxErrorCount : public ObIntSysVar
{
public:
  ObSysVarMaxErrorCount() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_MAX_ERROR_COUNT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(386); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(386); }
};
class ObSysVarMaxInsertDelayedThreads : public ObIntSysVar
{
public:
  ObSysVarMaxInsertDelayedThreads() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_MAX_INSERT_DELAYED_THREADS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(387); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(387); }
};
class ObSysVarFtStopwordFile : public ObVarcharSysVar
{
public:
  ObSysVarFtStopwordFile() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_FT_STOPWORD_FILE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(388); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(388); }
};
class ObSysVarInnodbFtCacheSize : public ObIntSysVar
{
public:
  ObSysVarInnodbFtCacheSize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_FT_CACHE_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(389); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(389); }
};
class ObSysVarInnodbFtSortPllDegree : public ObIntSysVar
{
public:
  ObSysVarInnodbFtSortPllDegree() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_FT_SORT_PLL_DEGREE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(390); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(390); }
};
class ObSysVarInnodbFtTotalCacheSize : public ObIntSysVar
{
public:
  ObSysVarInnodbFtTotalCacheSize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_FT_TOTAL_CACHE_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(391); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(391); }
};
class ObSysVarMecabRcFile : public ObVarcharSysVar
{
public:
  ObSysVarMecabRcFile() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_MECAB_RC_FILE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(392); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(392); }
};
class ObSysVarMetadataLocksCacheSize : public ObIntSysVar
{
public:
  ObSysVarMetadataLocksCacheSize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_METADATA_LOCKS_CACHE_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(393); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(393); }
};
class ObSysVarMetadataLocksHashInstances : public ObIntSysVar
{
public:
  ObSysVarMetadataLocksHashInstances() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_METADATA_LOCKS_HASH_INSTANCES; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(394); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(394); }
};
class ObSysVarInnodbTempDataFilePath : public ObVarcharSysVar
{
public:
  ObSysVarInnodbTempDataFilePath() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_TEMP_DATA_FILE_PATH; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(395); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(395); }
};
class ObSysVarInnodbDataFilePath : public ObVarcharSysVar
{
public:
  ObSysVarInnodbDataFilePath() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_DATA_FILE_PATH; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(396); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(396); }
};
class ObSysVarInnodbDataHomeDir : public ObVarcharSysVar
{
public:
  ObSysVarInnodbDataHomeDir() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_DATA_HOME_DIR; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(397); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(397); }
};
class ObSysVarDefaultTmpStorageEngine : public ObEnumSysVar
{
public:
  const static char * DEFAULT_TMP_STORAGE_ENGINE_NAMES[];
public:
  ObSysVarDefaultTmpStorageEngine() : ObEnumSysVar(DEFAULT_TMP_STORAGE_ENGINE_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_DEFAULT_TMP_STORAGE_ENGINE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(398); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(398); }
};
class ObSysVarInnodbFtEnableDiagPrint : public ObBoolSysVar
{
public:
  ObSysVarInnodbFtEnableDiagPrint() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_FT_ENABLE_DIAG_PRINT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(399); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(399); }
};
class ObSysVarInnodbFtNumWordOptimize : public ObIntSysVar
{
public:
  ObSysVarInnodbFtNumWordOptimize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_FT_NUM_WORD_OPTIMIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(400); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(400); }
};
class ObSysVarInnodbFtResultCacheLimit : public ObIntSysVar
{
public:
  ObSysVarInnodbFtResultCacheLimit() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_FT_RESULT_CACHE_LIMIT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(401); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(401); }
};
class ObSysVarInnodbFtServerStopwordTable : public ObVarcharSysVar
{
public:
  ObSysVarInnodbFtServerStopwordTable() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_FT_SERVER_STOPWORD_TABLE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(402); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(402); }
};
class ObSysVarInnodbOptimizeFulltextOnly : public ObBoolSysVar
{
public:
  ObSysVarInnodbOptimizeFulltextOnly() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_OPTIMIZE_FULLTEXT_ONLY; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(403); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(403); }
};
class ObSysVarMaxTmpTables : public ObIntSysVar
{
public:
  ObSysVarMaxTmpTables() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_MAX_TMP_TABLES; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(404); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(404); }
};
class ObSysVarInnodbTmpdir : public ObVarcharSysVar
{
public:
  ObSysVarInnodbTmpdir() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_TMPDIR; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(405); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(405); }
};
class ObSysVarGroupReplicationGroupSeeds : public ObVarcharSysVar
{
public:
  ObSysVarGroupReplicationGroupSeeds() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_GROUP_REPLICATION_GROUP_SEEDS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(406); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(406); }
};
class ObSysVarSlaveRowsSearchAlgorithms : public ObEnumSysVar
{
public:
  const static char * SLAVE_ROWS_SEARCH_ALGORITHMS_NAMES[];
public:
  ObSysVarSlaveRowsSearchAlgorithms() : ObEnumSysVar(SLAVE_ROWS_SEARCH_ALGORITHMS_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SLAVE_ROWS_SEARCH_ALGORITHMS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(407); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(407); }
};
class ObSysVarSlaveTypeConversions : public ObEnumSysVar
{
public:
  const static char * SLAVE_TYPE_CONVERSIONS_NAMES[];
public:
  ObSysVarSlaveTypeConversions() : ObEnumSysVar(SLAVE_TYPE_CONVERSIONS_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SLAVE_TYPE_CONVERSIONS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(408); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(408); }
};
class ObSysVarObHnswEfSearch : public ObIntSysVar
{
public:
  ObSysVarObHnswEfSearch() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_OB_HNSW_EF_SEARCH; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(409); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(409); }
};
class ObSysVarNdbAllowCopyingAlterTable : public ObBoolSysVar
{
public:
  ObSysVarNdbAllowCopyingAlterTable() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_NDB_ALLOW_COPYING_ALTER_TABLE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(410); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(410); }
};
class ObSysVarNdbAutoincrementPrefetchSz : public ObIntSysVar
{
public:
  ObSysVarNdbAutoincrementPrefetchSz() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_NDB_AUTOINCREMENT_PREFETCH_SZ; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(411); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(411); }
};
class ObSysVarNdbBlobReadBatchBytes : public ObIntSysVar
{
public:
  ObSysVarNdbBlobReadBatchBytes() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_NDB_BLOB_READ_BATCH_BYTES; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(412); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(412); }
};
class ObSysVarNdbBlobWriteBatchBytes : public ObIntSysVar
{
public:
  ObSysVarNdbBlobWriteBatchBytes() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_NDB_BLOB_WRITE_BATCH_BYTES; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(413); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(413); }
};
class ObSysVarNdbCacheCheckTime : public ObIntSysVar
{
public:
  ObSysVarNdbCacheCheckTime() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_NDB_CACHE_CHECK_TIME; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(414); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(414); }
};
class ObSysVarNdbClearApplyStatus : public ObBoolSysVar
{
public:
  ObSysVarNdbClearApplyStatus() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_NDB_CLEAR_APPLY_STATUS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(415); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(415); }
};
class ObSysVarNdbDataNodeNeighbour : public ObIntSysVar
{
public:
  ObSysVarNdbDataNodeNeighbour() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_NDB_DATA_NODE_NEIGHBOUR; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(416); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(416); }
};
class ObSysVarNdbDefaultColumnFormat : public ObEnumSysVar
{
public:
  const static char * NDB_DEFAULT_COLUMN_FORMAT_NAMES[];
public:
  ObSysVarNdbDefaultColumnFormat() : ObEnumSysVar(NDB_DEFAULT_COLUMN_FORMAT_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_NDB_DEFAULT_COLUMN_FORMAT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(417); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(417); }
};
class ObSysVarNdbDeferredConstraints : public ObIntSysVar
{
public:
  ObSysVarNdbDeferredConstraints() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_NDB_DEFERRED_CONSTRAINTS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(418); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(418); }
};
class ObSysVarNdbDistribution : public ObEnumSysVar
{
public:
  const static char * NDB_DISTRIBUTION_NAMES[];
public:
  ObSysVarNdbDistribution() : ObEnumSysVar(NDB_DISTRIBUTION_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_NDB_DISTRIBUTION; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(419); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(419); }
};
class ObSysVarNdbEventbufferFreePercent : public ObIntSysVar
{
public:
  ObSysVarNdbEventbufferFreePercent() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_NDB_EVENTBUFFER_FREE_PERCENT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(420); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(420); }
};
class ObSysVarNdbEventbufferMaxAlloc : public ObIntSysVar
{
public:
  ObSysVarNdbEventbufferMaxAlloc() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_NDB_EVENTBUFFER_MAX_ALLOC; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(421); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(421); }
};
class ObSysVarNdbExtraLogging : public ObIntSysVar
{
public:
  ObSysVarNdbExtraLogging() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_NDB_EXTRA_LOGGING; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(422); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(422); }
};
class ObSysVarNdbForceSend : public ObBoolSysVar
{
public:
  ObSysVarNdbForceSend() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_NDB_FORCE_SEND; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(423); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(423); }
};
class ObSysVarNdbFullyReplicated : public ObBoolSysVar
{
public:
  ObSysVarNdbFullyReplicated() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_NDB_FULLY_REPLICATED; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(424); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(424); }
};
class ObSysVarNdbIndexStatEnable : public ObBoolSysVar
{
public:
  ObSysVarNdbIndexStatEnable() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_NDB_INDEX_STAT_ENABLE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(425); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(425); }
};
class ObSysVarNdbIndexStatOption : public ObVarcharSysVar
{
public:
  ObSysVarNdbIndexStatOption() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_NDB_INDEX_STAT_OPTION; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(426); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(426); }
};
class ObSysVarNdbJoinPushdown : public ObBoolSysVar
{
public:
  ObSysVarNdbJoinPushdown() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_NDB_JOIN_PUSHDOWN; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(427); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(427); }
};
class ObSysVarNdbLogBinlogIndex : public ObBoolSysVar
{
public:
  ObSysVarNdbLogBinlogIndex() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_NDB_LOG_BINLOG_INDEX; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(428); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(428); }
};
class ObSysVarNdbLogEmptyEpochs : public ObBoolSysVar
{
public:
  ObSysVarNdbLogEmptyEpochs() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_NDB_LOG_EMPTY_EPOCHS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(429); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(429); }
};
class ObSysVarNdbLogEmptyUpdate : public ObBoolSysVar
{
public:
  ObSysVarNdbLogEmptyUpdate() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_NDB_LOG_EMPTY_UPDATE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(430); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(430); }
};
class ObSysVarNdbLogExclusiveReads : public ObBoolSysVar
{
public:
  ObSysVarNdbLogExclusiveReads() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_NDB_LOG_EXCLUSIVE_READS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(431); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(431); }
};
class ObSysVarNdbLogUpdateAsWrite : public ObBoolSysVar
{
public:
  ObSysVarNdbLogUpdateAsWrite() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_NDB_LOG_UPDATE_AS_WRITE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(432); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(432); }
};
class ObSysVarNdbLogUpdateMinimal : public ObBoolSysVar
{
public:
  ObSysVarNdbLogUpdateMinimal() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_NDB_LOG_UPDATE_MINIMAL; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(433); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(433); }
};
class ObSysVarNdbLogUpdatedOnly : public ObBoolSysVar
{
public:
  ObSysVarNdbLogUpdatedOnly() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_NDB_LOG_UPDATED_ONLY; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(434); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(434); }
};
class ObSysVarNdbOptimizationDelay : public ObIntSysVar
{
public:
  ObSysVarNdbOptimizationDelay() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_NDB_OPTIMIZATION_DELAY; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(435); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(435); }
};
class ObSysVarNdbReadBackup : public ObBoolSysVar
{
public:
  ObSysVarNdbReadBackup() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_NDB_READ_BACKUP; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(436); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(436); }
};
class ObSysVarNdbRecvThreadActivationThreshold : public ObIntSysVar
{
public:
  ObSysVarNdbRecvThreadActivationThreshold() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_NDB_RECV_THREAD_ACTIVATION_THRESHOLD; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(437); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(437); }
};
class ObSysVarNdbRecvThreadCpuMask : public ObVarcharSysVar
{
public:
  ObSysVarNdbRecvThreadCpuMask() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_NDB_RECV_THREAD_CPU_MASK; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(438); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(438); }
};
class ObSysVarNdbReportThreshBinlogEpochSlip : public ObIntSysVar
{
public:
  ObSysVarNdbReportThreshBinlogEpochSlip() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_NDB_REPORT_THRESH_BINLOG_EPOCH_SLIP; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(439); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(439); }
};
class ObSysVarNdbReportThreshBinlogMemUsage : public ObIntSysVar
{
public:
  ObSysVarNdbReportThreshBinlogMemUsage() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_NDB_REPORT_THRESH_BINLOG_MEM_USAGE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(440); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(440); }
};
class ObSysVarNdbRowChecksum : public ObIntSysVar
{
public:
  ObSysVarNdbRowChecksum() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_NDB_ROW_CHECKSUM; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(441); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(441); }
};
class ObSysVarNdbShowForeignKeyMockTables : public ObBoolSysVar
{
public:
  ObSysVarNdbShowForeignKeyMockTables() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_NDB_SHOW_FOREIGN_KEY_MOCK_TABLES; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(442); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(442); }
};
class ObSysVarNdbSlaveConflictRole : public ObEnumSysVar
{
public:
  const static char * NDB_SLAVE_CONFLICT_ROLE_NAMES[];
public:
  ObSysVarNdbSlaveConflictRole() : ObEnumSysVar(NDB_SLAVE_CONFLICT_ROLE_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_NDB_SLAVE_CONFLICT_ROLE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(443); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(443); }
};
class ObSysVarNdbTableNoLogging : public ObBoolSysVar
{
public:
  ObSysVarNdbTableNoLogging() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_NDB_TABLE_NO_LOGGING; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(444); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(444); }
};
class ObSysVarNdbTableTemporary : public ObBoolSysVar
{
public:
  ObSysVarNdbTableTemporary() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_NDB_TABLE_TEMPORARY; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(445); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(445); }
};
class ObSysVarNdbUseExactCount : public ObBoolSysVar
{
public:
  ObSysVarNdbUseExactCount() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_NDB_USE_EXACT_COUNT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(446); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(446); }
};
class ObSysVarNdbUseTransactions : public ObBoolSysVar
{
public:
  ObSysVarNdbUseTransactions() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_NDB_USE_TRANSACTIONS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(447); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(447); }
};
class ObSysVarNdbinfoMaxBytes : public ObIntSysVar
{
public:
  ObSysVarNdbinfoMaxBytes() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_NDBINFO_MAX_BYTES; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(448); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(448); }
};
class ObSysVarNdbinfoMaxRows : public ObIntSysVar
{
public:
  ObSysVarNdbinfoMaxRows() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_NDBINFO_MAX_ROWS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(449); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(449); }
};
class ObSysVarNdbinfoOffline : public ObBoolSysVar
{
public:
  ObSysVarNdbinfoOffline() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_NDBINFO_OFFLINE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(450); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(450); }
};
class ObSysVarNdbinfoShowHidden : public ObBoolSysVar
{
public:
  ObSysVarNdbinfoShowHidden() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_NDBINFO_SHOW_HIDDEN; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(451); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(451); }
};
class ObSysVarMyisamDataPointerSize : public ObIntSysVar
{
public:
  ObSysVarMyisamDataPointerSize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_MYISAM_DATA_POINTER_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(452); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(452); }
};
class ObSysVarMyisamMaxSortFileSize : public ObIntSysVar
{
public:
  ObSysVarMyisamMaxSortFileSize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_MYISAM_MAX_SORT_FILE_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(453); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(453); }
};
class ObSysVarMyisamRepairThreads : public ObIntSysVar
{
public:
  ObSysVarMyisamRepairThreads() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_MYISAM_REPAIR_THREADS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(454); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(454); }
};
class ObSysVarMyisamSortBufferSize : public ObIntSysVar
{
public:
  ObSysVarMyisamSortBufferSize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_MYISAM_SORT_BUFFER_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(455); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(455); }
};
class ObSysVarMyisamStatsMethod : public ObEnumSysVar
{
public:
  const static char * MYISAM_STATS_METHOD_NAMES[];
public:
  ObSysVarMyisamStatsMethod() : ObEnumSysVar(MYISAM_STATS_METHOD_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_MYISAM_STATS_METHOD; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(456); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(456); }
};
class ObSysVarMyisamUseMmap : public ObBoolSysVar
{
public:
  ObSysVarMyisamUseMmap() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_MYISAM_USE_MMAP; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(457); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(457); }
};
class ObSysVarPreloadBufferSize : public ObIntSysVar
{
public:
  ObSysVarPreloadBufferSize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_PRELOAD_BUFFER_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(458); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(458); }
};
class ObSysVarReadBufferSize : public ObIntSysVar
{
public:
  ObSysVarReadBufferSize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_READ_BUFFER_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(459); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(459); }
};
class ObSysVarReadRndBufferSize : public ObIntSysVar
{
public:
  ObSysVarReadRndBufferSize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_READ_RND_BUFFER_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(460); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(460); }
};
class ObSysVarSyncFrm : public ObIntSysVar
{
public:
  ObSysVarSyncFrm() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SYNC_FRM; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(461); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(461); }
};
class ObSysVarSyncMasterInfo : public ObIntSysVar
{
public:
  ObSysVarSyncMasterInfo() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SYNC_MASTER_INFO; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(462); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(462); }
};
class ObSysVarTableOpenCache : public ObIntSysVar
{
public:
  ObSysVarTableOpenCache() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_TABLE_OPEN_CACHE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(463); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(463); }
};
class ObSysVarInnodbMonitorDisable : public ObVarcharSysVar
{
public:
  ObSysVarInnodbMonitorDisable() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_MONITOR_DISABLE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(464); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(464); }
};
class ObSysVarInnodbMonitorEnable : public ObVarcharSysVar
{
public:
  ObSysVarInnodbMonitorEnable() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_MONITOR_ENABLE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(465); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(465); }
};
class ObSysVarInnodbMonitorReset : public ObVarcharSysVar
{
public:
  ObSysVarInnodbMonitorReset() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_MONITOR_RESET; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(466); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(466); }
};
class ObSysVarInnodbMonitorResetAll : public ObVarcharSysVar
{
public:
  ObSysVarInnodbMonitorResetAll() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_MONITOR_RESET_ALL; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(467); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(467); }
};
class ObSysVarInnodbOldBlocksPct : public ObIntSysVar
{
public:
  ObSysVarInnodbOldBlocksPct() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_OLD_BLOCKS_PCT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(468); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(468); }
};
class ObSysVarInnodbOldBlocksTime : public ObIntSysVar
{
public:
  ObSysVarInnodbOldBlocksTime() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_OLD_BLOCKS_TIME; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(469); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(469); }
};
class ObSysVarInnodbPurgeBatchSize : public ObIntSysVar
{
public:
  ObSysVarInnodbPurgeBatchSize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_PURGE_BATCH_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(470); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(470); }
};
class ObSysVarInnodbPurgeRsegTruncateFrequency : public ObIntSysVar
{
public:
  ObSysVarInnodbPurgeRsegTruncateFrequency() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_PURGE_RSEG_TRUNCATE_FREQUENCY; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(471); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(471); }
};
class ObSysVarInnodbRandomReadAhead : public ObBoolSysVar
{
public:
  ObSysVarInnodbRandomReadAhead() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_RANDOM_READ_AHEAD; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(472); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(472); }
};
class ObSysVarInnodbReadAheadThreshold : public ObIntSysVar
{
public:
  ObSysVarInnodbReadAheadThreshold() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_READ_AHEAD_THRESHOLD; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(473); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(473); }
};
class ObSysVarInnodbRollbackSegments : public ObIntSysVar
{
public:
  ObSysVarInnodbRollbackSegments() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_ROLLBACK_SEGMENTS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(474); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(474); }
};
class ObSysVarInnodbSpinWaitDelay : public ObIntSysVar
{
public:
  ObSysVarInnodbSpinWaitDelay() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_SPIN_WAIT_DELAY; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(475); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(475); }
};
class ObSysVarInnodbStatusOutput : public ObBoolSysVar
{
public:
  ObSysVarInnodbStatusOutput() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_STATUS_OUTPUT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(476); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(476); }
};
class ObSysVarInnodbStatusOutputLocks : public ObBoolSysVar
{
public:
  ObSysVarInnodbStatusOutputLocks() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_STATUS_OUTPUT_LOCKS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(477); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(477); }
};
class ObSysVarInnodbSyncSpinLoops : public ObIntSysVar
{
public:
  ObSysVarInnodbSyncSpinLoops() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_SYNC_SPIN_LOOPS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(478); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(478); }
};
class ObSysVarInternalTmpDiskStorageEngine : public ObEnumSysVar
{
public:
  const static char * INTERNAL_TMP_DISK_STORAGE_ENGINE_NAMES[];
public:
  ObSysVarInternalTmpDiskStorageEngine() : ObEnumSysVar(INTERNAL_TMP_DISK_STORAGE_ENGINE_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INTERNAL_TMP_DISK_STORAGE_ENGINE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(479); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(479); }
};
class ObSysVarKeepFilesOnCreate : public ObBoolSysVar
{
public:
  ObSysVarKeepFilesOnCreate() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_KEEP_FILES_ON_CREATE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(480); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(480); }
};
class ObSysVarMaxHeapTableSize : public ObIntSysVar
{
public:
  ObSysVarMaxHeapTableSize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_MAX_HEAP_TABLE_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(481); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(481); }
};
class ObSysVarBulkInsertBufferSize : public ObIntSysVar
{
public:
  ObSysVarBulkInsertBufferSize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_BULK_INSERT_BUFFER_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(482); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(482); }
};
class ObSysVarHostCacheSize : public ObIntSysVar
{
public:
  ObSysVarHostCacheSize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_HOST_CACHE_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(483); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(483); }
};
class ObSysVarInitSlave : public ObVarcharSysVar
{
public:
  ObSysVarInitSlave() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INIT_SLAVE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(484); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(484); }
};
class ObSysVarInnodbFastShutdown : public ObIntSysVar
{
public:
  ObSysVarInnodbFastShutdown() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_FAST_SHUTDOWN; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(485); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(485); }
};
class ObSysVarInnodbIoCapacity : public ObIntSysVar
{
public:
  ObSysVarInnodbIoCapacity() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_IO_CAPACITY; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(486); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(486); }
};
class ObSysVarInnodbIoCapacityMax : public ObIntSysVar
{
public:
  ObSysVarInnodbIoCapacityMax() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_IO_CAPACITY_MAX; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(487); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(487); }
};
class ObSysVarInnodbThreadConcurrency : public ObIntSysVar
{
public:
  ObSysVarInnodbThreadConcurrency() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_THREAD_CONCURRENCY; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(488); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(488); }
};
class ObSysVarInnodbThreadSleepDelay : public ObIntSysVar
{
public:
  ObSysVarInnodbThreadSleepDelay() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_THREAD_SLEEP_DELAY; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(489); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(489); }
};
class ObSysVarLogErrorVerbosity : public ObIntSysVar
{
public:
  ObSysVarLogErrorVerbosity() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_LOG_ERROR_VERBOSITY; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(490); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(490); }
};
class ObSysVarLogOutput : public ObVarcharSysVar
{
public:
  ObSysVarLogOutput() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_LOG_OUTPUT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(491); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(491); }
};
class ObSysVarLogQueriesNotUsingIndexes : public ObBoolSysVar
{
public:
  ObSysVarLogQueriesNotUsingIndexes() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_LOG_QUERIES_NOT_USING_INDEXES; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(492); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(492); }
};
class ObSysVarLogSlowAdminStatements : public ObBoolSysVar
{
public:
  ObSysVarLogSlowAdminStatements() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_LOG_SLOW_ADMIN_STATEMENTS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(493); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(493); }
};
class ObSysVarLogSlowSlaveStatements : public ObBoolSysVar
{
public:
  ObSysVarLogSlowSlaveStatements() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_LOG_SLOW_SLAVE_STATEMENTS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(494); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(494); }
};
class ObSysVarLogStatementsUnsafeForBinlog : public ObIntSysVar
{
public:
  ObSysVarLogStatementsUnsafeForBinlog() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_LOG_STATEMENTS_UNSAFE_FOR_BINLOG; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(495); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(495); }
};
class ObSysVarLogSyslog : public ObBoolSysVar
{
public:
  ObSysVarLogSyslog() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_LOG_SYSLOG; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(496); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(496); }
};
class ObSysVarLogSyslogFacility : public ObVarcharSysVar
{
public:
  ObSysVarLogSyslogFacility() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_LOG_SYSLOG_FACILITY; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(497); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(497); }
};
class ObSysVarLogSyslogIncludePid : public ObIntSysVar
{
public:
  ObSysVarLogSyslogIncludePid() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_LOG_SYSLOG_INCLUDE_PID; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(498); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(498); }
};
class ObSysVarLogSyslogTag : public ObVarcharSysVar
{
public:
  ObSysVarLogSyslogTag() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_LOG_SYSLOG_TAG; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(499); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(499); }
};
class ObSysVarLogThrottleQueriesNotUsingIndexes : public ObIntSysVar
{
public:
  ObSysVarLogThrottleQueriesNotUsingIndexes() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_LOG_THROTTLE_QUERIES_NOT_USING_INDEXES; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(500); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(500); }
};
class ObSysVarLogTimestamps : public ObEnumSysVar
{
public:
  const static char * LOG_TIMESTAMPS_NAMES[];
public:
  ObSysVarLogTimestamps() : ObEnumSysVar(LOG_TIMESTAMPS_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_LOG_TIMESTAMPS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(501); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(501); }
};
class ObSysVarLogWarnings : public ObIntSysVar
{
public:
  ObSysVarLogWarnings() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_LOG_WARNINGS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(502); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(502); }
};
class ObSysVarMaxDelayedThreads : public ObIntSysVar
{
public:
  ObSysVarMaxDelayedThreads() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_MAX_DELAYED_THREADS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(503); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(503); }
};
class ObSysVarOfflineMode : public ObIntSysVar
{
public:
  ObSysVarOfflineMode() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_OFFLINE_MODE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(504); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(504); }
};
class ObSysVarRequireSecureTransport : public ObBoolSysVar
{
public:
  ObSysVarRequireSecureTransport() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_REQUIRE_SECURE_TRANSPORT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(505); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(505); }
};
class ObSysVarSlowLaunchTime : public ObIntSysVar
{
public:
  ObSysVarSlowLaunchTime() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SLOW_LAUNCH_TIME; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(506); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(506); }
};
class ObSysVarSqlLogOff : public ObBoolSysVar
{
public:
  ObSysVarSqlLogOff() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SQL_LOG_OFF; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(507); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(507); }
};
class ObSysVarThreadCacheSize : public ObIntSysVar
{
public:
  ObSysVarThreadCacheSize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_THREAD_CACHE_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(508); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(508); }
};
class ObSysVarThreadPoolHighPriorityConnection : public ObIntSysVar
{
public:
  ObSysVarThreadPoolHighPriorityConnection() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_THREAD_POOL_HIGH_PRIORITY_CONNECTION; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(509); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(509); }
};
class ObSysVarThreadPoolMaxUnusedThreads : public ObIntSysVar
{
public:
  ObSysVarThreadPoolMaxUnusedThreads() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_THREAD_POOL_MAX_UNUSED_THREADS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(510); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(510); }
};
class ObSysVarThreadPoolPrioKickupTimer : public ObIntSysVar
{
public:
  ObSysVarThreadPoolPrioKickupTimer() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_THREAD_POOL_PRIO_KICKUP_TIMER; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(511); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(511); }
};
class ObSysVarThreadPoolStallLimit : public ObIntSysVar
{
public:
  ObSysVarThreadPoolStallLimit() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_THREAD_POOL_STALL_LIMIT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(512); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(512); }
};
class ObSysVarHaveStatementTimeout : public ObBoolSysVar
{
public:
  ObSysVarHaveStatementTimeout() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_HAVE_STATEMENT_TIMEOUT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(513); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(513); }
};
class ObSysVarMysqlxBindAddress : public ObVarcharSysVar
{
public:
  ObSysVarMysqlxBindAddress() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_MYSQLX_BIND_ADDRESS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(514); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(514); }
};
class ObSysVarMysqlxPort : public ObIntSysVar
{
public:
  ObSysVarMysqlxPort() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_MYSQLX_PORT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(515); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(515); }
};
class ObSysVarMysqlxPortOpenTimeout : public ObIntSysVar
{
public:
  ObSysVarMysqlxPortOpenTimeout() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_MYSQLX_PORT_OPEN_TIMEOUT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(516); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(516); }
};
class ObSysVarMysqlxSocket : public ObVarcharSysVar
{
public:
  ObSysVarMysqlxSocket() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_MYSQLX_SOCKET; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(517); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(517); }
};
class ObSysVarMysqlxSslCa : public ObVarcharSysVar
{
public:
  ObSysVarMysqlxSslCa() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_MYSQLX_SSL_CA; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(518); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(518); }
};
class ObSysVarMysqlxSslCapath : public ObVarcharSysVar
{
public:
  ObSysVarMysqlxSslCapath() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_MYSQLX_SSL_CAPATH; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(519); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(519); }
};
class ObSysVarMysqlxSslCert : public ObVarcharSysVar
{
public:
  ObSysVarMysqlxSslCert() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_MYSQLX_SSL_CERT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(520); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(520); }
};
class ObSysVarMysqlxSslCipher : public ObVarcharSysVar
{
public:
  ObSysVarMysqlxSslCipher() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_MYSQLX_SSL_CIPHER; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(521); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(521); }
};
class ObSysVarMysqlxSslCrl : public ObVarcharSysVar
{
public:
  ObSysVarMysqlxSslCrl() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_MYSQLX_SSL_CRL; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(522); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(522); }
};
class ObSysVarMysqlxSslCrlpath : public ObVarcharSysVar
{
public:
  ObSysVarMysqlxSslCrlpath() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_MYSQLX_SSL_CRLPATH; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(523); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(523); }
};
class ObSysVarMysqlxSslKey : public ObVarcharSysVar
{
public:
  ObSysVarMysqlxSslKey() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_MYSQLX_SSL_KEY; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(524); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(524); }
};
class ObSysVarOld : public ObBoolSysVar
{
public:
  ObSysVarOld() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_OLD; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(525); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(525); }
};
class ObSysVarPerformanceSchemaAccountsSize : public ObIntSysVar
{
public:
  ObSysVarPerformanceSchemaAccountsSize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_PERFORMANCE_SCHEMA_ACCOUNTS_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(526); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(526); }
};
class ObSysVarPerformanceSchemaDigestsSize : public ObIntSysVar
{
public:
  ObSysVarPerformanceSchemaDigestsSize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_PERFORMANCE_SCHEMA_DIGESTS_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(527); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(527); }
};
class ObSysVarPerformanceSchemaEventsStagesHistoryLongSize : public ObIntSysVar
{
public:
  ObSysVarPerformanceSchemaEventsStagesHistoryLongSize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_PERFORMANCE_SCHEMA_EVENTS_STAGES_HISTORY_LONG_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(528); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(528); }
};
class ObSysVarPerformanceSchemaEventsStagesHistorySize : public ObIntSysVar
{
public:
  ObSysVarPerformanceSchemaEventsStagesHistorySize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_PERFORMANCE_SCHEMA_EVENTS_STAGES_HISTORY_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(529); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(529); }
};
class ObSysVarPerformanceSchemaEventsStatementsHistoryLongSize : public ObIntSysVar
{
public:
  ObSysVarPerformanceSchemaEventsStatementsHistoryLongSize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_PERFORMANCE_SCHEMA_EVENTS_STATEMENTS_HISTORY_LONG_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(530); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(530); }
};
class ObSysVarPerformanceSchemaEventsStatementsHistorySize : public ObIntSysVar
{
public:
  ObSysVarPerformanceSchemaEventsStatementsHistorySize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_PERFORMANCE_SCHEMA_EVENTS_STATEMENTS_HISTORY_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(531); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(531); }
};
class ObSysVarPerformanceSchemaEventsTransactionsHistoryLongSize : public ObIntSysVar
{
public:
  ObSysVarPerformanceSchemaEventsTransactionsHistoryLongSize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_PERFORMANCE_SCHEMA_EVENTS_TRANSACTIONS_HISTORY_LONG_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(532); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(532); }
};
class ObSysVarPerformanceSchemaEventsTransactionsHistorySize : public ObIntSysVar
{
public:
  ObSysVarPerformanceSchemaEventsTransactionsHistorySize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_PERFORMANCE_SCHEMA_EVENTS_TRANSACTIONS_HISTORY_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(533); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(533); }
};
class ObSysVarPerformanceSchemaEventsWaitsHistoryLongSize : public ObIntSysVar
{
public:
  ObSysVarPerformanceSchemaEventsWaitsHistoryLongSize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_PERFORMANCE_SCHEMA_EVENTS_WAITS_HISTORY_LONG_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(534); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(534); }
};
class ObSysVarPerformanceSchemaEventsWaitsHistorySize : public ObIntSysVar
{
public:
  ObSysVarPerformanceSchemaEventsWaitsHistorySize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_PERFORMANCE_SCHEMA_EVENTS_WAITS_HISTORY_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(535); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(535); }
};
class ObSysVarPerformanceSchemaHostsSize : public ObIntSysVar
{
public:
  ObSysVarPerformanceSchemaHostsSize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_PERFORMANCE_SCHEMA_HOSTS_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(536); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(536); }
};
class ObSysVarPerformanceSchemaMaxCondClasses : public ObIntSysVar
{
public:
  ObSysVarPerformanceSchemaMaxCondClasses() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_COND_CLASSES; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(537); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(537); }
};
class ObSysVarPerformanceSchemaMaxCondInstances : public ObIntSysVar
{
public:
  ObSysVarPerformanceSchemaMaxCondInstances() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_COND_INSTANCES; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(538); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(538); }
};
class ObSysVarPerformanceSchemaMaxDigestLength : public ObIntSysVar
{
public:
  ObSysVarPerformanceSchemaMaxDigestLength() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_DIGEST_LENGTH; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(539); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(539); }
};
class ObSysVarPerformanceSchemaMaxFileClasses : public ObIntSysVar
{
public:
  ObSysVarPerformanceSchemaMaxFileClasses() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_FILE_CLASSES; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(540); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(540); }
};
class ObSysVarPerformanceSchemaMaxFileHandles : public ObIntSysVar
{
public:
  ObSysVarPerformanceSchemaMaxFileHandles() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_FILE_HANDLES; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(541); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(541); }
};
class ObSysVarPerformanceSchemaMaxFileInstances : public ObIntSysVar
{
public:
  ObSysVarPerformanceSchemaMaxFileInstances() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_FILE_INSTANCES; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(542); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(542); }
};
class ObSysVarPerformanceSchemaMaxIndexStat : public ObIntSysVar
{
public:
  ObSysVarPerformanceSchemaMaxIndexStat() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_INDEX_STAT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(543); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(543); }
};
class ObSysVarPerformanceSchemaMaxMemoryClasses : public ObIntSysVar
{
public:
  ObSysVarPerformanceSchemaMaxMemoryClasses() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_MEMORY_CLASSES; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(544); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(544); }
};
class ObSysVarPerformanceSchemaMaxMetadataLocks : public ObIntSysVar
{
public:
  ObSysVarPerformanceSchemaMaxMetadataLocks() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_METADATA_LOCKS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(545); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(545); }
};
class ObSysVarPerformanceSchemaMaxMutexClasses : public ObIntSysVar
{
public:
  ObSysVarPerformanceSchemaMaxMutexClasses() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_MUTEX_CLASSES; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(546); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(546); }
};
class ObSysVarPerformanceSchemaMaxMutexInstances : public ObIntSysVar
{
public:
  ObSysVarPerformanceSchemaMaxMutexInstances() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_MUTEX_INSTANCES; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(547); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(547); }
};
class ObSysVarPerformanceSchemaMaxPreparedStatementsInstances : public ObIntSysVar
{
public:
  ObSysVarPerformanceSchemaMaxPreparedStatementsInstances() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_PREPARED_STATEMENTS_INSTANCES; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(548); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(548); }
};
class ObSysVarPerformanceSchemaMaxProgramInstances : public ObIntSysVar
{
public:
  ObSysVarPerformanceSchemaMaxProgramInstances() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_PROGRAM_INSTANCES; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(549); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(549); }
};
class ObSysVarPerformanceSchemaMaxRwlockClasses : public ObIntSysVar
{
public:
  ObSysVarPerformanceSchemaMaxRwlockClasses() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_RWLOCK_CLASSES; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(550); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(550); }
};
class ObSysVarPerformanceSchemaMaxRwlockInstances : public ObIntSysVar
{
public:
  ObSysVarPerformanceSchemaMaxRwlockInstances() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_RWLOCK_INSTANCES; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(551); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(551); }
};
class ObSysVarPerformanceSchemaMaxSocketClasses : public ObIntSysVar
{
public:
  ObSysVarPerformanceSchemaMaxSocketClasses() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_SOCKET_CLASSES; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(552); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(552); }
};
class ObSysVarPerformanceSchemaMaxSocketInstances : public ObIntSysVar
{
public:
  ObSysVarPerformanceSchemaMaxSocketInstances() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_SOCKET_INSTANCES; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(553); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(553); }
};
class ObSysVarPerformanceSchemaMaxSqlTextLength : public ObIntSysVar
{
public:
  ObSysVarPerformanceSchemaMaxSqlTextLength() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_SQL_TEXT_LENGTH; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(554); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(554); }
};
class ObSysVarPerformanceSchemaMaxStageClasses : public ObIntSysVar
{
public:
  ObSysVarPerformanceSchemaMaxStageClasses() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_STAGE_CLASSES; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(555); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(555); }
};
class ObSysVarPerformanceSchemaMaxStatementClasses : public ObIntSysVar
{
public:
  ObSysVarPerformanceSchemaMaxStatementClasses() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_STATEMENT_CLASSES; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(556); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(556); }
};
class ObSysVarPerformanceSchemaMaxStatementStack : public ObIntSysVar
{
public:
  ObSysVarPerformanceSchemaMaxStatementStack() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_STATEMENT_STACK; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(557); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(557); }
};
class ObSysVarPerformanceSchemaMaxTableHandles : public ObIntSysVar
{
public:
  ObSysVarPerformanceSchemaMaxTableHandles() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_TABLE_HANDLES; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(558); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(558); }
};
class ObSysVarPerformanceSchemaMaxTableInstances : public ObIntSysVar
{
public:
  ObSysVarPerformanceSchemaMaxTableInstances() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_TABLE_INSTANCES; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(559); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(559); }
};
class ObSysVarPerformanceSchemaMaxTableLockStat : public ObIntSysVar
{
public:
  ObSysVarPerformanceSchemaMaxTableLockStat() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_TABLE_LOCK_STAT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(560); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(560); }
};
class ObSysVarPerformanceSchemaMaxThreadClasses : public ObIntSysVar
{
public:
  ObSysVarPerformanceSchemaMaxThreadClasses() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_THREAD_CLASSES; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(561); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(561); }
};
class ObSysVarPerformanceSchemaMaxThreadInstances : public ObIntSysVar
{
public:
  ObSysVarPerformanceSchemaMaxThreadInstances() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_THREAD_INSTANCES; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(562); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(562); }
};
class ObSysVarPerformanceSchemaSessionConnectAttrsSize : public ObIntSysVar
{
public:
  ObSysVarPerformanceSchemaSessionConnectAttrsSize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_PERFORMANCE_SCHEMA_SESSION_CONNECT_ATTRS_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(563); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(563); }
};
class ObSysVarPerformanceSchemaSetupActorsSize : public ObIntSysVar
{
public:
  ObSysVarPerformanceSchemaSetupActorsSize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_PERFORMANCE_SCHEMA_SETUP_ACTORS_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(564); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(564); }
};
class ObSysVarPerformanceSchemaSetupObjectsSize : public ObIntSysVar
{
public:
  ObSysVarPerformanceSchemaSetupObjectsSize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_PERFORMANCE_SCHEMA_SETUP_OBJECTS_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(565); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(565); }
};
class ObSysVarPerformanceSchemaUsersSize : public ObIntSysVar
{
public:
  ObSysVarPerformanceSchemaUsersSize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_PERFORMANCE_SCHEMA_USERS_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(566); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(566); }
};
class ObSysVarVersionTokensSessionNumber : public ObIntSysVar
{
public:
  ObSysVarVersionTokensSessionNumber() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_VERSION_TOKENS_SESSION_NUMBER; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(567); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(567); }
};
class ObSysVarBackLog : public ObIntSysVar
{
public:
  ObSysVarBackLog() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_BACK_LOG; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(568); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(568); }
};
class ObSysVarBasedir : public ObVarcharSysVar
{
public:
  ObSysVarBasedir() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_BASEDIR; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(569); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(569); }
};
class ObSysVarBindAddress : public ObVarcharSysVar
{
public:
  ObSysVarBindAddress() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_BIND_ADDRESS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(570); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(570); }
};
class ObSysVarCoreFile : public ObBoolSysVar
{
public:
  ObSysVarCoreFile() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_CORE_FILE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(571); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(571); }
};
class ObSysVarHaveCompress : public ObBoolSysVar
{
public:
  ObSysVarHaveCompress() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_HAVE_COMPRESS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(572); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(572); }
};
class ObSysVarIgnoreDbDirs : public ObVarcharSysVar
{
public:
  ObSysVarIgnoreDbDirs() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_IGNORE_DB_DIRS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(573); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(573); }
};
class ObSysVarInitFile : public ObVarcharSysVar
{
public:
  ObSysVarInitFile() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INIT_FILE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(574); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(574); }
};
class ObSysVarInnodbOpenFiles : public ObIntSysVar
{
public:
  ObSysVarInnodbOpenFiles() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_OPEN_FILES; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(575); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(575); }
};
class ObSysVarInnodbPageCleaners : public ObIntSysVar
{
public:
  ObSysVarInnodbPageCleaners() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_PAGE_CLEANERS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(576); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(576); }
};
class ObSysVarInnodbPurgeThreads : public ObIntSysVar
{
public:
  ObSysVarInnodbPurgeThreads() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_PURGE_THREADS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(577); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(577); }
};
class ObSysVarInnodbReadIoThreads : public ObIntSysVar
{
public:
  ObSysVarInnodbReadIoThreads() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_READ_IO_THREADS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(578); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(578); }
};
class ObSysVarInnodbSyncArraySize : public ObIntSysVar
{
public:
  ObSysVarInnodbSyncArraySize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_SYNC_ARRAY_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(579); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(579); }
};
class ObSysVarInnodbUseNativeAio : public ObIntSysVar
{
public:
  ObSysVarInnodbUseNativeAio() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_USE_NATIVE_AIO; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(580); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(580); }
};
class ObSysVarInnodbWriteIoThreads : public ObIntSysVar
{
public:
  ObSysVarInnodbWriteIoThreads() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_WRITE_IO_THREADS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(581); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(581); }
};
class ObSysVarLargeFilesSupport : public ObIntSysVar
{
public:
  ObSysVarLargeFilesSupport() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_LARGE_FILES_SUPPORT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(582); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(582); }
};
class ObSysVarLockedInMemory : public ObBoolSysVar
{
public:
  ObSysVarLockedInMemory() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_LOCKED_IN_MEMORY; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(583); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(583); }
};
class ObSysVarLogError : public ObVarcharSysVar
{
public:
  ObSysVarLogError() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_LOG_ERROR; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(584); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(584); }
};
class ObSysVarNamedPipe : public ObBoolSysVar
{
public:
  ObSysVarNamedPipe() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_NAMED_PIPE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(585); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(585); }
};
class ObSysVarNamedPipeFullAccessGroup : public ObVarcharSysVar
{
public:
  ObSysVarNamedPipeFullAccessGroup() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_NAMED_PIPE_FULL_ACCESS_GROUP; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(586); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(586); }
};
class ObSysVarOpenFilesLimit : public ObIntSysVar
{
public:
  ObSysVarOpenFilesLimit() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_OPEN_FILES_LIMIT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(587); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(587); }
};
class ObSysVarReportHost : public ObVarcharSysVar
{
public:
  ObSysVarReportHost() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_REPORT_HOST; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(588); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(588); }
};
class ObSysVarReportPassword : public ObVarcharSysVar
{
public:
  ObSysVarReportPassword() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_REPORT_PASSWORD; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(589); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(589); }
};
class ObSysVarReportPort : public ObVarcharSysVar
{
public:
  ObSysVarReportPort() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_REPORT_PORT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(590); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(590); }
};
class ObSysVarReportUser : public ObVarcharSysVar
{
public:
  ObSysVarReportUser() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_REPORT_USER; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(591); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(591); }
};
class ObSysVarServerIdBits : public ObIntSysVar
{
public:
  ObSysVarServerIdBits() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SERVER_ID_BITS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(592); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(592); }
};
class ObSysVarSharedMemory : public ObBoolSysVar
{
public:
  ObSysVarSharedMemory() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SHARED_MEMORY; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(593); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(593); }
};
class ObSysVarSharedMemoryBaseName : public ObVarcharSysVar
{
public:
  ObSysVarSharedMemoryBaseName() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SHARED_MEMORY_BASE_NAME; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(594); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(594); }
};
class ObSysVarSkipNameResolve : public ObBoolSysVar
{
public:
  ObSysVarSkipNameResolve() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SKIP_NAME_RESOLVE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(595); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(595); }
};
class ObSysVarSkipNetworking : public ObBoolSysVar
{
public:
  ObSysVarSkipNetworking() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SKIP_NETWORKING; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(596); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(596); }
};
class ObSysVarThreadHandling : public ObEnumSysVar
{
public:
  const static char * THREAD_HANDLING_NAMES[];
public:
  ObSysVarThreadHandling() : ObEnumSysVar(THREAD_HANDLING_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_THREAD_HANDLING; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(597); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(597); }
};
class ObSysVarThreadPoolAlgorithm : public ObIntSysVar
{
public:
  ObSysVarThreadPoolAlgorithm() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_THREAD_POOL_ALGORITHM; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(598); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(598); }
};
class ObSysVarThreadPoolSize : public ObIntSysVar
{
public:
  ObSysVarThreadPoolSize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_THREAD_POOL_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(599); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(599); }
};
class ObSysVarThreadStack : public ObIntSysVar
{
public:
  ObSysVarThreadStack() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_THREAD_STACK; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(600); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(600); }
};
class ObSysVarBinlogGtidSimpleRecovery : public ObIntSysVar
{
public:
  ObSysVarBinlogGtidSimpleRecovery() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_BINLOG_GTID_SIMPLE_RECOVERY; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(601); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(601); }
};
class ObSysVarInnodbApiEnableBinlog : public ObBoolSysVar
{
public:
  ObSysVarInnodbApiEnableBinlog() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_API_ENABLE_BINLOG; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(602); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(602); }
};
class ObSysVarInnodbLocksUnsafeForBinlog : public ObBoolSysVar
{
public:
  ObSysVarInnodbLocksUnsafeForBinlog() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_LOCKS_UNSAFE_FOR_BINLOG; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(603); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(603); }
};
class ObSysVarInnodbLogBufferSize : public ObIntSysVar
{
public:
  ObSysVarInnodbLogBufferSize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_LOG_BUFFER_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(604); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(604); }
};
class ObSysVarInnodbLogFilesInGroup : public ObIntSysVar
{
public:
  ObSysVarInnodbLogFilesInGroup() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_LOG_FILES_IN_GROUP; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(605); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(605); }
};
class ObSysVarInnodbLogFileSize : public ObIntSysVar
{
public:
  ObSysVarInnodbLogFileSize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_LOG_FILE_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(606); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(606); }
};
class ObSysVarInnodbLogGroupHomeDir : public ObVarcharSysVar
{
public:
  ObSysVarInnodbLogGroupHomeDir() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_LOG_GROUP_HOME_DIR; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(607); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(607); }
};
class ObSysVarInnodbUndoDirectory : public ObVarcharSysVar
{
public:
  ObSysVarInnodbUndoDirectory() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_UNDO_DIRECTORY; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(608); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(608); }
};
class ObSysVarInnodbUndoTablespaces : public ObIntSysVar
{
public:
  ObSysVarInnodbUndoTablespaces() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_UNDO_TABLESPACES; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(609); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(609); }
};
class ObSysVarLogBinBasename : public ObVarcharSysVar
{
public:
  ObSysVarLogBinBasename() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_LOG_BIN_BASENAME; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(610); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(610); }
};
class ObSysVarLogBinIndex : public ObVarcharSysVar
{
public:
  ObSysVarLogBinIndex() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_LOG_BIN_INDEX; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(611); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(611); }
};
class ObSysVarLogSlaveUpdates : public ObBoolSysVar
{
public:
  ObSysVarLogSlaveUpdates() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_LOG_SLAVE_UPDATES; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(612); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(612); }
};
class ObSysVarRelayLog : public ObVarcharSysVar
{
public:
  ObSysVarRelayLog() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_RELAY_LOG; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(613); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(613); }
};
class ObSysVarRelayLogBasename : public ObVarcharSysVar
{
public:
  ObSysVarRelayLogBasename() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_RELAY_LOG_BASENAME; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(614); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(614); }
};
class ObSysVarRelayLogIndex : public ObVarcharSysVar
{
public:
  ObSysVarRelayLogIndex() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_RELAY_LOG_INDEX; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(615); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(615); }
};
class ObSysVarRelayLogInfoFile : public ObVarcharSysVar
{
public:
  ObSysVarRelayLogInfoFile() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_RELAY_LOG_INFO_FILE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(616); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(616); }
};
class ObSysVarRelayLogRecovery : public ObBoolSysVar
{
public:
  ObSysVarRelayLogRecovery() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_RELAY_LOG_RECOVERY; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(617); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(617); }
};
class ObSysVarRelayLogSpaceLimit : public ObIntSysVar
{
public:
  ObSysVarRelayLogSpaceLimit() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_RELAY_LOG_SPACE_LIMIT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(618); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(618); }
};
class ObSysVarDelayKeyWrite : public ObEnumSysVar
{
public:
  const static char * DELAY_KEY_WRITE_NAMES[];
public:
  ObSysVarDelayKeyWrite() : ObEnumSysVar(DELAY_KEY_WRITE_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_DELAY_KEY_WRITE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(619); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(619); }
};
class ObSysVarInnodbLargePrefix : public ObEnumSysVar
{
public:
  const static char * INNODB_LARGE_PREFIX_NAMES[];
public:
  ObSysVarInnodbLargePrefix() : ObEnumSysVar(INNODB_LARGE_PREFIX_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_LARGE_PREFIX; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(620); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(620); }
};
class ObSysVarKeyBufferSize : public ObIntSysVar
{
public:
  ObSysVarKeyBufferSize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_KEY_BUFFER_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(621); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(621); }
};
class ObSysVarKeyCacheAgeThreshold : public ObIntSysVar
{
public:
  ObSysVarKeyCacheAgeThreshold() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_KEY_CACHE_AGE_THRESHOLD; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(622); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(622); }
};
class ObSysVarKeyCacheDivisionLimit : public ObIntSysVar
{
public:
  ObSysVarKeyCacheDivisionLimit() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_KEY_CACHE_DIVISION_LIMIT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(623); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(623); }
};
class ObSysVarMaxSeeksForKey : public ObIntSysVar
{
public:
  ObSysVarMaxSeeksForKey() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_MAX_SEEKS_FOR_KEY; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(624); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(624); }
};
class ObSysVarOldAlterTable : public ObEnumSysVar
{
public:
  const static char * OLD_ALTER_TABLE_NAMES[];
public:
  ObSysVarOldAlterTable() : ObEnumSysVar(OLD_ALTER_TABLE_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_OLD_ALTER_TABLE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(625); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(625); }
};
class ObSysVarTableDefinitionCache : public ObIntSysVar
{
public:
  ObSysVarTableDefinitionCache() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_TABLE_DEFINITION_CACHE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(626); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(626); }
};
class ObSysVarInnodbSortBufferSize : public ObIntSysVar
{
public:
  ObSysVarInnodbSortBufferSize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_SORT_BUFFER_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(627); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(627); }
};
class ObSysVarKeyCacheBlockSize : public ObIntSysVar
{
public:
  ObSysVarKeyCacheBlockSize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_KEY_CACHE_BLOCK_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(628); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(628); }
};
class ObSysVarCharacterSetsDir : public ObVarcharSysVar
{
public:
  ObSysVarCharacterSetsDir() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_CHARACTER_SETS_DIR; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(629); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(629); }
};
class ObSysVarDateFormat : public ObVarcharSysVar
{
public:
  ObSysVarDateFormat() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_DATE_FORMAT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(630); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(630); }
};
class ObSysVarDatetimeFormat : public ObVarcharSysVar
{
public:
  ObSysVarDatetimeFormat() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_DATETIME_FORMAT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(631); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(631); }
};
class ObSysVarDisconnectOnExpiredPassword : public ObBoolSysVar
{
public:
  ObSysVarDisconnectOnExpiredPassword() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_DISCONNECT_ON_EXPIRED_PASSWORD; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(632); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(632); }
};
class ObSysVarExternalUser : public ObVarcharSysVar
{
public:
  ObSysVarExternalUser() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_EXTERNAL_USER; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(633); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(633); }
};
class ObSysVarHaveCrypt : public ObVarcharSysVar
{
public:
  ObSysVarHaveCrypt() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_HAVE_CRYPT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(634); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(634); }
};
class ObSysVarLanguage : public ObVarcharSysVar
{
public:
  ObSysVarLanguage() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_LANGUAGE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(635); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(635); }
};
class ObSysVarLcMessagesDir : public ObVarcharSysVar
{
public:
  ObSysVarLcMessagesDir() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_LC_MESSAGES_DIR; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(636); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(636); }
};
class ObSysVarLowerCaseFileSystem : public ObBoolSysVar
{
public:
  ObSysVarLowerCaseFileSystem() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_LOWER_CASE_FILE_SYSTEM; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(637); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(637); }
};
class ObSysVarMaxDigestLength : public ObIntSysVar
{
public:
  ObSysVarMaxDigestLength() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_MAX_DIGEST_LENGTH; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(638); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(638); }
};
class ObSysVarNdbinfoDatabase : public ObVarcharSysVar
{
public:
  ObSysVarNdbinfoDatabase() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_NDBINFO_DATABASE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(639); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(639); }
};
class ObSysVarNdbinfoTablePrefix : public ObVarcharSysVar
{
public:
  ObSysVarNdbinfoTablePrefix() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_NDBINFO_TABLE_PREFIX; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(640); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(640); }
};
class ObSysVarNdbinfoVersion : public ObVarcharSysVar
{
public:
  ObSysVarNdbinfoVersion() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_NDBINFO_VERSION; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(641); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(641); }
};
class ObSysVarNdbBatchSize : public ObIntSysVar
{
public:
  ObSysVarNdbBatchSize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_NDB_BATCH_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(642); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(642); }
};
class ObSysVarNdbClusterConnectionPool : public ObIntSysVar
{
public:
  ObSysVarNdbClusterConnectionPool() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_NDB_CLUSTER_CONNECTION_POOL; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(643); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(643); }
};
class ObSysVarNdbClusterConnectionPoolNodeids : public ObVarcharSysVar
{
public:
  ObSysVarNdbClusterConnectionPoolNodeids() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_NDB_CLUSTER_CONNECTION_POOL_NODEIDS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(644); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(644); }
};
class ObSysVarNdbLogApplyStatus : public ObBoolSysVar
{
public:
  ObSysVarNdbLogApplyStatus() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_NDB_LOG_APPLY_STATUS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(645); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(645); }
};
class ObSysVarNdbLogBin : public ObBoolSysVar
{
public:
  ObSysVarNdbLogBin() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_NDB_LOG_BIN; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(646); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(646); }
};
class ObSysVarNdbLogFailTerminate : public ObBoolSysVar
{
public:
  ObSysVarNdbLogFailTerminate() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_NDB_LOG_FAIL_TERMINATE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(647); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(647); }
};
class ObSysVarNdbLogOrig : public ObBoolSysVar
{
public:
  ObSysVarNdbLogOrig() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_NDB_LOG_ORIG; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(648); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(648); }
};
class ObSysVarNdbLogTransactionId : public ObBoolSysVar
{
public:
  ObSysVarNdbLogTransactionId() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_NDB_LOG_TRANSACTION_ID; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(649); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(649); }
};
class ObSysVarNdbOptimizedNodeSelection : public ObIntSysVar
{
public:
  ObSysVarNdbOptimizedNodeSelection() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_NDB_OPTIMIZED_NODE_SELECTION; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(650); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(650); }
};
class ObSysVarNdbSystemName : public ObVarcharSysVar
{
public:
  ObSysVarNdbSystemName() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_NDB_SYSTEM_NAME; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(651); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(651); }
};
class ObSysVarNdbUseCopyingAlterTable : public ObBoolSysVar
{
public:
  ObSysVarNdbUseCopyingAlterTable() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_NDB_USE_COPYING_ALTER_TABLE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(652); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(652); }
};
class ObSysVarNdbVersionString : public ObVarcharSysVar
{
public:
  ObSysVarNdbVersionString() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_NDB_VERSION_STRING; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(653); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(653); }
};
class ObSysVarNdbWaitConnected : public ObIntSysVar
{
public:
  ObSysVarNdbWaitConnected() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_NDB_WAIT_CONNECTED; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(654); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(654); }
};
class ObSysVarNdbWaitSetup : public ObIntSysVar
{
public:
  ObSysVarNdbWaitSetup() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_NDB_WAIT_SETUP; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(655); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(655); }
};
class ObSysVarProxyUser : public ObVarcharSysVar
{
public:
  ObSysVarProxyUser() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_PROXY_USER; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(656); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(656); }
};
class ObSysVarSha256PasswordAutoGenerateRsaKeys : public ObBoolSysVar
{
public:
  ObSysVarSha256PasswordAutoGenerateRsaKeys() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SHA256_PASSWORD_AUTO_GENERATE_RSA_KEYS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(657); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(657); }
};
class ObSysVarSha256PasswordPrivateKeyPath : public ObVarcharSysVar
{
public:
  ObSysVarSha256PasswordPrivateKeyPath() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SHA256_PASSWORD_PRIVATE_KEY_PATH; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(658); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(658); }
};
class ObSysVarSha256PasswordPublicKeyPath : public ObVarcharSysVar
{
public:
  ObSysVarSha256PasswordPublicKeyPath() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SHA256_PASSWORD_PUBLIC_KEY_PATH; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(659); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(659); }
};
class ObSysVarSkipShowDatabase : public ObVarcharSysVar
{
public:
  ObSysVarSkipShowDatabase() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SKIP_SHOW_DATABASE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(660); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(660); }
};
class ObSysVarBigTables : public ObBoolSysVar
{
public:
  ObSysVarBigTables() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_BIG_TABLES; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(661); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(661); }
};
class ObSysVarCheckProxyUsers : public ObBoolSysVar
{
public:
  ObSysVarCheckProxyUsers() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_CHECK_PROXY_USERS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(662); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(662); }
};
class ObSysVarDefaultWeekFormat : public ObIntSysVar
{
public:
  ObSysVarDefaultWeekFormat() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_DEFAULT_WEEK_FORMAT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(663); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(663); }
};
class ObSysVarDelayedInsertTimeout : public ObIntSysVar
{
public:
  ObSysVarDelayedInsertTimeout() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_DELAYED_INSERT_TIMEOUT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(664); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(664); }
};
class ObSysVarDelayedQueueSize : public ObIntSysVar
{
public:
  ObSysVarDelayedQueueSize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_DELAYED_QUEUE_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(665); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(665); }
};
class ObSysVarEqRangeIndexDiveLimit : public ObIntSysVar
{
public:
  ObSysVarEqRangeIndexDiveLimit() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_EQ_RANGE_INDEX_DIVE_LIMIT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(666); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(666); }
};
class ObSysVarInnodbStatsAutoRecalc : public ObBoolSysVar
{
public:
  ObSysVarInnodbStatsAutoRecalc() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_STATS_AUTO_RECALC; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(667); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(667); }
};
class ObSysVarInnodbStatsIncludeDeleteMarked : public ObBoolSysVar
{
public:
  ObSysVarInnodbStatsIncludeDeleteMarked() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_STATS_INCLUDE_DELETE_MARKED; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(668); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(668); }
};
class ObSysVarInnodbStatsMethod : public ObEnumSysVar
{
public:
  const static char * INNODB_STATS_METHOD_NAMES[];
public:
  ObSysVarInnodbStatsMethod() : ObEnumSysVar(INNODB_STATS_METHOD_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_STATS_METHOD; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(669); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(669); }
};
class ObSysVarInnodbStatsOnMetadata : public ObBoolSysVar
{
public:
  ObSysVarInnodbStatsOnMetadata() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_STATS_ON_METADATA; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(670); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(670); }
};
class ObSysVarVersionTokensSession : public ObVarcharSysVar
{
public:
  ObSysVarVersionTokensSession() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_VERSION_TOKENS_SESSION; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(671); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(671); }
};
class ObSysVarInnodbStatsPersistentSamplePages : public ObIntSysVar
{
public:
  ObSysVarInnodbStatsPersistentSamplePages() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_STATS_PERSISTENT_SAMPLE_PAGES; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(672); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(672); }
};
class ObSysVarInnodbStatsSamplePages : public ObIntSysVar
{
public:
  ObSysVarInnodbStatsSamplePages() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_STATS_SAMPLE_PAGES; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(673); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(673); }
};
class ObSysVarInnodbStatsTransientSamplePages : public ObIntSysVar
{
public:
  ObSysVarInnodbStatsTransientSamplePages() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_INNODB_STATS_TRANSIENT_SAMPLE_PAGES; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(674); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(674); }
};
class ObSysVarOptimizerSwitch : public ObVarcharSysVar
{
public:
  ObSysVarOptimizerSwitch() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_OPTIMIZER_SWITCH; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(675); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(675); }
};
class ObSysVarMaxConnectErrors : public ObIntSysVar
{
public:
  ObSysVarMaxConnectErrors() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_MAX_CONNECT_ERRORS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(676); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(676); }
};
class ObSysVarMysqlFirewallMode : public ObBoolSysVar
{
public:
  ObSysVarMysqlFirewallMode() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_MYSQL_FIREWALL_MODE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(677); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(677); }
};
class ObSysVarMysqlFirewallTrace : public ObBoolSysVar
{
public:
  ObSysVarMysqlFirewallTrace() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_MYSQL_FIREWALL_TRACE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(678); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(678); }
};
class ObSysVarMysqlNativePasswordProxyUsers : public ObBoolSysVar
{
public:
  ObSysVarMysqlNativePasswordProxyUsers() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_MYSQL_NATIVE_PASSWORD_PROXY_USERS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(679); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(679); }
};
class ObSysVarNetRetryCount : public ObIntSysVar
{
public:
  ObSysVarNetRetryCount() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_NET_RETRY_COUNT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(680); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(680); }
};
class ObSysVarNew : public ObBoolSysVar
{
public:
  ObSysVarNew() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_NEW; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(681); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(681); }
};
class ObSysVarOldPasswords : public ObEnumSysVar
{
public:
  const static char * OLD_PASSWORDS_NAMES[];
public:
  ObSysVarOldPasswords() : ObEnumSysVar(OLD_PASSWORDS_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_OLD_PASSWORDS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(682); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(682); }
};
class ObSysVarOptimizerPruneLevel : public ObIntSysVar
{
public:
  ObSysVarOptimizerPruneLevel() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_OPTIMIZER_PRUNE_LEVEL; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(683); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(683); }
};
class ObSysVarOptimizerSearchDepth : public ObIntSysVar
{
public:
  ObSysVarOptimizerSearchDepth() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_OPTIMIZER_SEARCH_DEPTH; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(684); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(684); }
};
class ObSysVarOptimizerTrace : public ObVarcharSysVar
{
public:
  ObSysVarOptimizerTrace() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_OPTIMIZER_TRACE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(685); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(685); }
};
class ObSysVarOptimizerTraceFeatures : public ObVarcharSysVar
{
public:
  ObSysVarOptimizerTraceFeatures() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_OPTIMIZER_TRACE_FEATURES; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(686); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(686); }
};
class ObSysVarOptimizerTraceLimit : public ObIntSysVar
{
public:
  ObSysVarOptimizerTraceLimit() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_OPTIMIZER_TRACE_LIMIT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(687); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(687); }
};
class ObSysVarOptimizerTraceMaxMemSize : public ObIntSysVar
{
public:
  ObSysVarOptimizerTraceMaxMemSize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_OPTIMIZER_TRACE_MAX_MEM_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(688); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(688); }
};
class ObSysVarOptimizerTraceOffset : public ObIntSysVar
{
public:
  ObSysVarOptimizerTraceOffset() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_OPTIMIZER_TRACE_OFFSET; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(689); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(689); }
};
class ObSysVarParserMaxMemSize : public ObIntSysVar
{
public:
  ObSysVarParserMaxMemSize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_PARSER_MAX_MEM_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(690); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(690); }
};
class ObSysVarRandSeed1 : public ObIntSysVar
{
public:
  ObSysVarRandSeed1() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_RAND_SEED1; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(691); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(691); }
};
class ObSysVarRandSeed2 : public ObIntSysVar
{
public:
  ObSysVarRandSeed2() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_RAND_SEED2; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(692); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(692); }
};
class ObSysVarRangeAllocBlockSize : public ObIntSysVar
{
public:
  ObSysVarRangeAllocBlockSize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_RANGE_ALLOC_BLOCK_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(693); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(693); }
};
class ObSysVarRangeOptimizerMaxMemSize : public ObIntSysVar
{
public:
  ObSysVarRangeOptimizerMaxMemSize() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_RANGE_OPTIMIZER_MAX_MEM_SIZE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(694); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(694); }
};
class ObSysVarRewriterEnabled : public ObBoolSysVar
{
public:
  ObSysVarRewriterEnabled() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_REWRITER_ENABLED; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(695); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(695); }
};
class ObSysVarRewriterVerbose : public ObIntSysVar
{
public:
  ObSysVarRewriterVerbose() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_REWRITER_VERBOSE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(696); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(696); }
};
class ObSysVarSecureAuth : public ObBoolSysVar
{
public:
  ObSysVarSecureAuth() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SECURE_AUTH; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(697); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(697); }
};
class ObSysVarSha256PasswordProxyUsers : public ObBoolSysVar
{
public:
  ObSysVarSha256PasswordProxyUsers() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SHA256_PASSWORD_PROXY_USERS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(698); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(698); }
};
class ObSysVarShowCompatibility56 : public ObBoolSysVar
{
public:
  ObSysVarShowCompatibility56() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SHOW_COMPATIBILITY_56; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(699); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(699); }
};
class ObSysVarShowCreateTableVerbosity : public ObBoolSysVar
{
public:
  ObSysVarShowCreateTableVerbosity() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SHOW_CREATE_TABLE_VERBOSITY; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(700); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(700); }
};
class ObSysVarShowOldTemporals : public ObBoolSysVar
{
public:
  ObSysVarShowOldTemporals() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SHOW_OLD_TEMPORALS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(701); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(701); }
};
class ObSysVarSqlBigSelects : public ObBoolSysVar
{
public:
  ObSysVarSqlBigSelects() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SQL_BIG_SELECTS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(702); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(702); }
};
class ObSysVarUpdatableViewsWithLimit : public ObEnumSysVar
{
public:
  const static char * UPDATABLE_VIEWS_WITH_LIMIT_NAMES[];
public:
  ObSysVarUpdatableViewsWithLimit() : ObEnumSysVar(UPDATABLE_VIEWS_WITH_LIMIT_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_UPDATABLE_VIEWS_WITH_LIMIT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(703); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(703); }
};
class ObSysVarValidatePasswordDictionaryFile : public ObVarcharSysVar
{
public:
  ObSysVarValidatePasswordDictionaryFile() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_VALIDATE_PASSWORD_DICTIONARY_FILE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(704); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(704); }
};
class ObSysVarDelayedInsertLimit : public ObIntSysVar
{
public:
  ObSysVarDelayedInsertLimit() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_DELAYED_INSERT_LIMIT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(705); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(705); }
};
class ObSysVarNdbVersion : public ObVarcharSysVar
{
public:
  ObSysVarNdbVersion() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_NDB_VERSION; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(706); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(706); }
};
class ObSysVarAutoGenerateCerts : public ObBoolSysVar
{
public:
  ObSysVarAutoGenerateCerts() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_AUTO_GENERATE_CERTS; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(707); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(707); }
};
class ObSysVarOptimizerCostBasedTransformation : public ObIntSysVar
{
public:
  ObSysVarOptimizerCostBasedTransformation() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR__OPTIMIZER_COST_BASED_TRANSFORMATION; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(708); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(708); }
};
class ObSysVarRangeIndexDiveLimit : public ObIntSysVar
{
public:
  ObSysVarRangeIndexDiveLimit() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_RANGE_INDEX_DIVE_LIMIT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(709); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(709); }
};
class ObSysVarPartitionIndexDiveLimit : public ObIntSysVar
{
public:
  ObSysVarPartitionIndexDiveLimit() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_PARTITION_INDEX_DIVE_LIMIT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(710); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(710); }
};
class ObSysVarPidFile : public ObVarcharSysVar
{
public:
  ObSysVarPidFile() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_PID_FILE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(711); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(711); }
};
class ObSysVarPort : public ObIntSysVar
{
public:
  ObSysVarPort() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_PORT; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(712); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(712); }
};
class ObSysVarSocket : public ObVarcharSysVar
{
public:
  ObSysVarSocket() : ObVarcharSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_SOCKET; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(713); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(713); }
};
class ObSysVarEnableOptimizerRowgoal : public ObEnumSysVar
{
public:
  const static char * ENABLE_OPTIMIZER_ROWGOAL_NAMES[];
public:
  ObSysVarEnableOptimizerRowgoal() : ObEnumSysVar(ENABLE_OPTIMIZER_ROWGOAL_NAMES, NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_ENABLE_OPTIMIZER_ROWGOAL; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(714); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(714); }
};
class ObSysVarObIvfNprobes : public ObIntSysVar
{
public:
  ObSysVarObIvfNprobes() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_OB_IVF_NPROBES; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(715); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(715); }
};
class ObSysVarPushJoinPredicate : public ObBoolSysVar
{
public:
  ObSysVarPushJoinPredicate() : ObBoolSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR__PUSH_JOIN_PREDICATE; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(716); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(716); }
};
class ObSysVarObSparseDropRatioSearch : public ObIntSysVar
{
public:
  ObSysVarObSparseDropRatioSearch() : ObIntSysVar(NULL, NULL, NULL, NULL, NULL) {}
  inline virtual share::ObSysVarClassType get_type() const { return share::SYS_VAR_OB_SPARSE_DROP_RATIO_SEARCH; }
  inline virtual const common::Datum &get_global_default_datum() const { return share::ObSysVariables::get_default_datum(717); }
  inline virtual const common::SqlTypeMeta &get_global_default_meta() const { return share::ObSysVariables::get_default_meta(717); }
};


class ObSysVarFactory
{
public:
  ObSysVarFactory();
  virtual ~ObSysVarFactory();
  void destroy();
  int create_sys_var(share::ObSysVarClassType sys_var_id, ObBasicSysVar *&sys_var, int64_t store_idx = -1);
  int create_all_sys_vars();
  int create_all_sys_vars_();
  int free_sys_var(ObBasicSysVar *sys_var, int64_t sys_var_idx);
  static int create_sys_var(common::ObIAllocator &allocator_, share::ObSysVarClassType sys_var_id, ObBasicSysVar *&sys_var_ptr);
private:
  int try_init_store_mem();

private:
  common::ObArenaAllocator allocator_;
  ObBasicSysVar **store_;
  ObBasicSysVar **store_buf_;
  bool all_sys_vars_created_;
};

}
}
#endif //OCEANBASE_SQL_SESSION_OB_SYSTEM_VARIABLE_FACTORY_