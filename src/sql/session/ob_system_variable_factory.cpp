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

#define USING_LOG_PREFIX SQL_SESSION
#include "sql/session/ob_system_variable_factory.h"
#include "lib/utility/ob_smart_call.h"
#include "share/system_variable/ob_system_variable_init.h"
#include "share/system_variable/ob_sys_var_meta.h"
using namespace oceanbase::common;

namespace oceanbase
{
namespace sql
{
const char *ObSysVarBinlogRowImage::BINLOG_ROW_IMAGE_NAMES[] = {
  "MINIMAL",
  "NOBLOB",
  "FULL",
  0
};
const char *ObSysVarQueryCacheType::QUERY_CACHE_TYPE_NAMES[] = {
  "OFF",
  "ON",
  "DEMAND",
  0
};
const char *ObSysVarBinlogFormat::BINLOG_FORMAT_NAMES[] = {
  "MIXED",
  "STATEMENT",
  "ROW",
  0
};
const char *ObSysVarProfiling::PROFILING_NAMES[] = {
  "OFF",
  "ON",
  0
};
const char *ObSysVarObReadConsistency::OB_READ_CONSISTENCY_NAMES[] = {
  "",
  "FROZEN",
  "WEAK",
  "STRONG",
  0
};
const char *ObSysVarBlockEncryptionMode::BLOCK_ENCRYPTION_MODE_NAMES[] = {
  "aes-128-ecb",
  "aes-192-ecb",
  "aes-256-ecb",
  "aes-128-cbc",
  "aes-192-cbc",
  "aes-256-cbc",
  "aes-128-cfb1",
  "aes-192-cfb1",
  "aes-256-cfb1",
  "aes-128-cfb8",
  "aes-192-cfb8",
  "aes-256-cfb8",
  "aes-128-cfb128",
  "aes-192-cfb128",
  "aes-256-cfb128",
  "aes-128-ofb",
  "aes-192-ofb",
  "aes-256-ofb",
  "sm4-ecb",
  "sm4-cbc",
  "sm4-cfb",
  "sm4-ofb",
  0
};
const char *ObSysVarValidatePasswordCheckUserName::VALIDATE_PASSWORD_CHECK_USER_NAME_NAMES[] = {
  "on",
  "off",
  0
};
const char *ObSysVarValidatePasswordPolicy::VALIDATE_PASSWORD_POLICY_NAMES[] = {
  "low",
  "medium",
  0
};
const char *ObSysVarCursorSharing::CURSOR_SHARING_NAMES[] = {
  "FORCE",
  "EXACT",
  0
};
const char *ObSysVarParallelDegreePolicy::PARALLEL_DEGREE_POLICY_NAMES[] = {
  "MANUAL",
  "AUTO",
  0
};
const char *ObSysVarInnodbStatsPersistent::INNODB_STATS_PERSISTENT_NAMES[] = {
  "OFF",
  "ON",
  0
};
const char *ObSysVarCardinalityEstimationModel::CARDINALITY_ESTIMATION_MODEL_NAMES[] = {
  "INDEPENDENT",
  "PARTIAL",
  "FULL",
  0
};
const char *ObSysVarFlush::FLUSH_NAMES[] = {
  "OFF",
  "ON",
  0
};
const char *ObSysVarInnodbAdaptiveFlushing::INNODB_ADAPTIVE_FLUSHING_NAMES[] = {
  "OFF",
  "ON",
  0
};
const char *ObSysVarInnodbAdaptiveHashIndex::INNODB_ADAPTIVE_HASH_INDEX_NAMES[] = {
  "OFF",
  "ON",
  0
};
const char *ObSysVarInnodbBackgroundDropListEmpty::INNODB_BACKGROUND_DROP_LIST_EMPTY_NAMES[] = {
  "OFF",
  "ON",
  0
};
const char *ObSysVarInnodbBufferPoolDumpAtShutdown::INNODB_BUFFER_POOL_DUMP_AT_SHUTDOWN_NAMES[] = {
  "OFF",
  "ON",
  0
};
const char *ObSysVarInnodbBufferPoolDumpNow::INNODB_BUFFER_POOL_DUMP_NOW_NAMES[] = {
  "OFF",
  "ON",
  0
};
const char *ObSysVarInnodbBufferPoolLoadAbort::INNODB_BUFFER_POOL_LOAD_ABORT_NAMES[] = {
  "OFF",
  "ON",
  0
};
const char *ObSysVarInnodbBufferPoolLoadNow::INNODB_BUFFER_POOL_LOAD_NOW_NAMES[] = {
  "OFF",
  "ON",
  0
};
const char *ObSysVarInnodbChangeBuffering::INNODB_CHANGE_BUFFERING_NAMES[] = {
  "none",
  "inserts",
  "deletes",
  "changes",
  "purges",
  "all",
  0
};
const char *ObSysVarInnodbChecksumAlgorithm::INNODB_CHECKSUM_ALGORITHM_NAMES[] = {
  "crc32",
  "strict_crc32",
  "innodb",
  "strict_innodb",
  "none",
  "strict_none",
  0
};
const char *ObSysVarInnodbCmpPerIndexEnabled::INNODB_CMP_PER_INDEX_ENABLED_NAMES[] = {
  "OFF",
  "ON",
  0
};
const char *ObSysVarInnodbDefaultRowFormat::INNODB_DEFAULT_ROW_FORMAT_NAMES[] = {
  "REDUNDANT",
  "COMPACT",
  "DYNAMIC",
  0
};
const char *ObSysVarInnodbDisableSortFileCache::INNODB_DISABLE_SORT_FILE_CACHE_NAMES[] = {
  "OFF",
  "ON",
  0
};
const char *ObSysVarInnodbFileFormat::INNODB_FILE_FORMAT_NAMES[] = {
  "Antelope",
  "Barracuda",
  0
};
const char *ObSysVarInnodbFileFormatMax::INNODB_FILE_FORMAT_MAX_NAMES[] = {
  "Antelope",
  "Barracuda",
  0
};
const char *ObSysVarInnodbFilePerTable::INNODB_FILE_PER_TABLE_NAMES[] = {
  "OFF",
  "ON",
  0
};
const char *ObSysVarInnodbFlushNeighbors::INNODB_FLUSH_NEIGHBORS_NAMES[] = {
  "0",
  "1",
  "2",
  0
};
const char *ObSysVarInnodbFlushSync::INNODB_FLUSH_SYNC_NAMES[] = {
  "OFF",
  "ON",
  0
};
const char *ObSysVarHaveSymlink::HAVE_SYMLINK_NAMES[] = {
  "NO",
  "YES",
  0
};
const char *ObSysVarIgnoreBuiltinInnodb::IGNORE_BUILTIN_INNODB_NAMES[] = {
  "NO",
  "YES",
  0
};
const char *ObSysVarInnodbBufferPoolLoadAtStartup::INNODB_BUFFER_POOL_LOAD_AT_STARTUP_NAMES[] = {
  "OFF",
  "ON",
  0
};
const char *ObSysVarInnodbChecksums::INNODB_CHECKSUMS_NAMES[] = {
  "OFF",
  "ON",
  0
};
const char *ObSysVarInnodbDoublewrite::INNODB_DOUBLEWRITE_NAMES[] = {
  "OFF",
  "ON",
  0
};
const char *ObSysVarInnodbFileFormatCheck::INNODB_FILE_FORMAT_CHECK_NAMES[] = {
  "OFF",
  "ON",
  0
};
const char *ObSysVarInnodbFlushMethod::INNODB_FLUSH_METHOD_NAMES[] = {
  "null",
  "fsync",
  "O_DSYNC",
  "littlesync",
  "nosync",
  "O_DIRECT",
  "O_DIRECT_NO_FSYNC",
  0
};
const char *ObSysVarInnodbForceLoadCorrupted::INNODB_FORCE_LOAD_CORRUPTED_NAMES[] = {
  "OFF",
  "ON",
  0
};
const char *ObSysVarInnodbPageSize::INNODB_PAGE_SIZE_NAMES[] = {
  "4096",
  "8192",
  "16384",
  "32768",
  "65536",
  0
};
const char *ObSysVarInnodbVersion::INNODB_VERSION_NAMES[] = {
  "5.7.38",
  0
};
const char *ObSysVarCompletionType::COMPLETION_TYPE_NAMES[] = {
  "NO_CHAIN",
  "CHAIN",
  "RELEASE",
  0
};
const char *ObSysVarEnforceGtidConsistency::ENFORCE_GTID_CONSISTENCY_NAMES[] = {
  "OFF",
  "ON",
  "WARN",
  0
};
const char *ObSysVarGtidMode::GTID_MODE_NAMES[] = {
  "OFF",
  "OFF_PERMISSIVE",
  "ON_PERMISSIVE",
  "ON",
  0
};
const char *ObSysVarGtidNext::GTID_NEXT_NAMES[] = {
  "AUTOMATIC",
  "ANONYMOUS",
  0
};
const char *ObSysVarSessionTrackGtids::SESSION_TRACK_GTIDS_NAMES[] = {
  "OFF",
  "OWN_GTID",
  "ALL_GTIDS",
  0
};
const char *ObSysVarSessionTrackTransactionInfo::SESSION_TRACK_TRANSACTION_INFO_NAMES[] = {
  "OFF",
  "STATE",
  "CHARACTERISTICS",
  0
};
const char *ObSysVarTransactionWriteSetExtraction::TRANSACTION_WRITE_SET_EXTRACTION_NAMES[] = {
  "OFF",
  "MURMUR32",
  "XXHASH64",
  0
};
const char *ObSysVarGroupReplicationExitStateAction::GROUP_REPLICATION_EXIT_STATE_ACTION_NAMES[] = {
  "ABORT_SERVER",
  "READ_ONLY",
  0
};
const char *ObSysVarGroupReplicationFlowControlMode::GROUP_REPLICATION_FLOW_CONTROL_MODE_NAMES[] = {
  "DISABLED",
  "QUOTA",
  0
};
const char *ObSysVarGroupReplicationRecoveryCompleteAt::GROUP_REPLICATION_RECOVERY_COMPLETE_AT_NAMES[] = {
  "TRANSACTIONS_CERTIFIED",
  "TRANSACTIONS_APPLIED",
  0
};
const char *ObSysVarGroupReplicationSslMode::GROUP_REPLICATION_SSL_MODE_NAMES[] = {
  "DISABLED",
  "REQUIRED",
  "VERIFY_CA",
  "VERIFY_IDENTITY",
  0
};
const char *ObSysVarRbrExecMode::RBR_EXEC_MODE_NAMES[] = {
  "STRICT",
  "IDEMPOTENT",
  0
};
const char *ObSysVarRplSemiSyncMasterWaitPoint::RPL_SEMI_SYNC_MASTER_WAIT_POINT_NAMES[] = {
  "AFTER_SYNC",
  "AFTER_COMMIT",
  0
};
const char *ObSysVarSlaveExecMode::SLAVE_EXEC_MODE_NAMES[] = {
  "STRICT",
  "IDEMPOTENT",
  0
};
const char *ObSysVarSlaveParallelType::SLAVE_PARALLEL_TYPE_NAMES[] = {
  "DATABASE",
  "LOGICAL_CLOCK",
  0
};
const char *ObSysVarBinlogErrorAction::BINLOG_ERROR_ACTION_NAMES[] = {
  "IGNORE_ERROR",
  "ABORT_SERVER",
  0
};
const char *ObSysVarBinlogTransactionDependencyTracking::BINLOG_TRANSACTION_DEPENDENCY_TRACKING_NAMES[] = {
  "COMMIT_ORDER",
  "WRITESET",
  "WRITESET_SESSION",
  0
};
const char *ObSysVarDefaultTmpStorageEngine::DEFAULT_TMP_STORAGE_ENGINE_NAMES[] = {
  "InnoDB",
  0
};
const char *ObSysVarSlaveRowsSearchAlgorithms::SLAVE_ROWS_SEARCH_ALGORITHMS_NAMES[] = {
  "TABLE_SCAN,INDEX_SCAN",
  "INDEX_SCAN,HASH_SCAN",
  "TABLE_SCAN,HASH_SCAN",
  "TABLE_SCAN,INDEX_SCAN,HASH_SCAN",
  0
};
const char *ObSysVarSlaveTypeConversions::SLAVE_TYPE_CONVERSIONS_NAMES[] = {
  "ALL_LOSSY",
  "ALL_NON_LOSSY",
  "ALL_SIGNED",
  "ALL_UNSIGNED",
  0
};
const char *ObSysVarNdbDefaultColumnFormat::NDB_DEFAULT_COLUMN_FORMAT_NAMES[] = {
  "FIXED",
  "DYNAMIC",
  0
};
const char *ObSysVarNdbDistribution::NDB_DISTRIBUTION_NAMES[] = {
  "LINHASH",
  "KEYHASH",
  0
};
const char *ObSysVarNdbSlaveConflictRole::NDB_SLAVE_CONFLICT_ROLE_NAMES[] = {
  "NONE",
  "PRIMARY",
  "SECONDARY",
  "PASS",
  0
};
const char *ObSysVarMyisamStatsMethod::MYISAM_STATS_METHOD_NAMES[] = {
  "nulls_unequal",
  "nulls_equal",
  "nulls_ignored",
  0
};
const char *ObSysVarInternalTmpDiskStorageEngine::INTERNAL_TMP_DISK_STORAGE_ENGINE_NAMES[] = {
  "MYISAM",
  "INNODB",
  0
};
const char *ObSysVarLogTimestamps::LOG_TIMESTAMPS_NAMES[] = {
  "UTC",
  "SYSTEM",
  0
};
const char *ObSysVarThreadHandling::THREAD_HANDLING_NAMES[] = {
  "no-threads",
  "one-thread-per-connection",
  "loaded-dynamically",
  0
};
const char *ObSysVarDelayKeyWrite::DELAY_KEY_WRITE_NAMES[] = {
  "ON",
  "OFF",
  "ALL",
  0
};
const char *ObSysVarInnodbLargePrefix::INNODB_LARGE_PREFIX_NAMES[] = {
  "ON",
  "OFF",
  0
};
const char *ObSysVarOldAlterTable::OLD_ALTER_TABLE_NAMES[] = {
  "OFF",
  "ON",
  0
};
const char *ObSysVarInnodbStatsMethod::INNODB_STATS_METHOD_NAMES[] = {
  "nulls_equal",
  "nulls_unequal",
  "nulls_ignored",
  0
};
const char *ObSysVarOldPasswords::OLD_PASSWORDS_NAMES[] = {
  "0",
  "1",
  "2",
  0
};
const char *ObSysVarUpdatableViewsWithLimit::UPDATABLE_VIEWS_WITH_LIMIT_NAMES[] = {
  "OFF",
  "ON",
  "NO",
  "YES",
  0
};
const char *ObSysVarEnableOptimizerRowgoal::ENABLE_OPTIMIZER_ROWGOAL_NAMES[] = {
  "OFF",
  "AUTO",
  "ON",
  0
};

ObSysVarFactory::ObSysVarFactory()
  : allocator_(ObMemAttr(ObModIds::OB_COMMON_SYS_VAR_FAC)),
    store_(nullptr), store_buf_(nullptr), all_sys_vars_created_(false)
{
}

int ObSysVarFactory::try_init_store_mem()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(store_)) {
    void *store_ptr = NULL;
    if (OB_ISNULL(store_ptr = allocator_.alloc(sizeof(ObBasicSysVar *) * share::ObSysVarMeta::ALL_SYS_VARS_COUNT))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to alloc store_.", K(ret));
    } else {
      store_ = static_cast<ObBasicSysVar **>(store_ptr);
      MEMSET(store_, 0, sizeof(ObBasicSysVar *) * share::ObSysVarMeta::ALL_SYS_VARS_COUNT);
    }
  }
  if (OB_ISNULL(store_buf_)) {
    void *store_buf_ptr = NULL;
    if (OB_ISNULL(store_buf_ptr = allocator_.alloc(sizeof(ObBasicSysVar *) * share::ObSysVarMeta::ALL_SYS_VARS_COUNT))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to alloc store_buf_.", K(ret));
    } else {
      store_buf_ = static_cast<ObBasicSysVar **>(store_buf_ptr);
      MEMSET(store_buf_, 0, sizeof(ObBasicSysVar *) * share::ObSysVarMeta::ALL_SYS_VARS_COUNT);
    }
  }
  return ret;
}

ObSysVarFactory::~ObSysVarFactory()
{
  destroy();
}

void ObSysVarFactory::destroy()
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(store_)) {
    for (int64_t i = 0; i < share::ObSysVarMeta::ALL_SYS_VARS_COUNT; ++i) {
      if (OB_NOT_NULL(store_[i])) {
        store_[i]->~ObBasicSysVar();
        store_[i] = nullptr;
      }
    }
    store_ = nullptr;
  }
  if (OB_NOT_NULL(store_buf_)) {
    for (int64_t i = 0; i < share::ObSysVarMeta::ALL_SYS_VARS_COUNT; ++i) {
      if (OB_NOT_NULL(store_buf_[i])) {
        store_buf_[i]->~ObBasicSysVar();
        store_buf_[i] = nullptr;
      }
    }
    store_buf_ = nullptr;
  }
  allocator_.reset();
  all_sys_vars_created_ = false;
}

int ObSysVarFactory::free_sys_var(ObBasicSysVar *sys_var, int64_t sys_var_idx)
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(store_) && OB_NOT_NULL(store_buf_)) {
    OV (OB_NOT_NULL(sys_var));
    OV (share::ObSysVarMeta::is_valid_sys_var_store_idx(sys_var_idx));
    OV (sys_var == store_[sys_var_idx], OB_ERR_UNEXPECTED, sys_var, sys_var_idx);
    if (OB_NOT_NULL(store_buf_[sys_var_idx])) {
      OX (store_buf_[sys_var_idx]->~ObBasicSysVar());
      OX (allocator_.free(store_buf_[sys_var_idx]));
      OX (store_buf_[sys_var_idx] = nullptr);
    }
    OX (store_buf_[sys_var_idx] = store_[sys_var_idx]);
    OX (store_buf_[sys_var_idx]->clean_value());
    OX (store_[sys_var_idx] = nullptr);
  }
  return ret;
}

int ObSysVarFactory::create_all_sys_vars()
{
  return SMART_CALL(create_all_sys_vars_());
}

int ObSysVarFactory::create_all_sys_vars_()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(try_init_store_mem())) {
    LOG_WARN("Fail to init", K(ret));
  } else if (!all_sys_vars_created_) {
    int64_t store_idx = -1;
    ObBasicSysVar *sys_var_ptr = NULL;
    int64_t total_mem_size = 0
        + sizeof(ObSysVarAutoIncrementIncrement)
        + sizeof(ObSysVarAutoIncrementOffset)
        + sizeof(ObSysVarAutocommit)
        + sizeof(ObSysVarCharacterSetClient)
        + sizeof(ObSysVarCharacterSetConnection)
        + sizeof(ObSysVarCharacterSetDatabase)
        + sizeof(ObSysVarCharacterSetResults)
        + sizeof(ObSysVarCharacterSetServer)
        + sizeof(ObSysVarCharacterSetSystem)
        + sizeof(ObSysVarCollationConnection)
        + sizeof(ObSysVarCollationDatabase)
        + sizeof(ObSysVarCollationServer)
        + sizeof(ObSysVarInteractiveTimeout)
        + sizeof(ObSysVarLastInsertId)
        + sizeof(ObSysVarMaxAllowedPacket)
        + sizeof(ObSysVarSqlMode)
        + sizeof(ObSysVarTimeZone)
        + sizeof(ObSysVarTxIsolation)
        + sizeof(ObSysVarVersionComment)
        + sizeof(ObSysVarWaitTimeout)
        + sizeof(ObSysVarBinlogRowImage)
        + sizeof(ObSysVarCharacterSetFilesystem)
        + sizeof(ObSysVarConnectTimeout)
        + sizeof(ObSysVarDatadir)
        + sizeof(ObSysVarDebugSync)
        + sizeof(ObSysVarDivPrecisionIncrement)
        + sizeof(ObSysVarExplicitDefaultsForTimestamp)
        + sizeof(ObSysVarGroupConcatMaxLen)
        + sizeof(ObSysVarIdentity)
        + sizeof(ObSysVarLowerCaseTableNames)
        + sizeof(ObSysVarNetReadTimeout)
        + sizeof(ObSysVarNetWriteTimeout)
        + sizeof(ObSysVarReadOnly)
        + sizeof(ObSysVarSqlAutoIsNull)
        + sizeof(ObSysVarSqlSelectLimit)
        + sizeof(ObSysVarTimestamp)
        + sizeof(ObSysVarTxReadOnly)
        + sizeof(ObSysVarVersion)
        + sizeof(ObSysVarSqlWarnings)
        + sizeof(ObSysVarMaxUserConnections)
        + sizeof(ObSysVarInitConnect)
        + sizeof(ObSysVarLicense)
        + sizeof(ObSysVarNetBufferLength)
        + sizeof(ObSysVarSystemTimeZone)
        + sizeof(ObSysVarQueryCacheSize)
        + sizeof(ObSysVarQueryCacheType)
        + sizeof(ObSysVarSqlQuoteShowCreate)
        + sizeof(ObSysVarMaxSpRecursionDepth)
        + sizeof(ObSysVarSqlSafeUpdates)
        + sizeof(ObSysVarConcurrentInsert)
        + sizeof(ObSysVarDefaultAuthenticationPlugin)
        + sizeof(ObSysVarDisabledStorageEngines)
        + sizeof(ObSysVarErrorCount)
        + sizeof(ObSysVarGeneralLog)
        + sizeof(ObSysVarHaveOpenssl)
        + sizeof(ObSysVarHaveProfiling)
        + sizeof(ObSysVarHaveSsl)
        + sizeof(ObSysVarHostname)
        + sizeof(ObSysVarLcMessages)
        + sizeof(ObSysVarLocalInfile)
        + sizeof(ObSysVarLockWaitTimeout)
        + sizeof(ObSysVarLongQueryTime)
        + sizeof(ObSysVarMaxConnections)
        + sizeof(ObSysVarMaxExecutionTime)
        + sizeof(ObSysVarProtocolVersion)
        + sizeof(ObSysVarServerId)
        + sizeof(ObSysVarSslCa)
        + sizeof(ObSysVarSslCapath)
        + sizeof(ObSysVarSslCert)
        + sizeof(ObSysVarSslCipher)
        + sizeof(ObSysVarSslCrl)
        + sizeof(ObSysVarSslCrlpath)
        + sizeof(ObSysVarSslKey)
        + sizeof(ObSysVarTimeFormat)
        + sizeof(ObSysVarTlsVersion)
        + sizeof(ObSysVarTmpTableSize)
        + sizeof(ObSysVarTmpdir)
        + sizeof(ObSysVarUniqueChecks)
        + sizeof(ObSysVarVersionCompileMachine)
        + sizeof(ObSysVarVersionCompileOs)
        + sizeof(ObSysVarWarningCount)
        + sizeof(ObSysVarSessionTrackSchema)
        + sizeof(ObSysVarSessionTrackSystemVariables)
        + sizeof(ObSysVarSessionTrackStateChange)
        + sizeof(ObSysVarHaveQueryCache)
        + sizeof(ObSysVarQueryCacheLimit)
        + sizeof(ObSysVarQueryCacheMinResUnit)
        + sizeof(ObSysVarQueryCacheWlockInvalidate)
        + sizeof(ObSysVarBinlogFormat)
        + sizeof(ObSysVarBinlogChecksum)
        + sizeof(ObSysVarBinlogRowsQueryLogEvents)
        + sizeof(ObSysVarLogBin)
        + sizeof(ObSysVarServerUuid)
        + sizeof(ObSysVarDefaultStorageEngine)
        + sizeof(ObSysVarCteMaxRecursionDepth)
        + sizeof(ObSysVarRegexpStackLimit)
        + sizeof(ObSysVarRegexpTimeLimit)
        + sizeof(ObSysVarProfiling)
        + sizeof(ObSysVarProfilingHistorySize)
        + sizeof(ObSysVarObLogLevel)
        + sizeof(ObSysVarObQueryTimeout)
        + sizeof(ObSysVarObReadConsistency)
        + sizeof(ObSysVarObEnableTransformation)
        + sizeof(ObSysVarObTrxTimeout)
        + sizeof(ObSysVarObEnablePlanCache)
        + sizeof(ObSysVarObEnableIndexDirectSelect)
        + sizeof(ObSysVarObEnableAggregationPushdown)
        + sizeof(ObSysVarObGlobalDebugSync)
        + sizeof(ObSysVarObEnableShowTrace)
        + sizeof(ObSysVarObPlanCachePercentage)
        + sizeof(ObSysVarObPlanCacheEvictHighPercentage)
        + sizeof(ObSysVarObPlanCacheEvictLowPercentage)
        + sizeof(ObSysVarRecyclebin)
        + sizeof(ObSysVarIsResultAccurate)
        + sizeof(ObSysVarErrorOnOverlapTime)
        + sizeof(ObSysVarObSqlWorkAreaPercentage)
        + sizeof(ObSysVarForeignKeyChecks)
        + sizeof(ObSysVarObTcpInvitedNodes)
        + sizeof(ObSysVarAutoIncrementCacheSize)
        + sizeof(ObSysVarParallelServersTarget)
        + sizeof(ObSysVarObTrxIdleTimeout)
        + sizeof(ObSysVarBlockEncryptionMode)
        + sizeof(ObSysVarNljBatchingEnabled)
        + sizeof(ObSysVarTransactionIsolation)
        + sizeof(ObSysVarObTrxLockTimeout)
        + sizeof(ObSysVarValidatePasswordCheckUserName)
        + sizeof(ObSysVarValidatePasswordLength)
        + sizeof(ObSysVarValidatePasswordMixedCaseCount)
        + sizeof(ObSysVarValidatePasswordNumberCount)
        + sizeof(ObSysVarValidatePasswordPolicy)
        + sizeof(ObSysVarValidatePasswordSpecialCharCount)
        + sizeof(ObSysVarDefaultPasswordLifetime)
        + sizeof(ObSysVarEnableParallelDml)
        + sizeof(ObSysVarSecureFilePriv)
        + sizeof(ObSysVarEnableParallelQuery)
        + sizeof(ObSysVarForceParallelQueryDop)
        + sizeof(ObSysVarForceParallelDmlDop)
        + sizeof(ObSysVarObPlBlockTimeout)
        + sizeof(ObSysVarTransactionReadOnly)
        + sizeof(ObSysVarPerformanceSchema)
        + sizeof(ObSysVarEnableParallelDdl)
        + sizeof(ObSysVarForceParallelDdlDop)
        + sizeof(ObSysVarCursorSharing)
        + sizeof(ObSysVarAggregationOptimizationSettings)
        + sizeof(ObSysVarPxSharedHashJoin)
        + sizeof(ObSysVarSqlNotes)
        + sizeof(ObSysVarInnodbStrictMode)
        + sizeof(ObSysVarWindowfuncOptimizationSettings)
        + sizeof(ObSysVarLogRowValueOptions)
        + sizeof(ObSysVarObMaxReadStaleTime)
        + sizeof(ObSysVarOptimizerGatherStatsOnLoad)
        + sizeof(ObSysVarShowDdlInCompatMode)
        + sizeof(ObSysVarParallelDegreePolicy)
        + sizeof(ObSysVarParallelDegreeLimit)
        + sizeof(ObSysVarParallelMinScanTimeThreshold)
        + sizeof(ObSysVarOptimizerDynamicSampling)
        + sizeof(ObSysVarRuntimeFilterType)
        + sizeof(ObSysVarRuntimeFilterWaitTimeMs)
        + sizeof(ObSysVarRuntimeFilterMaxInNum)
        + sizeof(ObSysVarRuntimeBloomFilterMaxSize)
        + sizeof(ObSysVarAutomaticSpPrivileges)
        + sizeof(ObSysVarObEnablePlCache)
        + sizeof(ObSysVarObDefaultLobInrowThreshold)
        + sizeof(ObSysVarEnableStorageCardinalityEstimation)
        + sizeof(ObSysVarLcTimeNames)
        + sizeof(ObSysVarActivateAllRolesOnLogin)
        + sizeof(ObSysVarInnodbStatsPersistent)
        + sizeof(ObSysVarDebug)
        + sizeof(ObSysVarInnodbChangeBufferingDebug)
        + sizeof(ObSysVarInnodbDisableResizeBufferPoolDebug)
        + sizeof(ObSysVarInnodbFilMakePageDirtyDebug)
        + sizeof(ObSysVarInnodbLimitOptimisticInsertDebug)
        + sizeof(ObSysVarInnodbMergeThresholdSetAllDebug)
        + sizeof(ObSysVarInnodbSavedPageNumberDebug)
        + sizeof(ObSysVarInnodbTrxPurgeViewUpdateOnlyDebug)
        + sizeof(ObSysVarInnodbTrxRsegNSlotsDebug)
        + sizeof(ObSysVarStoredProgramCache)
        + sizeof(ObSysVarCardinalityEstimationModel)
        + sizeof(ObSysVarFlush)
        + sizeof(ObSysVarFlushTime)
        + sizeof(ObSysVarInnodbAdaptiveFlushing)
        + sizeof(ObSysVarInnodbAdaptiveFlushingLwm)
        + sizeof(ObSysVarInnodbAdaptiveHashIndex)
        + sizeof(ObSysVarInnodbAdaptiveHashIndexParts)
        + sizeof(ObSysVarInnodbAdaptiveMaxSleepDelay)
        + sizeof(ObSysVarInnodbAutoextendIncrement)
        + sizeof(ObSysVarInnodbBackgroundDropListEmpty)
        + sizeof(ObSysVarInnodbBufferPoolDumpAtShutdown)
        + sizeof(ObSysVarInnodbBufferPoolDumpNow)
        + sizeof(ObSysVarInnodbBufferPoolDumpPct)
        + sizeof(ObSysVarInnodbBufferPoolFilename)
        + sizeof(ObSysVarInnodbBufferPoolLoadAbort)
        + sizeof(ObSysVarInnodbBufferPoolLoadNow)
        + sizeof(ObSysVarInnodbBufferPoolSize)
        + sizeof(ObSysVarInnodbChangeBufferMaxSize)
        + sizeof(ObSysVarInnodbChangeBuffering)
        + sizeof(ObSysVarInnodbChecksumAlgorithm)
        + sizeof(ObSysVarInnodbCmpPerIndexEnabled)
        + sizeof(ObSysVarInnodbCommitConcurrency)
        + sizeof(ObSysVarInnodbCompressionFailureThresholdPct)
        + sizeof(ObSysVarInnodbCompressionLevel)
        + sizeof(ObSysVarInnodbCompressionPadPctMax)
        + sizeof(ObSysVarInnodbConcurrencyTickets)
        + sizeof(ObSysVarInnodbDefaultRowFormat)
        + sizeof(ObSysVarInnodbDisableSortFileCache)
        + sizeof(ObSysVarInnodbFileFormat)
        + sizeof(ObSysVarInnodbFileFormatMax)
        + sizeof(ObSysVarInnodbFilePerTable)
        + sizeof(ObSysVarInnodbFillFactor)
        + sizeof(ObSysVarInnodbFlushNeighbors)
        + sizeof(ObSysVarInnodbFlushSync)
        + sizeof(ObSysVarInnodbFlushingAvgLoops)
        + sizeof(ObSysVarInnodbLruScanDepth)
        + sizeof(ObSysVarInnodbMaxDirtyPagesPct)
        + sizeof(ObSysVarInnodbMaxDirtyPagesPctLwm)
        + sizeof(ObSysVarInnodbMaxPurgeLag)
        + sizeof(ObSysVarInnodbMaxPurgeLagDelay)
        + sizeof(ObSysVarHaveSymlink)
        + sizeof(ObSysVarIgnoreBuiltinInnodb)
        + sizeof(ObSysVarInnodbBufferPoolChunkSize)
        + sizeof(ObSysVarInnodbBufferPoolInstances)
        + sizeof(ObSysVarInnodbBufferPoolLoadAtStartup)
        + sizeof(ObSysVarInnodbChecksums)
        + sizeof(ObSysVarInnodbDoublewrite)
        + sizeof(ObSysVarInnodbFileFormatCheck)
        + sizeof(ObSysVarInnodbFlushMethod)
        + sizeof(ObSysVarInnodbForceLoadCorrupted)
        + sizeof(ObSysVarInnodbPageSize)
        + sizeof(ObSysVarInnodbVersion)
        + sizeof(ObSysVarMyisamMmapSize)
        + sizeof(ObSysVarTableOpenCacheInstances)
        + sizeof(ObSysVarGtidExecuted)
        + sizeof(ObSysVarGtidOwned)
        + sizeof(ObSysVarInnodbRollbackOnTimeout)
        + sizeof(ObSysVarCompletionType)
        + sizeof(ObSysVarEnforceGtidConsistency)
        + sizeof(ObSysVarGtidExecutedCompressionPeriod)
        + sizeof(ObSysVarGtidMode)
        + sizeof(ObSysVarGtidNext)
        + sizeof(ObSysVarGtidPurged)
        + sizeof(ObSysVarInnodbApiBkCommitInterval)
        + sizeof(ObSysVarInnodbApiTrxLevel)
        + sizeof(ObSysVarSessionTrackGtids)
        + sizeof(ObSysVarSessionTrackTransactionInfo)
        + sizeof(ObSysVarTransactionAllocBlockSize)
        + sizeof(ObSysVarTransactionAllowBatching)
        + sizeof(ObSysVarTransactionPreallocSize)
        + sizeof(ObSysVarTransactionWriteSetExtraction)
        + sizeof(ObSysVarInformationSchemaStatsExpiry)
        + sizeof(ObSysVarGroupReplicationAllowLocalDisjointGtidsJoin)
        + sizeof(ObSysVarGroupReplicationAllowLocalLowerVersionJoin)
        + sizeof(ObSysVarGroupReplicationAutoIncrementIncrement)
        + sizeof(ObSysVarGroupReplicationBootstrapGroup)
        + sizeof(ObSysVarGroupReplicationComponentsStopTimeout)
        + sizeof(ObSysVarGroupReplicationCompressionThreshold)
        + sizeof(ObSysVarGroupReplicationEnforceUpdateEverywhereChecks)
        + sizeof(ObSysVarGroupReplicationExitStateAction)
        + sizeof(ObSysVarGroupReplicationFlowControlApplierThreshold)
        + sizeof(ObSysVarGroupReplicationFlowControlCertifierThreshold)
        + sizeof(ObSysVarGroupReplicationFlowControlMode)
        + sizeof(ObSysVarGroupReplicationForceMembers)
        + sizeof(ObSysVarGroupReplicationGroupName)
        + sizeof(ObSysVarGroupReplicationGtidAssignmentBlockSize)
        + sizeof(ObSysVarGroupReplicationIpWhitelist)
        + sizeof(ObSysVarGroupReplicationLocalAddress)
        + sizeof(ObSysVarGroupReplicationMemberWeight)
        + sizeof(ObSysVarGroupReplicationPollSpinLoops)
        + sizeof(ObSysVarGroupReplicationRecoveryCompleteAt)
        + sizeof(ObSysVarGroupReplicationRecoveryReconnectInterval)
        + sizeof(ObSysVarGroupReplicationRecoveryRetryCount)
        + sizeof(ObSysVarGroupReplicationRecoverySslCa)
        + sizeof(ObSysVarGroupReplicationRecoverySslCapath)
        + sizeof(ObSysVarGroupReplicationRecoverySslCert)
        + sizeof(ObSysVarGroupReplicationRecoverySslCipher)
        + sizeof(ObSysVarGroupReplicationRecoverySslCrl)
        + sizeof(ObSysVarGroupReplicationRecoverySslCrlpath)
        + sizeof(ObSysVarGroupReplicationRecoverySslKey)
        + sizeof(ObSysVarGroupReplicationRecoverySslVerifyServerCert)
        + sizeof(ObSysVarGroupReplicationRecoveryUseSsl)
        + sizeof(ObSysVarGroupReplicationSinglePrimaryMode)
        + sizeof(ObSysVarGroupReplicationSslMode)
        + sizeof(ObSysVarGroupReplicationStartOnBoot)
        + sizeof(ObSysVarGroupReplicationTransactionSizeLimit)
        + sizeof(ObSysVarGroupReplicationUnreachableMajorityTimeout)
        + sizeof(ObSysVarInnodbReplicationDelay)
        + sizeof(ObSysVarMasterInfoRepository)
        + sizeof(ObSysVarMasterVerifyChecksum)
        + sizeof(ObSysVarPseudoSlaveMode)
        + sizeof(ObSysVarPseudoThreadId)
        + sizeof(ObSysVarRbrExecMode)
        + sizeof(ObSysVarReplicationOptimizeForStaticPluginConfig)
        + sizeof(ObSysVarReplicationSenderObserveCommitOnly)
        + sizeof(ObSysVarRplSemiSyncMasterEnabled)
        + sizeof(ObSysVarRplSemiSyncMasterTimeout)
        + sizeof(ObSysVarRplSemiSyncMasterTraceLevel)
        + sizeof(ObSysVarRplSemiSyncMasterWaitForSlaveCount)
        + sizeof(ObSysVarRplSemiSyncMasterWaitNoSlave)
        + sizeof(ObSysVarRplSemiSyncMasterWaitPoint)
        + sizeof(ObSysVarRplSemiSyncSlaveEnabled)
        + sizeof(ObSysVarRplSemiSyncSlaveTraceLevel)
        + sizeof(ObSysVarRplStopSlaveTimeout)
        + sizeof(ObSysVarSlaveAllowBatching)
        + sizeof(ObSysVarSlaveCheckpointGroup)
        + sizeof(ObSysVarSlaveCheckpointPeriod)
        + sizeof(ObSysVarSlaveCompressedProtocol)
        + sizeof(ObSysVarSlaveExecMode)
        + sizeof(ObSysVarSlaveMaxAllowedPacket)
        + sizeof(ObSysVarSlaveNetTimeout)
        + sizeof(ObSysVarSlaveParallelType)
        + sizeof(ObSysVarSlaveParallelWorkers)
        + sizeof(ObSysVarSlavePendingJobsSizeMax)
        + sizeof(ObSysVarSlavePreserveCommitOrder)
        + sizeof(ObSysVarSlaveSqlVerifyChecksum)
        + sizeof(ObSysVarSlaveTransactionRetries)
        + sizeof(ObSysVarSqlSlaveSkipCounter)
        + sizeof(ObSysVarInnodbForceRecovery)
        + sizeof(ObSysVarSkipSlaveStart)
        + sizeof(ObSysVarSlaveLoadTmpdir)
        + sizeof(ObSysVarSlaveSkipErrors)
        + sizeof(ObSysVarInnodbSyncDebug)
        + sizeof(ObSysVarDefaultCollationForUtf8mb4)
        + sizeof(ObSysVarInsertId)
        + sizeof(ObSysVarJoinBufferSize)
        + sizeof(ObSysVarMaxJoinSize)
        + sizeof(ObSysVarMaxLengthForSortData)
        + sizeof(ObSysVarMaxPreparedStmtCount)
        + sizeof(ObSysVarMaxSortLength)
        + sizeof(ObSysVarMinExaminedRowLimit)
        + sizeof(ObSysVarMultiRangeCount)
        + sizeof(ObSysVarMysqlxConnectTimeout)
        + sizeof(ObSysVarMysqlxIdleWorkerThreadTimeout)
        + sizeof(ObSysVarMysqlxMaxAllowedPacket)
        + sizeof(ObSysVarMysqlxMaxConnections)
        + sizeof(ObSysVarMysqlxMinWorkerThreads)
        + sizeof(ObSysVarPerformanceSchemaShowProcesslist)
        + sizeof(ObSysVarQueryAllocBlockSize)
        + sizeof(ObSysVarQueryPreallocSize)
        + sizeof(ObSysVarSlowQueryLog)
        + sizeof(ObSysVarSlowQueryLogFile)
        + sizeof(ObSysVarSortBufferSize)
        + sizeof(ObSysVarSqlBufferResult)
        + sizeof(ObSysVarBinlogCacheSize)
        + sizeof(ObSysVarBinlogDirectNonTransactionalUpdates)
        + sizeof(ObSysVarBinlogErrorAction)
        + sizeof(ObSysVarBinlogGroupCommitSyncDelay)
        + sizeof(ObSysVarBinlogGroupCommitSyncNoDelayCount)
        + sizeof(ObSysVarBinlogMaxFlushQueueTime)
        + sizeof(ObSysVarBinlogOrderCommits)
        + sizeof(ObSysVarBinlogStmtCacheSize)
        + sizeof(ObSysVarBinlogTransactionDependencyHistorySize)
        + sizeof(ObSysVarBinlogTransactionDependencyTracking)
        + sizeof(ObSysVarExpireLogsDays)
        + sizeof(ObSysVarInnodbFlushLogAtTimeout)
        + sizeof(ObSysVarInnodbFlushLogAtTrxCommit)
        + sizeof(ObSysVarInnodbLogCheckpointNow)
        + sizeof(ObSysVarInnodbLogChecksums)
        + sizeof(ObSysVarInnodbLogCompressedPages)
        + sizeof(ObSysVarInnodbLogWriteAheadSize)
        + sizeof(ObSysVarInnodbMaxUndoLogSize)
        + sizeof(ObSysVarInnodbOnlineAlterLogMaxSize)
        + sizeof(ObSysVarInnodbUndoLogTruncate)
        + sizeof(ObSysVarInnodbUndoLogs)
        + sizeof(ObSysVarLogBinTrustFunctionCreators)
        + sizeof(ObSysVarLogBinUseV1RowEvents)
        + sizeof(ObSysVarLogBuiltinAsIdentifiedByPassword)
        + sizeof(ObSysVarMaxBinlogCacheSize)
        + sizeof(ObSysVarMaxBinlogSize)
        + sizeof(ObSysVarMaxBinlogStmtCacheSize)
        + sizeof(ObSysVarMaxRelayLogSize)
        + sizeof(ObSysVarRelayLogInfoRepository)
        + sizeof(ObSysVarRelayLogPurge)
        + sizeof(ObSysVarSyncBinlog)
        + sizeof(ObSysVarSyncRelayLog)
        + sizeof(ObSysVarSyncRelayLogInfo)
        + sizeof(ObSysVarInnodbDeadlockDetect)
        + sizeof(ObSysVarInnodbLockWaitTimeout)
        + sizeof(ObSysVarInnodbPrintAllDeadlocks)
        + sizeof(ObSysVarInnodbTableLocks)
        + sizeof(ObSysVarMaxWriteLockCount)
        + sizeof(ObSysVarObEnableRoleIds)
        + sizeof(ObSysVarInnodbReadOnly)
        + sizeof(ObSysVarInnodbApiDisableRowlock)
        + sizeof(ObSysVarInnodbAutoincLockMode)
        + sizeof(ObSysVarSkipExternalLocking)
        + sizeof(ObSysVarSuperReadOnly)
        + sizeof(ObSysVarLowPriorityUpdates)
        + sizeof(ObSysVarMaxErrorCount)
        + sizeof(ObSysVarMaxInsertDelayedThreads)
        + sizeof(ObSysVarFtStopwordFile)
        + sizeof(ObSysVarInnodbFtCacheSize)
        + sizeof(ObSysVarInnodbFtSortPllDegree)
        + sizeof(ObSysVarInnodbFtTotalCacheSize)
        + sizeof(ObSysVarMecabRcFile)
        + sizeof(ObSysVarMetadataLocksCacheSize)
        + sizeof(ObSysVarMetadataLocksHashInstances)
        + sizeof(ObSysVarInnodbTempDataFilePath)
        + sizeof(ObSysVarInnodbDataFilePath)
        + sizeof(ObSysVarInnodbDataHomeDir)
        + sizeof(ObSysVarDefaultTmpStorageEngine)
        + sizeof(ObSysVarInnodbFtEnableDiagPrint)
        + sizeof(ObSysVarInnodbFtNumWordOptimize)
        + sizeof(ObSysVarInnodbFtResultCacheLimit)
        + sizeof(ObSysVarInnodbFtServerStopwordTable)
        + sizeof(ObSysVarInnodbOptimizeFulltextOnly)
        + sizeof(ObSysVarMaxTmpTables)
        + sizeof(ObSysVarInnodbTmpdir)
        + sizeof(ObSysVarGroupReplicationGroupSeeds)
        + sizeof(ObSysVarSlaveRowsSearchAlgorithms)
        + sizeof(ObSysVarSlaveTypeConversions)
        + sizeof(ObSysVarObHnswEfSearch)
        + sizeof(ObSysVarNdbAllowCopyingAlterTable)
        + sizeof(ObSysVarNdbAutoincrementPrefetchSz)
        + sizeof(ObSysVarNdbBlobReadBatchBytes)
        + sizeof(ObSysVarNdbBlobWriteBatchBytes)
        + sizeof(ObSysVarNdbCacheCheckTime)
        + sizeof(ObSysVarNdbClearApplyStatus)
        + sizeof(ObSysVarNdbDataNodeNeighbour)
        + sizeof(ObSysVarNdbDefaultColumnFormat)
        + sizeof(ObSysVarNdbDeferredConstraints)
        + sizeof(ObSysVarNdbDistribution)
        + sizeof(ObSysVarNdbEventbufferFreePercent)
        + sizeof(ObSysVarNdbEventbufferMaxAlloc)
        + sizeof(ObSysVarNdbExtraLogging)
        + sizeof(ObSysVarNdbForceSend)
        + sizeof(ObSysVarNdbFullyReplicated)
        + sizeof(ObSysVarNdbIndexStatEnable)
        + sizeof(ObSysVarNdbIndexStatOption)
        + sizeof(ObSysVarNdbJoinPushdown)
        + sizeof(ObSysVarNdbLogBinlogIndex)
        + sizeof(ObSysVarNdbLogEmptyEpochs)
        + sizeof(ObSysVarNdbLogEmptyUpdate)
        + sizeof(ObSysVarNdbLogExclusiveReads)
        + sizeof(ObSysVarNdbLogUpdateAsWrite)
        + sizeof(ObSysVarNdbLogUpdateMinimal)
        + sizeof(ObSysVarNdbLogUpdatedOnly)
        + sizeof(ObSysVarNdbOptimizationDelay)
        + sizeof(ObSysVarNdbReadBackup)
        + sizeof(ObSysVarNdbRecvThreadActivationThreshold)
        + sizeof(ObSysVarNdbRecvThreadCpuMask)
        + sizeof(ObSysVarNdbReportThreshBinlogEpochSlip)
        + sizeof(ObSysVarNdbReportThreshBinlogMemUsage)
        + sizeof(ObSysVarNdbRowChecksum)
        + sizeof(ObSysVarNdbShowForeignKeyMockTables)
        + sizeof(ObSysVarNdbSlaveConflictRole)
        + sizeof(ObSysVarNdbTableNoLogging)
        + sizeof(ObSysVarNdbTableTemporary)
        + sizeof(ObSysVarNdbUseExactCount)
        + sizeof(ObSysVarNdbUseTransactions)
        + sizeof(ObSysVarNdbinfoMaxBytes)
        + sizeof(ObSysVarNdbinfoMaxRows)
        + sizeof(ObSysVarNdbinfoOffline)
        + sizeof(ObSysVarNdbinfoShowHidden)
        + sizeof(ObSysVarMyisamDataPointerSize)
        + sizeof(ObSysVarMyisamMaxSortFileSize)
        + sizeof(ObSysVarMyisamRepairThreads)
        + sizeof(ObSysVarMyisamSortBufferSize)
        + sizeof(ObSysVarMyisamStatsMethod)
        + sizeof(ObSysVarMyisamUseMmap)
        + sizeof(ObSysVarPreloadBufferSize)
        + sizeof(ObSysVarReadBufferSize)
        + sizeof(ObSysVarReadRndBufferSize)
        + sizeof(ObSysVarSyncFrm)
        + sizeof(ObSysVarSyncMasterInfo)
        + sizeof(ObSysVarTableOpenCache)
        + sizeof(ObSysVarInnodbMonitorDisable)
        + sizeof(ObSysVarInnodbMonitorEnable)
        + sizeof(ObSysVarInnodbMonitorReset)
        + sizeof(ObSysVarInnodbMonitorResetAll)
        + sizeof(ObSysVarInnodbOldBlocksPct)
        + sizeof(ObSysVarInnodbOldBlocksTime)
        + sizeof(ObSysVarInnodbPurgeBatchSize)
        + sizeof(ObSysVarInnodbPurgeRsegTruncateFrequency)
        + sizeof(ObSysVarInnodbRandomReadAhead)
        + sizeof(ObSysVarInnodbReadAheadThreshold)
        + sizeof(ObSysVarInnodbRollbackSegments)
        + sizeof(ObSysVarInnodbSpinWaitDelay)
        + sizeof(ObSysVarInnodbStatusOutput)
        + sizeof(ObSysVarInnodbStatusOutputLocks)
        + sizeof(ObSysVarInnodbSyncSpinLoops)
        + sizeof(ObSysVarInternalTmpDiskStorageEngine)
        + sizeof(ObSysVarKeepFilesOnCreate)
        + sizeof(ObSysVarMaxHeapTableSize)
        + sizeof(ObSysVarBulkInsertBufferSize)
        + sizeof(ObSysVarHostCacheSize)
        + sizeof(ObSysVarInitSlave)
        + sizeof(ObSysVarInnodbFastShutdown)
        + sizeof(ObSysVarInnodbIoCapacity)
        + sizeof(ObSysVarInnodbIoCapacityMax)
        + sizeof(ObSysVarInnodbThreadConcurrency)
        + sizeof(ObSysVarInnodbThreadSleepDelay)
        + sizeof(ObSysVarLogErrorVerbosity)
        + sizeof(ObSysVarLogOutput)
        + sizeof(ObSysVarLogQueriesNotUsingIndexes)
        + sizeof(ObSysVarLogSlowAdminStatements)
        + sizeof(ObSysVarLogSlowSlaveStatements)
        + sizeof(ObSysVarLogStatementsUnsafeForBinlog)
        + sizeof(ObSysVarLogSyslog)
        + sizeof(ObSysVarLogSyslogFacility)
        + sizeof(ObSysVarLogSyslogIncludePid)
        + sizeof(ObSysVarLogSyslogTag)
        + sizeof(ObSysVarLogThrottleQueriesNotUsingIndexes)
        + sizeof(ObSysVarLogTimestamps)
        + sizeof(ObSysVarLogWarnings)
        + sizeof(ObSysVarMaxDelayedThreads)
        + sizeof(ObSysVarOfflineMode)
        + sizeof(ObSysVarRequireSecureTransport)
        + sizeof(ObSysVarSlowLaunchTime)
        + sizeof(ObSysVarSqlLogOff)
        + sizeof(ObSysVarThreadCacheSize)
        + sizeof(ObSysVarThreadPoolHighPriorityConnection)
        + sizeof(ObSysVarThreadPoolMaxUnusedThreads)
        + sizeof(ObSysVarThreadPoolPrioKickupTimer)
        + sizeof(ObSysVarThreadPoolStallLimit)
        + sizeof(ObSysVarHaveStatementTimeout)
        + sizeof(ObSysVarMysqlxBindAddress)
        + sizeof(ObSysVarMysqlxPort)
        + sizeof(ObSysVarMysqlxPortOpenTimeout)
        + sizeof(ObSysVarMysqlxSocket)
        + sizeof(ObSysVarMysqlxSslCa)
        + sizeof(ObSysVarMysqlxSslCapath)
        + sizeof(ObSysVarMysqlxSslCert)
        + sizeof(ObSysVarMysqlxSslCipher)
        + sizeof(ObSysVarMysqlxSslCrl)
        + sizeof(ObSysVarMysqlxSslCrlpath)
        + sizeof(ObSysVarMysqlxSslKey)
        + sizeof(ObSysVarOld)
        + sizeof(ObSysVarPerformanceSchemaAccountsSize)
        + sizeof(ObSysVarPerformanceSchemaDigestsSize)
        + sizeof(ObSysVarPerformanceSchemaEventsStagesHistoryLongSize)
        + sizeof(ObSysVarPerformanceSchemaEventsStagesHistorySize)
        + sizeof(ObSysVarPerformanceSchemaEventsStatementsHistoryLongSize)
        + sizeof(ObSysVarPerformanceSchemaEventsStatementsHistorySize)
        + sizeof(ObSysVarPerformanceSchemaEventsTransactionsHistoryLongSize)
        + sizeof(ObSysVarPerformanceSchemaEventsTransactionsHistorySize)
        + sizeof(ObSysVarPerformanceSchemaEventsWaitsHistoryLongSize)
        + sizeof(ObSysVarPerformanceSchemaEventsWaitsHistorySize)
        + sizeof(ObSysVarPerformanceSchemaHostsSize)
        + sizeof(ObSysVarPerformanceSchemaMaxCondClasses)
        + sizeof(ObSysVarPerformanceSchemaMaxCondInstances)
        + sizeof(ObSysVarPerformanceSchemaMaxDigestLength)
        + sizeof(ObSysVarPerformanceSchemaMaxFileClasses)
        + sizeof(ObSysVarPerformanceSchemaMaxFileHandles)
        + sizeof(ObSysVarPerformanceSchemaMaxFileInstances)
        + sizeof(ObSysVarPerformanceSchemaMaxIndexStat)
        + sizeof(ObSysVarPerformanceSchemaMaxMemoryClasses)
        + sizeof(ObSysVarPerformanceSchemaMaxMetadataLocks)
        + sizeof(ObSysVarPerformanceSchemaMaxMutexClasses)
        + sizeof(ObSysVarPerformanceSchemaMaxMutexInstances)
        + sizeof(ObSysVarPerformanceSchemaMaxPreparedStatementsInstances)
        + sizeof(ObSysVarPerformanceSchemaMaxProgramInstances)
        + sizeof(ObSysVarPerformanceSchemaMaxRwlockClasses)
        + sizeof(ObSysVarPerformanceSchemaMaxRwlockInstances)
        + sizeof(ObSysVarPerformanceSchemaMaxSocketClasses)
        + sizeof(ObSysVarPerformanceSchemaMaxSocketInstances)
        + sizeof(ObSysVarPerformanceSchemaMaxSqlTextLength)
        + sizeof(ObSysVarPerformanceSchemaMaxStageClasses)
        + sizeof(ObSysVarPerformanceSchemaMaxStatementClasses)
        + sizeof(ObSysVarPerformanceSchemaMaxStatementStack)
        + sizeof(ObSysVarPerformanceSchemaMaxTableHandles)
        + sizeof(ObSysVarPerformanceSchemaMaxTableInstances)
        + sizeof(ObSysVarPerformanceSchemaMaxTableLockStat)
        + sizeof(ObSysVarPerformanceSchemaMaxThreadClasses)
        + sizeof(ObSysVarPerformanceSchemaMaxThreadInstances)
        + sizeof(ObSysVarPerformanceSchemaSessionConnectAttrsSize)
        + sizeof(ObSysVarPerformanceSchemaSetupActorsSize)
        + sizeof(ObSysVarPerformanceSchemaSetupObjectsSize)
        + sizeof(ObSysVarPerformanceSchemaUsersSize)
        + sizeof(ObSysVarVersionTokensSessionNumber)
        + sizeof(ObSysVarBackLog)
        + sizeof(ObSysVarBasedir)
        + sizeof(ObSysVarBindAddress)
        + sizeof(ObSysVarCoreFile)
        + sizeof(ObSysVarHaveCompress)
        + sizeof(ObSysVarIgnoreDbDirs)
        + sizeof(ObSysVarInitFile)
        + sizeof(ObSysVarInnodbOpenFiles)
        + sizeof(ObSysVarInnodbPageCleaners)
        + sizeof(ObSysVarInnodbPurgeThreads)
        + sizeof(ObSysVarInnodbReadIoThreads)
        + sizeof(ObSysVarInnodbSyncArraySize)
        + sizeof(ObSysVarInnodbUseNativeAio)
        + sizeof(ObSysVarInnodbWriteIoThreads)
        + sizeof(ObSysVarLargeFilesSupport)
        + sizeof(ObSysVarLockedInMemory)
        + sizeof(ObSysVarLogError)
        + sizeof(ObSysVarNamedPipe)
        + sizeof(ObSysVarNamedPipeFullAccessGroup)
        + sizeof(ObSysVarOpenFilesLimit)
        + sizeof(ObSysVarReportHost)
        + sizeof(ObSysVarReportPassword)
        + sizeof(ObSysVarReportPort)
        + sizeof(ObSysVarReportUser)
        + sizeof(ObSysVarServerIdBits)
        + sizeof(ObSysVarSharedMemory)
        + sizeof(ObSysVarSharedMemoryBaseName)
        + sizeof(ObSysVarSkipNameResolve)
        + sizeof(ObSysVarSkipNetworking)
        + sizeof(ObSysVarThreadHandling)
        + sizeof(ObSysVarThreadPoolAlgorithm)
        + sizeof(ObSysVarThreadPoolSize)
        + sizeof(ObSysVarThreadStack)
        + sizeof(ObSysVarBinlogGtidSimpleRecovery)
        + sizeof(ObSysVarInnodbApiEnableBinlog)
        + sizeof(ObSysVarInnodbLocksUnsafeForBinlog)
        + sizeof(ObSysVarInnodbLogBufferSize)
        + sizeof(ObSysVarInnodbLogFilesInGroup)
        + sizeof(ObSysVarInnodbLogFileSize)
        + sizeof(ObSysVarInnodbLogGroupHomeDir)
        + sizeof(ObSysVarInnodbUndoDirectory)
        + sizeof(ObSysVarInnodbUndoTablespaces)
        + sizeof(ObSysVarLogBinBasename)
        + sizeof(ObSysVarLogBinIndex)
        + sizeof(ObSysVarLogSlaveUpdates)
        + sizeof(ObSysVarRelayLog)
        + sizeof(ObSysVarRelayLogBasename)
        + sizeof(ObSysVarRelayLogIndex)
        + sizeof(ObSysVarRelayLogInfoFile)
        + sizeof(ObSysVarRelayLogRecovery)
        + sizeof(ObSysVarRelayLogSpaceLimit)
        + sizeof(ObSysVarDelayKeyWrite)
        + sizeof(ObSysVarInnodbLargePrefix)
        + sizeof(ObSysVarKeyBufferSize)
        + sizeof(ObSysVarKeyCacheAgeThreshold)
        + sizeof(ObSysVarKeyCacheDivisionLimit)
        + sizeof(ObSysVarMaxSeeksForKey)
        + sizeof(ObSysVarOldAlterTable)
        + sizeof(ObSysVarTableDefinitionCache)
        + sizeof(ObSysVarInnodbSortBufferSize)
        + sizeof(ObSysVarKeyCacheBlockSize)
        + sizeof(ObSysVarCharacterSetsDir)
        + sizeof(ObSysVarDateFormat)
        + sizeof(ObSysVarDatetimeFormat)
        + sizeof(ObSysVarDisconnectOnExpiredPassword)
        + sizeof(ObSysVarExternalUser)
        + sizeof(ObSysVarHaveCrypt)
        + sizeof(ObSysVarLanguage)
        + sizeof(ObSysVarLcMessagesDir)
        + sizeof(ObSysVarLowerCaseFileSystem)
        + sizeof(ObSysVarMaxDigestLength)
        + sizeof(ObSysVarNdbinfoDatabase)
        + sizeof(ObSysVarNdbinfoTablePrefix)
        + sizeof(ObSysVarNdbinfoVersion)
        + sizeof(ObSysVarNdbBatchSize)
        + sizeof(ObSysVarNdbClusterConnectionPool)
        + sizeof(ObSysVarNdbClusterConnectionPoolNodeids)
        + sizeof(ObSysVarNdbLogApplyStatus)
        + sizeof(ObSysVarNdbLogBin)
        + sizeof(ObSysVarNdbLogFailTerminate)
        + sizeof(ObSysVarNdbLogOrig)
        + sizeof(ObSysVarNdbLogTransactionId)
        + sizeof(ObSysVarNdbOptimizedNodeSelection)
        + sizeof(ObSysVarNdbSystemName)
        + sizeof(ObSysVarNdbUseCopyingAlterTable)
        + sizeof(ObSysVarNdbVersionString)
        + sizeof(ObSysVarNdbWaitConnected)
        + sizeof(ObSysVarNdbWaitSetup)
        + sizeof(ObSysVarProxyUser)
        + sizeof(ObSysVarSha256PasswordAutoGenerateRsaKeys)
        + sizeof(ObSysVarSha256PasswordPrivateKeyPath)
        + sizeof(ObSysVarSha256PasswordPublicKeyPath)
        + sizeof(ObSysVarSkipShowDatabase)
        + sizeof(ObSysVarBigTables)
        + sizeof(ObSysVarCheckProxyUsers)
        + sizeof(ObSysVarDefaultWeekFormat)
        + sizeof(ObSysVarDelayedInsertTimeout)
        + sizeof(ObSysVarDelayedQueueSize)
        + sizeof(ObSysVarEqRangeIndexDiveLimit)
        + sizeof(ObSysVarInnodbStatsAutoRecalc)
        + sizeof(ObSysVarInnodbStatsIncludeDeleteMarked)
        + sizeof(ObSysVarInnodbStatsMethod)
        + sizeof(ObSysVarInnodbStatsOnMetadata)
        + sizeof(ObSysVarVersionTokensSession)
        + sizeof(ObSysVarInnodbStatsPersistentSamplePages)
        + sizeof(ObSysVarInnodbStatsSamplePages)
        + sizeof(ObSysVarInnodbStatsTransientSamplePages)
        + sizeof(ObSysVarOptimizerSwitch)
        + sizeof(ObSysVarMaxConnectErrors)
        + sizeof(ObSysVarMysqlFirewallMode)
        + sizeof(ObSysVarMysqlFirewallTrace)
        + sizeof(ObSysVarMysqlNativePasswordProxyUsers)
        + sizeof(ObSysVarNetRetryCount)
        + sizeof(ObSysVarNew)
        + sizeof(ObSysVarOldPasswords)
        + sizeof(ObSysVarOptimizerPruneLevel)
        + sizeof(ObSysVarOptimizerSearchDepth)
        + sizeof(ObSysVarOptimizerTrace)
        + sizeof(ObSysVarOptimizerTraceFeatures)
        + sizeof(ObSysVarOptimizerTraceLimit)
        + sizeof(ObSysVarOptimizerTraceMaxMemSize)
        + sizeof(ObSysVarOptimizerTraceOffset)
        + sizeof(ObSysVarParserMaxMemSize)
        + sizeof(ObSysVarRandSeed1)
        + sizeof(ObSysVarRandSeed2)
        + sizeof(ObSysVarRangeAllocBlockSize)
        + sizeof(ObSysVarRangeOptimizerMaxMemSize)
        + sizeof(ObSysVarRewriterEnabled)
        + sizeof(ObSysVarRewriterVerbose)
        + sizeof(ObSysVarSecureAuth)
        + sizeof(ObSysVarSha256PasswordProxyUsers)
        + sizeof(ObSysVarShowCompatibility56)
        + sizeof(ObSysVarShowCreateTableVerbosity)
        + sizeof(ObSysVarShowOldTemporals)
        + sizeof(ObSysVarSqlBigSelects)
        + sizeof(ObSysVarUpdatableViewsWithLimit)
        + sizeof(ObSysVarValidatePasswordDictionaryFile)
        + sizeof(ObSysVarDelayedInsertLimit)
        + sizeof(ObSysVarNdbVersion)
        + sizeof(ObSysVarAutoGenerateCerts)
        + sizeof(ObSysVarOptimizerCostBasedTransformation)
        + sizeof(ObSysVarRangeIndexDiveLimit)
        + sizeof(ObSysVarPartitionIndexDiveLimit)
        + sizeof(ObSysVarPidFile)
        + sizeof(ObSysVarPort)
        + sizeof(ObSysVarSocket)
        + sizeof(ObSysVarEnableOptimizerRowgoal)
        + sizeof(ObSysVarObIvfNprobes)
        + sizeof(ObSysVarObHnswExtraInfoMaxSize)
        + sizeof(ObSysVarPushJoinPredicate)
        + sizeof(ObSysVarObSparseDropRatioSearch)
        ;
    void *ptr = NULL;
    if (OB_ISNULL(ptr = allocator_.alloc(total_mem_size))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_ERROR("fail to alloc memory", K(ret));
    } else {
      all_sys_vars_created_ = true;
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarAutoIncrementIncrement())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarAutoIncrementIncrement", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_AUTO_INCREMENT_INCREMENT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarAutoIncrementIncrement));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarAutoIncrementOffset())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarAutoIncrementOffset", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_AUTO_INCREMENT_OFFSET))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarAutoIncrementOffset));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarAutocommit())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarAutocommit", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_AUTOCOMMIT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarAutocommit));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarCharacterSetClient())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarCharacterSetClient", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_CHARACTER_SET_CLIENT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarCharacterSetClient));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarCharacterSetConnection())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarCharacterSetConnection", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_CHARACTER_SET_CONNECTION))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarCharacterSetConnection));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarCharacterSetDatabase())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarCharacterSetDatabase", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_CHARACTER_SET_DATABASE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarCharacterSetDatabase));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarCharacterSetResults())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarCharacterSetResults", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_CHARACTER_SET_RESULTS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarCharacterSetResults));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarCharacterSetServer())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarCharacterSetServer", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_CHARACTER_SET_SERVER))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarCharacterSetServer));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarCharacterSetSystem())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarCharacterSetSystem", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_CHARACTER_SET_SYSTEM))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarCharacterSetSystem));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarCollationConnection())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarCollationConnection", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_COLLATION_CONNECTION))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarCollationConnection));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarCollationDatabase())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarCollationDatabase", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_COLLATION_DATABASE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarCollationDatabase));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarCollationServer())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarCollationServer", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_COLLATION_SERVER))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarCollationServer));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInteractiveTimeout())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInteractiveTimeout", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INTERACTIVE_TIMEOUT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInteractiveTimeout));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarLastInsertId())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarLastInsertId", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_LAST_INSERT_ID))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarLastInsertId));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarMaxAllowedPacket())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarMaxAllowedPacket", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_MAX_ALLOWED_PACKET))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarMaxAllowedPacket));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarSqlMode())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarSqlMode", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SQL_MODE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarSqlMode));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarTimeZone())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarTimeZone", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_TIME_ZONE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarTimeZone));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarTxIsolation())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarTxIsolation", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_TX_ISOLATION))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarTxIsolation));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarVersionComment())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarVersionComment", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_VERSION_COMMENT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarVersionComment));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarWaitTimeout())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarWaitTimeout", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_WAIT_TIMEOUT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarWaitTimeout));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarBinlogRowImage())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarBinlogRowImage", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_BINLOG_ROW_IMAGE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarBinlogRowImage));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarCharacterSetFilesystem())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarCharacterSetFilesystem", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_CHARACTER_SET_FILESYSTEM))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarCharacterSetFilesystem));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarConnectTimeout())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarConnectTimeout", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_CONNECT_TIMEOUT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarConnectTimeout));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarDatadir())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarDatadir", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_DATADIR))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarDatadir));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarDebugSync())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarDebugSync", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_DEBUG_SYNC))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarDebugSync));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarDivPrecisionIncrement())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarDivPrecisionIncrement", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_DIV_PRECISION_INCREMENT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarDivPrecisionIncrement));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarExplicitDefaultsForTimestamp())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarExplicitDefaultsForTimestamp", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_EXPLICIT_DEFAULTS_FOR_TIMESTAMP))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarExplicitDefaultsForTimestamp));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarGroupConcatMaxLen())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarGroupConcatMaxLen", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_GROUP_CONCAT_MAX_LEN))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarGroupConcatMaxLen));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarIdentity())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarIdentity", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_IDENTITY))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarIdentity));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarLowerCaseTableNames())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarLowerCaseTableNames", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_LOWER_CASE_TABLE_NAMES))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarLowerCaseTableNames));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNetReadTimeout())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNetReadTimeout", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_NET_READ_TIMEOUT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNetReadTimeout));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNetWriteTimeout())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNetWriteTimeout", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_NET_WRITE_TIMEOUT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNetWriteTimeout));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarReadOnly())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarReadOnly", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_READ_ONLY))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarReadOnly));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarSqlAutoIsNull())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarSqlAutoIsNull", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SQL_AUTO_IS_NULL))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarSqlAutoIsNull));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarSqlSelectLimit())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarSqlSelectLimit", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SQL_SELECT_LIMIT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarSqlSelectLimit));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarTimestamp())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarTimestamp", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_TIMESTAMP))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarTimestamp));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarTxReadOnly())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarTxReadOnly", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_TX_READ_ONLY))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarTxReadOnly));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarVersion())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarVersion", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_VERSION))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarVersion));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarSqlWarnings())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarSqlWarnings", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SQL_WARNINGS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarSqlWarnings));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarMaxUserConnections())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarMaxUserConnections", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_MAX_USER_CONNECTIONS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarMaxUserConnections));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInitConnect())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInitConnect", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INIT_CONNECT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInitConnect));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarLicense())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarLicense", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_LICENSE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarLicense));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNetBufferLength())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNetBufferLength", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_NET_BUFFER_LENGTH))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNetBufferLength));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarSystemTimeZone())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarSystemTimeZone", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SYSTEM_TIME_ZONE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarSystemTimeZone));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarQueryCacheSize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarQueryCacheSize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_QUERY_CACHE_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarQueryCacheSize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarQueryCacheType())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarQueryCacheType", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_QUERY_CACHE_TYPE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarQueryCacheType));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarSqlQuoteShowCreate())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarSqlQuoteShowCreate", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SQL_QUOTE_SHOW_CREATE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarSqlQuoteShowCreate));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarMaxSpRecursionDepth())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarMaxSpRecursionDepth", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_MAX_SP_RECURSION_DEPTH))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarMaxSpRecursionDepth));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarSqlSafeUpdates())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarSqlSafeUpdates", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SQL_SAFE_UPDATES))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarSqlSafeUpdates));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarConcurrentInsert())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarConcurrentInsert", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_CONCURRENT_INSERT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarConcurrentInsert));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarDefaultAuthenticationPlugin())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarDefaultAuthenticationPlugin", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_DEFAULT_AUTHENTICATION_PLUGIN))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarDefaultAuthenticationPlugin));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarDisabledStorageEngines())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarDisabledStorageEngines", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_DISABLED_STORAGE_ENGINES))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarDisabledStorageEngines));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarErrorCount())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarErrorCount", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_ERROR_COUNT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarErrorCount));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarGeneralLog())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarGeneralLog", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_GENERAL_LOG))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarGeneralLog));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarHaveOpenssl())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarHaveOpenssl", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_HAVE_OPENSSL))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarHaveOpenssl));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarHaveProfiling())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarHaveProfiling", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_HAVE_PROFILING))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarHaveProfiling));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarHaveSsl())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarHaveSsl", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_HAVE_SSL))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarHaveSsl));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarHostname())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarHostname", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_HOSTNAME))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarHostname));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarLcMessages())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarLcMessages", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_LC_MESSAGES))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarLcMessages));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarLocalInfile())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarLocalInfile", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_LOCAL_INFILE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarLocalInfile));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarLockWaitTimeout())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarLockWaitTimeout", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_LOCK_WAIT_TIMEOUT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarLockWaitTimeout));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarLongQueryTime())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarLongQueryTime", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_LONG_QUERY_TIME))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarLongQueryTime));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarMaxConnections())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarMaxConnections", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_MAX_CONNECTIONS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarMaxConnections));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarMaxExecutionTime())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarMaxExecutionTime", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_MAX_EXECUTION_TIME))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarMaxExecutionTime));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarProtocolVersion())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarProtocolVersion", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_PROTOCOL_VERSION))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarProtocolVersion));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarServerId())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarServerId", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SERVER_ID))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarServerId));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarSslCa())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarSslCa", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SSL_CA))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarSslCa));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarSslCapath())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarSslCapath", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SSL_CAPATH))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarSslCapath));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarSslCert())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarSslCert", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SSL_CERT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarSslCert));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarSslCipher())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarSslCipher", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SSL_CIPHER))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarSslCipher));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarSslCrl())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarSslCrl", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SSL_CRL))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarSslCrl));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarSslCrlpath())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarSslCrlpath", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SSL_CRLPATH))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarSslCrlpath));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarSslKey())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarSslKey", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SSL_KEY))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarSslKey));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarTimeFormat())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarTimeFormat", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_TIME_FORMAT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarTimeFormat));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarTlsVersion())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarTlsVersion", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_TLS_VERSION))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarTlsVersion));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarTmpTableSize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarTmpTableSize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_TMP_TABLE_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarTmpTableSize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarTmpdir())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarTmpdir", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_TMPDIR))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarTmpdir));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarUniqueChecks())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarUniqueChecks", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_UNIQUE_CHECKS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarUniqueChecks));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarVersionCompileMachine())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarVersionCompileMachine", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_VERSION_COMPILE_MACHINE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarVersionCompileMachine));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarVersionCompileOs())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarVersionCompileOs", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_VERSION_COMPILE_OS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarVersionCompileOs));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarWarningCount())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarWarningCount", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_WARNING_COUNT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarWarningCount));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarSessionTrackSchema())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarSessionTrackSchema", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SESSION_TRACK_SCHEMA))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarSessionTrackSchema));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarSessionTrackSystemVariables())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarSessionTrackSystemVariables", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SESSION_TRACK_SYSTEM_VARIABLES))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarSessionTrackSystemVariables));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarSessionTrackStateChange())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarSessionTrackStateChange", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SESSION_TRACK_STATE_CHANGE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarSessionTrackStateChange));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarHaveQueryCache())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarHaveQueryCache", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_HAVE_QUERY_CACHE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarHaveQueryCache));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarQueryCacheLimit())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarQueryCacheLimit", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_QUERY_CACHE_LIMIT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarQueryCacheLimit));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarQueryCacheMinResUnit())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarQueryCacheMinResUnit", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_QUERY_CACHE_MIN_RES_UNIT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarQueryCacheMinResUnit));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarQueryCacheWlockInvalidate())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarQueryCacheWlockInvalidate", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_QUERY_CACHE_WLOCK_INVALIDATE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarQueryCacheWlockInvalidate));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarBinlogFormat())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarBinlogFormat", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_BINLOG_FORMAT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarBinlogFormat));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarBinlogChecksum())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarBinlogChecksum", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_BINLOG_CHECKSUM))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarBinlogChecksum));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarBinlogRowsQueryLogEvents())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarBinlogRowsQueryLogEvents", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_BINLOG_ROWS_QUERY_LOG_EVENTS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarBinlogRowsQueryLogEvents));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarLogBin())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarLogBin", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_LOG_BIN))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarLogBin));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarServerUuid())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarServerUuid", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SERVER_UUID))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarServerUuid));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarDefaultStorageEngine())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarDefaultStorageEngine", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_DEFAULT_STORAGE_ENGINE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarDefaultStorageEngine));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarCteMaxRecursionDepth())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarCteMaxRecursionDepth", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_CTE_MAX_RECURSION_DEPTH))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarCteMaxRecursionDepth));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarRegexpStackLimit())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarRegexpStackLimit", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_REGEXP_STACK_LIMIT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarRegexpStackLimit));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarRegexpTimeLimit())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarRegexpTimeLimit", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_REGEXP_TIME_LIMIT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarRegexpTimeLimit));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarProfiling())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarProfiling", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_PROFILING))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarProfiling));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarProfilingHistorySize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarProfilingHistorySize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_PROFILING_HISTORY_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarProfilingHistorySize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarObLogLevel())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarObLogLevel", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_OB_LOG_LEVEL))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarObLogLevel));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarObQueryTimeout())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarObQueryTimeout", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_OB_QUERY_TIMEOUT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarObQueryTimeout));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarObReadConsistency())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarObReadConsistency", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_OB_READ_CONSISTENCY))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarObReadConsistency));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarObEnableTransformation())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarObEnableTransformation", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_OB_ENABLE_TRANSFORMATION))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarObEnableTransformation));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarObTrxTimeout())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarObTrxTimeout", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_OB_TRX_TIMEOUT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarObTrxTimeout));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarObEnablePlanCache())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarObEnablePlanCache", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_OB_ENABLE_PLAN_CACHE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarObEnablePlanCache));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarObEnableIndexDirectSelect())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarObEnableIndexDirectSelect", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_OB_ENABLE_INDEX_DIRECT_SELECT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarObEnableIndexDirectSelect));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarObEnableAggregationPushdown())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarObEnableAggregationPushdown", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_OB_ENABLE_AGGREGATION_PUSHDOWN))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarObEnableAggregationPushdown));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarObGlobalDebugSync())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarObGlobalDebugSync", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_OB_GLOBAL_DEBUG_SYNC))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarObGlobalDebugSync));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarObEnableShowTrace())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarObEnableShowTrace", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_OB_ENABLE_SHOW_TRACE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarObEnableShowTrace));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarObPlanCachePercentage())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarObPlanCachePercentage", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_OB_PLAN_CACHE_PERCENTAGE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarObPlanCachePercentage));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarObPlanCacheEvictHighPercentage())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarObPlanCacheEvictHighPercentage", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_OB_PLAN_CACHE_EVICT_HIGH_PERCENTAGE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarObPlanCacheEvictHighPercentage));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarObPlanCacheEvictLowPercentage())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarObPlanCacheEvictLowPercentage", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_OB_PLAN_CACHE_EVICT_LOW_PERCENTAGE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarObPlanCacheEvictLowPercentage));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarRecyclebin())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarRecyclebin", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_RECYCLEBIN))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarRecyclebin));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarIsResultAccurate())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarIsResultAccurate", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_IS_RESULT_ACCURATE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarIsResultAccurate));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarErrorOnOverlapTime())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarErrorOnOverlapTime", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_ERROR_ON_OVERLAP_TIME))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarErrorOnOverlapTime));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarObSqlWorkAreaPercentage())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarObSqlWorkAreaPercentage", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_OB_SQL_WORK_AREA_PERCENTAGE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarObSqlWorkAreaPercentage));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarForeignKeyChecks())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarForeignKeyChecks", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_FOREIGN_KEY_CHECKS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarForeignKeyChecks));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarObTcpInvitedNodes())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarObTcpInvitedNodes", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_OB_TCP_INVITED_NODES))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarObTcpInvitedNodes));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarAutoIncrementCacheSize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarAutoIncrementCacheSize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_AUTO_INCREMENT_CACHE_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarAutoIncrementCacheSize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarParallelServersTarget())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarParallelServersTarget", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_PARALLEL_SERVERS_TARGET))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarParallelServersTarget));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarObTrxIdleTimeout())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarObTrxIdleTimeout", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_OB_TRX_IDLE_TIMEOUT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarObTrxIdleTimeout));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarBlockEncryptionMode())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarBlockEncryptionMode", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_BLOCK_ENCRYPTION_MODE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarBlockEncryptionMode));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNljBatchingEnabled())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNljBatchingEnabled", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR__NLJ_BATCHING_ENABLED))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNljBatchingEnabled));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarTransactionIsolation())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarTransactionIsolation", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_TRANSACTION_ISOLATION))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarTransactionIsolation));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarObTrxLockTimeout())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarObTrxLockTimeout", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_OB_TRX_LOCK_TIMEOUT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarObTrxLockTimeout));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarValidatePasswordCheckUserName())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarValidatePasswordCheckUserName", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_VALIDATE_PASSWORD_CHECK_USER_NAME))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarValidatePasswordCheckUserName));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarValidatePasswordLength())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarValidatePasswordLength", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_VALIDATE_PASSWORD_LENGTH))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarValidatePasswordLength));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarValidatePasswordMixedCaseCount())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarValidatePasswordMixedCaseCount", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_VALIDATE_PASSWORD_MIXED_CASE_COUNT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarValidatePasswordMixedCaseCount));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarValidatePasswordNumberCount())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarValidatePasswordNumberCount", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_VALIDATE_PASSWORD_NUMBER_COUNT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarValidatePasswordNumberCount));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarValidatePasswordPolicy())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarValidatePasswordPolicy", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_VALIDATE_PASSWORD_POLICY))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarValidatePasswordPolicy));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarValidatePasswordSpecialCharCount())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarValidatePasswordSpecialCharCount", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_VALIDATE_PASSWORD_SPECIAL_CHAR_COUNT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarValidatePasswordSpecialCharCount));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarDefaultPasswordLifetime())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarDefaultPasswordLifetime", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_DEFAULT_PASSWORD_LIFETIME))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarDefaultPasswordLifetime));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarEnableParallelDml())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarEnableParallelDml", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR__ENABLE_PARALLEL_DML))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarEnableParallelDml));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarSecureFilePriv())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarSecureFilePriv", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SECURE_FILE_PRIV))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarSecureFilePriv));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarEnableParallelQuery())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarEnableParallelQuery", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR__ENABLE_PARALLEL_QUERY))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarEnableParallelQuery));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarForceParallelQueryDop())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarForceParallelQueryDop", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR__FORCE_PARALLEL_QUERY_DOP))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarForceParallelQueryDop));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarForceParallelDmlDop())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarForceParallelDmlDop", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR__FORCE_PARALLEL_DML_DOP))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarForceParallelDmlDop));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarObPlBlockTimeout())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarObPlBlockTimeout", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_OB_PL_BLOCK_TIMEOUT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarObPlBlockTimeout));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarTransactionReadOnly())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarTransactionReadOnly", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_TRANSACTION_READ_ONLY))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarTransactionReadOnly));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarPerformanceSchema())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarPerformanceSchema", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_PERFORMANCE_SCHEMA))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarPerformanceSchema));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarEnableParallelDdl())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarEnableParallelDdl", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR__ENABLE_PARALLEL_DDL))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarEnableParallelDdl));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarForceParallelDdlDop())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarForceParallelDdlDop", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR__FORCE_PARALLEL_DDL_DOP))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarForceParallelDdlDop));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarCursorSharing())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarCursorSharing", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_CURSOR_SHARING))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarCursorSharing));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarAggregationOptimizationSettings())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarAggregationOptimizationSettings", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR__AGGREGATION_OPTIMIZATION_SETTINGS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarAggregationOptimizationSettings));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarPxSharedHashJoin())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarPxSharedHashJoin", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR__PX_SHARED_HASH_JOIN))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarPxSharedHashJoin));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarSqlNotes())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarSqlNotes", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SQL_NOTES))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarSqlNotes));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbStrictMode())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbStrictMode", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_STRICT_MODE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbStrictMode));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarWindowfuncOptimizationSettings())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarWindowfuncOptimizationSettings", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR__WINDOWFUNC_OPTIMIZATION_SETTINGS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarWindowfuncOptimizationSettings));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarLogRowValueOptions())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarLogRowValueOptions", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_LOG_ROW_VALUE_OPTIONS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarLogRowValueOptions));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarObMaxReadStaleTime())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarObMaxReadStaleTime", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_OB_MAX_READ_STALE_TIME))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarObMaxReadStaleTime));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarOptimizerGatherStatsOnLoad())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarOptimizerGatherStatsOnLoad", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR__OPTIMIZER_GATHER_STATS_ON_LOAD))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarOptimizerGatherStatsOnLoad));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarShowDdlInCompatMode())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarShowDdlInCompatMode", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR__SHOW_DDL_IN_COMPAT_MODE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarShowDdlInCompatMode));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarParallelDegreePolicy())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarParallelDegreePolicy", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_PARALLEL_DEGREE_POLICY))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarParallelDegreePolicy));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarParallelDegreeLimit())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarParallelDegreeLimit", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_PARALLEL_DEGREE_LIMIT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarParallelDegreeLimit));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarParallelMinScanTimeThreshold())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarParallelMinScanTimeThreshold", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_PARALLEL_MIN_SCAN_TIME_THRESHOLD))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarParallelMinScanTimeThreshold));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarOptimizerDynamicSampling())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarOptimizerDynamicSampling", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_OPTIMIZER_DYNAMIC_SAMPLING))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarOptimizerDynamicSampling));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarRuntimeFilterType())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarRuntimeFilterType", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_RUNTIME_FILTER_TYPE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarRuntimeFilterType));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarRuntimeFilterWaitTimeMs())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarRuntimeFilterWaitTimeMs", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_RUNTIME_FILTER_WAIT_TIME_MS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarRuntimeFilterWaitTimeMs));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarRuntimeFilterMaxInNum())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarRuntimeFilterMaxInNum", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_RUNTIME_FILTER_MAX_IN_NUM))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarRuntimeFilterMaxInNum));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarRuntimeBloomFilterMaxSize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarRuntimeBloomFilterMaxSize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_RUNTIME_BLOOM_FILTER_MAX_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarRuntimeBloomFilterMaxSize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarAutomaticSpPrivileges())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarAutomaticSpPrivileges", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_AUTOMATIC_SP_PRIVILEGES))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarAutomaticSpPrivileges));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarObEnablePlCache())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarObEnablePlCache", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_OB_ENABLE_PL_CACHE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarObEnablePlCache));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarObDefaultLobInrowThreshold())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarObDefaultLobInrowThreshold", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_OB_DEFAULT_LOB_INROW_THRESHOLD))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarObDefaultLobInrowThreshold));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarEnableStorageCardinalityEstimation())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarEnableStorageCardinalityEstimation", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR__ENABLE_STORAGE_CARDINALITY_ESTIMATION))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarEnableStorageCardinalityEstimation));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarLcTimeNames())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarLcTimeNames", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_LC_TIME_NAMES))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarLcTimeNames));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarActivateAllRolesOnLogin())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarActivateAllRolesOnLogin", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_ACTIVATE_ALL_ROLES_ON_LOGIN))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarActivateAllRolesOnLogin));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbStatsPersistent())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbStatsPersistent", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_STATS_PERSISTENT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbStatsPersistent));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarDebug())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarDebug", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_DEBUG))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarDebug));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbChangeBufferingDebug())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbChangeBufferingDebug", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_CHANGE_BUFFERING_DEBUG))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbChangeBufferingDebug));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbDisableResizeBufferPoolDebug())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbDisableResizeBufferPoolDebug", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_DISABLE_RESIZE_BUFFER_POOL_DEBUG))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbDisableResizeBufferPoolDebug));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbFilMakePageDirtyDebug())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbFilMakePageDirtyDebug", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_FIL_MAKE_PAGE_DIRTY_DEBUG))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbFilMakePageDirtyDebug));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbLimitOptimisticInsertDebug())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbLimitOptimisticInsertDebug", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_LIMIT_OPTIMISTIC_INSERT_DEBUG))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbLimitOptimisticInsertDebug));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbMergeThresholdSetAllDebug())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbMergeThresholdSetAllDebug", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_MERGE_THRESHOLD_SET_ALL_DEBUG))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbMergeThresholdSetAllDebug));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbSavedPageNumberDebug())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbSavedPageNumberDebug", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_SAVED_PAGE_NUMBER_DEBUG))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbSavedPageNumberDebug));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbTrxPurgeViewUpdateOnlyDebug())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbTrxPurgeViewUpdateOnlyDebug", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_TRX_PURGE_VIEW_UPDATE_ONLY_DEBUG))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbTrxPurgeViewUpdateOnlyDebug));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbTrxRsegNSlotsDebug())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbTrxRsegNSlotsDebug", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_TRX_RSEG_N_SLOTS_DEBUG))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbTrxRsegNSlotsDebug));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarStoredProgramCache())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarStoredProgramCache", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_STORED_PROGRAM_CACHE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarStoredProgramCache));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarCardinalityEstimationModel())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarCardinalityEstimationModel", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_CARDINALITY_ESTIMATION_MODEL))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarCardinalityEstimationModel));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarFlush())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarFlush", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_FLUSH))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarFlush));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarFlushTime())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarFlushTime", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_FLUSH_TIME))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarFlushTime));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbAdaptiveFlushing())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbAdaptiveFlushing", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_ADAPTIVE_FLUSHING))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbAdaptiveFlushing));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbAdaptiveFlushingLwm())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbAdaptiveFlushingLwm", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_ADAPTIVE_FLUSHING_LWM))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbAdaptiveFlushingLwm));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbAdaptiveHashIndex())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbAdaptiveHashIndex", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_ADAPTIVE_HASH_INDEX))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbAdaptiveHashIndex));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbAdaptiveHashIndexParts())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbAdaptiveHashIndexParts", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_ADAPTIVE_HASH_INDEX_PARTS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbAdaptiveHashIndexParts));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbAdaptiveMaxSleepDelay())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbAdaptiveMaxSleepDelay", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_ADAPTIVE_MAX_SLEEP_DELAY))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbAdaptiveMaxSleepDelay));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbAutoextendIncrement())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbAutoextendIncrement", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_AUTOEXTEND_INCREMENT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbAutoextendIncrement));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbBackgroundDropListEmpty())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbBackgroundDropListEmpty", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_BACKGROUND_DROP_LIST_EMPTY))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbBackgroundDropListEmpty));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbBufferPoolDumpAtShutdown())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbBufferPoolDumpAtShutdown", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_BUFFER_POOL_DUMP_AT_SHUTDOWN))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbBufferPoolDumpAtShutdown));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbBufferPoolDumpNow())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbBufferPoolDumpNow", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_BUFFER_POOL_DUMP_NOW))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbBufferPoolDumpNow));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbBufferPoolDumpPct())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbBufferPoolDumpPct", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_BUFFER_POOL_DUMP_PCT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbBufferPoolDumpPct));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbBufferPoolFilename())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbBufferPoolFilename", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_BUFFER_POOL_FILENAME))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbBufferPoolFilename));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbBufferPoolLoadAbort())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbBufferPoolLoadAbort", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_BUFFER_POOL_LOAD_ABORT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbBufferPoolLoadAbort));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbBufferPoolLoadNow())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbBufferPoolLoadNow", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_BUFFER_POOL_LOAD_NOW))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbBufferPoolLoadNow));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbBufferPoolSize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbBufferPoolSize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_BUFFER_POOL_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbBufferPoolSize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbChangeBufferMaxSize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbChangeBufferMaxSize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_CHANGE_BUFFER_MAX_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbChangeBufferMaxSize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbChangeBuffering())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbChangeBuffering", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_CHANGE_BUFFERING))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbChangeBuffering));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbChecksumAlgorithm())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbChecksumAlgorithm", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_CHECKSUM_ALGORITHM))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbChecksumAlgorithm));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbCmpPerIndexEnabled())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbCmpPerIndexEnabled", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_CMP_PER_INDEX_ENABLED))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbCmpPerIndexEnabled));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbCommitConcurrency())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbCommitConcurrency", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_COMMIT_CONCURRENCY))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbCommitConcurrency));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbCompressionFailureThresholdPct())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbCompressionFailureThresholdPct", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_COMPRESSION_FAILURE_THRESHOLD_PCT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbCompressionFailureThresholdPct));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbCompressionLevel())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbCompressionLevel", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_COMPRESSION_LEVEL))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbCompressionLevel));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbCompressionPadPctMax())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbCompressionPadPctMax", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_COMPRESSION_PAD_PCT_MAX))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbCompressionPadPctMax));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbConcurrencyTickets())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbConcurrencyTickets", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_CONCURRENCY_TICKETS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbConcurrencyTickets));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbDefaultRowFormat())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbDefaultRowFormat", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_DEFAULT_ROW_FORMAT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbDefaultRowFormat));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbDisableSortFileCache())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbDisableSortFileCache", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_DISABLE_SORT_FILE_CACHE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbDisableSortFileCache));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbFileFormat())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbFileFormat", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_FILE_FORMAT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbFileFormat));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbFileFormatMax())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbFileFormatMax", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_FILE_FORMAT_MAX))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbFileFormatMax));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbFilePerTable())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbFilePerTable", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_FILE_PER_TABLE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbFilePerTable));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbFillFactor())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbFillFactor", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_FILL_FACTOR))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbFillFactor));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbFlushNeighbors())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbFlushNeighbors", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_FLUSH_NEIGHBORS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbFlushNeighbors));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbFlushSync())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbFlushSync", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_FLUSH_SYNC))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbFlushSync));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbFlushingAvgLoops())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbFlushingAvgLoops", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_FLUSHING_AVG_LOOPS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbFlushingAvgLoops));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbLruScanDepth())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbLruScanDepth", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_LRU_SCAN_DEPTH))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbLruScanDepth));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbMaxDirtyPagesPct())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbMaxDirtyPagesPct", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_MAX_DIRTY_PAGES_PCT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbMaxDirtyPagesPct));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbMaxDirtyPagesPctLwm())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbMaxDirtyPagesPctLwm", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_MAX_DIRTY_PAGES_PCT_LWM))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbMaxDirtyPagesPctLwm));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbMaxPurgeLag())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbMaxPurgeLag", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_MAX_PURGE_LAG))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbMaxPurgeLag));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbMaxPurgeLagDelay())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbMaxPurgeLagDelay", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_MAX_PURGE_LAG_DELAY))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbMaxPurgeLagDelay));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarHaveSymlink())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarHaveSymlink", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_HAVE_SYMLINK))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarHaveSymlink));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarIgnoreBuiltinInnodb())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarIgnoreBuiltinInnodb", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_IGNORE_BUILTIN_INNODB))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarIgnoreBuiltinInnodb));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbBufferPoolChunkSize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbBufferPoolChunkSize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_BUFFER_POOL_CHUNK_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbBufferPoolChunkSize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbBufferPoolInstances())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbBufferPoolInstances", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_BUFFER_POOL_INSTANCES))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbBufferPoolInstances));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbBufferPoolLoadAtStartup())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbBufferPoolLoadAtStartup", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_BUFFER_POOL_LOAD_AT_STARTUP))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbBufferPoolLoadAtStartup));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbChecksums())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbChecksums", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_CHECKSUMS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbChecksums));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbDoublewrite())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbDoublewrite", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_DOUBLEWRITE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbDoublewrite));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbFileFormatCheck())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbFileFormatCheck", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_FILE_FORMAT_CHECK))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbFileFormatCheck));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbFlushMethod())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbFlushMethod", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_FLUSH_METHOD))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbFlushMethod));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbForceLoadCorrupted())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbForceLoadCorrupted", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_FORCE_LOAD_CORRUPTED))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbForceLoadCorrupted));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbPageSize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbPageSize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_PAGE_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbPageSize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbVersion())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbVersion", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_VERSION))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbVersion));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarMyisamMmapSize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarMyisamMmapSize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_MYISAM_MMAP_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarMyisamMmapSize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarTableOpenCacheInstances())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarTableOpenCacheInstances", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_TABLE_OPEN_CACHE_INSTANCES))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarTableOpenCacheInstances));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarGtidExecuted())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarGtidExecuted", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_GTID_EXECUTED))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarGtidExecuted));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarGtidOwned())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarGtidOwned", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_GTID_OWNED))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarGtidOwned));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbRollbackOnTimeout())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbRollbackOnTimeout", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_ROLLBACK_ON_TIMEOUT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbRollbackOnTimeout));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarCompletionType())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarCompletionType", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_COMPLETION_TYPE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarCompletionType));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarEnforceGtidConsistency())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarEnforceGtidConsistency", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_ENFORCE_GTID_CONSISTENCY))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarEnforceGtidConsistency));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarGtidExecutedCompressionPeriod())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarGtidExecutedCompressionPeriod", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_GTID_EXECUTED_COMPRESSION_PERIOD))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarGtidExecutedCompressionPeriod));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarGtidMode())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarGtidMode", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_GTID_MODE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarGtidMode));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarGtidNext())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarGtidNext", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_GTID_NEXT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarGtidNext));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarGtidPurged())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarGtidPurged", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_GTID_PURGED))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarGtidPurged));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbApiBkCommitInterval())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbApiBkCommitInterval", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_API_BK_COMMIT_INTERVAL))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbApiBkCommitInterval));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbApiTrxLevel())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbApiTrxLevel", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_API_TRX_LEVEL))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbApiTrxLevel));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarSessionTrackGtids())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarSessionTrackGtids", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SESSION_TRACK_GTIDS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarSessionTrackGtids));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarSessionTrackTransactionInfo())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarSessionTrackTransactionInfo", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SESSION_TRACK_TRANSACTION_INFO))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarSessionTrackTransactionInfo));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarTransactionAllocBlockSize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarTransactionAllocBlockSize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_TRANSACTION_ALLOC_BLOCK_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarTransactionAllocBlockSize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarTransactionAllowBatching())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarTransactionAllowBatching", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_TRANSACTION_ALLOW_BATCHING))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarTransactionAllowBatching));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarTransactionPreallocSize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarTransactionPreallocSize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_TRANSACTION_PREALLOC_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarTransactionPreallocSize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarTransactionWriteSetExtraction())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarTransactionWriteSetExtraction", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_TRANSACTION_WRITE_SET_EXTRACTION))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarTransactionWriteSetExtraction));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInformationSchemaStatsExpiry())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInformationSchemaStatsExpiry", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INFORMATION_SCHEMA_STATS_EXPIRY))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInformationSchemaStatsExpiry));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarGroupReplicationAllowLocalDisjointGtidsJoin())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarGroupReplicationAllowLocalDisjointGtidsJoin", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_GROUP_REPLICATION_ALLOW_LOCAL_DISJOINT_GTIDS_JOIN))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarGroupReplicationAllowLocalDisjointGtidsJoin));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarGroupReplicationAllowLocalLowerVersionJoin())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarGroupReplicationAllowLocalLowerVersionJoin", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_GROUP_REPLICATION_ALLOW_LOCAL_LOWER_VERSION_JOIN))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarGroupReplicationAllowLocalLowerVersionJoin));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarGroupReplicationAutoIncrementIncrement())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarGroupReplicationAutoIncrementIncrement", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_GROUP_REPLICATION_AUTO_INCREMENT_INCREMENT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarGroupReplicationAutoIncrementIncrement));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarGroupReplicationBootstrapGroup())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarGroupReplicationBootstrapGroup", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_GROUP_REPLICATION_BOOTSTRAP_GROUP))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarGroupReplicationBootstrapGroup));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarGroupReplicationComponentsStopTimeout())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarGroupReplicationComponentsStopTimeout", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_GROUP_REPLICATION_COMPONENTS_STOP_TIMEOUT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarGroupReplicationComponentsStopTimeout));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarGroupReplicationCompressionThreshold())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarGroupReplicationCompressionThreshold", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_GROUP_REPLICATION_COMPRESSION_THRESHOLD))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarGroupReplicationCompressionThreshold));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarGroupReplicationEnforceUpdateEverywhereChecks())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarGroupReplicationEnforceUpdateEverywhereChecks", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_GROUP_REPLICATION_ENFORCE_UPDATE_EVERYWHERE_CHECKS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarGroupReplicationEnforceUpdateEverywhereChecks));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarGroupReplicationExitStateAction())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarGroupReplicationExitStateAction", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_GROUP_REPLICATION_EXIT_STATE_ACTION))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarGroupReplicationExitStateAction));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarGroupReplicationFlowControlApplierThreshold())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarGroupReplicationFlowControlApplierThreshold", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_GROUP_REPLICATION_FLOW_CONTROL_APPLIER_THRESHOLD))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarGroupReplicationFlowControlApplierThreshold));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarGroupReplicationFlowControlCertifierThreshold())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarGroupReplicationFlowControlCertifierThreshold", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_GROUP_REPLICATION_FLOW_CONTROL_CERTIFIER_THRESHOLD))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarGroupReplicationFlowControlCertifierThreshold));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarGroupReplicationFlowControlMode())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarGroupReplicationFlowControlMode", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_GROUP_REPLICATION_FLOW_CONTROL_MODE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarGroupReplicationFlowControlMode));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarGroupReplicationForceMembers())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarGroupReplicationForceMembers", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_GROUP_REPLICATION_FORCE_MEMBERS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarGroupReplicationForceMembers));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarGroupReplicationGroupName())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarGroupReplicationGroupName", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_GROUP_REPLICATION_GROUP_NAME))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarGroupReplicationGroupName));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarGroupReplicationGtidAssignmentBlockSize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarGroupReplicationGtidAssignmentBlockSize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_GROUP_REPLICATION_GTID_ASSIGNMENT_BLOCK_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarGroupReplicationGtidAssignmentBlockSize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarGroupReplicationIpWhitelist())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarGroupReplicationIpWhitelist", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_GROUP_REPLICATION_IP_WHITELIST))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarGroupReplicationIpWhitelist));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarGroupReplicationLocalAddress())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarGroupReplicationLocalAddress", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_GROUP_REPLICATION_LOCAL_ADDRESS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarGroupReplicationLocalAddress));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarGroupReplicationMemberWeight())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarGroupReplicationMemberWeight", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_GROUP_REPLICATION_MEMBER_WEIGHT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarGroupReplicationMemberWeight));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarGroupReplicationPollSpinLoops())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarGroupReplicationPollSpinLoops", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_GROUP_REPLICATION_POLL_SPIN_LOOPS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarGroupReplicationPollSpinLoops));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarGroupReplicationRecoveryCompleteAt())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarGroupReplicationRecoveryCompleteAt", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_GROUP_REPLICATION_RECOVERY_COMPLETE_AT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarGroupReplicationRecoveryCompleteAt));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarGroupReplicationRecoveryReconnectInterval())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarGroupReplicationRecoveryReconnectInterval", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_GROUP_REPLICATION_RECOVERY_RECONNECT_INTERVAL))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarGroupReplicationRecoveryReconnectInterval));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarGroupReplicationRecoveryRetryCount())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarGroupReplicationRecoveryRetryCount", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_GROUP_REPLICATION_RECOVERY_RETRY_COUNT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarGroupReplicationRecoveryRetryCount));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarGroupReplicationRecoverySslCa())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarGroupReplicationRecoverySslCa", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_GROUP_REPLICATION_RECOVERY_SSL_CA))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarGroupReplicationRecoverySslCa));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarGroupReplicationRecoverySslCapath())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarGroupReplicationRecoverySslCapath", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_GROUP_REPLICATION_RECOVERY_SSL_CAPATH))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarGroupReplicationRecoverySslCapath));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarGroupReplicationRecoverySslCert())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarGroupReplicationRecoverySslCert", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_GROUP_REPLICATION_RECOVERY_SSL_CERT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarGroupReplicationRecoverySslCert));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarGroupReplicationRecoverySslCipher())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarGroupReplicationRecoverySslCipher", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_GROUP_REPLICATION_RECOVERY_SSL_CIPHER))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarGroupReplicationRecoverySslCipher));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarGroupReplicationRecoverySslCrl())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarGroupReplicationRecoverySslCrl", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_GROUP_REPLICATION_RECOVERY_SSL_CRL))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarGroupReplicationRecoverySslCrl));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarGroupReplicationRecoverySslCrlpath())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarGroupReplicationRecoverySslCrlpath", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_GROUP_REPLICATION_RECOVERY_SSL_CRLPATH))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarGroupReplicationRecoverySslCrlpath));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarGroupReplicationRecoverySslKey())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarGroupReplicationRecoverySslKey", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_GROUP_REPLICATION_RECOVERY_SSL_KEY))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarGroupReplicationRecoverySslKey));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarGroupReplicationRecoverySslVerifyServerCert())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarGroupReplicationRecoverySslVerifyServerCert", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_GROUP_REPLICATION_RECOVERY_SSL_VERIFY_SERVER_CERT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarGroupReplicationRecoverySslVerifyServerCert));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarGroupReplicationRecoveryUseSsl())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarGroupReplicationRecoveryUseSsl", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_GROUP_REPLICATION_RECOVERY_USE_SSL))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarGroupReplicationRecoveryUseSsl));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarGroupReplicationSinglePrimaryMode())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarGroupReplicationSinglePrimaryMode", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_GROUP_REPLICATION_SINGLE_PRIMARY_MODE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarGroupReplicationSinglePrimaryMode));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarGroupReplicationSslMode())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarGroupReplicationSslMode", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_GROUP_REPLICATION_SSL_MODE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarGroupReplicationSslMode));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarGroupReplicationStartOnBoot())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarGroupReplicationStartOnBoot", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_GROUP_REPLICATION_START_ON_BOOT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarGroupReplicationStartOnBoot));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarGroupReplicationTransactionSizeLimit())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarGroupReplicationTransactionSizeLimit", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_GROUP_REPLICATION_TRANSACTION_SIZE_LIMIT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarGroupReplicationTransactionSizeLimit));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarGroupReplicationUnreachableMajorityTimeout())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarGroupReplicationUnreachableMajorityTimeout", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_GROUP_REPLICATION_UNREACHABLE_MAJORITY_TIMEOUT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarGroupReplicationUnreachableMajorityTimeout));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbReplicationDelay())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbReplicationDelay", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_REPLICATION_DELAY))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbReplicationDelay));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarMasterInfoRepository())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarMasterInfoRepository", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_MASTER_INFO_REPOSITORY))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarMasterInfoRepository));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarMasterVerifyChecksum())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarMasterVerifyChecksum", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_MASTER_VERIFY_CHECKSUM))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarMasterVerifyChecksum));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarPseudoSlaveMode())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarPseudoSlaveMode", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_PSEUDO_SLAVE_MODE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarPseudoSlaveMode));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarPseudoThreadId())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarPseudoThreadId", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_PSEUDO_THREAD_ID))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarPseudoThreadId));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarRbrExecMode())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarRbrExecMode", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_RBR_EXEC_MODE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarRbrExecMode));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarReplicationOptimizeForStaticPluginConfig())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarReplicationOptimizeForStaticPluginConfig", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_REPLICATION_OPTIMIZE_FOR_STATIC_PLUGIN_CONFIG))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarReplicationOptimizeForStaticPluginConfig));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarReplicationSenderObserveCommitOnly())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarReplicationSenderObserveCommitOnly", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_REPLICATION_SENDER_OBSERVE_COMMIT_ONLY))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarReplicationSenderObserveCommitOnly));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarRplSemiSyncMasterEnabled())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarRplSemiSyncMasterEnabled", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_RPL_SEMI_SYNC_MASTER_ENABLED))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarRplSemiSyncMasterEnabled));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarRplSemiSyncMasterTimeout())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarRplSemiSyncMasterTimeout", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_RPL_SEMI_SYNC_MASTER_TIMEOUT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarRplSemiSyncMasterTimeout));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarRplSemiSyncMasterTraceLevel())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarRplSemiSyncMasterTraceLevel", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_RPL_SEMI_SYNC_MASTER_TRACE_LEVEL))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarRplSemiSyncMasterTraceLevel));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarRplSemiSyncMasterWaitForSlaveCount())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarRplSemiSyncMasterWaitForSlaveCount", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_RPL_SEMI_SYNC_MASTER_WAIT_FOR_SLAVE_COUNT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarRplSemiSyncMasterWaitForSlaveCount));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarRplSemiSyncMasterWaitNoSlave())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarRplSemiSyncMasterWaitNoSlave", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_RPL_SEMI_SYNC_MASTER_WAIT_NO_SLAVE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarRplSemiSyncMasterWaitNoSlave));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarRplSemiSyncMasterWaitPoint())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarRplSemiSyncMasterWaitPoint", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_RPL_SEMI_SYNC_MASTER_WAIT_POINT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarRplSemiSyncMasterWaitPoint));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarRplSemiSyncSlaveEnabled())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarRplSemiSyncSlaveEnabled", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_RPL_SEMI_SYNC_SLAVE_ENABLED))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarRplSemiSyncSlaveEnabled));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarRplSemiSyncSlaveTraceLevel())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarRplSemiSyncSlaveTraceLevel", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_RPL_SEMI_SYNC_SLAVE_TRACE_LEVEL))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarRplSemiSyncSlaveTraceLevel));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarRplStopSlaveTimeout())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarRplStopSlaveTimeout", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_RPL_STOP_SLAVE_TIMEOUT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarRplStopSlaveTimeout));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarSlaveAllowBatching())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarSlaveAllowBatching", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SLAVE_ALLOW_BATCHING))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarSlaveAllowBatching));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarSlaveCheckpointGroup())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarSlaveCheckpointGroup", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SLAVE_CHECKPOINT_GROUP))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarSlaveCheckpointGroup));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarSlaveCheckpointPeriod())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarSlaveCheckpointPeriod", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SLAVE_CHECKPOINT_PERIOD))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarSlaveCheckpointPeriod));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarSlaveCompressedProtocol())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarSlaveCompressedProtocol", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SLAVE_COMPRESSED_PROTOCOL))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarSlaveCompressedProtocol));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarSlaveExecMode())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarSlaveExecMode", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SLAVE_EXEC_MODE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarSlaveExecMode));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarSlaveMaxAllowedPacket())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarSlaveMaxAllowedPacket", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SLAVE_MAX_ALLOWED_PACKET))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarSlaveMaxAllowedPacket));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarSlaveNetTimeout())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarSlaveNetTimeout", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SLAVE_NET_TIMEOUT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarSlaveNetTimeout));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarSlaveParallelType())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarSlaveParallelType", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SLAVE_PARALLEL_TYPE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarSlaveParallelType));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarSlaveParallelWorkers())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarSlaveParallelWorkers", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SLAVE_PARALLEL_WORKERS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarSlaveParallelWorkers));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarSlavePendingJobsSizeMax())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarSlavePendingJobsSizeMax", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SLAVE_PENDING_JOBS_SIZE_MAX))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarSlavePendingJobsSizeMax));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarSlavePreserveCommitOrder())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarSlavePreserveCommitOrder", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SLAVE_PRESERVE_COMMIT_ORDER))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarSlavePreserveCommitOrder));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarSlaveSqlVerifyChecksum())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarSlaveSqlVerifyChecksum", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SLAVE_SQL_VERIFY_CHECKSUM))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarSlaveSqlVerifyChecksum));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarSlaveTransactionRetries())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarSlaveTransactionRetries", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SLAVE_TRANSACTION_RETRIES))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarSlaveTransactionRetries));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarSqlSlaveSkipCounter())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarSqlSlaveSkipCounter", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SQL_SLAVE_SKIP_COUNTER))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarSqlSlaveSkipCounter));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbForceRecovery())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbForceRecovery", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_FORCE_RECOVERY))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbForceRecovery));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarSkipSlaveStart())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarSkipSlaveStart", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SKIP_SLAVE_START))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarSkipSlaveStart));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarSlaveLoadTmpdir())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarSlaveLoadTmpdir", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SLAVE_LOAD_TMPDIR))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarSlaveLoadTmpdir));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarSlaveSkipErrors())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarSlaveSkipErrors", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SLAVE_SKIP_ERRORS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarSlaveSkipErrors));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbSyncDebug())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbSyncDebug", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_SYNC_DEBUG))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbSyncDebug));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarDefaultCollationForUtf8mb4())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarDefaultCollationForUtf8mb4", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_DEFAULT_COLLATION_FOR_UTF8MB4))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarDefaultCollationForUtf8mb4));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInsertId())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInsertId", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INSERT_ID))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInsertId));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarJoinBufferSize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarJoinBufferSize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_JOIN_BUFFER_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarJoinBufferSize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarMaxJoinSize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarMaxJoinSize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_MAX_JOIN_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarMaxJoinSize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarMaxLengthForSortData())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarMaxLengthForSortData", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_MAX_LENGTH_FOR_SORT_DATA))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarMaxLengthForSortData));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarMaxPreparedStmtCount())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarMaxPreparedStmtCount", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_MAX_PREPARED_STMT_COUNT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarMaxPreparedStmtCount));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarMaxSortLength())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarMaxSortLength", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_MAX_SORT_LENGTH))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarMaxSortLength));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarMinExaminedRowLimit())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarMinExaminedRowLimit", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_MIN_EXAMINED_ROW_LIMIT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarMinExaminedRowLimit));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarMultiRangeCount())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarMultiRangeCount", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_MULTI_RANGE_COUNT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarMultiRangeCount));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarMysqlxConnectTimeout())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarMysqlxConnectTimeout", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_MYSQLX_CONNECT_TIMEOUT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarMysqlxConnectTimeout));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarMysqlxIdleWorkerThreadTimeout())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarMysqlxIdleWorkerThreadTimeout", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_MYSQLX_IDLE_WORKER_THREAD_TIMEOUT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarMysqlxIdleWorkerThreadTimeout));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarMysqlxMaxAllowedPacket())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarMysqlxMaxAllowedPacket", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_MYSQLX_MAX_ALLOWED_PACKET))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarMysqlxMaxAllowedPacket));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarMysqlxMaxConnections())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarMysqlxMaxConnections", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_MYSQLX_MAX_CONNECTIONS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarMysqlxMaxConnections));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarMysqlxMinWorkerThreads())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarMysqlxMinWorkerThreads", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_MYSQLX_MIN_WORKER_THREADS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarMysqlxMinWorkerThreads));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarPerformanceSchemaShowProcesslist())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarPerformanceSchemaShowProcesslist", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_PERFORMANCE_SCHEMA_SHOW_PROCESSLIST))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarPerformanceSchemaShowProcesslist));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarQueryAllocBlockSize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarQueryAllocBlockSize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_QUERY_ALLOC_BLOCK_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarQueryAllocBlockSize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarQueryPreallocSize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarQueryPreallocSize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_QUERY_PREALLOC_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarQueryPreallocSize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarSlowQueryLog())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarSlowQueryLog", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SLOW_QUERY_LOG))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarSlowQueryLog));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarSlowQueryLogFile())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarSlowQueryLogFile", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SLOW_QUERY_LOG_FILE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarSlowQueryLogFile));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarSortBufferSize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarSortBufferSize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SORT_BUFFER_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarSortBufferSize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarSqlBufferResult())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarSqlBufferResult", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SQL_BUFFER_RESULT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarSqlBufferResult));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarBinlogCacheSize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarBinlogCacheSize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_BINLOG_CACHE_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarBinlogCacheSize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarBinlogDirectNonTransactionalUpdates())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarBinlogDirectNonTransactionalUpdates", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_BINLOG_DIRECT_NON_TRANSACTIONAL_UPDATES))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarBinlogDirectNonTransactionalUpdates));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarBinlogErrorAction())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarBinlogErrorAction", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_BINLOG_ERROR_ACTION))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarBinlogErrorAction));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarBinlogGroupCommitSyncDelay())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarBinlogGroupCommitSyncDelay", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_BINLOG_GROUP_COMMIT_SYNC_DELAY))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarBinlogGroupCommitSyncDelay));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarBinlogGroupCommitSyncNoDelayCount())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarBinlogGroupCommitSyncNoDelayCount", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_BINLOG_GROUP_COMMIT_SYNC_NO_DELAY_COUNT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarBinlogGroupCommitSyncNoDelayCount));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarBinlogMaxFlushQueueTime())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarBinlogMaxFlushQueueTime", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_BINLOG_MAX_FLUSH_QUEUE_TIME))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarBinlogMaxFlushQueueTime));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarBinlogOrderCommits())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarBinlogOrderCommits", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_BINLOG_ORDER_COMMITS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarBinlogOrderCommits));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarBinlogStmtCacheSize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarBinlogStmtCacheSize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_BINLOG_STMT_CACHE_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarBinlogStmtCacheSize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarBinlogTransactionDependencyHistorySize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarBinlogTransactionDependencyHistorySize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_BINLOG_TRANSACTION_DEPENDENCY_HISTORY_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarBinlogTransactionDependencyHistorySize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarBinlogTransactionDependencyTracking())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarBinlogTransactionDependencyTracking", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_BINLOG_TRANSACTION_DEPENDENCY_TRACKING))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarBinlogTransactionDependencyTracking));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarExpireLogsDays())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarExpireLogsDays", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_EXPIRE_LOGS_DAYS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarExpireLogsDays));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbFlushLogAtTimeout())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbFlushLogAtTimeout", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_FLUSH_LOG_AT_TIMEOUT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbFlushLogAtTimeout));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbFlushLogAtTrxCommit())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbFlushLogAtTrxCommit", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_FLUSH_LOG_AT_TRX_COMMIT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbFlushLogAtTrxCommit));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbLogCheckpointNow())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbLogCheckpointNow", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_LOG_CHECKPOINT_NOW))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbLogCheckpointNow));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbLogChecksums())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbLogChecksums", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_LOG_CHECKSUMS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbLogChecksums));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbLogCompressedPages())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbLogCompressedPages", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_LOG_COMPRESSED_PAGES))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbLogCompressedPages));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbLogWriteAheadSize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbLogWriteAheadSize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_LOG_WRITE_AHEAD_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbLogWriteAheadSize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbMaxUndoLogSize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbMaxUndoLogSize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_MAX_UNDO_LOG_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbMaxUndoLogSize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbOnlineAlterLogMaxSize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbOnlineAlterLogMaxSize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_ONLINE_ALTER_LOG_MAX_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbOnlineAlterLogMaxSize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbUndoLogTruncate())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbUndoLogTruncate", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_UNDO_LOG_TRUNCATE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbUndoLogTruncate));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbUndoLogs())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbUndoLogs", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_UNDO_LOGS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbUndoLogs));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarLogBinTrustFunctionCreators())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarLogBinTrustFunctionCreators", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_LOG_BIN_TRUST_FUNCTION_CREATORS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarLogBinTrustFunctionCreators));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarLogBinUseV1RowEvents())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarLogBinUseV1RowEvents", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_LOG_BIN_USE_V1_ROW_EVENTS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarLogBinUseV1RowEvents));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarLogBuiltinAsIdentifiedByPassword())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarLogBuiltinAsIdentifiedByPassword", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_LOG_BUILTIN_AS_IDENTIFIED_BY_PASSWORD))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarLogBuiltinAsIdentifiedByPassword));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarMaxBinlogCacheSize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarMaxBinlogCacheSize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_MAX_BINLOG_CACHE_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarMaxBinlogCacheSize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarMaxBinlogSize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarMaxBinlogSize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_MAX_BINLOG_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarMaxBinlogSize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarMaxBinlogStmtCacheSize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarMaxBinlogStmtCacheSize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_MAX_BINLOG_STMT_CACHE_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarMaxBinlogStmtCacheSize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarMaxRelayLogSize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarMaxRelayLogSize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_MAX_RELAY_LOG_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarMaxRelayLogSize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarRelayLogInfoRepository())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarRelayLogInfoRepository", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_RELAY_LOG_INFO_REPOSITORY))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarRelayLogInfoRepository));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarRelayLogPurge())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarRelayLogPurge", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_RELAY_LOG_PURGE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarRelayLogPurge));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarSyncBinlog())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarSyncBinlog", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SYNC_BINLOG))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarSyncBinlog));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarSyncRelayLog())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarSyncRelayLog", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SYNC_RELAY_LOG))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarSyncRelayLog));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarSyncRelayLogInfo())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarSyncRelayLogInfo", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SYNC_RELAY_LOG_INFO))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarSyncRelayLogInfo));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbDeadlockDetect())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbDeadlockDetect", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_DEADLOCK_DETECT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbDeadlockDetect));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbLockWaitTimeout())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbLockWaitTimeout", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_LOCK_WAIT_TIMEOUT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbLockWaitTimeout));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbPrintAllDeadlocks())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbPrintAllDeadlocks", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_PRINT_ALL_DEADLOCKS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbPrintAllDeadlocks));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbTableLocks())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbTableLocks", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_TABLE_LOCKS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbTableLocks));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarMaxWriteLockCount())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarMaxWriteLockCount", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_MAX_WRITE_LOCK_COUNT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarMaxWriteLockCount));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarObEnableRoleIds())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarObEnableRoleIds", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR__OB_ENABLE_ROLE_IDS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarObEnableRoleIds));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbReadOnly())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbReadOnly", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_READ_ONLY))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbReadOnly));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbApiDisableRowlock())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbApiDisableRowlock", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_API_DISABLE_ROWLOCK))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbApiDisableRowlock));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbAutoincLockMode())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbAutoincLockMode", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_AUTOINC_LOCK_MODE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbAutoincLockMode));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarSkipExternalLocking())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarSkipExternalLocking", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SKIP_EXTERNAL_LOCKING))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarSkipExternalLocking));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarSuperReadOnly())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarSuperReadOnly", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SUPER_READ_ONLY))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarSuperReadOnly));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarLowPriorityUpdates())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarLowPriorityUpdates", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_LOW_PRIORITY_UPDATES))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarLowPriorityUpdates));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarMaxErrorCount())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarMaxErrorCount", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_MAX_ERROR_COUNT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarMaxErrorCount));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarMaxInsertDelayedThreads())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarMaxInsertDelayedThreads", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_MAX_INSERT_DELAYED_THREADS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarMaxInsertDelayedThreads));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarFtStopwordFile())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarFtStopwordFile", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_FT_STOPWORD_FILE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarFtStopwordFile));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbFtCacheSize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbFtCacheSize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_FT_CACHE_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbFtCacheSize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbFtSortPllDegree())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbFtSortPllDegree", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_FT_SORT_PLL_DEGREE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbFtSortPllDegree));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbFtTotalCacheSize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbFtTotalCacheSize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_FT_TOTAL_CACHE_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbFtTotalCacheSize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarMecabRcFile())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarMecabRcFile", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_MECAB_RC_FILE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarMecabRcFile));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarMetadataLocksCacheSize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarMetadataLocksCacheSize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_METADATA_LOCKS_CACHE_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarMetadataLocksCacheSize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarMetadataLocksHashInstances())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarMetadataLocksHashInstances", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_METADATA_LOCKS_HASH_INSTANCES))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarMetadataLocksHashInstances));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbTempDataFilePath())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbTempDataFilePath", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_TEMP_DATA_FILE_PATH))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbTempDataFilePath));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbDataFilePath())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbDataFilePath", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_DATA_FILE_PATH))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbDataFilePath));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbDataHomeDir())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbDataHomeDir", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_DATA_HOME_DIR))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbDataHomeDir));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarDefaultTmpStorageEngine())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarDefaultTmpStorageEngine", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_DEFAULT_TMP_STORAGE_ENGINE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarDefaultTmpStorageEngine));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbFtEnableDiagPrint())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbFtEnableDiagPrint", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_FT_ENABLE_DIAG_PRINT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbFtEnableDiagPrint));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbFtNumWordOptimize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbFtNumWordOptimize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_FT_NUM_WORD_OPTIMIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbFtNumWordOptimize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbFtResultCacheLimit())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbFtResultCacheLimit", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_FT_RESULT_CACHE_LIMIT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbFtResultCacheLimit));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbFtServerStopwordTable())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbFtServerStopwordTable", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_FT_SERVER_STOPWORD_TABLE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbFtServerStopwordTable));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbOptimizeFulltextOnly())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbOptimizeFulltextOnly", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_OPTIMIZE_FULLTEXT_ONLY))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbOptimizeFulltextOnly));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarMaxTmpTables())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarMaxTmpTables", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_MAX_TMP_TABLES))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarMaxTmpTables));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbTmpdir())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbTmpdir", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_TMPDIR))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbTmpdir));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarGroupReplicationGroupSeeds())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarGroupReplicationGroupSeeds", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_GROUP_REPLICATION_GROUP_SEEDS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarGroupReplicationGroupSeeds));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarSlaveRowsSearchAlgorithms())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarSlaveRowsSearchAlgorithms", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SLAVE_ROWS_SEARCH_ALGORITHMS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarSlaveRowsSearchAlgorithms));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarSlaveTypeConversions())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarSlaveTypeConversions", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SLAVE_TYPE_CONVERSIONS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarSlaveTypeConversions));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarObHnswEfSearch())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarObHnswEfSearch", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_OB_HNSW_EF_SEARCH))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarObHnswEfSearch));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNdbAllowCopyingAlterTable())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNdbAllowCopyingAlterTable", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_NDB_ALLOW_COPYING_ALTER_TABLE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNdbAllowCopyingAlterTable));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNdbAutoincrementPrefetchSz())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNdbAutoincrementPrefetchSz", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_NDB_AUTOINCREMENT_PREFETCH_SZ))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNdbAutoincrementPrefetchSz));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNdbBlobReadBatchBytes())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNdbBlobReadBatchBytes", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_NDB_BLOB_READ_BATCH_BYTES))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNdbBlobReadBatchBytes));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNdbBlobWriteBatchBytes())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNdbBlobWriteBatchBytes", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_NDB_BLOB_WRITE_BATCH_BYTES))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNdbBlobWriteBatchBytes));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNdbCacheCheckTime())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNdbCacheCheckTime", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_NDB_CACHE_CHECK_TIME))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNdbCacheCheckTime));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNdbClearApplyStatus())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNdbClearApplyStatus", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_NDB_CLEAR_APPLY_STATUS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNdbClearApplyStatus));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNdbDataNodeNeighbour())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNdbDataNodeNeighbour", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_NDB_DATA_NODE_NEIGHBOUR))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNdbDataNodeNeighbour));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNdbDefaultColumnFormat())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNdbDefaultColumnFormat", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_NDB_DEFAULT_COLUMN_FORMAT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNdbDefaultColumnFormat));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNdbDeferredConstraints())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNdbDeferredConstraints", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_NDB_DEFERRED_CONSTRAINTS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNdbDeferredConstraints));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNdbDistribution())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNdbDistribution", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_NDB_DISTRIBUTION))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNdbDistribution));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNdbEventbufferFreePercent())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNdbEventbufferFreePercent", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_NDB_EVENTBUFFER_FREE_PERCENT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNdbEventbufferFreePercent));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNdbEventbufferMaxAlloc())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNdbEventbufferMaxAlloc", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_NDB_EVENTBUFFER_MAX_ALLOC))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNdbEventbufferMaxAlloc));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNdbExtraLogging())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNdbExtraLogging", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_NDB_EXTRA_LOGGING))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNdbExtraLogging));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNdbForceSend())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNdbForceSend", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_NDB_FORCE_SEND))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNdbForceSend));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNdbFullyReplicated())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNdbFullyReplicated", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_NDB_FULLY_REPLICATED))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNdbFullyReplicated));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNdbIndexStatEnable())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNdbIndexStatEnable", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_NDB_INDEX_STAT_ENABLE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNdbIndexStatEnable));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNdbIndexStatOption())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNdbIndexStatOption", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_NDB_INDEX_STAT_OPTION))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNdbIndexStatOption));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNdbJoinPushdown())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNdbJoinPushdown", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_NDB_JOIN_PUSHDOWN))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNdbJoinPushdown));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNdbLogBinlogIndex())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNdbLogBinlogIndex", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_NDB_LOG_BINLOG_INDEX))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNdbLogBinlogIndex));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNdbLogEmptyEpochs())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNdbLogEmptyEpochs", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_NDB_LOG_EMPTY_EPOCHS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNdbLogEmptyEpochs));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNdbLogEmptyUpdate())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNdbLogEmptyUpdate", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_NDB_LOG_EMPTY_UPDATE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNdbLogEmptyUpdate));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNdbLogExclusiveReads())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNdbLogExclusiveReads", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_NDB_LOG_EXCLUSIVE_READS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNdbLogExclusiveReads));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNdbLogUpdateAsWrite())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNdbLogUpdateAsWrite", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_NDB_LOG_UPDATE_AS_WRITE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNdbLogUpdateAsWrite));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNdbLogUpdateMinimal())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNdbLogUpdateMinimal", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_NDB_LOG_UPDATE_MINIMAL))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNdbLogUpdateMinimal));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNdbLogUpdatedOnly())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNdbLogUpdatedOnly", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_NDB_LOG_UPDATED_ONLY))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNdbLogUpdatedOnly));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNdbOptimizationDelay())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNdbOptimizationDelay", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_NDB_OPTIMIZATION_DELAY))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNdbOptimizationDelay));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNdbReadBackup())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNdbReadBackup", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_NDB_READ_BACKUP))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNdbReadBackup));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNdbRecvThreadActivationThreshold())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNdbRecvThreadActivationThreshold", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_NDB_RECV_THREAD_ACTIVATION_THRESHOLD))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNdbRecvThreadActivationThreshold));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNdbRecvThreadCpuMask())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNdbRecvThreadCpuMask", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_NDB_RECV_THREAD_CPU_MASK))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNdbRecvThreadCpuMask));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNdbReportThreshBinlogEpochSlip())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNdbReportThreshBinlogEpochSlip", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_NDB_REPORT_THRESH_BINLOG_EPOCH_SLIP))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNdbReportThreshBinlogEpochSlip));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNdbReportThreshBinlogMemUsage())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNdbReportThreshBinlogMemUsage", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_NDB_REPORT_THRESH_BINLOG_MEM_USAGE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNdbReportThreshBinlogMemUsage));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNdbRowChecksum())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNdbRowChecksum", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_NDB_ROW_CHECKSUM))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNdbRowChecksum));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNdbShowForeignKeyMockTables())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNdbShowForeignKeyMockTables", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_NDB_SHOW_FOREIGN_KEY_MOCK_TABLES))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNdbShowForeignKeyMockTables));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNdbSlaveConflictRole())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNdbSlaveConflictRole", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_NDB_SLAVE_CONFLICT_ROLE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNdbSlaveConflictRole));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNdbTableNoLogging())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNdbTableNoLogging", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_NDB_TABLE_NO_LOGGING))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNdbTableNoLogging));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNdbTableTemporary())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNdbTableTemporary", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_NDB_TABLE_TEMPORARY))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNdbTableTemporary));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNdbUseExactCount())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNdbUseExactCount", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_NDB_USE_EXACT_COUNT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNdbUseExactCount));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNdbUseTransactions())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNdbUseTransactions", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_NDB_USE_TRANSACTIONS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNdbUseTransactions));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNdbinfoMaxBytes())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNdbinfoMaxBytes", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_NDBINFO_MAX_BYTES))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNdbinfoMaxBytes));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNdbinfoMaxRows())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNdbinfoMaxRows", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_NDBINFO_MAX_ROWS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNdbinfoMaxRows));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNdbinfoOffline())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNdbinfoOffline", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_NDBINFO_OFFLINE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNdbinfoOffline));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNdbinfoShowHidden())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNdbinfoShowHidden", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_NDBINFO_SHOW_HIDDEN))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNdbinfoShowHidden));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarMyisamDataPointerSize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarMyisamDataPointerSize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_MYISAM_DATA_POINTER_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarMyisamDataPointerSize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarMyisamMaxSortFileSize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarMyisamMaxSortFileSize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_MYISAM_MAX_SORT_FILE_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarMyisamMaxSortFileSize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarMyisamRepairThreads())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarMyisamRepairThreads", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_MYISAM_REPAIR_THREADS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarMyisamRepairThreads));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarMyisamSortBufferSize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarMyisamSortBufferSize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_MYISAM_SORT_BUFFER_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarMyisamSortBufferSize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarMyisamStatsMethod())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarMyisamStatsMethod", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_MYISAM_STATS_METHOD))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarMyisamStatsMethod));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarMyisamUseMmap())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarMyisamUseMmap", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_MYISAM_USE_MMAP))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarMyisamUseMmap));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarPreloadBufferSize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarPreloadBufferSize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_PRELOAD_BUFFER_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarPreloadBufferSize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarReadBufferSize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarReadBufferSize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_READ_BUFFER_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarReadBufferSize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarReadRndBufferSize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarReadRndBufferSize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_READ_RND_BUFFER_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarReadRndBufferSize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarSyncFrm())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarSyncFrm", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SYNC_FRM))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarSyncFrm));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarSyncMasterInfo())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarSyncMasterInfo", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SYNC_MASTER_INFO))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarSyncMasterInfo));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarTableOpenCache())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarTableOpenCache", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_TABLE_OPEN_CACHE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarTableOpenCache));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbMonitorDisable())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbMonitorDisable", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_MONITOR_DISABLE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbMonitorDisable));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbMonitorEnable())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbMonitorEnable", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_MONITOR_ENABLE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbMonitorEnable));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbMonitorReset())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbMonitorReset", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_MONITOR_RESET))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbMonitorReset));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbMonitorResetAll())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbMonitorResetAll", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_MONITOR_RESET_ALL))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbMonitorResetAll));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbOldBlocksPct())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbOldBlocksPct", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_OLD_BLOCKS_PCT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbOldBlocksPct));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbOldBlocksTime())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbOldBlocksTime", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_OLD_BLOCKS_TIME))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbOldBlocksTime));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbPurgeBatchSize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbPurgeBatchSize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_PURGE_BATCH_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbPurgeBatchSize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbPurgeRsegTruncateFrequency())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbPurgeRsegTruncateFrequency", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_PURGE_RSEG_TRUNCATE_FREQUENCY))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbPurgeRsegTruncateFrequency));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbRandomReadAhead())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbRandomReadAhead", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_RANDOM_READ_AHEAD))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbRandomReadAhead));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbReadAheadThreshold())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbReadAheadThreshold", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_READ_AHEAD_THRESHOLD))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbReadAheadThreshold));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbRollbackSegments())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbRollbackSegments", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_ROLLBACK_SEGMENTS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbRollbackSegments));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbSpinWaitDelay())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbSpinWaitDelay", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_SPIN_WAIT_DELAY))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbSpinWaitDelay));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbStatusOutput())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbStatusOutput", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_STATUS_OUTPUT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbStatusOutput));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbStatusOutputLocks())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbStatusOutputLocks", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_STATUS_OUTPUT_LOCKS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbStatusOutputLocks));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbSyncSpinLoops())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbSyncSpinLoops", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_SYNC_SPIN_LOOPS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbSyncSpinLoops));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInternalTmpDiskStorageEngine())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInternalTmpDiskStorageEngine", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INTERNAL_TMP_DISK_STORAGE_ENGINE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInternalTmpDiskStorageEngine));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarKeepFilesOnCreate())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarKeepFilesOnCreate", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_KEEP_FILES_ON_CREATE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarKeepFilesOnCreate));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarMaxHeapTableSize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarMaxHeapTableSize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_MAX_HEAP_TABLE_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarMaxHeapTableSize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarBulkInsertBufferSize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarBulkInsertBufferSize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_BULK_INSERT_BUFFER_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarBulkInsertBufferSize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarHostCacheSize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarHostCacheSize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_HOST_CACHE_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarHostCacheSize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInitSlave())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInitSlave", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INIT_SLAVE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInitSlave));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbFastShutdown())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbFastShutdown", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_FAST_SHUTDOWN))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbFastShutdown));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbIoCapacity())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbIoCapacity", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_IO_CAPACITY))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbIoCapacity));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbIoCapacityMax())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbIoCapacityMax", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_IO_CAPACITY_MAX))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbIoCapacityMax));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbThreadConcurrency())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbThreadConcurrency", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_THREAD_CONCURRENCY))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbThreadConcurrency));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbThreadSleepDelay())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbThreadSleepDelay", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_THREAD_SLEEP_DELAY))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbThreadSleepDelay));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarLogErrorVerbosity())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarLogErrorVerbosity", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_LOG_ERROR_VERBOSITY))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarLogErrorVerbosity));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarLogOutput())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarLogOutput", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_LOG_OUTPUT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarLogOutput));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarLogQueriesNotUsingIndexes())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarLogQueriesNotUsingIndexes", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_LOG_QUERIES_NOT_USING_INDEXES))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarLogQueriesNotUsingIndexes));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarLogSlowAdminStatements())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarLogSlowAdminStatements", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_LOG_SLOW_ADMIN_STATEMENTS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarLogSlowAdminStatements));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarLogSlowSlaveStatements())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarLogSlowSlaveStatements", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_LOG_SLOW_SLAVE_STATEMENTS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarLogSlowSlaveStatements));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarLogStatementsUnsafeForBinlog())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarLogStatementsUnsafeForBinlog", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_LOG_STATEMENTS_UNSAFE_FOR_BINLOG))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarLogStatementsUnsafeForBinlog));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarLogSyslog())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarLogSyslog", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_LOG_SYSLOG))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarLogSyslog));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarLogSyslogFacility())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarLogSyslogFacility", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_LOG_SYSLOG_FACILITY))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarLogSyslogFacility));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarLogSyslogIncludePid())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarLogSyslogIncludePid", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_LOG_SYSLOG_INCLUDE_PID))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarLogSyslogIncludePid));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarLogSyslogTag())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarLogSyslogTag", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_LOG_SYSLOG_TAG))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarLogSyslogTag));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarLogThrottleQueriesNotUsingIndexes())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarLogThrottleQueriesNotUsingIndexes", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_LOG_THROTTLE_QUERIES_NOT_USING_INDEXES))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarLogThrottleQueriesNotUsingIndexes));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarLogTimestamps())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarLogTimestamps", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_LOG_TIMESTAMPS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarLogTimestamps));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarLogWarnings())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarLogWarnings", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_LOG_WARNINGS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarLogWarnings));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarMaxDelayedThreads())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarMaxDelayedThreads", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_MAX_DELAYED_THREADS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarMaxDelayedThreads));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarOfflineMode())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarOfflineMode", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_OFFLINE_MODE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarOfflineMode));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarRequireSecureTransport())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarRequireSecureTransport", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_REQUIRE_SECURE_TRANSPORT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarRequireSecureTransport));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarSlowLaunchTime())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarSlowLaunchTime", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SLOW_LAUNCH_TIME))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarSlowLaunchTime));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarSqlLogOff())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarSqlLogOff", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SQL_LOG_OFF))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarSqlLogOff));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarThreadCacheSize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarThreadCacheSize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_THREAD_CACHE_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarThreadCacheSize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarThreadPoolHighPriorityConnection())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarThreadPoolHighPriorityConnection", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_THREAD_POOL_HIGH_PRIORITY_CONNECTION))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarThreadPoolHighPriorityConnection));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarThreadPoolMaxUnusedThreads())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarThreadPoolMaxUnusedThreads", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_THREAD_POOL_MAX_UNUSED_THREADS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarThreadPoolMaxUnusedThreads));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarThreadPoolPrioKickupTimer())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarThreadPoolPrioKickupTimer", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_THREAD_POOL_PRIO_KICKUP_TIMER))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarThreadPoolPrioKickupTimer));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarThreadPoolStallLimit())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarThreadPoolStallLimit", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_THREAD_POOL_STALL_LIMIT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarThreadPoolStallLimit));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarHaveStatementTimeout())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarHaveStatementTimeout", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_HAVE_STATEMENT_TIMEOUT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarHaveStatementTimeout));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarMysqlxBindAddress())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarMysqlxBindAddress", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_MYSQLX_BIND_ADDRESS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarMysqlxBindAddress));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarMysqlxPort())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarMysqlxPort", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_MYSQLX_PORT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarMysqlxPort));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarMysqlxPortOpenTimeout())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarMysqlxPortOpenTimeout", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_MYSQLX_PORT_OPEN_TIMEOUT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarMysqlxPortOpenTimeout));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarMysqlxSocket())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarMysqlxSocket", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_MYSQLX_SOCKET))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarMysqlxSocket));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarMysqlxSslCa())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarMysqlxSslCa", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_MYSQLX_SSL_CA))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarMysqlxSslCa));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarMysqlxSslCapath())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarMysqlxSslCapath", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_MYSQLX_SSL_CAPATH))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarMysqlxSslCapath));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarMysqlxSslCert())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarMysqlxSslCert", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_MYSQLX_SSL_CERT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarMysqlxSslCert));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarMysqlxSslCipher())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarMysqlxSslCipher", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_MYSQLX_SSL_CIPHER))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarMysqlxSslCipher));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarMysqlxSslCrl())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarMysqlxSslCrl", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_MYSQLX_SSL_CRL))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarMysqlxSslCrl));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarMysqlxSslCrlpath())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarMysqlxSslCrlpath", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_MYSQLX_SSL_CRLPATH))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarMysqlxSslCrlpath));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarMysqlxSslKey())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarMysqlxSslKey", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_MYSQLX_SSL_KEY))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarMysqlxSslKey));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarOld())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarOld", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_OLD))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarOld));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarPerformanceSchemaAccountsSize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarPerformanceSchemaAccountsSize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_PERFORMANCE_SCHEMA_ACCOUNTS_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarPerformanceSchemaAccountsSize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarPerformanceSchemaDigestsSize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarPerformanceSchemaDigestsSize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_PERFORMANCE_SCHEMA_DIGESTS_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarPerformanceSchemaDigestsSize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarPerformanceSchemaEventsStagesHistoryLongSize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarPerformanceSchemaEventsStagesHistoryLongSize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_PERFORMANCE_SCHEMA_EVENTS_STAGES_HISTORY_LONG_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarPerformanceSchemaEventsStagesHistoryLongSize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarPerformanceSchemaEventsStagesHistorySize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarPerformanceSchemaEventsStagesHistorySize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_PERFORMANCE_SCHEMA_EVENTS_STAGES_HISTORY_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarPerformanceSchemaEventsStagesHistorySize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarPerformanceSchemaEventsStatementsHistoryLongSize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarPerformanceSchemaEventsStatementsHistoryLongSize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_PERFORMANCE_SCHEMA_EVENTS_STATEMENTS_HISTORY_LONG_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarPerformanceSchemaEventsStatementsHistoryLongSize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarPerformanceSchemaEventsStatementsHistorySize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarPerformanceSchemaEventsStatementsHistorySize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_PERFORMANCE_SCHEMA_EVENTS_STATEMENTS_HISTORY_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarPerformanceSchemaEventsStatementsHistorySize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarPerformanceSchemaEventsTransactionsHistoryLongSize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarPerformanceSchemaEventsTransactionsHistoryLongSize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_PERFORMANCE_SCHEMA_EVENTS_TRANSACTIONS_HISTORY_LONG_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarPerformanceSchemaEventsTransactionsHistoryLongSize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarPerformanceSchemaEventsTransactionsHistorySize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarPerformanceSchemaEventsTransactionsHistorySize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_PERFORMANCE_SCHEMA_EVENTS_TRANSACTIONS_HISTORY_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarPerformanceSchemaEventsTransactionsHistorySize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarPerformanceSchemaEventsWaitsHistoryLongSize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarPerformanceSchemaEventsWaitsHistoryLongSize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_PERFORMANCE_SCHEMA_EVENTS_WAITS_HISTORY_LONG_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarPerformanceSchemaEventsWaitsHistoryLongSize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarPerformanceSchemaEventsWaitsHistorySize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarPerformanceSchemaEventsWaitsHistorySize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_PERFORMANCE_SCHEMA_EVENTS_WAITS_HISTORY_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarPerformanceSchemaEventsWaitsHistorySize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarPerformanceSchemaHostsSize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarPerformanceSchemaHostsSize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_PERFORMANCE_SCHEMA_HOSTS_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarPerformanceSchemaHostsSize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarPerformanceSchemaMaxCondClasses())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarPerformanceSchemaMaxCondClasses", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_COND_CLASSES))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarPerformanceSchemaMaxCondClasses));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarPerformanceSchemaMaxCondInstances())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarPerformanceSchemaMaxCondInstances", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_COND_INSTANCES))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarPerformanceSchemaMaxCondInstances));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarPerformanceSchemaMaxDigestLength())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarPerformanceSchemaMaxDigestLength", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_DIGEST_LENGTH))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarPerformanceSchemaMaxDigestLength));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarPerformanceSchemaMaxFileClasses())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarPerformanceSchemaMaxFileClasses", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_FILE_CLASSES))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarPerformanceSchemaMaxFileClasses));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarPerformanceSchemaMaxFileHandles())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarPerformanceSchemaMaxFileHandles", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_FILE_HANDLES))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarPerformanceSchemaMaxFileHandles));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarPerformanceSchemaMaxFileInstances())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarPerformanceSchemaMaxFileInstances", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_FILE_INSTANCES))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarPerformanceSchemaMaxFileInstances));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarPerformanceSchemaMaxIndexStat())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarPerformanceSchemaMaxIndexStat", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_INDEX_STAT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarPerformanceSchemaMaxIndexStat));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarPerformanceSchemaMaxMemoryClasses())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarPerformanceSchemaMaxMemoryClasses", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_MEMORY_CLASSES))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarPerformanceSchemaMaxMemoryClasses));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarPerformanceSchemaMaxMetadataLocks())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarPerformanceSchemaMaxMetadataLocks", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_METADATA_LOCKS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarPerformanceSchemaMaxMetadataLocks));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarPerformanceSchemaMaxMutexClasses())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarPerformanceSchemaMaxMutexClasses", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_MUTEX_CLASSES))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarPerformanceSchemaMaxMutexClasses));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarPerformanceSchemaMaxMutexInstances())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarPerformanceSchemaMaxMutexInstances", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_MUTEX_INSTANCES))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarPerformanceSchemaMaxMutexInstances));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarPerformanceSchemaMaxPreparedStatementsInstances())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarPerformanceSchemaMaxPreparedStatementsInstances", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_PREPARED_STATEMENTS_INSTANCES))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarPerformanceSchemaMaxPreparedStatementsInstances));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarPerformanceSchemaMaxProgramInstances())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarPerformanceSchemaMaxProgramInstances", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_PROGRAM_INSTANCES))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarPerformanceSchemaMaxProgramInstances));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarPerformanceSchemaMaxRwlockClasses())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarPerformanceSchemaMaxRwlockClasses", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_RWLOCK_CLASSES))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarPerformanceSchemaMaxRwlockClasses));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarPerformanceSchemaMaxRwlockInstances())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarPerformanceSchemaMaxRwlockInstances", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_RWLOCK_INSTANCES))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarPerformanceSchemaMaxRwlockInstances));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarPerformanceSchemaMaxSocketClasses())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarPerformanceSchemaMaxSocketClasses", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_SOCKET_CLASSES))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarPerformanceSchemaMaxSocketClasses));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarPerformanceSchemaMaxSocketInstances())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarPerformanceSchemaMaxSocketInstances", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_SOCKET_INSTANCES))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarPerformanceSchemaMaxSocketInstances));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarPerformanceSchemaMaxSqlTextLength())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarPerformanceSchemaMaxSqlTextLength", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_SQL_TEXT_LENGTH))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarPerformanceSchemaMaxSqlTextLength));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarPerformanceSchemaMaxStageClasses())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarPerformanceSchemaMaxStageClasses", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_STAGE_CLASSES))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarPerformanceSchemaMaxStageClasses));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarPerformanceSchemaMaxStatementClasses())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarPerformanceSchemaMaxStatementClasses", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_STATEMENT_CLASSES))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarPerformanceSchemaMaxStatementClasses));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarPerformanceSchemaMaxStatementStack())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarPerformanceSchemaMaxStatementStack", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_STATEMENT_STACK))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarPerformanceSchemaMaxStatementStack));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarPerformanceSchemaMaxTableHandles())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarPerformanceSchemaMaxTableHandles", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_TABLE_HANDLES))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarPerformanceSchemaMaxTableHandles));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarPerformanceSchemaMaxTableInstances())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarPerformanceSchemaMaxTableInstances", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_TABLE_INSTANCES))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarPerformanceSchemaMaxTableInstances));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarPerformanceSchemaMaxTableLockStat())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarPerformanceSchemaMaxTableLockStat", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_TABLE_LOCK_STAT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarPerformanceSchemaMaxTableLockStat));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarPerformanceSchemaMaxThreadClasses())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarPerformanceSchemaMaxThreadClasses", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_THREAD_CLASSES))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarPerformanceSchemaMaxThreadClasses));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarPerformanceSchemaMaxThreadInstances())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarPerformanceSchemaMaxThreadInstances", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_THREAD_INSTANCES))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarPerformanceSchemaMaxThreadInstances));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarPerformanceSchemaSessionConnectAttrsSize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarPerformanceSchemaSessionConnectAttrsSize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_PERFORMANCE_SCHEMA_SESSION_CONNECT_ATTRS_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarPerformanceSchemaSessionConnectAttrsSize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarPerformanceSchemaSetupActorsSize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarPerformanceSchemaSetupActorsSize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_PERFORMANCE_SCHEMA_SETUP_ACTORS_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarPerformanceSchemaSetupActorsSize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarPerformanceSchemaSetupObjectsSize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarPerformanceSchemaSetupObjectsSize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_PERFORMANCE_SCHEMA_SETUP_OBJECTS_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarPerformanceSchemaSetupObjectsSize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarPerformanceSchemaUsersSize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarPerformanceSchemaUsersSize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_PERFORMANCE_SCHEMA_USERS_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarPerformanceSchemaUsersSize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarVersionTokensSessionNumber())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarVersionTokensSessionNumber", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_VERSION_TOKENS_SESSION_NUMBER))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarVersionTokensSessionNumber));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarBackLog())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarBackLog", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_BACK_LOG))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarBackLog));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarBasedir())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarBasedir", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_BASEDIR))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarBasedir));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarBindAddress())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarBindAddress", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_BIND_ADDRESS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarBindAddress));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarCoreFile())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarCoreFile", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_CORE_FILE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarCoreFile));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarHaveCompress())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarHaveCompress", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_HAVE_COMPRESS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarHaveCompress));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarIgnoreDbDirs())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarIgnoreDbDirs", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_IGNORE_DB_DIRS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarIgnoreDbDirs));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInitFile())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInitFile", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INIT_FILE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInitFile));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbOpenFiles())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbOpenFiles", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_OPEN_FILES))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbOpenFiles));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbPageCleaners())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbPageCleaners", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_PAGE_CLEANERS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbPageCleaners));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbPurgeThreads())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbPurgeThreads", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_PURGE_THREADS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbPurgeThreads));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbReadIoThreads())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbReadIoThreads", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_READ_IO_THREADS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbReadIoThreads));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbSyncArraySize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbSyncArraySize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_SYNC_ARRAY_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbSyncArraySize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbUseNativeAio())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbUseNativeAio", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_USE_NATIVE_AIO))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbUseNativeAio));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbWriteIoThreads())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbWriteIoThreads", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_WRITE_IO_THREADS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbWriteIoThreads));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarLargeFilesSupport())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarLargeFilesSupport", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_LARGE_FILES_SUPPORT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarLargeFilesSupport));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarLockedInMemory())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarLockedInMemory", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_LOCKED_IN_MEMORY))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarLockedInMemory));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarLogError())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarLogError", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_LOG_ERROR))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarLogError));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNamedPipe())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNamedPipe", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_NAMED_PIPE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNamedPipe));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNamedPipeFullAccessGroup())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNamedPipeFullAccessGroup", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_NAMED_PIPE_FULL_ACCESS_GROUP))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNamedPipeFullAccessGroup));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarOpenFilesLimit())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarOpenFilesLimit", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_OPEN_FILES_LIMIT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarOpenFilesLimit));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarReportHost())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarReportHost", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_REPORT_HOST))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarReportHost));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarReportPassword())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarReportPassword", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_REPORT_PASSWORD))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarReportPassword));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarReportPort())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarReportPort", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_REPORT_PORT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarReportPort));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarReportUser())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarReportUser", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_REPORT_USER))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarReportUser));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarServerIdBits())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarServerIdBits", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SERVER_ID_BITS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarServerIdBits));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarSharedMemory())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarSharedMemory", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SHARED_MEMORY))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarSharedMemory));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarSharedMemoryBaseName())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarSharedMemoryBaseName", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SHARED_MEMORY_BASE_NAME))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarSharedMemoryBaseName));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarSkipNameResolve())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarSkipNameResolve", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SKIP_NAME_RESOLVE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarSkipNameResolve));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarSkipNetworking())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarSkipNetworking", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SKIP_NETWORKING))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarSkipNetworking));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarThreadHandling())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarThreadHandling", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_THREAD_HANDLING))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarThreadHandling));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarThreadPoolAlgorithm())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarThreadPoolAlgorithm", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_THREAD_POOL_ALGORITHM))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarThreadPoolAlgorithm));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarThreadPoolSize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarThreadPoolSize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_THREAD_POOL_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarThreadPoolSize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarThreadStack())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarThreadStack", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_THREAD_STACK))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarThreadStack));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarBinlogGtidSimpleRecovery())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarBinlogGtidSimpleRecovery", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_BINLOG_GTID_SIMPLE_RECOVERY))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarBinlogGtidSimpleRecovery));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbApiEnableBinlog())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbApiEnableBinlog", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_API_ENABLE_BINLOG))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbApiEnableBinlog));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbLocksUnsafeForBinlog())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbLocksUnsafeForBinlog", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_LOCKS_UNSAFE_FOR_BINLOG))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbLocksUnsafeForBinlog));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbLogBufferSize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbLogBufferSize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_LOG_BUFFER_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbLogBufferSize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbLogFilesInGroup())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbLogFilesInGroup", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_LOG_FILES_IN_GROUP))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbLogFilesInGroup));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbLogFileSize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbLogFileSize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_LOG_FILE_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbLogFileSize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbLogGroupHomeDir())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbLogGroupHomeDir", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_LOG_GROUP_HOME_DIR))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbLogGroupHomeDir));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbUndoDirectory())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbUndoDirectory", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_UNDO_DIRECTORY))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbUndoDirectory));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbUndoTablespaces())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbUndoTablespaces", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_UNDO_TABLESPACES))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbUndoTablespaces));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarLogBinBasename())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarLogBinBasename", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_LOG_BIN_BASENAME))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarLogBinBasename));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarLogBinIndex())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarLogBinIndex", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_LOG_BIN_INDEX))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarLogBinIndex));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarLogSlaveUpdates())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarLogSlaveUpdates", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_LOG_SLAVE_UPDATES))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarLogSlaveUpdates));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarRelayLog())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarRelayLog", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_RELAY_LOG))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarRelayLog));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarRelayLogBasename())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarRelayLogBasename", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_RELAY_LOG_BASENAME))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarRelayLogBasename));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarRelayLogIndex())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarRelayLogIndex", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_RELAY_LOG_INDEX))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarRelayLogIndex));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarRelayLogInfoFile())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarRelayLogInfoFile", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_RELAY_LOG_INFO_FILE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarRelayLogInfoFile));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarRelayLogRecovery())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarRelayLogRecovery", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_RELAY_LOG_RECOVERY))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarRelayLogRecovery));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarRelayLogSpaceLimit())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarRelayLogSpaceLimit", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_RELAY_LOG_SPACE_LIMIT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarRelayLogSpaceLimit));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarDelayKeyWrite())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarDelayKeyWrite", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_DELAY_KEY_WRITE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarDelayKeyWrite));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbLargePrefix())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbLargePrefix", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_LARGE_PREFIX))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbLargePrefix));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarKeyBufferSize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarKeyBufferSize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_KEY_BUFFER_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarKeyBufferSize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarKeyCacheAgeThreshold())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarKeyCacheAgeThreshold", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_KEY_CACHE_AGE_THRESHOLD))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarKeyCacheAgeThreshold));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarKeyCacheDivisionLimit())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarKeyCacheDivisionLimit", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_KEY_CACHE_DIVISION_LIMIT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarKeyCacheDivisionLimit));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarMaxSeeksForKey())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarMaxSeeksForKey", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_MAX_SEEKS_FOR_KEY))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarMaxSeeksForKey));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarOldAlterTable())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarOldAlterTable", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_OLD_ALTER_TABLE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarOldAlterTable));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarTableDefinitionCache())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarTableDefinitionCache", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_TABLE_DEFINITION_CACHE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarTableDefinitionCache));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbSortBufferSize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbSortBufferSize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_SORT_BUFFER_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbSortBufferSize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarKeyCacheBlockSize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarKeyCacheBlockSize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_KEY_CACHE_BLOCK_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarKeyCacheBlockSize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarCharacterSetsDir())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarCharacterSetsDir", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_CHARACTER_SETS_DIR))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarCharacterSetsDir));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarDateFormat())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarDateFormat", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_DATE_FORMAT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarDateFormat));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarDatetimeFormat())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarDatetimeFormat", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_DATETIME_FORMAT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarDatetimeFormat));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarDisconnectOnExpiredPassword())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarDisconnectOnExpiredPassword", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_DISCONNECT_ON_EXPIRED_PASSWORD))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarDisconnectOnExpiredPassword));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarExternalUser())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarExternalUser", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_EXTERNAL_USER))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarExternalUser));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarHaveCrypt())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarHaveCrypt", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_HAVE_CRYPT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarHaveCrypt));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarLanguage())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarLanguage", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_LANGUAGE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarLanguage));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarLcMessagesDir())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarLcMessagesDir", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_LC_MESSAGES_DIR))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarLcMessagesDir));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarLowerCaseFileSystem())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarLowerCaseFileSystem", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_LOWER_CASE_FILE_SYSTEM))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarLowerCaseFileSystem));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarMaxDigestLength())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarMaxDigestLength", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_MAX_DIGEST_LENGTH))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarMaxDigestLength));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNdbinfoDatabase())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNdbinfoDatabase", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_NDBINFO_DATABASE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNdbinfoDatabase));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNdbinfoTablePrefix())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNdbinfoTablePrefix", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_NDBINFO_TABLE_PREFIX))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNdbinfoTablePrefix));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNdbinfoVersion())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNdbinfoVersion", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_NDBINFO_VERSION))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNdbinfoVersion));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNdbBatchSize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNdbBatchSize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_NDB_BATCH_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNdbBatchSize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNdbClusterConnectionPool())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNdbClusterConnectionPool", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_NDB_CLUSTER_CONNECTION_POOL))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNdbClusterConnectionPool));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNdbClusterConnectionPoolNodeids())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNdbClusterConnectionPoolNodeids", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_NDB_CLUSTER_CONNECTION_POOL_NODEIDS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNdbClusterConnectionPoolNodeids));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNdbLogApplyStatus())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNdbLogApplyStatus", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_NDB_LOG_APPLY_STATUS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNdbLogApplyStatus));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNdbLogBin())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNdbLogBin", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_NDB_LOG_BIN))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNdbLogBin));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNdbLogFailTerminate())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNdbLogFailTerminate", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_NDB_LOG_FAIL_TERMINATE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNdbLogFailTerminate));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNdbLogOrig())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNdbLogOrig", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_NDB_LOG_ORIG))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNdbLogOrig));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNdbLogTransactionId())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNdbLogTransactionId", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_NDB_LOG_TRANSACTION_ID))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNdbLogTransactionId));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNdbOptimizedNodeSelection())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNdbOptimizedNodeSelection", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_NDB_OPTIMIZED_NODE_SELECTION))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNdbOptimizedNodeSelection));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNdbSystemName())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNdbSystemName", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_NDB_SYSTEM_NAME))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNdbSystemName));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNdbUseCopyingAlterTable())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNdbUseCopyingAlterTable", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_NDB_USE_COPYING_ALTER_TABLE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNdbUseCopyingAlterTable));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNdbVersionString())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNdbVersionString", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_NDB_VERSION_STRING))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNdbVersionString));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNdbWaitConnected())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNdbWaitConnected", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_NDB_WAIT_CONNECTED))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNdbWaitConnected));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNdbWaitSetup())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNdbWaitSetup", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_NDB_WAIT_SETUP))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNdbWaitSetup));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarProxyUser())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarProxyUser", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_PROXY_USER))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarProxyUser));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarSha256PasswordAutoGenerateRsaKeys())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarSha256PasswordAutoGenerateRsaKeys", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SHA256_PASSWORD_AUTO_GENERATE_RSA_KEYS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarSha256PasswordAutoGenerateRsaKeys));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarSha256PasswordPrivateKeyPath())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarSha256PasswordPrivateKeyPath", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SHA256_PASSWORD_PRIVATE_KEY_PATH))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarSha256PasswordPrivateKeyPath));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarSha256PasswordPublicKeyPath())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarSha256PasswordPublicKeyPath", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SHA256_PASSWORD_PUBLIC_KEY_PATH))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarSha256PasswordPublicKeyPath));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarSkipShowDatabase())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarSkipShowDatabase", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SKIP_SHOW_DATABASE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarSkipShowDatabase));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarBigTables())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarBigTables", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_BIG_TABLES))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarBigTables));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarCheckProxyUsers())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarCheckProxyUsers", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_CHECK_PROXY_USERS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarCheckProxyUsers));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarDefaultWeekFormat())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarDefaultWeekFormat", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_DEFAULT_WEEK_FORMAT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarDefaultWeekFormat));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarDelayedInsertTimeout())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarDelayedInsertTimeout", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_DELAYED_INSERT_TIMEOUT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarDelayedInsertTimeout));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarDelayedQueueSize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarDelayedQueueSize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_DELAYED_QUEUE_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarDelayedQueueSize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarEqRangeIndexDiveLimit())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarEqRangeIndexDiveLimit", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_EQ_RANGE_INDEX_DIVE_LIMIT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarEqRangeIndexDiveLimit));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbStatsAutoRecalc())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbStatsAutoRecalc", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_STATS_AUTO_RECALC))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbStatsAutoRecalc));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbStatsIncludeDeleteMarked())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbStatsIncludeDeleteMarked", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_STATS_INCLUDE_DELETE_MARKED))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbStatsIncludeDeleteMarked));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbStatsMethod())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbStatsMethod", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_STATS_METHOD))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbStatsMethod));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbStatsOnMetadata())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbStatsOnMetadata", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_STATS_ON_METADATA))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbStatsOnMetadata));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarVersionTokensSession())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarVersionTokensSession", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_VERSION_TOKENS_SESSION))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarVersionTokensSession));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbStatsPersistentSamplePages())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbStatsPersistentSamplePages", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_STATS_PERSISTENT_SAMPLE_PAGES))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbStatsPersistentSamplePages));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbStatsSamplePages())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbStatsSamplePages", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_STATS_SAMPLE_PAGES))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbStatsSamplePages));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarInnodbStatsTransientSamplePages())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarInnodbStatsTransientSamplePages", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_INNODB_STATS_TRANSIENT_SAMPLE_PAGES))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarInnodbStatsTransientSamplePages));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarOptimizerSwitch())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarOptimizerSwitch", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_OPTIMIZER_SWITCH))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarOptimizerSwitch));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarMaxConnectErrors())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarMaxConnectErrors", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_MAX_CONNECT_ERRORS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarMaxConnectErrors));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarMysqlFirewallMode())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarMysqlFirewallMode", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_MYSQL_FIREWALL_MODE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarMysqlFirewallMode));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarMysqlFirewallTrace())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarMysqlFirewallTrace", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_MYSQL_FIREWALL_TRACE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarMysqlFirewallTrace));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarMysqlNativePasswordProxyUsers())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarMysqlNativePasswordProxyUsers", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_MYSQL_NATIVE_PASSWORD_PROXY_USERS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarMysqlNativePasswordProxyUsers));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNetRetryCount())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNetRetryCount", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_NET_RETRY_COUNT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNetRetryCount));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNew())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNew", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_NEW))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNew));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarOldPasswords())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarOldPasswords", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_OLD_PASSWORDS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarOldPasswords));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarOptimizerPruneLevel())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarOptimizerPruneLevel", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_OPTIMIZER_PRUNE_LEVEL))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarOptimizerPruneLevel));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarOptimizerSearchDepth())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarOptimizerSearchDepth", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_OPTIMIZER_SEARCH_DEPTH))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarOptimizerSearchDepth));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarOptimizerTrace())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarOptimizerTrace", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_OPTIMIZER_TRACE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarOptimizerTrace));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarOptimizerTraceFeatures())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarOptimizerTraceFeatures", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_OPTIMIZER_TRACE_FEATURES))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarOptimizerTraceFeatures));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarOptimizerTraceLimit())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarOptimizerTraceLimit", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_OPTIMIZER_TRACE_LIMIT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarOptimizerTraceLimit));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarOptimizerTraceMaxMemSize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarOptimizerTraceMaxMemSize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_OPTIMIZER_TRACE_MAX_MEM_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarOptimizerTraceMaxMemSize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarOptimizerTraceOffset())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarOptimizerTraceOffset", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_OPTIMIZER_TRACE_OFFSET))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarOptimizerTraceOffset));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarParserMaxMemSize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarParserMaxMemSize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_PARSER_MAX_MEM_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarParserMaxMemSize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarRandSeed1())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarRandSeed1", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_RAND_SEED1))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarRandSeed1));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarRandSeed2())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarRandSeed2", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_RAND_SEED2))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarRandSeed2));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarRangeAllocBlockSize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarRangeAllocBlockSize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_RANGE_ALLOC_BLOCK_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarRangeAllocBlockSize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarRangeOptimizerMaxMemSize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarRangeOptimizerMaxMemSize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_RANGE_OPTIMIZER_MAX_MEM_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarRangeOptimizerMaxMemSize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarRewriterEnabled())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarRewriterEnabled", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_REWRITER_ENABLED))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarRewriterEnabled));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarRewriterVerbose())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarRewriterVerbose", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_REWRITER_VERBOSE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarRewriterVerbose));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarSecureAuth())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarSecureAuth", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SECURE_AUTH))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarSecureAuth));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarSha256PasswordProxyUsers())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarSha256PasswordProxyUsers", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SHA256_PASSWORD_PROXY_USERS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarSha256PasswordProxyUsers));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarShowCompatibility56())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarShowCompatibility56", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SHOW_COMPATIBILITY_56))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarShowCompatibility56));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarShowCreateTableVerbosity())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarShowCreateTableVerbosity", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SHOW_CREATE_TABLE_VERBOSITY))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarShowCreateTableVerbosity));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarShowOldTemporals())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarShowOldTemporals", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SHOW_OLD_TEMPORALS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarShowOldTemporals));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarSqlBigSelects())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarSqlBigSelects", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SQL_BIG_SELECTS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarSqlBigSelects));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarUpdatableViewsWithLimit())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarUpdatableViewsWithLimit", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_UPDATABLE_VIEWS_WITH_LIMIT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarUpdatableViewsWithLimit));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarValidatePasswordDictionaryFile())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarValidatePasswordDictionaryFile", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_VALIDATE_PASSWORD_DICTIONARY_FILE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarValidatePasswordDictionaryFile));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarDelayedInsertLimit())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarDelayedInsertLimit", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_DELAYED_INSERT_LIMIT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarDelayedInsertLimit));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarNdbVersion())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarNdbVersion", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_NDB_VERSION))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarNdbVersion));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarAutoGenerateCerts())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarAutoGenerateCerts", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_AUTO_GENERATE_CERTS))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarAutoGenerateCerts));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarOptimizerCostBasedTransformation())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarOptimizerCostBasedTransformation", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR__OPTIMIZER_COST_BASED_TRANSFORMATION))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarOptimizerCostBasedTransformation));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarRangeIndexDiveLimit())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarRangeIndexDiveLimit", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_RANGE_INDEX_DIVE_LIMIT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarRangeIndexDiveLimit));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarPartitionIndexDiveLimit())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarPartitionIndexDiveLimit", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_PARTITION_INDEX_DIVE_LIMIT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarPartitionIndexDiveLimit));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarPidFile())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarPidFile", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_PID_FILE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarPidFile));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarPort())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarPort", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_PORT))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarPort));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarSocket())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarSocket", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_SOCKET))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarSocket));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarEnableOptimizerRowgoal())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarEnableOptimizerRowgoal", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_ENABLE_OPTIMIZER_ROWGOAL))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarEnableOptimizerRowgoal));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarObIvfNprobes())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarObIvfNprobes", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_OB_IVF_NPROBES))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarObIvfNprobes));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarObHnswExtraInfoMaxSize())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarObHnswExtraInfoMaxSize", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_OB_HNSW_EXTRA_INFO_MAX_SIZE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarObHnswExtraInfoMaxSize));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarPushJoinPredicate())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarPushJoinPredicate", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR__PUSH_JOIN_PREDICATE))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarPushJoinPredicate));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(sys_var_ptr = new (ptr)ObSysVarObSparseDropRatioSearch())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to new ObSysVarObSparseDropRatioSearch", K(ret));
      } else {
        store_buf_[share::ObSysVarsToIdxMap::get_store_idx(static_cast<int64_t>(share::SYS_VAR_OB_SPARSE_DROP_RATIO_SEARCH))] = sys_var_ptr;
        ptr = (void *)((char *)ptr + sizeof(ObSysVarObSparseDropRatioSearch));
      }
    }

  }
  return ret;
}

template <typename T>
static int create_one_sys_var(ObIAllocator &allocator_, ObBasicSysVar *&sys_var_ptr, const char *cls_name)
{
  int ret = OB_SUCCESS;
  void *ptr = NULL;
  if (OB_ISNULL(ptr = allocator_.alloc(sizeof(T)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_ERROR("fail to alloc sys var", K(ret), K(cls_name));
  } else if (OB_ISNULL(sys_var_ptr = new (ptr)T())) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_ERROR("fail to new sys var", K(ret), K(cls_name));
  }
  return ret;
}

int ObSysVarFactory::create_sys_var(ObIAllocator &allocator_, share::ObSysVarClassType sys_var_id,
                                        ObBasicSysVar *&sys_var_ptr)
{
  int ret = OB_SUCCESS;
  switch(sys_var_id) {
    case share::SYS_VAR_AUTO_INCREMENT_INCREMENT: {
      ret = create_one_sys_var<ObSysVarAutoIncrementIncrement>(allocator_, sys_var_ptr, "ObSysVarAutoIncrementIncrement");
      break;
    }
    case share::SYS_VAR_AUTO_INCREMENT_OFFSET: {
      ret = create_one_sys_var<ObSysVarAutoIncrementOffset>(allocator_, sys_var_ptr, "ObSysVarAutoIncrementOffset");
      break;
    }
    case share::SYS_VAR_AUTOCOMMIT: {
      ret = create_one_sys_var<ObSysVarAutocommit>(allocator_, sys_var_ptr, "ObSysVarAutocommit");
      break;
    }
    case share::SYS_VAR_CHARACTER_SET_CLIENT: {
      ret = create_one_sys_var<ObSysVarCharacterSetClient>(allocator_, sys_var_ptr, "ObSysVarCharacterSetClient");
      break;
    }
    case share::SYS_VAR_CHARACTER_SET_CONNECTION: {
      ret = create_one_sys_var<ObSysVarCharacterSetConnection>(allocator_, sys_var_ptr, "ObSysVarCharacterSetConnection");
      break;
    }
    case share::SYS_VAR_CHARACTER_SET_DATABASE: {
      ret = create_one_sys_var<ObSysVarCharacterSetDatabase>(allocator_, sys_var_ptr, "ObSysVarCharacterSetDatabase");
      break;
    }
    case share::SYS_VAR_CHARACTER_SET_RESULTS: {
      ret = create_one_sys_var<ObSysVarCharacterSetResults>(allocator_, sys_var_ptr, "ObSysVarCharacterSetResults");
      break;
    }
    case share::SYS_VAR_CHARACTER_SET_SERVER: {
      ret = create_one_sys_var<ObSysVarCharacterSetServer>(allocator_, sys_var_ptr, "ObSysVarCharacterSetServer");
      break;
    }
    case share::SYS_VAR_CHARACTER_SET_SYSTEM: {
      ret = create_one_sys_var<ObSysVarCharacterSetSystem>(allocator_, sys_var_ptr, "ObSysVarCharacterSetSystem");
      break;
    }
    case share::SYS_VAR_COLLATION_CONNECTION: {
      ret = create_one_sys_var<ObSysVarCollationConnection>(allocator_, sys_var_ptr, "ObSysVarCollationConnection");
      break;
    }
    case share::SYS_VAR_COLLATION_DATABASE: {
      ret = create_one_sys_var<ObSysVarCollationDatabase>(allocator_, sys_var_ptr, "ObSysVarCollationDatabase");
      break;
    }
    case share::SYS_VAR_COLLATION_SERVER: {
      ret = create_one_sys_var<ObSysVarCollationServer>(allocator_, sys_var_ptr, "ObSysVarCollationServer");
      break;
    }
    case share::SYS_VAR_INTERACTIVE_TIMEOUT: {
      ret = create_one_sys_var<ObSysVarInteractiveTimeout>(allocator_, sys_var_ptr, "ObSysVarInteractiveTimeout");
      break;
    }
    case share::SYS_VAR_LAST_INSERT_ID: {
      ret = create_one_sys_var<ObSysVarLastInsertId>(allocator_, sys_var_ptr, "ObSysVarLastInsertId");
      break;
    }
    case share::SYS_VAR_MAX_ALLOWED_PACKET: {
      ret = create_one_sys_var<ObSysVarMaxAllowedPacket>(allocator_, sys_var_ptr, "ObSysVarMaxAllowedPacket");
      break;
    }
    case share::SYS_VAR_SQL_MODE: {
      ret = create_one_sys_var<ObSysVarSqlMode>(allocator_, sys_var_ptr, "ObSysVarSqlMode");
      break;
    }
    case share::SYS_VAR_TIME_ZONE: {
      ret = create_one_sys_var<ObSysVarTimeZone>(allocator_, sys_var_ptr, "ObSysVarTimeZone");
      break;
    }
    case share::SYS_VAR_TX_ISOLATION: {
      ret = create_one_sys_var<ObSysVarTxIsolation>(allocator_, sys_var_ptr, "ObSysVarTxIsolation");
      break;
    }
    case share::SYS_VAR_VERSION_COMMENT: {
      ret = create_one_sys_var<ObSysVarVersionComment>(allocator_, sys_var_ptr, "ObSysVarVersionComment");
      break;
    }
    case share::SYS_VAR_WAIT_TIMEOUT: {
      ret = create_one_sys_var<ObSysVarWaitTimeout>(allocator_, sys_var_ptr, "ObSysVarWaitTimeout");
      break;
    }
    case share::SYS_VAR_BINLOG_ROW_IMAGE: {
      ret = create_one_sys_var<ObSysVarBinlogRowImage>(allocator_, sys_var_ptr, "ObSysVarBinlogRowImage");
      break;
    }
    case share::SYS_VAR_CHARACTER_SET_FILESYSTEM: {
      ret = create_one_sys_var<ObSysVarCharacterSetFilesystem>(allocator_, sys_var_ptr, "ObSysVarCharacterSetFilesystem");
      break;
    }
    case share::SYS_VAR_CONNECT_TIMEOUT: {
      ret = create_one_sys_var<ObSysVarConnectTimeout>(allocator_, sys_var_ptr, "ObSysVarConnectTimeout");
      break;
    }
    case share::SYS_VAR_DATADIR: {
      ret = create_one_sys_var<ObSysVarDatadir>(allocator_, sys_var_ptr, "ObSysVarDatadir");
      break;
    }
    case share::SYS_VAR_DEBUG_SYNC: {
      ret = create_one_sys_var<ObSysVarDebugSync>(allocator_, sys_var_ptr, "ObSysVarDebugSync");
      break;
    }
    case share::SYS_VAR_DIV_PRECISION_INCREMENT: {
      ret = create_one_sys_var<ObSysVarDivPrecisionIncrement>(allocator_, sys_var_ptr, "ObSysVarDivPrecisionIncrement");
      break;
    }
    case share::SYS_VAR_EXPLICIT_DEFAULTS_FOR_TIMESTAMP: {
      ret = create_one_sys_var<ObSysVarExplicitDefaultsForTimestamp>(allocator_, sys_var_ptr, "ObSysVarExplicitDefaultsForTimestamp");
      break;
    }
    case share::SYS_VAR_GROUP_CONCAT_MAX_LEN: {
      ret = create_one_sys_var<ObSysVarGroupConcatMaxLen>(allocator_, sys_var_ptr, "ObSysVarGroupConcatMaxLen");
      break;
    }
    case share::SYS_VAR_IDENTITY: {
      ret = create_one_sys_var<ObSysVarIdentity>(allocator_, sys_var_ptr, "ObSysVarIdentity");
      break;
    }
    case share::SYS_VAR_LOWER_CASE_TABLE_NAMES: {
      ret = create_one_sys_var<ObSysVarLowerCaseTableNames>(allocator_, sys_var_ptr, "ObSysVarLowerCaseTableNames");
      break;
    }
    case share::SYS_VAR_NET_READ_TIMEOUT: {
      ret = create_one_sys_var<ObSysVarNetReadTimeout>(allocator_, sys_var_ptr, "ObSysVarNetReadTimeout");
      break;
    }
    case share::SYS_VAR_NET_WRITE_TIMEOUT: {
      ret = create_one_sys_var<ObSysVarNetWriteTimeout>(allocator_, sys_var_ptr, "ObSysVarNetWriteTimeout");
      break;
    }
    case share::SYS_VAR_READ_ONLY: {
      ret = create_one_sys_var<ObSysVarReadOnly>(allocator_, sys_var_ptr, "ObSysVarReadOnly");
      break;
    }
    case share::SYS_VAR_SQL_AUTO_IS_NULL: {
      ret = create_one_sys_var<ObSysVarSqlAutoIsNull>(allocator_, sys_var_ptr, "ObSysVarSqlAutoIsNull");
      break;
    }
    case share::SYS_VAR_SQL_SELECT_LIMIT: {
      ret = create_one_sys_var<ObSysVarSqlSelectLimit>(allocator_, sys_var_ptr, "ObSysVarSqlSelectLimit");
      break;
    }
    case share::SYS_VAR_TIMESTAMP: {
      ret = create_one_sys_var<ObSysVarTimestamp>(allocator_, sys_var_ptr, "ObSysVarTimestamp");
      break;
    }
    case share::SYS_VAR_TX_READ_ONLY: {
      ret = create_one_sys_var<ObSysVarTxReadOnly>(allocator_, sys_var_ptr, "ObSysVarTxReadOnly");
      break;
    }
    case share::SYS_VAR_VERSION: {
      ret = create_one_sys_var<ObSysVarVersion>(allocator_, sys_var_ptr, "ObSysVarVersion");
      break;
    }
    case share::SYS_VAR_SQL_WARNINGS: {
      ret = create_one_sys_var<ObSysVarSqlWarnings>(allocator_, sys_var_ptr, "ObSysVarSqlWarnings");
      break;
    }
    case share::SYS_VAR_MAX_USER_CONNECTIONS: {
      ret = create_one_sys_var<ObSysVarMaxUserConnections>(allocator_, sys_var_ptr, "ObSysVarMaxUserConnections");
      break;
    }
    case share::SYS_VAR_INIT_CONNECT: {
      ret = create_one_sys_var<ObSysVarInitConnect>(allocator_, sys_var_ptr, "ObSysVarInitConnect");
      break;
    }
    case share::SYS_VAR_LICENSE: {
      ret = create_one_sys_var<ObSysVarLicense>(allocator_, sys_var_ptr, "ObSysVarLicense");
      break;
    }
    case share::SYS_VAR_NET_BUFFER_LENGTH: {
      ret = create_one_sys_var<ObSysVarNetBufferLength>(allocator_, sys_var_ptr, "ObSysVarNetBufferLength");
      break;
    }
    case share::SYS_VAR_SYSTEM_TIME_ZONE: {
      ret = create_one_sys_var<ObSysVarSystemTimeZone>(allocator_, sys_var_ptr, "ObSysVarSystemTimeZone");
      break;
    }
    case share::SYS_VAR_QUERY_CACHE_SIZE: {
      ret = create_one_sys_var<ObSysVarQueryCacheSize>(allocator_, sys_var_ptr, "ObSysVarQueryCacheSize");
      break;
    }
    case share::SYS_VAR_QUERY_CACHE_TYPE: {
      ret = create_one_sys_var<ObSysVarQueryCacheType>(allocator_, sys_var_ptr, "ObSysVarQueryCacheType");
      break;
    }
    case share::SYS_VAR_SQL_QUOTE_SHOW_CREATE: {
      ret = create_one_sys_var<ObSysVarSqlQuoteShowCreate>(allocator_, sys_var_ptr, "ObSysVarSqlQuoteShowCreate");
      break;
    }
    case share::SYS_VAR_MAX_SP_RECURSION_DEPTH: {
      ret = create_one_sys_var<ObSysVarMaxSpRecursionDepth>(allocator_, sys_var_ptr, "ObSysVarMaxSpRecursionDepth");
      break;
    }
    case share::SYS_VAR_SQL_SAFE_UPDATES: {
      ret = create_one_sys_var<ObSysVarSqlSafeUpdates>(allocator_, sys_var_ptr, "ObSysVarSqlSafeUpdates");
      break;
    }
    case share::SYS_VAR_CONCURRENT_INSERT: {
      ret = create_one_sys_var<ObSysVarConcurrentInsert>(allocator_, sys_var_ptr, "ObSysVarConcurrentInsert");
      break;
    }
    case share::SYS_VAR_DEFAULT_AUTHENTICATION_PLUGIN: {
      ret = create_one_sys_var<ObSysVarDefaultAuthenticationPlugin>(allocator_, sys_var_ptr, "ObSysVarDefaultAuthenticationPlugin");
      break;
    }
    case share::SYS_VAR_DISABLED_STORAGE_ENGINES: {
      ret = create_one_sys_var<ObSysVarDisabledStorageEngines>(allocator_, sys_var_ptr, "ObSysVarDisabledStorageEngines");
      break;
    }
    case share::SYS_VAR_ERROR_COUNT: {
      ret = create_one_sys_var<ObSysVarErrorCount>(allocator_, sys_var_ptr, "ObSysVarErrorCount");
      break;
    }
    case share::SYS_VAR_GENERAL_LOG: {
      ret = create_one_sys_var<ObSysVarGeneralLog>(allocator_, sys_var_ptr, "ObSysVarGeneralLog");
      break;
    }
    case share::SYS_VAR_HAVE_OPENSSL: {
      ret = create_one_sys_var<ObSysVarHaveOpenssl>(allocator_, sys_var_ptr, "ObSysVarHaveOpenssl");
      break;
    }
    case share::SYS_VAR_HAVE_PROFILING: {
      ret = create_one_sys_var<ObSysVarHaveProfiling>(allocator_, sys_var_ptr, "ObSysVarHaveProfiling");
      break;
    }
    case share::SYS_VAR_HAVE_SSL: {
      ret = create_one_sys_var<ObSysVarHaveSsl>(allocator_, sys_var_ptr, "ObSysVarHaveSsl");
      break;
    }
    case share::SYS_VAR_HOSTNAME: {
      ret = create_one_sys_var<ObSysVarHostname>(allocator_, sys_var_ptr, "ObSysVarHostname");
      break;
    }
    case share::SYS_VAR_LC_MESSAGES: {
      ret = create_one_sys_var<ObSysVarLcMessages>(allocator_, sys_var_ptr, "ObSysVarLcMessages");
      break;
    }
    case share::SYS_VAR_LOCAL_INFILE: {
      ret = create_one_sys_var<ObSysVarLocalInfile>(allocator_, sys_var_ptr, "ObSysVarLocalInfile");
      break;
    }
    case share::SYS_VAR_LOCK_WAIT_TIMEOUT: {
      ret = create_one_sys_var<ObSysVarLockWaitTimeout>(allocator_, sys_var_ptr, "ObSysVarLockWaitTimeout");
      break;
    }
    case share::SYS_VAR_LONG_QUERY_TIME: {
      ret = create_one_sys_var<ObSysVarLongQueryTime>(allocator_, sys_var_ptr, "ObSysVarLongQueryTime");
      break;
    }
    case share::SYS_VAR_MAX_CONNECTIONS: {
      ret = create_one_sys_var<ObSysVarMaxConnections>(allocator_, sys_var_ptr, "ObSysVarMaxConnections");
      break;
    }
    case share::SYS_VAR_MAX_EXECUTION_TIME: {
      ret = create_one_sys_var<ObSysVarMaxExecutionTime>(allocator_, sys_var_ptr, "ObSysVarMaxExecutionTime");
      break;
    }
    case share::SYS_VAR_PROTOCOL_VERSION: {
      ret = create_one_sys_var<ObSysVarProtocolVersion>(allocator_, sys_var_ptr, "ObSysVarProtocolVersion");
      break;
    }
    case share::SYS_VAR_SERVER_ID: {
      ret = create_one_sys_var<ObSysVarServerId>(allocator_, sys_var_ptr, "ObSysVarServerId");
      break;
    }
    case share::SYS_VAR_SSL_CA: {
      ret = create_one_sys_var<ObSysVarSslCa>(allocator_, sys_var_ptr, "ObSysVarSslCa");
      break;
    }
    case share::SYS_VAR_SSL_CAPATH: {
      ret = create_one_sys_var<ObSysVarSslCapath>(allocator_, sys_var_ptr, "ObSysVarSslCapath");
      break;
    }
    case share::SYS_VAR_SSL_CERT: {
      ret = create_one_sys_var<ObSysVarSslCert>(allocator_, sys_var_ptr, "ObSysVarSslCert");
      break;
    }
    case share::SYS_VAR_SSL_CIPHER: {
      ret = create_one_sys_var<ObSysVarSslCipher>(allocator_, sys_var_ptr, "ObSysVarSslCipher");
      break;
    }
    case share::SYS_VAR_SSL_CRL: {
      ret = create_one_sys_var<ObSysVarSslCrl>(allocator_, sys_var_ptr, "ObSysVarSslCrl");
      break;
    }
    case share::SYS_VAR_SSL_CRLPATH: {
      ret = create_one_sys_var<ObSysVarSslCrlpath>(allocator_, sys_var_ptr, "ObSysVarSslCrlpath");
      break;
    }
    case share::SYS_VAR_SSL_KEY: {
      ret = create_one_sys_var<ObSysVarSslKey>(allocator_, sys_var_ptr, "ObSysVarSslKey");
      break;
    }
    case share::SYS_VAR_TIME_FORMAT: {
      ret = create_one_sys_var<ObSysVarTimeFormat>(allocator_, sys_var_ptr, "ObSysVarTimeFormat");
      break;
    }
    case share::SYS_VAR_TLS_VERSION: {
      ret = create_one_sys_var<ObSysVarTlsVersion>(allocator_, sys_var_ptr, "ObSysVarTlsVersion");
      break;
    }
    case share::SYS_VAR_TMP_TABLE_SIZE: {
      ret = create_one_sys_var<ObSysVarTmpTableSize>(allocator_, sys_var_ptr, "ObSysVarTmpTableSize");
      break;
    }
    case share::SYS_VAR_TMPDIR: {
      ret = create_one_sys_var<ObSysVarTmpdir>(allocator_, sys_var_ptr, "ObSysVarTmpdir");
      break;
    }
    case share::SYS_VAR_UNIQUE_CHECKS: {
      ret = create_one_sys_var<ObSysVarUniqueChecks>(allocator_, sys_var_ptr, "ObSysVarUniqueChecks");
      break;
    }
    case share::SYS_VAR_VERSION_COMPILE_MACHINE: {
      ret = create_one_sys_var<ObSysVarVersionCompileMachine>(allocator_, sys_var_ptr, "ObSysVarVersionCompileMachine");
      break;
    }
    case share::SYS_VAR_VERSION_COMPILE_OS: {
      ret = create_one_sys_var<ObSysVarVersionCompileOs>(allocator_, sys_var_ptr, "ObSysVarVersionCompileOs");
      break;
    }
    case share::SYS_VAR_WARNING_COUNT: {
      ret = create_one_sys_var<ObSysVarWarningCount>(allocator_, sys_var_ptr, "ObSysVarWarningCount");
      break;
    }
    case share::SYS_VAR_SESSION_TRACK_SCHEMA: {
      ret = create_one_sys_var<ObSysVarSessionTrackSchema>(allocator_, sys_var_ptr, "ObSysVarSessionTrackSchema");
      break;
    }
    case share::SYS_VAR_SESSION_TRACK_SYSTEM_VARIABLES: {
      ret = create_one_sys_var<ObSysVarSessionTrackSystemVariables>(allocator_, sys_var_ptr, "ObSysVarSessionTrackSystemVariables");
      break;
    }
    case share::SYS_VAR_SESSION_TRACK_STATE_CHANGE: {
      ret = create_one_sys_var<ObSysVarSessionTrackStateChange>(allocator_, sys_var_ptr, "ObSysVarSessionTrackStateChange");
      break;
    }
    case share::SYS_VAR_HAVE_QUERY_CACHE: {
      ret = create_one_sys_var<ObSysVarHaveQueryCache>(allocator_, sys_var_ptr, "ObSysVarHaveQueryCache");
      break;
    }
    case share::SYS_VAR_QUERY_CACHE_LIMIT: {
      ret = create_one_sys_var<ObSysVarQueryCacheLimit>(allocator_, sys_var_ptr, "ObSysVarQueryCacheLimit");
      break;
    }
    case share::SYS_VAR_QUERY_CACHE_MIN_RES_UNIT: {
      ret = create_one_sys_var<ObSysVarQueryCacheMinResUnit>(allocator_, sys_var_ptr, "ObSysVarQueryCacheMinResUnit");
      break;
    }
    case share::SYS_VAR_QUERY_CACHE_WLOCK_INVALIDATE: {
      ret = create_one_sys_var<ObSysVarQueryCacheWlockInvalidate>(allocator_, sys_var_ptr, "ObSysVarQueryCacheWlockInvalidate");
      break;
    }
    case share::SYS_VAR_BINLOG_FORMAT: {
      ret = create_one_sys_var<ObSysVarBinlogFormat>(allocator_, sys_var_ptr, "ObSysVarBinlogFormat");
      break;
    }
    case share::SYS_VAR_BINLOG_CHECKSUM: {
      ret = create_one_sys_var<ObSysVarBinlogChecksum>(allocator_, sys_var_ptr, "ObSysVarBinlogChecksum");
      break;
    }
    case share::SYS_VAR_BINLOG_ROWS_QUERY_LOG_EVENTS: {
      ret = create_one_sys_var<ObSysVarBinlogRowsQueryLogEvents>(allocator_, sys_var_ptr, "ObSysVarBinlogRowsQueryLogEvents");
      break;
    }
    case share::SYS_VAR_LOG_BIN: {
      ret = create_one_sys_var<ObSysVarLogBin>(allocator_, sys_var_ptr, "ObSysVarLogBin");
      break;
    }
    case share::SYS_VAR_SERVER_UUID: {
      ret = create_one_sys_var<ObSysVarServerUuid>(allocator_, sys_var_ptr, "ObSysVarServerUuid");
      break;
    }
    case share::SYS_VAR_DEFAULT_STORAGE_ENGINE: {
      ret = create_one_sys_var<ObSysVarDefaultStorageEngine>(allocator_, sys_var_ptr, "ObSysVarDefaultStorageEngine");
      break;
    }
    case share::SYS_VAR_CTE_MAX_RECURSION_DEPTH: {
      ret = create_one_sys_var<ObSysVarCteMaxRecursionDepth>(allocator_, sys_var_ptr, "ObSysVarCteMaxRecursionDepth");
      break;
    }
    case share::SYS_VAR_REGEXP_STACK_LIMIT: {
      ret = create_one_sys_var<ObSysVarRegexpStackLimit>(allocator_, sys_var_ptr, "ObSysVarRegexpStackLimit");
      break;
    }
    case share::SYS_VAR_REGEXP_TIME_LIMIT: {
      ret = create_one_sys_var<ObSysVarRegexpTimeLimit>(allocator_, sys_var_ptr, "ObSysVarRegexpTimeLimit");
      break;
    }
    case share::SYS_VAR_PROFILING: {
      ret = create_one_sys_var<ObSysVarProfiling>(allocator_, sys_var_ptr, "ObSysVarProfiling");
      break;
    }
    case share::SYS_VAR_PROFILING_HISTORY_SIZE: {
      ret = create_one_sys_var<ObSysVarProfilingHistorySize>(allocator_, sys_var_ptr, "ObSysVarProfilingHistorySize");
      break;
    }
    case share::SYS_VAR_OB_LOG_LEVEL: {
      ret = create_one_sys_var<ObSysVarObLogLevel>(allocator_, sys_var_ptr, "ObSysVarObLogLevel");
      break;
    }
    case share::SYS_VAR_OB_QUERY_TIMEOUT: {
      ret = create_one_sys_var<ObSysVarObQueryTimeout>(allocator_, sys_var_ptr, "ObSysVarObQueryTimeout");
      break;
    }
    case share::SYS_VAR_OB_READ_CONSISTENCY: {
      ret = create_one_sys_var<ObSysVarObReadConsistency>(allocator_, sys_var_ptr, "ObSysVarObReadConsistency");
      break;
    }
    case share::SYS_VAR_OB_ENABLE_TRANSFORMATION: {
      ret = create_one_sys_var<ObSysVarObEnableTransformation>(allocator_, sys_var_ptr, "ObSysVarObEnableTransformation");
      break;
    }
    case share::SYS_VAR_OB_TRX_TIMEOUT: {
      ret = create_one_sys_var<ObSysVarObTrxTimeout>(allocator_, sys_var_ptr, "ObSysVarObTrxTimeout");
      break;
    }
    case share::SYS_VAR_OB_ENABLE_PLAN_CACHE: {
      ret = create_one_sys_var<ObSysVarObEnablePlanCache>(allocator_, sys_var_ptr, "ObSysVarObEnablePlanCache");
      break;
    }
    case share::SYS_VAR_OB_ENABLE_INDEX_DIRECT_SELECT: {
      ret = create_one_sys_var<ObSysVarObEnableIndexDirectSelect>(allocator_, sys_var_ptr, "ObSysVarObEnableIndexDirectSelect");
      break;
    }
    case share::SYS_VAR_OB_ENABLE_AGGREGATION_PUSHDOWN: {
      ret = create_one_sys_var<ObSysVarObEnableAggregationPushdown>(allocator_, sys_var_ptr, "ObSysVarObEnableAggregationPushdown");
      break;
    }
    case share::SYS_VAR_OB_GLOBAL_DEBUG_SYNC: {
      ret = create_one_sys_var<ObSysVarObGlobalDebugSync>(allocator_, sys_var_ptr, "ObSysVarObGlobalDebugSync");
      break;
    }
    case share::SYS_VAR_OB_ENABLE_SHOW_TRACE: {
      ret = create_one_sys_var<ObSysVarObEnableShowTrace>(allocator_, sys_var_ptr, "ObSysVarObEnableShowTrace");
      break;
    }
    case share::SYS_VAR_OB_PLAN_CACHE_PERCENTAGE: {
      ret = create_one_sys_var<ObSysVarObPlanCachePercentage>(allocator_, sys_var_ptr, "ObSysVarObPlanCachePercentage");
      break;
    }
    case share::SYS_VAR_OB_PLAN_CACHE_EVICT_HIGH_PERCENTAGE: {
      ret = create_one_sys_var<ObSysVarObPlanCacheEvictHighPercentage>(allocator_, sys_var_ptr, "ObSysVarObPlanCacheEvictHighPercentage");
      break;
    }
    case share::SYS_VAR_OB_PLAN_CACHE_EVICT_LOW_PERCENTAGE: {
      ret = create_one_sys_var<ObSysVarObPlanCacheEvictLowPercentage>(allocator_, sys_var_ptr, "ObSysVarObPlanCacheEvictLowPercentage");
      break;
    }
    case share::SYS_VAR_RECYCLEBIN: {
      ret = create_one_sys_var<ObSysVarRecyclebin>(allocator_, sys_var_ptr, "ObSysVarRecyclebin");
      break;
    }
    case share::SYS_VAR_IS_RESULT_ACCURATE: {
      ret = create_one_sys_var<ObSysVarIsResultAccurate>(allocator_, sys_var_ptr, "ObSysVarIsResultAccurate");
      break;
    }
    case share::SYS_VAR_ERROR_ON_OVERLAP_TIME: {
      ret = create_one_sys_var<ObSysVarErrorOnOverlapTime>(allocator_, sys_var_ptr, "ObSysVarErrorOnOverlapTime");
      break;
    }
    case share::SYS_VAR_OB_SQL_WORK_AREA_PERCENTAGE: {
      ret = create_one_sys_var<ObSysVarObSqlWorkAreaPercentage>(allocator_, sys_var_ptr, "ObSysVarObSqlWorkAreaPercentage");
      break;
    }
    case share::SYS_VAR_FOREIGN_KEY_CHECKS: {
      ret = create_one_sys_var<ObSysVarForeignKeyChecks>(allocator_, sys_var_ptr, "ObSysVarForeignKeyChecks");
      break;
    }
    case share::SYS_VAR_OB_TCP_INVITED_NODES: {
      ret = create_one_sys_var<ObSysVarObTcpInvitedNodes>(allocator_, sys_var_ptr, "ObSysVarObTcpInvitedNodes");
      break;
    }
    case share::SYS_VAR_AUTO_INCREMENT_CACHE_SIZE: {
      ret = create_one_sys_var<ObSysVarAutoIncrementCacheSize>(allocator_, sys_var_ptr, "ObSysVarAutoIncrementCacheSize");
      break;
    }
    case share::SYS_VAR_PARALLEL_SERVERS_TARGET: {
      ret = create_one_sys_var<ObSysVarParallelServersTarget>(allocator_, sys_var_ptr, "ObSysVarParallelServersTarget");
      break;
    }
    case share::SYS_VAR_OB_TRX_IDLE_TIMEOUT: {
      ret = create_one_sys_var<ObSysVarObTrxIdleTimeout>(allocator_, sys_var_ptr, "ObSysVarObTrxIdleTimeout");
      break;
    }
    case share::SYS_VAR_BLOCK_ENCRYPTION_MODE: {
      ret = create_one_sys_var<ObSysVarBlockEncryptionMode>(allocator_, sys_var_ptr, "ObSysVarBlockEncryptionMode");
      break;
    }
    case share::SYS_VAR__NLJ_BATCHING_ENABLED: {
      ret = create_one_sys_var<ObSysVarNljBatchingEnabled>(allocator_, sys_var_ptr, "ObSysVarNljBatchingEnabled");
      break;
    }
    case share::SYS_VAR_TRANSACTION_ISOLATION: {
      ret = create_one_sys_var<ObSysVarTransactionIsolation>(allocator_, sys_var_ptr, "ObSysVarTransactionIsolation");
      break;
    }
    case share::SYS_VAR_OB_TRX_LOCK_TIMEOUT: {
      ret = create_one_sys_var<ObSysVarObTrxLockTimeout>(allocator_, sys_var_ptr, "ObSysVarObTrxLockTimeout");
      break;
    }
    case share::SYS_VAR_VALIDATE_PASSWORD_CHECK_USER_NAME: {
      ret = create_one_sys_var<ObSysVarValidatePasswordCheckUserName>(allocator_, sys_var_ptr, "ObSysVarValidatePasswordCheckUserName");
      break;
    }
    case share::SYS_VAR_VALIDATE_PASSWORD_LENGTH: {
      ret = create_one_sys_var<ObSysVarValidatePasswordLength>(allocator_, sys_var_ptr, "ObSysVarValidatePasswordLength");
      break;
    }
    case share::SYS_VAR_VALIDATE_PASSWORD_MIXED_CASE_COUNT: {
      ret = create_one_sys_var<ObSysVarValidatePasswordMixedCaseCount>(allocator_, sys_var_ptr, "ObSysVarValidatePasswordMixedCaseCount");
      break;
    }
    case share::SYS_VAR_VALIDATE_PASSWORD_NUMBER_COUNT: {
      ret = create_one_sys_var<ObSysVarValidatePasswordNumberCount>(allocator_, sys_var_ptr, "ObSysVarValidatePasswordNumberCount");
      break;
    }
    case share::SYS_VAR_VALIDATE_PASSWORD_POLICY: {
      ret = create_one_sys_var<ObSysVarValidatePasswordPolicy>(allocator_, sys_var_ptr, "ObSysVarValidatePasswordPolicy");
      break;
    }
    case share::SYS_VAR_VALIDATE_PASSWORD_SPECIAL_CHAR_COUNT: {
      ret = create_one_sys_var<ObSysVarValidatePasswordSpecialCharCount>(allocator_, sys_var_ptr, "ObSysVarValidatePasswordSpecialCharCount");
      break;
    }
    case share::SYS_VAR_DEFAULT_PASSWORD_LIFETIME: {
      ret = create_one_sys_var<ObSysVarDefaultPasswordLifetime>(allocator_, sys_var_ptr, "ObSysVarDefaultPasswordLifetime");
      break;
    }
    case share::SYS_VAR__ENABLE_PARALLEL_DML: {
      ret = create_one_sys_var<ObSysVarEnableParallelDml>(allocator_, sys_var_ptr, "ObSysVarEnableParallelDml");
      break;
    }
    case share::SYS_VAR_SECURE_FILE_PRIV: {
      ret = create_one_sys_var<ObSysVarSecureFilePriv>(allocator_, sys_var_ptr, "ObSysVarSecureFilePriv");
      break;
    }
    case share::SYS_VAR__ENABLE_PARALLEL_QUERY: {
      ret = create_one_sys_var<ObSysVarEnableParallelQuery>(allocator_, sys_var_ptr, "ObSysVarEnableParallelQuery");
      break;
    }
    case share::SYS_VAR__FORCE_PARALLEL_QUERY_DOP: {
      ret = create_one_sys_var<ObSysVarForceParallelQueryDop>(allocator_, sys_var_ptr, "ObSysVarForceParallelQueryDop");
      break;
    }
    case share::SYS_VAR__FORCE_PARALLEL_DML_DOP: {
      ret = create_one_sys_var<ObSysVarForceParallelDmlDop>(allocator_, sys_var_ptr, "ObSysVarForceParallelDmlDop");
      break;
    }
    case share::SYS_VAR_OB_PL_BLOCK_TIMEOUT: {
      ret = create_one_sys_var<ObSysVarObPlBlockTimeout>(allocator_, sys_var_ptr, "ObSysVarObPlBlockTimeout");
      break;
    }
    case share::SYS_VAR_TRANSACTION_READ_ONLY: {
      ret = create_one_sys_var<ObSysVarTransactionReadOnly>(allocator_, sys_var_ptr, "ObSysVarTransactionReadOnly");
      break;
    }
    case share::SYS_VAR_PERFORMANCE_SCHEMA: {
      ret = create_one_sys_var<ObSysVarPerformanceSchema>(allocator_, sys_var_ptr, "ObSysVarPerformanceSchema");
      break;
    }
    case share::SYS_VAR__ENABLE_PARALLEL_DDL: {
      ret = create_one_sys_var<ObSysVarEnableParallelDdl>(allocator_, sys_var_ptr, "ObSysVarEnableParallelDdl");
      break;
    }
    case share::SYS_VAR__FORCE_PARALLEL_DDL_DOP: {
      ret = create_one_sys_var<ObSysVarForceParallelDdlDop>(allocator_, sys_var_ptr, "ObSysVarForceParallelDdlDop");
      break;
    }
    case share::SYS_VAR_CURSOR_SHARING: {
      ret = create_one_sys_var<ObSysVarCursorSharing>(allocator_, sys_var_ptr, "ObSysVarCursorSharing");
      break;
    }
    case share::SYS_VAR__AGGREGATION_OPTIMIZATION_SETTINGS: {
      ret = create_one_sys_var<ObSysVarAggregationOptimizationSettings>(allocator_, sys_var_ptr, "ObSysVarAggregationOptimizationSettings");
      break;
    }
    case share::SYS_VAR__PX_SHARED_HASH_JOIN: {
      ret = create_one_sys_var<ObSysVarPxSharedHashJoin>(allocator_, sys_var_ptr, "ObSysVarPxSharedHashJoin");
      break;
    }
    case share::SYS_VAR_SQL_NOTES: {
      ret = create_one_sys_var<ObSysVarSqlNotes>(allocator_, sys_var_ptr, "ObSysVarSqlNotes");
      break;
    }
    case share::SYS_VAR_INNODB_STRICT_MODE: {
      ret = create_one_sys_var<ObSysVarInnodbStrictMode>(allocator_, sys_var_ptr, "ObSysVarInnodbStrictMode");
      break;
    }
    case share::SYS_VAR__WINDOWFUNC_OPTIMIZATION_SETTINGS: {
      ret = create_one_sys_var<ObSysVarWindowfuncOptimizationSettings>(allocator_, sys_var_ptr, "ObSysVarWindowfuncOptimizationSettings");
      break;
    }
    case share::SYS_VAR_LOG_ROW_VALUE_OPTIONS: {
      ret = create_one_sys_var<ObSysVarLogRowValueOptions>(allocator_, sys_var_ptr, "ObSysVarLogRowValueOptions");
      break;
    }
    case share::SYS_VAR_OB_MAX_READ_STALE_TIME: {
      ret = create_one_sys_var<ObSysVarObMaxReadStaleTime>(allocator_, sys_var_ptr, "ObSysVarObMaxReadStaleTime");
      break;
    }
    case share::SYS_VAR__OPTIMIZER_GATHER_STATS_ON_LOAD: {
      ret = create_one_sys_var<ObSysVarOptimizerGatherStatsOnLoad>(allocator_, sys_var_ptr, "ObSysVarOptimizerGatherStatsOnLoad");
      break;
    }
    case share::SYS_VAR__SHOW_DDL_IN_COMPAT_MODE: {
      ret = create_one_sys_var<ObSysVarShowDdlInCompatMode>(allocator_, sys_var_ptr, "ObSysVarShowDdlInCompatMode");
      break;
    }
    case share::SYS_VAR_PARALLEL_DEGREE_POLICY: {
      ret = create_one_sys_var<ObSysVarParallelDegreePolicy>(allocator_, sys_var_ptr, "ObSysVarParallelDegreePolicy");
      break;
    }
    case share::SYS_VAR_PARALLEL_DEGREE_LIMIT: {
      ret = create_one_sys_var<ObSysVarParallelDegreeLimit>(allocator_, sys_var_ptr, "ObSysVarParallelDegreeLimit");
      break;
    }
    case share::SYS_VAR_PARALLEL_MIN_SCAN_TIME_THRESHOLD: {
      ret = create_one_sys_var<ObSysVarParallelMinScanTimeThreshold>(allocator_, sys_var_ptr, "ObSysVarParallelMinScanTimeThreshold");
      break;
    }
    case share::SYS_VAR_OPTIMIZER_DYNAMIC_SAMPLING: {
      ret = create_one_sys_var<ObSysVarOptimizerDynamicSampling>(allocator_, sys_var_ptr, "ObSysVarOptimizerDynamicSampling");
      break;
    }
    case share::SYS_VAR_RUNTIME_FILTER_TYPE: {
      ret = create_one_sys_var<ObSysVarRuntimeFilterType>(allocator_, sys_var_ptr, "ObSysVarRuntimeFilterType");
      break;
    }
    case share::SYS_VAR_RUNTIME_FILTER_WAIT_TIME_MS: {
      ret = create_one_sys_var<ObSysVarRuntimeFilterWaitTimeMs>(allocator_, sys_var_ptr, "ObSysVarRuntimeFilterWaitTimeMs");
      break;
    }
    case share::SYS_VAR_RUNTIME_FILTER_MAX_IN_NUM: {
      ret = create_one_sys_var<ObSysVarRuntimeFilterMaxInNum>(allocator_, sys_var_ptr, "ObSysVarRuntimeFilterMaxInNum");
      break;
    }
    case share::SYS_VAR_RUNTIME_BLOOM_FILTER_MAX_SIZE: {
      ret = create_one_sys_var<ObSysVarRuntimeBloomFilterMaxSize>(allocator_, sys_var_ptr, "ObSysVarRuntimeBloomFilterMaxSize");
      break;
    }
    case share::SYS_VAR_AUTOMATIC_SP_PRIVILEGES: {
      ret = create_one_sys_var<ObSysVarAutomaticSpPrivileges>(allocator_, sys_var_ptr, "ObSysVarAutomaticSpPrivileges");
      break;
    }
    case share::SYS_VAR_OB_ENABLE_PL_CACHE: {
      ret = create_one_sys_var<ObSysVarObEnablePlCache>(allocator_, sys_var_ptr, "ObSysVarObEnablePlCache");
      break;
    }
    case share::SYS_VAR_OB_DEFAULT_LOB_INROW_THRESHOLD: {
      ret = create_one_sys_var<ObSysVarObDefaultLobInrowThreshold>(allocator_, sys_var_ptr, "ObSysVarObDefaultLobInrowThreshold");
      break;
    }
    case share::SYS_VAR__ENABLE_STORAGE_CARDINALITY_ESTIMATION: {
      ret = create_one_sys_var<ObSysVarEnableStorageCardinalityEstimation>(allocator_, sys_var_ptr, "ObSysVarEnableStorageCardinalityEstimation");
      break;
    }
    case share::SYS_VAR_LC_TIME_NAMES: {
      ret = create_one_sys_var<ObSysVarLcTimeNames>(allocator_, sys_var_ptr, "ObSysVarLcTimeNames");
      break;
    }
    case share::SYS_VAR_ACTIVATE_ALL_ROLES_ON_LOGIN: {
      ret = create_one_sys_var<ObSysVarActivateAllRolesOnLogin>(allocator_, sys_var_ptr, "ObSysVarActivateAllRolesOnLogin");
      break;
    }
    case share::SYS_VAR_INNODB_STATS_PERSISTENT: {
      ret = create_one_sys_var<ObSysVarInnodbStatsPersistent>(allocator_, sys_var_ptr, "ObSysVarInnodbStatsPersistent");
      break;
    }
    case share::SYS_VAR_DEBUG: {
      ret = create_one_sys_var<ObSysVarDebug>(allocator_, sys_var_ptr, "ObSysVarDebug");
      break;
    }
    case share::SYS_VAR_INNODB_CHANGE_BUFFERING_DEBUG: {
      ret = create_one_sys_var<ObSysVarInnodbChangeBufferingDebug>(allocator_, sys_var_ptr, "ObSysVarInnodbChangeBufferingDebug");
      break;
    }
    case share::SYS_VAR_INNODB_DISABLE_RESIZE_BUFFER_POOL_DEBUG: {
      ret = create_one_sys_var<ObSysVarInnodbDisableResizeBufferPoolDebug>(allocator_, sys_var_ptr, "ObSysVarInnodbDisableResizeBufferPoolDebug");
      break;
    }
    case share::SYS_VAR_INNODB_FIL_MAKE_PAGE_DIRTY_DEBUG: {
      ret = create_one_sys_var<ObSysVarInnodbFilMakePageDirtyDebug>(allocator_, sys_var_ptr, "ObSysVarInnodbFilMakePageDirtyDebug");
      break;
    }
    case share::SYS_VAR_INNODB_LIMIT_OPTIMISTIC_INSERT_DEBUG: {
      ret = create_one_sys_var<ObSysVarInnodbLimitOptimisticInsertDebug>(allocator_, sys_var_ptr, "ObSysVarInnodbLimitOptimisticInsertDebug");
      break;
    }
    case share::SYS_VAR_INNODB_MERGE_THRESHOLD_SET_ALL_DEBUG: {
      ret = create_one_sys_var<ObSysVarInnodbMergeThresholdSetAllDebug>(allocator_, sys_var_ptr, "ObSysVarInnodbMergeThresholdSetAllDebug");
      break;
    }
    case share::SYS_VAR_INNODB_SAVED_PAGE_NUMBER_DEBUG: {
      ret = create_one_sys_var<ObSysVarInnodbSavedPageNumberDebug>(allocator_, sys_var_ptr, "ObSysVarInnodbSavedPageNumberDebug");
      break;
    }
    case share::SYS_VAR_INNODB_TRX_PURGE_VIEW_UPDATE_ONLY_DEBUG: {
      ret = create_one_sys_var<ObSysVarInnodbTrxPurgeViewUpdateOnlyDebug>(allocator_, sys_var_ptr, "ObSysVarInnodbTrxPurgeViewUpdateOnlyDebug");
      break;
    }
    case share::SYS_VAR_INNODB_TRX_RSEG_N_SLOTS_DEBUG: {
      ret = create_one_sys_var<ObSysVarInnodbTrxRsegNSlotsDebug>(allocator_, sys_var_ptr, "ObSysVarInnodbTrxRsegNSlotsDebug");
      break;
    }
    case share::SYS_VAR_STORED_PROGRAM_CACHE: {
      ret = create_one_sys_var<ObSysVarStoredProgramCache>(allocator_, sys_var_ptr, "ObSysVarStoredProgramCache");
      break;
    }
    case share::SYS_VAR_CARDINALITY_ESTIMATION_MODEL: {
      ret = create_one_sys_var<ObSysVarCardinalityEstimationModel>(allocator_, sys_var_ptr, "ObSysVarCardinalityEstimationModel");
      break;
    }
    case share::SYS_VAR_FLUSH: {
      ret = create_one_sys_var<ObSysVarFlush>(allocator_, sys_var_ptr, "ObSysVarFlush");
      break;
    }
    case share::SYS_VAR_FLUSH_TIME: {
      ret = create_one_sys_var<ObSysVarFlushTime>(allocator_, sys_var_ptr, "ObSysVarFlushTime");
      break;
    }
    case share::SYS_VAR_INNODB_ADAPTIVE_FLUSHING: {
      ret = create_one_sys_var<ObSysVarInnodbAdaptiveFlushing>(allocator_, sys_var_ptr, "ObSysVarInnodbAdaptiveFlushing");
      break;
    }
    case share::SYS_VAR_INNODB_ADAPTIVE_FLUSHING_LWM: {
      ret = create_one_sys_var<ObSysVarInnodbAdaptiveFlushingLwm>(allocator_, sys_var_ptr, "ObSysVarInnodbAdaptiveFlushingLwm");
      break;
    }
    case share::SYS_VAR_INNODB_ADAPTIVE_HASH_INDEX: {
      ret = create_one_sys_var<ObSysVarInnodbAdaptiveHashIndex>(allocator_, sys_var_ptr, "ObSysVarInnodbAdaptiveHashIndex");
      break;
    }
    case share::SYS_VAR_INNODB_ADAPTIVE_HASH_INDEX_PARTS: {
      ret = create_one_sys_var<ObSysVarInnodbAdaptiveHashIndexParts>(allocator_, sys_var_ptr, "ObSysVarInnodbAdaptiveHashIndexParts");
      break;
    }
    case share::SYS_VAR_INNODB_ADAPTIVE_MAX_SLEEP_DELAY: {
      ret = create_one_sys_var<ObSysVarInnodbAdaptiveMaxSleepDelay>(allocator_, sys_var_ptr, "ObSysVarInnodbAdaptiveMaxSleepDelay");
      break;
    }
    case share::SYS_VAR_INNODB_AUTOEXTEND_INCREMENT: {
      ret = create_one_sys_var<ObSysVarInnodbAutoextendIncrement>(allocator_, sys_var_ptr, "ObSysVarInnodbAutoextendIncrement");
      break;
    }
    case share::SYS_VAR_INNODB_BACKGROUND_DROP_LIST_EMPTY: {
      ret = create_one_sys_var<ObSysVarInnodbBackgroundDropListEmpty>(allocator_, sys_var_ptr, "ObSysVarInnodbBackgroundDropListEmpty");
      break;
    }
    case share::SYS_VAR_INNODB_BUFFER_POOL_DUMP_AT_SHUTDOWN: {
      ret = create_one_sys_var<ObSysVarInnodbBufferPoolDumpAtShutdown>(allocator_, sys_var_ptr, "ObSysVarInnodbBufferPoolDumpAtShutdown");
      break;
    }
    case share::SYS_VAR_INNODB_BUFFER_POOL_DUMP_NOW: {
      ret = create_one_sys_var<ObSysVarInnodbBufferPoolDumpNow>(allocator_, sys_var_ptr, "ObSysVarInnodbBufferPoolDumpNow");
      break;
    }
    case share::SYS_VAR_INNODB_BUFFER_POOL_DUMP_PCT: {
      ret = create_one_sys_var<ObSysVarInnodbBufferPoolDumpPct>(allocator_, sys_var_ptr, "ObSysVarInnodbBufferPoolDumpPct");
      break;
    }
    case share::SYS_VAR_INNODB_BUFFER_POOL_FILENAME: {
      ret = create_one_sys_var<ObSysVarInnodbBufferPoolFilename>(allocator_, sys_var_ptr, "ObSysVarInnodbBufferPoolFilename");
      break;
    }
    case share::SYS_VAR_INNODB_BUFFER_POOL_LOAD_ABORT: {
      ret = create_one_sys_var<ObSysVarInnodbBufferPoolLoadAbort>(allocator_, sys_var_ptr, "ObSysVarInnodbBufferPoolLoadAbort");
      break;
    }
    case share::SYS_VAR_INNODB_BUFFER_POOL_LOAD_NOW: {
      ret = create_one_sys_var<ObSysVarInnodbBufferPoolLoadNow>(allocator_, sys_var_ptr, "ObSysVarInnodbBufferPoolLoadNow");
      break;
    }
    case share::SYS_VAR_INNODB_BUFFER_POOL_SIZE: {
      ret = create_one_sys_var<ObSysVarInnodbBufferPoolSize>(allocator_, sys_var_ptr, "ObSysVarInnodbBufferPoolSize");
      break;
    }
    case share::SYS_VAR_INNODB_CHANGE_BUFFER_MAX_SIZE: {
      ret = create_one_sys_var<ObSysVarInnodbChangeBufferMaxSize>(allocator_, sys_var_ptr, "ObSysVarInnodbChangeBufferMaxSize");
      break;
    }
    case share::SYS_VAR_INNODB_CHANGE_BUFFERING: {
      ret = create_one_sys_var<ObSysVarInnodbChangeBuffering>(allocator_, sys_var_ptr, "ObSysVarInnodbChangeBuffering");
      break;
    }
    case share::SYS_VAR_INNODB_CHECKSUM_ALGORITHM: {
      ret = create_one_sys_var<ObSysVarInnodbChecksumAlgorithm>(allocator_, sys_var_ptr, "ObSysVarInnodbChecksumAlgorithm");
      break;
    }
    case share::SYS_VAR_INNODB_CMP_PER_INDEX_ENABLED: {
      ret = create_one_sys_var<ObSysVarInnodbCmpPerIndexEnabled>(allocator_, sys_var_ptr, "ObSysVarInnodbCmpPerIndexEnabled");
      break;
    }
    case share::SYS_VAR_INNODB_COMMIT_CONCURRENCY: {
      ret = create_one_sys_var<ObSysVarInnodbCommitConcurrency>(allocator_, sys_var_ptr, "ObSysVarInnodbCommitConcurrency");
      break;
    }
    case share::SYS_VAR_INNODB_COMPRESSION_FAILURE_THRESHOLD_PCT: {
      ret = create_one_sys_var<ObSysVarInnodbCompressionFailureThresholdPct>(allocator_, sys_var_ptr, "ObSysVarInnodbCompressionFailureThresholdPct");
      break;
    }
    case share::SYS_VAR_INNODB_COMPRESSION_LEVEL: {
      ret = create_one_sys_var<ObSysVarInnodbCompressionLevel>(allocator_, sys_var_ptr, "ObSysVarInnodbCompressionLevel");
      break;
    }
    case share::SYS_VAR_INNODB_COMPRESSION_PAD_PCT_MAX: {
      ret = create_one_sys_var<ObSysVarInnodbCompressionPadPctMax>(allocator_, sys_var_ptr, "ObSysVarInnodbCompressionPadPctMax");
      break;
    }
    case share::SYS_VAR_INNODB_CONCURRENCY_TICKETS: {
      ret = create_one_sys_var<ObSysVarInnodbConcurrencyTickets>(allocator_, sys_var_ptr, "ObSysVarInnodbConcurrencyTickets");
      break;
    }
    case share::SYS_VAR_INNODB_DEFAULT_ROW_FORMAT: {
      ret = create_one_sys_var<ObSysVarInnodbDefaultRowFormat>(allocator_, sys_var_ptr, "ObSysVarInnodbDefaultRowFormat");
      break;
    }
    case share::SYS_VAR_INNODB_DISABLE_SORT_FILE_CACHE: {
      ret = create_one_sys_var<ObSysVarInnodbDisableSortFileCache>(allocator_, sys_var_ptr, "ObSysVarInnodbDisableSortFileCache");
      break;
    }
    case share::SYS_VAR_INNODB_FILE_FORMAT: {
      ret = create_one_sys_var<ObSysVarInnodbFileFormat>(allocator_, sys_var_ptr, "ObSysVarInnodbFileFormat");
      break;
    }
    case share::SYS_VAR_INNODB_FILE_FORMAT_MAX: {
      ret = create_one_sys_var<ObSysVarInnodbFileFormatMax>(allocator_, sys_var_ptr, "ObSysVarInnodbFileFormatMax");
      break;
    }
    case share::SYS_VAR_INNODB_FILE_PER_TABLE: {
      ret = create_one_sys_var<ObSysVarInnodbFilePerTable>(allocator_, sys_var_ptr, "ObSysVarInnodbFilePerTable");
      break;
    }
    case share::SYS_VAR_INNODB_FILL_FACTOR: {
      ret = create_one_sys_var<ObSysVarInnodbFillFactor>(allocator_, sys_var_ptr, "ObSysVarInnodbFillFactor");
      break;
    }
    case share::SYS_VAR_INNODB_FLUSH_NEIGHBORS: {
      ret = create_one_sys_var<ObSysVarInnodbFlushNeighbors>(allocator_, sys_var_ptr, "ObSysVarInnodbFlushNeighbors");
      break;
    }
    case share::SYS_VAR_INNODB_FLUSH_SYNC: {
      ret = create_one_sys_var<ObSysVarInnodbFlushSync>(allocator_, sys_var_ptr, "ObSysVarInnodbFlushSync");
      break;
    }
    case share::SYS_VAR_INNODB_FLUSHING_AVG_LOOPS: {
      ret = create_one_sys_var<ObSysVarInnodbFlushingAvgLoops>(allocator_, sys_var_ptr, "ObSysVarInnodbFlushingAvgLoops");
      break;
    }
    case share::SYS_VAR_INNODB_LRU_SCAN_DEPTH: {
      ret = create_one_sys_var<ObSysVarInnodbLruScanDepth>(allocator_, sys_var_ptr, "ObSysVarInnodbLruScanDepth");
      break;
    }
    case share::SYS_VAR_INNODB_MAX_DIRTY_PAGES_PCT: {
      ret = create_one_sys_var<ObSysVarInnodbMaxDirtyPagesPct>(allocator_, sys_var_ptr, "ObSysVarInnodbMaxDirtyPagesPct");
      break;
    }
    case share::SYS_VAR_INNODB_MAX_DIRTY_PAGES_PCT_LWM: {
      ret = create_one_sys_var<ObSysVarInnodbMaxDirtyPagesPctLwm>(allocator_, sys_var_ptr, "ObSysVarInnodbMaxDirtyPagesPctLwm");
      break;
    }
    case share::SYS_VAR_INNODB_MAX_PURGE_LAG: {
      ret = create_one_sys_var<ObSysVarInnodbMaxPurgeLag>(allocator_, sys_var_ptr, "ObSysVarInnodbMaxPurgeLag");
      break;
    }
    case share::SYS_VAR_INNODB_MAX_PURGE_LAG_DELAY: {
      ret = create_one_sys_var<ObSysVarInnodbMaxPurgeLagDelay>(allocator_, sys_var_ptr, "ObSysVarInnodbMaxPurgeLagDelay");
      break;
    }
    case share::SYS_VAR_HAVE_SYMLINK: {
      ret = create_one_sys_var<ObSysVarHaveSymlink>(allocator_, sys_var_ptr, "ObSysVarHaveSymlink");
      break;
    }
    case share::SYS_VAR_IGNORE_BUILTIN_INNODB: {
      ret = create_one_sys_var<ObSysVarIgnoreBuiltinInnodb>(allocator_, sys_var_ptr, "ObSysVarIgnoreBuiltinInnodb");
      break;
    }
    case share::SYS_VAR_INNODB_BUFFER_POOL_CHUNK_SIZE: {
      ret = create_one_sys_var<ObSysVarInnodbBufferPoolChunkSize>(allocator_, sys_var_ptr, "ObSysVarInnodbBufferPoolChunkSize");
      break;
    }
    case share::SYS_VAR_INNODB_BUFFER_POOL_INSTANCES: {
      ret = create_one_sys_var<ObSysVarInnodbBufferPoolInstances>(allocator_, sys_var_ptr, "ObSysVarInnodbBufferPoolInstances");
      break;
    }
    case share::SYS_VAR_INNODB_BUFFER_POOL_LOAD_AT_STARTUP: {
      ret = create_one_sys_var<ObSysVarInnodbBufferPoolLoadAtStartup>(allocator_, sys_var_ptr, "ObSysVarInnodbBufferPoolLoadAtStartup");
      break;
    }
    case share::SYS_VAR_INNODB_CHECKSUMS: {
      ret = create_one_sys_var<ObSysVarInnodbChecksums>(allocator_, sys_var_ptr, "ObSysVarInnodbChecksums");
      break;
    }
    case share::SYS_VAR_INNODB_DOUBLEWRITE: {
      ret = create_one_sys_var<ObSysVarInnodbDoublewrite>(allocator_, sys_var_ptr, "ObSysVarInnodbDoublewrite");
      break;
    }
    case share::SYS_VAR_INNODB_FILE_FORMAT_CHECK: {
      ret = create_one_sys_var<ObSysVarInnodbFileFormatCheck>(allocator_, sys_var_ptr, "ObSysVarInnodbFileFormatCheck");
      break;
    }
    case share::SYS_VAR_INNODB_FLUSH_METHOD: {
      ret = create_one_sys_var<ObSysVarInnodbFlushMethod>(allocator_, sys_var_ptr, "ObSysVarInnodbFlushMethod");
      break;
    }
    case share::SYS_VAR_INNODB_FORCE_LOAD_CORRUPTED: {
      ret = create_one_sys_var<ObSysVarInnodbForceLoadCorrupted>(allocator_, sys_var_ptr, "ObSysVarInnodbForceLoadCorrupted");
      break;
    }
    case share::SYS_VAR_INNODB_PAGE_SIZE: {
      ret = create_one_sys_var<ObSysVarInnodbPageSize>(allocator_, sys_var_ptr, "ObSysVarInnodbPageSize");
      break;
    }
    case share::SYS_VAR_INNODB_VERSION: {
      ret = create_one_sys_var<ObSysVarInnodbVersion>(allocator_, sys_var_ptr, "ObSysVarInnodbVersion");
      break;
    }
    case share::SYS_VAR_MYISAM_MMAP_SIZE: {
      ret = create_one_sys_var<ObSysVarMyisamMmapSize>(allocator_, sys_var_ptr, "ObSysVarMyisamMmapSize");
      break;
    }
    case share::SYS_VAR_TABLE_OPEN_CACHE_INSTANCES: {
      ret = create_one_sys_var<ObSysVarTableOpenCacheInstances>(allocator_, sys_var_ptr, "ObSysVarTableOpenCacheInstances");
      break;
    }
    case share::SYS_VAR_GTID_EXECUTED: {
      ret = create_one_sys_var<ObSysVarGtidExecuted>(allocator_, sys_var_ptr, "ObSysVarGtidExecuted");
      break;
    }
    case share::SYS_VAR_GTID_OWNED: {
      ret = create_one_sys_var<ObSysVarGtidOwned>(allocator_, sys_var_ptr, "ObSysVarGtidOwned");
      break;
    }
    case share::SYS_VAR_INNODB_ROLLBACK_ON_TIMEOUT: {
      ret = create_one_sys_var<ObSysVarInnodbRollbackOnTimeout>(allocator_, sys_var_ptr, "ObSysVarInnodbRollbackOnTimeout");
      break;
    }
    case share::SYS_VAR_COMPLETION_TYPE: {
      ret = create_one_sys_var<ObSysVarCompletionType>(allocator_, sys_var_ptr, "ObSysVarCompletionType");
      break;
    }
    case share::SYS_VAR_ENFORCE_GTID_CONSISTENCY: {
      ret = create_one_sys_var<ObSysVarEnforceGtidConsistency>(allocator_, sys_var_ptr, "ObSysVarEnforceGtidConsistency");
      break;
    }
    case share::SYS_VAR_GTID_EXECUTED_COMPRESSION_PERIOD: {
      ret = create_one_sys_var<ObSysVarGtidExecutedCompressionPeriod>(allocator_, sys_var_ptr, "ObSysVarGtidExecutedCompressionPeriod");
      break;
    }
    case share::SYS_VAR_GTID_MODE: {
      ret = create_one_sys_var<ObSysVarGtidMode>(allocator_, sys_var_ptr, "ObSysVarGtidMode");
      break;
    }
    case share::SYS_VAR_GTID_NEXT: {
      ret = create_one_sys_var<ObSysVarGtidNext>(allocator_, sys_var_ptr, "ObSysVarGtidNext");
      break;
    }
    case share::SYS_VAR_GTID_PURGED: {
      ret = create_one_sys_var<ObSysVarGtidPurged>(allocator_, sys_var_ptr, "ObSysVarGtidPurged");
      break;
    }
    case share::SYS_VAR_INNODB_API_BK_COMMIT_INTERVAL: {
      ret = create_one_sys_var<ObSysVarInnodbApiBkCommitInterval>(allocator_, sys_var_ptr, "ObSysVarInnodbApiBkCommitInterval");
      break;
    }
    case share::SYS_VAR_INNODB_API_TRX_LEVEL: {
      ret = create_one_sys_var<ObSysVarInnodbApiTrxLevel>(allocator_, sys_var_ptr, "ObSysVarInnodbApiTrxLevel");
      break;
    }
    case share::SYS_VAR_SESSION_TRACK_GTIDS: {
      ret = create_one_sys_var<ObSysVarSessionTrackGtids>(allocator_, sys_var_ptr, "ObSysVarSessionTrackGtids");
      break;
    }
    case share::SYS_VAR_SESSION_TRACK_TRANSACTION_INFO: {
      ret = create_one_sys_var<ObSysVarSessionTrackTransactionInfo>(allocator_, sys_var_ptr, "ObSysVarSessionTrackTransactionInfo");
      break;
    }
    case share::SYS_VAR_TRANSACTION_ALLOC_BLOCK_SIZE: {
      ret = create_one_sys_var<ObSysVarTransactionAllocBlockSize>(allocator_, sys_var_ptr, "ObSysVarTransactionAllocBlockSize");
      break;
    }
    case share::SYS_VAR_TRANSACTION_ALLOW_BATCHING: {
      ret = create_one_sys_var<ObSysVarTransactionAllowBatching>(allocator_, sys_var_ptr, "ObSysVarTransactionAllowBatching");
      break;
    }
    case share::SYS_VAR_TRANSACTION_PREALLOC_SIZE: {
      ret = create_one_sys_var<ObSysVarTransactionPreallocSize>(allocator_, sys_var_ptr, "ObSysVarTransactionPreallocSize");
      break;
    }
    case share::SYS_VAR_TRANSACTION_WRITE_SET_EXTRACTION: {
      ret = create_one_sys_var<ObSysVarTransactionWriteSetExtraction>(allocator_, sys_var_ptr, "ObSysVarTransactionWriteSetExtraction");
      break;
    }
    case share::SYS_VAR_INFORMATION_SCHEMA_STATS_EXPIRY: {
      ret = create_one_sys_var<ObSysVarInformationSchemaStatsExpiry>(allocator_, sys_var_ptr, "ObSysVarInformationSchemaStatsExpiry");
      break;
    }
    case share::SYS_VAR_GROUP_REPLICATION_ALLOW_LOCAL_DISJOINT_GTIDS_JOIN: {
      ret = create_one_sys_var<ObSysVarGroupReplicationAllowLocalDisjointGtidsJoin>(allocator_, sys_var_ptr, "ObSysVarGroupReplicationAllowLocalDisjointGtidsJoin");
      break;
    }
    case share::SYS_VAR_GROUP_REPLICATION_ALLOW_LOCAL_LOWER_VERSION_JOIN: {
      ret = create_one_sys_var<ObSysVarGroupReplicationAllowLocalLowerVersionJoin>(allocator_, sys_var_ptr, "ObSysVarGroupReplicationAllowLocalLowerVersionJoin");
      break;
    }
    case share::SYS_VAR_GROUP_REPLICATION_AUTO_INCREMENT_INCREMENT: {
      ret = create_one_sys_var<ObSysVarGroupReplicationAutoIncrementIncrement>(allocator_, sys_var_ptr, "ObSysVarGroupReplicationAutoIncrementIncrement");
      break;
    }
    case share::SYS_VAR_GROUP_REPLICATION_BOOTSTRAP_GROUP: {
      ret = create_one_sys_var<ObSysVarGroupReplicationBootstrapGroup>(allocator_, sys_var_ptr, "ObSysVarGroupReplicationBootstrapGroup");
      break;
    }
    case share::SYS_VAR_GROUP_REPLICATION_COMPONENTS_STOP_TIMEOUT: {
      ret = create_one_sys_var<ObSysVarGroupReplicationComponentsStopTimeout>(allocator_, sys_var_ptr, "ObSysVarGroupReplicationComponentsStopTimeout");
      break;
    }
    case share::SYS_VAR_GROUP_REPLICATION_COMPRESSION_THRESHOLD: {
      ret = create_one_sys_var<ObSysVarGroupReplicationCompressionThreshold>(allocator_, sys_var_ptr, "ObSysVarGroupReplicationCompressionThreshold");
      break;
    }
    case share::SYS_VAR_GROUP_REPLICATION_ENFORCE_UPDATE_EVERYWHERE_CHECKS: {
      ret = create_one_sys_var<ObSysVarGroupReplicationEnforceUpdateEverywhereChecks>(allocator_, sys_var_ptr, "ObSysVarGroupReplicationEnforceUpdateEverywhereChecks");
      break;
    }
    case share::SYS_VAR_GROUP_REPLICATION_EXIT_STATE_ACTION: {
      ret = create_one_sys_var<ObSysVarGroupReplicationExitStateAction>(allocator_, sys_var_ptr, "ObSysVarGroupReplicationExitStateAction");
      break;
    }
    case share::SYS_VAR_GROUP_REPLICATION_FLOW_CONTROL_APPLIER_THRESHOLD: {
      ret = create_one_sys_var<ObSysVarGroupReplicationFlowControlApplierThreshold>(allocator_, sys_var_ptr, "ObSysVarGroupReplicationFlowControlApplierThreshold");
      break;
    }
    case share::SYS_VAR_GROUP_REPLICATION_FLOW_CONTROL_CERTIFIER_THRESHOLD: {
      ret = create_one_sys_var<ObSysVarGroupReplicationFlowControlCertifierThreshold>(allocator_, sys_var_ptr, "ObSysVarGroupReplicationFlowControlCertifierThreshold");
      break;
    }
    case share::SYS_VAR_GROUP_REPLICATION_FLOW_CONTROL_MODE: {
      ret = create_one_sys_var<ObSysVarGroupReplicationFlowControlMode>(allocator_, sys_var_ptr, "ObSysVarGroupReplicationFlowControlMode");
      break;
    }
    case share::SYS_VAR_GROUP_REPLICATION_FORCE_MEMBERS: {
      ret = create_one_sys_var<ObSysVarGroupReplicationForceMembers>(allocator_, sys_var_ptr, "ObSysVarGroupReplicationForceMembers");
      break;
    }
    case share::SYS_VAR_GROUP_REPLICATION_GROUP_NAME: {
      ret = create_one_sys_var<ObSysVarGroupReplicationGroupName>(allocator_, sys_var_ptr, "ObSysVarGroupReplicationGroupName");
      break;
    }
    case share::SYS_VAR_GROUP_REPLICATION_GTID_ASSIGNMENT_BLOCK_SIZE: {
      ret = create_one_sys_var<ObSysVarGroupReplicationGtidAssignmentBlockSize>(allocator_, sys_var_ptr, "ObSysVarGroupReplicationGtidAssignmentBlockSize");
      break;
    }
    case share::SYS_VAR_GROUP_REPLICATION_IP_WHITELIST: {
      ret = create_one_sys_var<ObSysVarGroupReplicationIpWhitelist>(allocator_, sys_var_ptr, "ObSysVarGroupReplicationIpWhitelist");
      break;
    }
    case share::SYS_VAR_GROUP_REPLICATION_LOCAL_ADDRESS: {
      ret = create_one_sys_var<ObSysVarGroupReplicationLocalAddress>(allocator_, sys_var_ptr, "ObSysVarGroupReplicationLocalAddress");
      break;
    }
    case share::SYS_VAR_GROUP_REPLICATION_MEMBER_WEIGHT: {
      ret = create_one_sys_var<ObSysVarGroupReplicationMemberWeight>(allocator_, sys_var_ptr, "ObSysVarGroupReplicationMemberWeight");
      break;
    }
    case share::SYS_VAR_GROUP_REPLICATION_POLL_SPIN_LOOPS: {
      ret = create_one_sys_var<ObSysVarGroupReplicationPollSpinLoops>(allocator_, sys_var_ptr, "ObSysVarGroupReplicationPollSpinLoops");
      break;
    }
    case share::SYS_VAR_GROUP_REPLICATION_RECOVERY_COMPLETE_AT: {
      ret = create_one_sys_var<ObSysVarGroupReplicationRecoveryCompleteAt>(allocator_, sys_var_ptr, "ObSysVarGroupReplicationRecoveryCompleteAt");
      break;
    }
    case share::SYS_VAR_GROUP_REPLICATION_RECOVERY_RECONNECT_INTERVAL: {
      ret = create_one_sys_var<ObSysVarGroupReplicationRecoveryReconnectInterval>(allocator_, sys_var_ptr, "ObSysVarGroupReplicationRecoveryReconnectInterval");
      break;
    }
    case share::SYS_VAR_GROUP_REPLICATION_RECOVERY_RETRY_COUNT: {
      ret = create_one_sys_var<ObSysVarGroupReplicationRecoveryRetryCount>(allocator_, sys_var_ptr, "ObSysVarGroupReplicationRecoveryRetryCount");
      break;
    }
    case share::SYS_VAR_GROUP_REPLICATION_RECOVERY_SSL_CA: {
      ret = create_one_sys_var<ObSysVarGroupReplicationRecoverySslCa>(allocator_, sys_var_ptr, "ObSysVarGroupReplicationRecoverySslCa");
      break;
    }
    case share::SYS_VAR_GROUP_REPLICATION_RECOVERY_SSL_CAPATH: {
      ret = create_one_sys_var<ObSysVarGroupReplicationRecoverySslCapath>(allocator_, sys_var_ptr, "ObSysVarGroupReplicationRecoverySslCapath");
      break;
    }
    case share::SYS_VAR_GROUP_REPLICATION_RECOVERY_SSL_CERT: {
      ret = create_one_sys_var<ObSysVarGroupReplicationRecoverySslCert>(allocator_, sys_var_ptr, "ObSysVarGroupReplicationRecoverySslCert");
      break;
    }
    case share::SYS_VAR_GROUP_REPLICATION_RECOVERY_SSL_CIPHER: {
      ret = create_one_sys_var<ObSysVarGroupReplicationRecoverySslCipher>(allocator_, sys_var_ptr, "ObSysVarGroupReplicationRecoverySslCipher");
      break;
    }
    case share::SYS_VAR_GROUP_REPLICATION_RECOVERY_SSL_CRL: {
      ret = create_one_sys_var<ObSysVarGroupReplicationRecoverySslCrl>(allocator_, sys_var_ptr, "ObSysVarGroupReplicationRecoverySslCrl");
      break;
    }
    case share::SYS_VAR_GROUP_REPLICATION_RECOVERY_SSL_CRLPATH: {
      ret = create_one_sys_var<ObSysVarGroupReplicationRecoverySslCrlpath>(allocator_, sys_var_ptr, "ObSysVarGroupReplicationRecoverySslCrlpath");
      break;
    }
    case share::SYS_VAR_GROUP_REPLICATION_RECOVERY_SSL_KEY: {
      ret = create_one_sys_var<ObSysVarGroupReplicationRecoverySslKey>(allocator_, sys_var_ptr, "ObSysVarGroupReplicationRecoverySslKey");
      break;
    }
    case share::SYS_VAR_GROUP_REPLICATION_RECOVERY_SSL_VERIFY_SERVER_CERT: {
      ret = create_one_sys_var<ObSysVarGroupReplicationRecoverySslVerifyServerCert>(allocator_, sys_var_ptr, "ObSysVarGroupReplicationRecoverySslVerifyServerCert");
      break;
    }
    case share::SYS_VAR_GROUP_REPLICATION_RECOVERY_USE_SSL: {
      ret = create_one_sys_var<ObSysVarGroupReplicationRecoveryUseSsl>(allocator_, sys_var_ptr, "ObSysVarGroupReplicationRecoveryUseSsl");
      break;
    }
    case share::SYS_VAR_GROUP_REPLICATION_SINGLE_PRIMARY_MODE: {
      ret = create_one_sys_var<ObSysVarGroupReplicationSinglePrimaryMode>(allocator_, sys_var_ptr, "ObSysVarGroupReplicationSinglePrimaryMode");
      break;
    }
    case share::SYS_VAR_GROUP_REPLICATION_SSL_MODE: {
      ret = create_one_sys_var<ObSysVarGroupReplicationSslMode>(allocator_, sys_var_ptr, "ObSysVarGroupReplicationSslMode");
      break;
    }
    case share::SYS_VAR_GROUP_REPLICATION_START_ON_BOOT: {
      ret = create_one_sys_var<ObSysVarGroupReplicationStartOnBoot>(allocator_, sys_var_ptr, "ObSysVarGroupReplicationStartOnBoot");
      break;
    }
    case share::SYS_VAR_GROUP_REPLICATION_TRANSACTION_SIZE_LIMIT: {
      ret = create_one_sys_var<ObSysVarGroupReplicationTransactionSizeLimit>(allocator_, sys_var_ptr, "ObSysVarGroupReplicationTransactionSizeLimit");
      break;
    }
    case share::SYS_VAR_GROUP_REPLICATION_UNREACHABLE_MAJORITY_TIMEOUT: {
      ret = create_one_sys_var<ObSysVarGroupReplicationUnreachableMajorityTimeout>(allocator_, sys_var_ptr, "ObSysVarGroupReplicationUnreachableMajorityTimeout");
      break;
    }
    case share::SYS_VAR_INNODB_REPLICATION_DELAY: {
      ret = create_one_sys_var<ObSysVarInnodbReplicationDelay>(allocator_, sys_var_ptr, "ObSysVarInnodbReplicationDelay");
      break;
    }
    case share::SYS_VAR_MASTER_INFO_REPOSITORY: {
      ret = create_one_sys_var<ObSysVarMasterInfoRepository>(allocator_, sys_var_ptr, "ObSysVarMasterInfoRepository");
      break;
    }
    case share::SYS_VAR_MASTER_VERIFY_CHECKSUM: {
      ret = create_one_sys_var<ObSysVarMasterVerifyChecksum>(allocator_, sys_var_ptr, "ObSysVarMasterVerifyChecksum");
      break;
    }
    case share::SYS_VAR_PSEUDO_SLAVE_MODE: {
      ret = create_one_sys_var<ObSysVarPseudoSlaveMode>(allocator_, sys_var_ptr, "ObSysVarPseudoSlaveMode");
      break;
    }
    case share::SYS_VAR_PSEUDO_THREAD_ID: {
      ret = create_one_sys_var<ObSysVarPseudoThreadId>(allocator_, sys_var_ptr, "ObSysVarPseudoThreadId");
      break;
    }
    case share::SYS_VAR_RBR_EXEC_MODE: {
      ret = create_one_sys_var<ObSysVarRbrExecMode>(allocator_, sys_var_ptr, "ObSysVarRbrExecMode");
      break;
    }
    case share::SYS_VAR_REPLICATION_OPTIMIZE_FOR_STATIC_PLUGIN_CONFIG: {
      ret = create_one_sys_var<ObSysVarReplicationOptimizeForStaticPluginConfig>(allocator_, sys_var_ptr, "ObSysVarReplicationOptimizeForStaticPluginConfig");
      break;
    }
    case share::SYS_VAR_REPLICATION_SENDER_OBSERVE_COMMIT_ONLY: {
      ret = create_one_sys_var<ObSysVarReplicationSenderObserveCommitOnly>(allocator_, sys_var_ptr, "ObSysVarReplicationSenderObserveCommitOnly");
      break;
    }
    case share::SYS_VAR_RPL_SEMI_SYNC_MASTER_ENABLED: {
      ret = create_one_sys_var<ObSysVarRplSemiSyncMasterEnabled>(allocator_, sys_var_ptr, "ObSysVarRplSemiSyncMasterEnabled");
      break;
    }
    case share::SYS_VAR_RPL_SEMI_SYNC_MASTER_TIMEOUT: {
      ret = create_one_sys_var<ObSysVarRplSemiSyncMasterTimeout>(allocator_, sys_var_ptr, "ObSysVarRplSemiSyncMasterTimeout");
      break;
    }
    case share::SYS_VAR_RPL_SEMI_SYNC_MASTER_TRACE_LEVEL: {
      ret = create_one_sys_var<ObSysVarRplSemiSyncMasterTraceLevel>(allocator_, sys_var_ptr, "ObSysVarRplSemiSyncMasterTraceLevel");
      break;
    }
    case share::SYS_VAR_RPL_SEMI_SYNC_MASTER_WAIT_FOR_SLAVE_COUNT: {
      ret = create_one_sys_var<ObSysVarRplSemiSyncMasterWaitForSlaveCount>(allocator_, sys_var_ptr, "ObSysVarRplSemiSyncMasterWaitForSlaveCount");
      break;
    }
    case share::SYS_VAR_RPL_SEMI_SYNC_MASTER_WAIT_NO_SLAVE: {
      ret = create_one_sys_var<ObSysVarRplSemiSyncMasterWaitNoSlave>(allocator_, sys_var_ptr, "ObSysVarRplSemiSyncMasterWaitNoSlave");
      break;
    }
    case share::SYS_VAR_RPL_SEMI_SYNC_MASTER_WAIT_POINT: {
      ret = create_one_sys_var<ObSysVarRplSemiSyncMasterWaitPoint>(allocator_, sys_var_ptr, "ObSysVarRplSemiSyncMasterWaitPoint");
      break;
    }
    case share::SYS_VAR_RPL_SEMI_SYNC_SLAVE_ENABLED: {
      ret = create_one_sys_var<ObSysVarRplSemiSyncSlaveEnabled>(allocator_, sys_var_ptr, "ObSysVarRplSemiSyncSlaveEnabled");
      break;
    }
    case share::SYS_VAR_RPL_SEMI_SYNC_SLAVE_TRACE_LEVEL: {
      ret = create_one_sys_var<ObSysVarRplSemiSyncSlaveTraceLevel>(allocator_, sys_var_ptr, "ObSysVarRplSemiSyncSlaveTraceLevel");
      break;
    }
    case share::SYS_VAR_RPL_STOP_SLAVE_TIMEOUT: {
      ret = create_one_sys_var<ObSysVarRplStopSlaveTimeout>(allocator_, sys_var_ptr, "ObSysVarRplStopSlaveTimeout");
      break;
    }
    case share::SYS_VAR_SLAVE_ALLOW_BATCHING: {
      ret = create_one_sys_var<ObSysVarSlaveAllowBatching>(allocator_, sys_var_ptr, "ObSysVarSlaveAllowBatching");
      break;
    }
    case share::SYS_VAR_SLAVE_CHECKPOINT_GROUP: {
      ret = create_one_sys_var<ObSysVarSlaveCheckpointGroup>(allocator_, sys_var_ptr, "ObSysVarSlaveCheckpointGroup");
      break;
    }
    case share::SYS_VAR_SLAVE_CHECKPOINT_PERIOD: {
      ret = create_one_sys_var<ObSysVarSlaveCheckpointPeriod>(allocator_, sys_var_ptr, "ObSysVarSlaveCheckpointPeriod");
      break;
    }
    case share::SYS_VAR_SLAVE_COMPRESSED_PROTOCOL: {
      ret = create_one_sys_var<ObSysVarSlaveCompressedProtocol>(allocator_, sys_var_ptr, "ObSysVarSlaveCompressedProtocol");
      break;
    }
    case share::SYS_VAR_SLAVE_EXEC_MODE: {
      ret = create_one_sys_var<ObSysVarSlaveExecMode>(allocator_, sys_var_ptr, "ObSysVarSlaveExecMode");
      break;
    }
    case share::SYS_VAR_SLAVE_MAX_ALLOWED_PACKET: {
      ret = create_one_sys_var<ObSysVarSlaveMaxAllowedPacket>(allocator_, sys_var_ptr, "ObSysVarSlaveMaxAllowedPacket");
      break;
    }
    case share::SYS_VAR_SLAVE_NET_TIMEOUT: {
      ret = create_one_sys_var<ObSysVarSlaveNetTimeout>(allocator_, sys_var_ptr, "ObSysVarSlaveNetTimeout");
      break;
    }
    case share::SYS_VAR_SLAVE_PARALLEL_TYPE: {
      ret = create_one_sys_var<ObSysVarSlaveParallelType>(allocator_, sys_var_ptr, "ObSysVarSlaveParallelType");
      break;
    }
    case share::SYS_VAR_SLAVE_PARALLEL_WORKERS: {
      ret = create_one_sys_var<ObSysVarSlaveParallelWorkers>(allocator_, sys_var_ptr, "ObSysVarSlaveParallelWorkers");
      break;
    }
    case share::SYS_VAR_SLAVE_PENDING_JOBS_SIZE_MAX: {
      ret = create_one_sys_var<ObSysVarSlavePendingJobsSizeMax>(allocator_, sys_var_ptr, "ObSysVarSlavePendingJobsSizeMax");
      break;
    }
    case share::SYS_VAR_SLAVE_PRESERVE_COMMIT_ORDER: {
      ret = create_one_sys_var<ObSysVarSlavePreserveCommitOrder>(allocator_, sys_var_ptr, "ObSysVarSlavePreserveCommitOrder");
      break;
    }
    case share::SYS_VAR_SLAVE_SQL_VERIFY_CHECKSUM: {
      ret = create_one_sys_var<ObSysVarSlaveSqlVerifyChecksum>(allocator_, sys_var_ptr, "ObSysVarSlaveSqlVerifyChecksum");
      break;
    }
    case share::SYS_VAR_SLAVE_TRANSACTION_RETRIES: {
      ret = create_one_sys_var<ObSysVarSlaveTransactionRetries>(allocator_, sys_var_ptr, "ObSysVarSlaveTransactionRetries");
      break;
    }
    case share::SYS_VAR_SQL_SLAVE_SKIP_COUNTER: {
      ret = create_one_sys_var<ObSysVarSqlSlaveSkipCounter>(allocator_, sys_var_ptr, "ObSysVarSqlSlaveSkipCounter");
      break;
    }
    case share::SYS_VAR_INNODB_FORCE_RECOVERY: {
      ret = create_one_sys_var<ObSysVarInnodbForceRecovery>(allocator_, sys_var_ptr, "ObSysVarInnodbForceRecovery");
      break;
    }
    case share::SYS_VAR_SKIP_SLAVE_START: {
      ret = create_one_sys_var<ObSysVarSkipSlaveStart>(allocator_, sys_var_ptr, "ObSysVarSkipSlaveStart");
      break;
    }
    case share::SYS_VAR_SLAVE_LOAD_TMPDIR: {
      ret = create_one_sys_var<ObSysVarSlaveLoadTmpdir>(allocator_, sys_var_ptr, "ObSysVarSlaveLoadTmpdir");
      break;
    }
    case share::SYS_VAR_SLAVE_SKIP_ERRORS: {
      ret = create_one_sys_var<ObSysVarSlaveSkipErrors>(allocator_, sys_var_ptr, "ObSysVarSlaveSkipErrors");
      break;
    }
    case share::SYS_VAR_INNODB_SYNC_DEBUG: {
      ret = create_one_sys_var<ObSysVarInnodbSyncDebug>(allocator_, sys_var_ptr, "ObSysVarInnodbSyncDebug");
      break;
    }
    case share::SYS_VAR_DEFAULT_COLLATION_FOR_UTF8MB4: {
      ret = create_one_sys_var<ObSysVarDefaultCollationForUtf8mb4>(allocator_, sys_var_ptr, "ObSysVarDefaultCollationForUtf8mb4");
      break;
    }
    case share::SYS_VAR_INSERT_ID: {
      ret = create_one_sys_var<ObSysVarInsertId>(allocator_, sys_var_ptr, "ObSysVarInsertId");
      break;
    }
    case share::SYS_VAR_JOIN_BUFFER_SIZE: {
      ret = create_one_sys_var<ObSysVarJoinBufferSize>(allocator_, sys_var_ptr, "ObSysVarJoinBufferSize");
      break;
    }
    case share::SYS_VAR_MAX_JOIN_SIZE: {
      ret = create_one_sys_var<ObSysVarMaxJoinSize>(allocator_, sys_var_ptr, "ObSysVarMaxJoinSize");
      break;
    }
    case share::SYS_VAR_MAX_LENGTH_FOR_SORT_DATA: {
      ret = create_one_sys_var<ObSysVarMaxLengthForSortData>(allocator_, sys_var_ptr, "ObSysVarMaxLengthForSortData");
      break;
    }
    case share::SYS_VAR_MAX_PREPARED_STMT_COUNT: {
      ret = create_one_sys_var<ObSysVarMaxPreparedStmtCount>(allocator_, sys_var_ptr, "ObSysVarMaxPreparedStmtCount");
      break;
    }
    case share::SYS_VAR_MAX_SORT_LENGTH: {
      ret = create_one_sys_var<ObSysVarMaxSortLength>(allocator_, sys_var_ptr, "ObSysVarMaxSortLength");
      break;
    }
    case share::SYS_VAR_MIN_EXAMINED_ROW_LIMIT: {
      ret = create_one_sys_var<ObSysVarMinExaminedRowLimit>(allocator_, sys_var_ptr, "ObSysVarMinExaminedRowLimit");
      break;
    }
    case share::SYS_VAR_MULTI_RANGE_COUNT: {
      ret = create_one_sys_var<ObSysVarMultiRangeCount>(allocator_, sys_var_ptr, "ObSysVarMultiRangeCount");
      break;
    }
    case share::SYS_VAR_MYSQLX_CONNECT_TIMEOUT: {
      ret = create_one_sys_var<ObSysVarMysqlxConnectTimeout>(allocator_, sys_var_ptr, "ObSysVarMysqlxConnectTimeout");
      break;
    }
    case share::SYS_VAR_MYSQLX_IDLE_WORKER_THREAD_TIMEOUT: {
      ret = create_one_sys_var<ObSysVarMysqlxIdleWorkerThreadTimeout>(allocator_, sys_var_ptr, "ObSysVarMysqlxIdleWorkerThreadTimeout");
      break;
    }
    case share::SYS_VAR_MYSQLX_MAX_ALLOWED_PACKET: {
      ret = create_one_sys_var<ObSysVarMysqlxMaxAllowedPacket>(allocator_, sys_var_ptr, "ObSysVarMysqlxMaxAllowedPacket");
      break;
    }
    case share::SYS_VAR_MYSQLX_MAX_CONNECTIONS: {
      ret = create_one_sys_var<ObSysVarMysqlxMaxConnections>(allocator_, sys_var_ptr, "ObSysVarMysqlxMaxConnections");
      break;
    }
    case share::SYS_VAR_MYSQLX_MIN_WORKER_THREADS: {
      ret = create_one_sys_var<ObSysVarMysqlxMinWorkerThreads>(allocator_, sys_var_ptr, "ObSysVarMysqlxMinWorkerThreads");
      break;
    }
    case share::SYS_VAR_PERFORMANCE_SCHEMA_SHOW_PROCESSLIST: {
      ret = create_one_sys_var<ObSysVarPerformanceSchemaShowProcesslist>(allocator_, sys_var_ptr, "ObSysVarPerformanceSchemaShowProcesslist");
      break;
    }
    case share::SYS_VAR_QUERY_ALLOC_BLOCK_SIZE: {
      ret = create_one_sys_var<ObSysVarQueryAllocBlockSize>(allocator_, sys_var_ptr, "ObSysVarQueryAllocBlockSize");
      break;
    }
    case share::SYS_VAR_QUERY_PREALLOC_SIZE: {
      ret = create_one_sys_var<ObSysVarQueryPreallocSize>(allocator_, sys_var_ptr, "ObSysVarQueryPreallocSize");
      break;
    }
    case share::SYS_VAR_SLOW_QUERY_LOG: {
      ret = create_one_sys_var<ObSysVarSlowQueryLog>(allocator_, sys_var_ptr, "ObSysVarSlowQueryLog");
      break;
    }
    case share::SYS_VAR_SLOW_QUERY_LOG_FILE: {
      ret = create_one_sys_var<ObSysVarSlowQueryLogFile>(allocator_, sys_var_ptr, "ObSysVarSlowQueryLogFile");
      break;
    }
    case share::SYS_VAR_SORT_BUFFER_SIZE: {
      ret = create_one_sys_var<ObSysVarSortBufferSize>(allocator_, sys_var_ptr, "ObSysVarSortBufferSize");
      break;
    }
    case share::SYS_VAR_SQL_BUFFER_RESULT: {
      ret = create_one_sys_var<ObSysVarSqlBufferResult>(allocator_, sys_var_ptr, "ObSysVarSqlBufferResult");
      break;
    }
    case share::SYS_VAR_BINLOG_CACHE_SIZE: {
      ret = create_one_sys_var<ObSysVarBinlogCacheSize>(allocator_, sys_var_ptr, "ObSysVarBinlogCacheSize");
      break;
    }
    case share::SYS_VAR_BINLOG_DIRECT_NON_TRANSACTIONAL_UPDATES: {
      ret = create_one_sys_var<ObSysVarBinlogDirectNonTransactionalUpdates>(allocator_, sys_var_ptr, "ObSysVarBinlogDirectNonTransactionalUpdates");
      break;
    }
    case share::SYS_VAR_BINLOG_ERROR_ACTION: {
      ret = create_one_sys_var<ObSysVarBinlogErrorAction>(allocator_, sys_var_ptr, "ObSysVarBinlogErrorAction");
      break;
    }
    case share::SYS_VAR_BINLOG_GROUP_COMMIT_SYNC_DELAY: {
      ret = create_one_sys_var<ObSysVarBinlogGroupCommitSyncDelay>(allocator_, sys_var_ptr, "ObSysVarBinlogGroupCommitSyncDelay");
      break;
    }
    case share::SYS_VAR_BINLOG_GROUP_COMMIT_SYNC_NO_DELAY_COUNT: {
      ret = create_one_sys_var<ObSysVarBinlogGroupCommitSyncNoDelayCount>(allocator_, sys_var_ptr, "ObSysVarBinlogGroupCommitSyncNoDelayCount");
      break;
    }
    case share::SYS_VAR_BINLOG_MAX_FLUSH_QUEUE_TIME: {
      ret = create_one_sys_var<ObSysVarBinlogMaxFlushQueueTime>(allocator_, sys_var_ptr, "ObSysVarBinlogMaxFlushQueueTime");
      break;
    }
    case share::SYS_VAR_BINLOG_ORDER_COMMITS: {
      ret = create_one_sys_var<ObSysVarBinlogOrderCommits>(allocator_, sys_var_ptr, "ObSysVarBinlogOrderCommits");
      break;
    }
    case share::SYS_VAR_BINLOG_STMT_CACHE_SIZE: {
      ret = create_one_sys_var<ObSysVarBinlogStmtCacheSize>(allocator_, sys_var_ptr, "ObSysVarBinlogStmtCacheSize");
      break;
    }
    case share::SYS_VAR_BINLOG_TRANSACTION_DEPENDENCY_HISTORY_SIZE: {
      ret = create_one_sys_var<ObSysVarBinlogTransactionDependencyHistorySize>(allocator_, sys_var_ptr, "ObSysVarBinlogTransactionDependencyHistorySize");
      break;
    }
    case share::SYS_VAR_BINLOG_TRANSACTION_DEPENDENCY_TRACKING: {
      ret = create_one_sys_var<ObSysVarBinlogTransactionDependencyTracking>(allocator_, sys_var_ptr, "ObSysVarBinlogTransactionDependencyTracking");
      break;
    }
    case share::SYS_VAR_EXPIRE_LOGS_DAYS: {
      ret = create_one_sys_var<ObSysVarExpireLogsDays>(allocator_, sys_var_ptr, "ObSysVarExpireLogsDays");
      break;
    }
    case share::SYS_VAR_INNODB_FLUSH_LOG_AT_TIMEOUT: {
      ret = create_one_sys_var<ObSysVarInnodbFlushLogAtTimeout>(allocator_, sys_var_ptr, "ObSysVarInnodbFlushLogAtTimeout");
      break;
    }
    case share::SYS_VAR_INNODB_FLUSH_LOG_AT_TRX_COMMIT: {
      ret = create_one_sys_var<ObSysVarInnodbFlushLogAtTrxCommit>(allocator_, sys_var_ptr, "ObSysVarInnodbFlushLogAtTrxCommit");
      break;
    }
    case share::SYS_VAR_INNODB_LOG_CHECKPOINT_NOW: {
      ret = create_one_sys_var<ObSysVarInnodbLogCheckpointNow>(allocator_, sys_var_ptr, "ObSysVarInnodbLogCheckpointNow");
      break;
    }
    case share::SYS_VAR_INNODB_LOG_CHECKSUMS: {
      ret = create_one_sys_var<ObSysVarInnodbLogChecksums>(allocator_, sys_var_ptr, "ObSysVarInnodbLogChecksums");
      break;
    }
    case share::SYS_VAR_INNODB_LOG_COMPRESSED_PAGES: {
      ret = create_one_sys_var<ObSysVarInnodbLogCompressedPages>(allocator_, sys_var_ptr, "ObSysVarInnodbLogCompressedPages");
      break;
    }
    case share::SYS_VAR_INNODB_LOG_WRITE_AHEAD_SIZE: {
      ret = create_one_sys_var<ObSysVarInnodbLogWriteAheadSize>(allocator_, sys_var_ptr, "ObSysVarInnodbLogWriteAheadSize");
      break;
    }
    case share::SYS_VAR_INNODB_MAX_UNDO_LOG_SIZE: {
      ret = create_one_sys_var<ObSysVarInnodbMaxUndoLogSize>(allocator_, sys_var_ptr, "ObSysVarInnodbMaxUndoLogSize");
      break;
    }
    case share::SYS_VAR_INNODB_ONLINE_ALTER_LOG_MAX_SIZE: {
      ret = create_one_sys_var<ObSysVarInnodbOnlineAlterLogMaxSize>(allocator_, sys_var_ptr, "ObSysVarInnodbOnlineAlterLogMaxSize");
      break;
    }
    case share::SYS_VAR_INNODB_UNDO_LOG_TRUNCATE: {
      ret = create_one_sys_var<ObSysVarInnodbUndoLogTruncate>(allocator_, sys_var_ptr, "ObSysVarInnodbUndoLogTruncate");
      break;
    }
    case share::SYS_VAR_INNODB_UNDO_LOGS: {
      ret = create_one_sys_var<ObSysVarInnodbUndoLogs>(allocator_, sys_var_ptr, "ObSysVarInnodbUndoLogs");
      break;
    }
    case share::SYS_VAR_LOG_BIN_TRUST_FUNCTION_CREATORS: {
      ret = create_one_sys_var<ObSysVarLogBinTrustFunctionCreators>(allocator_, sys_var_ptr, "ObSysVarLogBinTrustFunctionCreators");
      break;
    }
    case share::SYS_VAR_LOG_BIN_USE_V1_ROW_EVENTS: {
      ret = create_one_sys_var<ObSysVarLogBinUseV1RowEvents>(allocator_, sys_var_ptr, "ObSysVarLogBinUseV1RowEvents");
      break;
    }
    case share::SYS_VAR_LOG_BUILTIN_AS_IDENTIFIED_BY_PASSWORD: {
      ret = create_one_sys_var<ObSysVarLogBuiltinAsIdentifiedByPassword>(allocator_, sys_var_ptr, "ObSysVarLogBuiltinAsIdentifiedByPassword");
      break;
    }
    case share::SYS_VAR_MAX_BINLOG_CACHE_SIZE: {
      ret = create_one_sys_var<ObSysVarMaxBinlogCacheSize>(allocator_, sys_var_ptr, "ObSysVarMaxBinlogCacheSize");
      break;
    }
    case share::SYS_VAR_MAX_BINLOG_SIZE: {
      ret = create_one_sys_var<ObSysVarMaxBinlogSize>(allocator_, sys_var_ptr, "ObSysVarMaxBinlogSize");
      break;
    }
    case share::SYS_VAR_MAX_BINLOG_STMT_CACHE_SIZE: {
      ret = create_one_sys_var<ObSysVarMaxBinlogStmtCacheSize>(allocator_, sys_var_ptr, "ObSysVarMaxBinlogStmtCacheSize");
      break;
    }
    case share::SYS_VAR_MAX_RELAY_LOG_SIZE: {
      ret = create_one_sys_var<ObSysVarMaxRelayLogSize>(allocator_, sys_var_ptr, "ObSysVarMaxRelayLogSize");
      break;
    }
    case share::SYS_VAR_RELAY_LOG_INFO_REPOSITORY: {
      ret = create_one_sys_var<ObSysVarRelayLogInfoRepository>(allocator_, sys_var_ptr, "ObSysVarRelayLogInfoRepository");
      break;
    }
    case share::SYS_VAR_RELAY_LOG_PURGE: {
      ret = create_one_sys_var<ObSysVarRelayLogPurge>(allocator_, sys_var_ptr, "ObSysVarRelayLogPurge");
      break;
    }
    case share::SYS_VAR_SYNC_BINLOG: {
      ret = create_one_sys_var<ObSysVarSyncBinlog>(allocator_, sys_var_ptr, "ObSysVarSyncBinlog");
      break;
    }
    case share::SYS_VAR_SYNC_RELAY_LOG: {
      ret = create_one_sys_var<ObSysVarSyncRelayLog>(allocator_, sys_var_ptr, "ObSysVarSyncRelayLog");
      break;
    }
    case share::SYS_VAR_SYNC_RELAY_LOG_INFO: {
      ret = create_one_sys_var<ObSysVarSyncRelayLogInfo>(allocator_, sys_var_ptr, "ObSysVarSyncRelayLogInfo");
      break;
    }
    case share::SYS_VAR_INNODB_DEADLOCK_DETECT: {
      ret = create_one_sys_var<ObSysVarInnodbDeadlockDetect>(allocator_, sys_var_ptr, "ObSysVarInnodbDeadlockDetect");
      break;
    }
    case share::SYS_VAR_INNODB_LOCK_WAIT_TIMEOUT: {
      ret = create_one_sys_var<ObSysVarInnodbLockWaitTimeout>(allocator_, sys_var_ptr, "ObSysVarInnodbLockWaitTimeout");
      break;
    }
    case share::SYS_VAR_INNODB_PRINT_ALL_DEADLOCKS: {
      ret = create_one_sys_var<ObSysVarInnodbPrintAllDeadlocks>(allocator_, sys_var_ptr, "ObSysVarInnodbPrintAllDeadlocks");
      break;
    }
    case share::SYS_VAR_INNODB_TABLE_LOCKS: {
      ret = create_one_sys_var<ObSysVarInnodbTableLocks>(allocator_, sys_var_ptr, "ObSysVarInnodbTableLocks");
      break;
    }
    case share::SYS_VAR_MAX_WRITE_LOCK_COUNT: {
      ret = create_one_sys_var<ObSysVarMaxWriteLockCount>(allocator_, sys_var_ptr, "ObSysVarMaxWriteLockCount");
      break;
    }
    case share::SYS_VAR__OB_ENABLE_ROLE_IDS: {
      ret = create_one_sys_var<ObSysVarObEnableRoleIds>(allocator_, sys_var_ptr, "ObSysVarObEnableRoleIds");
      break;
    }
    case share::SYS_VAR_INNODB_READ_ONLY: {
      ret = create_one_sys_var<ObSysVarInnodbReadOnly>(allocator_, sys_var_ptr, "ObSysVarInnodbReadOnly");
      break;
    }
    case share::SYS_VAR_INNODB_API_DISABLE_ROWLOCK: {
      ret = create_one_sys_var<ObSysVarInnodbApiDisableRowlock>(allocator_, sys_var_ptr, "ObSysVarInnodbApiDisableRowlock");
      break;
    }
    case share::SYS_VAR_INNODB_AUTOINC_LOCK_MODE: {
      ret = create_one_sys_var<ObSysVarInnodbAutoincLockMode>(allocator_, sys_var_ptr, "ObSysVarInnodbAutoincLockMode");
      break;
    }
    case share::SYS_VAR_SKIP_EXTERNAL_LOCKING: {
      ret = create_one_sys_var<ObSysVarSkipExternalLocking>(allocator_, sys_var_ptr, "ObSysVarSkipExternalLocking");
      break;
    }
    case share::SYS_VAR_SUPER_READ_ONLY: {
      ret = create_one_sys_var<ObSysVarSuperReadOnly>(allocator_, sys_var_ptr, "ObSysVarSuperReadOnly");
      break;
    }
    case share::SYS_VAR_LOW_PRIORITY_UPDATES: {
      ret = create_one_sys_var<ObSysVarLowPriorityUpdates>(allocator_, sys_var_ptr, "ObSysVarLowPriorityUpdates");
      break;
    }
    case share::SYS_VAR_MAX_ERROR_COUNT: {
      ret = create_one_sys_var<ObSysVarMaxErrorCount>(allocator_, sys_var_ptr, "ObSysVarMaxErrorCount");
      break;
    }
    case share::SYS_VAR_MAX_INSERT_DELAYED_THREADS: {
      ret = create_one_sys_var<ObSysVarMaxInsertDelayedThreads>(allocator_, sys_var_ptr, "ObSysVarMaxInsertDelayedThreads");
      break;
    }
    case share::SYS_VAR_FT_STOPWORD_FILE: {
      ret = create_one_sys_var<ObSysVarFtStopwordFile>(allocator_, sys_var_ptr, "ObSysVarFtStopwordFile");
      break;
    }
    case share::SYS_VAR_INNODB_FT_CACHE_SIZE: {
      ret = create_one_sys_var<ObSysVarInnodbFtCacheSize>(allocator_, sys_var_ptr, "ObSysVarInnodbFtCacheSize");
      break;
    }
    case share::SYS_VAR_INNODB_FT_SORT_PLL_DEGREE: {
      ret = create_one_sys_var<ObSysVarInnodbFtSortPllDegree>(allocator_, sys_var_ptr, "ObSysVarInnodbFtSortPllDegree");
      break;
    }
    case share::SYS_VAR_INNODB_FT_TOTAL_CACHE_SIZE: {
      ret = create_one_sys_var<ObSysVarInnodbFtTotalCacheSize>(allocator_, sys_var_ptr, "ObSysVarInnodbFtTotalCacheSize");
      break;
    }
    case share::SYS_VAR_MECAB_RC_FILE: {
      ret = create_one_sys_var<ObSysVarMecabRcFile>(allocator_, sys_var_ptr, "ObSysVarMecabRcFile");
      break;
    }
    case share::SYS_VAR_METADATA_LOCKS_CACHE_SIZE: {
      ret = create_one_sys_var<ObSysVarMetadataLocksCacheSize>(allocator_, sys_var_ptr, "ObSysVarMetadataLocksCacheSize");
      break;
    }
    case share::SYS_VAR_METADATA_LOCKS_HASH_INSTANCES: {
      ret = create_one_sys_var<ObSysVarMetadataLocksHashInstances>(allocator_, sys_var_ptr, "ObSysVarMetadataLocksHashInstances");
      break;
    }
    case share::SYS_VAR_INNODB_TEMP_DATA_FILE_PATH: {
      ret = create_one_sys_var<ObSysVarInnodbTempDataFilePath>(allocator_, sys_var_ptr, "ObSysVarInnodbTempDataFilePath");
      break;
    }
    case share::SYS_VAR_INNODB_DATA_FILE_PATH: {
      ret = create_one_sys_var<ObSysVarInnodbDataFilePath>(allocator_, sys_var_ptr, "ObSysVarInnodbDataFilePath");
      break;
    }
    case share::SYS_VAR_INNODB_DATA_HOME_DIR: {
      ret = create_one_sys_var<ObSysVarInnodbDataHomeDir>(allocator_, sys_var_ptr, "ObSysVarInnodbDataHomeDir");
      break;
    }
    case share::SYS_VAR_DEFAULT_TMP_STORAGE_ENGINE: {
      ret = create_one_sys_var<ObSysVarDefaultTmpStorageEngine>(allocator_, sys_var_ptr, "ObSysVarDefaultTmpStorageEngine");
      break;
    }
    case share::SYS_VAR_INNODB_FT_ENABLE_DIAG_PRINT: {
      ret = create_one_sys_var<ObSysVarInnodbFtEnableDiagPrint>(allocator_, sys_var_ptr, "ObSysVarInnodbFtEnableDiagPrint");
      break;
    }
    case share::SYS_VAR_INNODB_FT_NUM_WORD_OPTIMIZE: {
      ret = create_one_sys_var<ObSysVarInnodbFtNumWordOptimize>(allocator_, sys_var_ptr, "ObSysVarInnodbFtNumWordOptimize");
      break;
    }
    case share::SYS_VAR_INNODB_FT_RESULT_CACHE_LIMIT: {
      ret = create_one_sys_var<ObSysVarInnodbFtResultCacheLimit>(allocator_, sys_var_ptr, "ObSysVarInnodbFtResultCacheLimit");
      break;
    }
    case share::SYS_VAR_INNODB_FT_SERVER_STOPWORD_TABLE: {
      ret = create_one_sys_var<ObSysVarInnodbFtServerStopwordTable>(allocator_, sys_var_ptr, "ObSysVarInnodbFtServerStopwordTable");
      break;
    }
    case share::SYS_VAR_INNODB_OPTIMIZE_FULLTEXT_ONLY: {
      ret = create_one_sys_var<ObSysVarInnodbOptimizeFulltextOnly>(allocator_, sys_var_ptr, "ObSysVarInnodbOptimizeFulltextOnly");
      break;
    }
    case share::SYS_VAR_MAX_TMP_TABLES: {
      ret = create_one_sys_var<ObSysVarMaxTmpTables>(allocator_, sys_var_ptr, "ObSysVarMaxTmpTables");
      break;
    }
    case share::SYS_VAR_INNODB_TMPDIR: {
      ret = create_one_sys_var<ObSysVarInnodbTmpdir>(allocator_, sys_var_ptr, "ObSysVarInnodbTmpdir");
      break;
    }
    case share::SYS_VAR_GROUP_REPLICATION_GROUP_SEEDS: {
      ret = create_one_sys_var<ObSysVarGroupReplicationGroupSeeds>(allocator_, sys_var_ptr, "ObSysVarGroupReplicationGroupSeeds");
      break;
    }
    case share::SYS_VAR_SLAVE_ROWS_SEARCH_ALGORITHMS: {
      ret = create_one_sys_var<ObSysVarSlaveRowsSearchAlgorithms>(allocator_, sys_var_ptr, "ObSysVarSlaveRowsSearchAlgorithms");
      break;
    }
    case share::SYS_VAR_SLAVE_TYPE_CONVERSIONS: {
      ret = create_one_sys_var<ObSysVarSlaveTypeConversions>(allocator_, sys_var_ptr, "ObSysVarSlaveTypeConversions");
      break;
    }
    case share::SYS_VAR_OB_HNSW_EF_SEARCH: {
      ret = create_one_sys_var<ObSysVarObHnswEfSearch>(allocator_, sys_var_ptr, "ObSysVarObHnswEfSearch");
      break;
    }
    case share::SYS_VAR_NDB_ALLOW_COPYING_ALTER_TABLE: {
      ret = create_one_sys_var<ObSysVarNdbAllowCopyingAlterTable>(allocator_, sys_var_ptr, "ObSysVarNdbAllowCopyingAlterTable");
      break;
    }
    case share::SYS_VAR_NDB_AUTOINCREMENT_PREFETCH_SZ: {
      ret = create_one_sys_var<ObSysVarNdbAutoincrementPrefetchSz>(allocator_, sys_var_ptr, "ObSysVarNdbAutoincrementPrefetchSz");
      break;
    }
    case share::SYS_VAR_NDB_BLOB_READ_BATCH_BYTES: {
      ret = create_one_sys_var<ObSysVarNdbBlobReadBatchBytes>(allocator_, sys_var_ptr, "ObSysVarNdbBlobReadBatchBytes");
      break;
    }
    case share::SYS_VAR_NDB_BLOB_WRITE_BATCH_BYTES: {
      ret = create_one_sys_var<ObSysVarNdbBlobWriteBatchBytes>(allocator_, sys_var_ptr, "ObSysVarNdbBlobWriteBatchBytes");
      break;
    }
    case share::SYS_VAR_NDB_CACHE_CHECK_TIME: {
      ret = create_one_sys_var<ObSysVarNdbCacheCheckTime>(allocator_, sys_var_ptr, "ObSysVarNdbCacheCheckTime");
      break;
    }
    case share::SYS_VAR_NDB_CLEAR_APPLY_STATUS: {
      ret = create_one_sys_var<ObSysVarNdbClearApplyStatus>(allocator_, sys_var_ptr, "ObSysVarNdbClearApplyStatus");
      break;
    }
    case share::SYS_VAR_NDB_DATA_NODE_NEIGHBOUR: {
      ret = create_one_sys_var<ObSysVarNdbDataNodeNeighbour>(allocator_, sys_var_ptr, "ObSysVarNdbDataNodeNeighbour");
      break;
    }
    case share::SYS_VAR_NDB_DEFAULT_COLUMN_FORMAT: {
      ret = create_one_sys_var<ObSysVarNdbDefaultColumnFormat>(allocator_, sys_var_ptr, "ObSysVarNdbDefaultColumnFormat");
      break;
    }
    case share::SYS_VAR_NDB_DEFERRED_CONSTRAINTS: {
      ret = create_one_sys_var<ObSysVarNdbDeferredConstraints>(allocator_, sys_var_ptr, "ObSysVarNdbDeferredConstraints");
      break;
    }
    case share::SYS_VAR_NDB_DISTRIBUTION: {
      ret = create_one_sys_var<ObSysVarNdbDistribution>(allocator_, sys_var_ptr, "ObSysVarNdbDistribution");
      break;
    }
    case share::SYS_VAR_NDB_EVENTBUFFER_FREE_PERCENT: {
      ret = create_one_sys_var<ObSysVarNdbEventbufferFreePercent>(allocator_, sys_var_ptr, "ObSysVarNdbEventbufferFreePercent");
      break;
    }
    case share::SYS_VAR_NDB_EVENTBUFFER_MAX_ALLOC: {
      ret = create_one_sys_var<ObSysVarNdbEventbufferMaxAlloc>(allocator_, sys_var_ptr, "ObSysVarNdbEventbufferMaxAlloc");
      break;
    }
    case share::SYS_VAR_NDB_EXTRA_LOGGING: {
      ret = create_one_sys_var<ObSysVarNdbExtraLogging>(allocator_, sys_var_ptr, "ObSysVarNdbExtraLogging");
      break;
    }
    case share::SYS_VAR_NDB_FORCE_SEND: {
      ret = create_one_sys_var<ObSysVarNdbForceSend>(allocator_, sys_var_ptr, "ObSysVarNdbForceSend");
      break;
    }
    case share::SYS_VAR_NDB_FULLY_REPLICATED: {
      ret = create_one_sys_var<ObSysVarNdbFullyReplicated>(allocator_, sys_var_ptr, "ObSysVarNdbFullyReplicated");
      break;
    }
    case share::SYS_VAR_NDB_INDEX_STAT_ENABLE: {
      ret = create_one_sys_var<ObSysVarNdbIndexStatEnable>(allocator_, sys_var_ptr, "ObSysVarNdbIndexStatEnable");
      break;
    }
    case share::SYS_VAR_NDB_INDEX_STAT_OPTION: {
      ret = create_one_sys_var<ObSysVarNdbIndexStatOption>(allocator_, sys_var_ptr, "ObSysVarNdbIndexStatOption");
      break;
    }
    case share::SYS_VAR_NDB_JOIN_PUSHDOWN: {
      ret = create_one_sys_var<ObSysVarNdbJoinPushdown>(allocator_, sys_var_ptr, "ObSysVarNdbJoinPushdown");
      break;
    }
    case share::SYS_VAR_NDB_LOG_BINLOG_INDEX: {
      ret = create_one_sys_var<ObSysVarNdbLogBinlogIndex>(allocator_, sys_var_ptr, "ObSysVarNdbLogBinlogIndex");
      break;
    }
    case share::SYS_VAR_NDB_LOG_EMPTY_EPOCHS: {
      ret = create_one_sys_var<ObSysVarNdbLogEmptyEpochs>(allocator_, sys_var_ptr, "ObSysVarNdbLogEmptyEpochs");
      break;
    }
    case share::SYS_VAR_NDB_LOG_EMPTY_UPDATE: {
      ret = create_one_sys_var<ObSysVarNdbLogEmptyUpdate>(allocator_, sys_var_ptr, "ObSysVarNdbLogEmptyUpdate");
      break;
    }
    case share::SYS_VAR_NDB_LOG_EXCLUSIVE_READS: {
      ret = create_one_sys_var<ObSysVarNdbLogExclusiveReads>(allocator_, sys_var_ptr, "ObSysVarNdbLogExclusiveReads");
      break;
    }
    case share::SYS_VAR_NDB_LOG_UPDATE_AS_WRITE: {
      ret = create_one_sys_var<ObSysVarNdbLogUpdateAsWrite>(allocator_, sys_var_ptr, "ObSysVarNdbLogUpdateAsWrite");
      break;
    }
    case share::SYS_VAR_NDB_LOG_UPDATE_MINIMAL: {
      ret = create_one_sys_var<ObSysVarNdbLogUpdateMinimal>(allocator_, sys_var_ptr, "ObSysVarNdbLogUpdateMinimal");
      break;
    }
    case share::SYS_VAR_NDB_LOG_UPDATED_ONLY: {
      ret = create_one_sys_var<ObSysVarNdbLogUpdatedOnly>(allocator_, sys_var_ptr, "ObSysVarNdbLogUpdatedOnly");
      break;
    }
    case share::SYS_VAR_NDB_OPTIMIZATION_DELAY: {
      ret = create_one_sys_var<ObSysVarNdbOptimizationDelay>(allocator_, sys_var_ptr, "ObSysVarNdbOptimizationDelay");
      break;
    }
    case share::SYS_VAR_NDB_READ_BACKUP: {
      ret = create_one_sys_var<ObSysVarNdbReadBackup>(allocator_, sys_var_ptr, "ObSysVarNdbReadBackup");
      break;
    }
    case share::SYS_VAR_NDB_RECV_THREAD_ACTIVATION_THRESHOLD: {
      ret = create_one_sys_var<ObSysVarNdbRecvThreadActivationThreshold>(allocator_, sys_var_ptr, "ObSysVarNdbRecvThreadActivationThreshold");
      break;
    }
    case share::SYS_VAR_NDB_RECV_THREAD_CPU_MASK: {
      ret = create_one_sys_var<ObSysVarNdbRecvThreadCpuMask>(allocator_, sys_var_ptr, "ObSysVarNdbRecvThreadCpuMask");
      break;
    }
    case share::SYS_VAR_NDB_REPORT_THRESH_BINLOG_EPOCH_SLIP: {
      ret = create_one_sys_var<ObSysVarNdbReportThreshBinlogEpochSlip>(allocator_, sys_var_ptr, "ObSysVarNdbReportThreshBinlogEpochSlip");
      break;
    }
    case share::SYS_VAR_NDB_REPORT_THRESH_BINLOG_MEM_USAGE: {
      ret = create_one_sys_var<ObSysVarNdbReportThreshBinlogMemUsage>(allocator_, sys_var_ptr, "ObSysVarNdbReportThreshBinlogMemUsage");
      break;
    }
    case share::SYS_VAR_NDB_ROW_CHECKSUM: {
      ret = create_one_sys_var<ObSysVarNdbRowChecksum>(allocator_, sys_var_ptr, "ObSysVarNdbRowChecksum");
      break;
    }
    case share::SYS_VAR_NDB_SHOW_FOREIGN_KEY_MOCK_TABLES: {
      ret = create_one_sys_var<ObSysVarNdbShowForeignKeyMockTables>(allocator_, sys_var_ptr, "ObSysVarNdbShowForeignKeyMockTables");
      break;
    }
    case share::SYS_VAR_NDB_SLAVE_CONFLICT_ROLE: {
      ret = create_one_sys_var<ObSysVarNdbSlaveConflictRole>(allocator_, sys_var_ptr, "ObSysVarNdbSlaveConflictRole");
      break;
    }
    case share::SYS_VAR_NDB_TABLE_NO_LOGGING: {
      ret = create_one_sys_var<ObSysVarNdbTableNoLogging>(allocator_, sys_var_ptr, "ObSysVarNdbTableNoLogging");
      break;
    }
    case share::SYS_VAR_NDB_TABLE_TEMPORARY: {
      ret = create_one_sys_var<ObSysVarNdbTableTemporary>(allocator_, sys_var_ptr, "ObSysVarNdbTableTemporary");
      break;
    }
    case share::SYS_VAR_NDB_USE_EXACT_COUNT: {
      ret = create_one_sys_var<ObSysVarNdbUseExactCount>(allocator_, sys_var_ptr, "ObSysVarNdbUseExactCount");
      break;
    }
    case share::SYS_VAR_NDB_USE_TRANSACTIONS: {
      ret = create_one_sys_var<ObSysVarNdbUseTransactions>(allocator_, sys_var_ptr, "ObSysVarNdbUseTransactions");
      break;
    }
    case share::SYS_VAR_NDBINFO_MAX_BYTES: {
      ret = create_one_sys_var<ObSysVarNdbinfoMaxBytes>(allocator_, sys_var_ptr, "ObSysVarNdbinfoMaxBytes");
      break;
    }
    case share::SYS_VAR_NDBINFO_MAX_ROWS: {
      ret = create_one_sys_var<ObSysVarNdbinfoMaxRows>(allocator_, sys_var_ptr, "ObSysVarNdbinfoMaxRows");
      break;
    }
    case share::SYS_VAR_NDBINFO_OFFLINE: {
      ret = create_one_sys_var<ObSysVarNdbinfoOffline>(allocator_, sys_var_ptr, "ObSysVarNdbinfoOffline");
      break;
    }
    case share::SYS_VAR_NDBINFO_SHOW_HIDDEN: {
      ret = create_one_sys_var<ObSysVarNdbinfoShowHidden>(allocator_, sys_var_ptr, "ObSysVarNdbinfoShowHidden");
      break;
    }
    case share::SYS_VAR_MYISAM_DATA_POINTER_SIZE: {
      ret = create_one_sys_var<ObSysVarMyisamDataPointerSize>(allocator_, sys_var_ptr, "ObSysVarMyisamDataPointerSize");
      break;
    }
    case share::SYS_VAR_MYISAM_MAX_SORT_FILE_SIZE: {
      ret = create_one_sys_var<ObSysVarMyisamMaxSortFileSize>(allocator_, sys_var_ptr, "ObSysVarMyisamMaxSortFileSize");
      break;
    }
    case share::SYS_VAR_MYISAM_REPAIR_THREADS: {
      ret = create_one_sys_var<ObSysVarMyisamRepairThreads>(allocator_, sys_var_ptr, "ObSysVarMyisamRepairThreads");
      break;
    }
    case share::SYS_VAR_MYISAM_SORT_BUFFER_SIZE: {
      ret = create_one_sys_var<ObSysVarMyisamSortBufferSize>(allocator_, sys_var_ptr, "ObSysVarMyisamSortBufferSize");
      break;
    }
    case share::SYS_VAR_MYISAM_STATS_METHOD: {
      ret = create_one_sys_var<ObSysVarMyisamStatsMethod>(allocator_, sys_var_ptr, "ObSysVarMyisamStatsMethod");
      break;
    }
    case share::SYS_VAR_MYISAM_USE_MMAP: {
      ret = create_one_sys_var<ObSysVarMyisamUseMmap>(allocator_, sys_var_ptr, "ObSysVarMyisamUseMmap");
      break;
    }
    case share::SYS_VAR_PRELOAD_BUFFER_SIZE: {
      ret = create_one_sys_var<ObSysVarPreloadBufferSize>(allocator_, sys_var_ptr, "ObSysVarPreloadBufferSize");
      break;
    }
    case share::SYS_VAR_READ_BUFFER_SIZE: {
      ret = create_one_sys_var<ObSysVarReadBufferSize>(allocator_, sys_var_ptr, "ObSysVarReadBufferSize");
      break;
    }
    case share::SYS_VAR_READ_RND_BUFFER_SIZE: {
      ret = create_one_sys_var<ObSysVarReadRndBufferSize>(allocator_, sys_var_ptr, "ObSysVarReadRndBufferSize");
      break;
    }
    case share::SYS_VAR_SYNC_FRM: {
      ret = create_one_sys_var<ObSysVarSyncFrm>(allocator_, sys_var_ptr, "ObSysVarSyncFrm");
      break;
    }
    case share::SYS_VAR_SYNC_MASTER_INFO: {
      ret = create_one_sys_var<ObSysVarSyncMasterInfo>(allocator_, sys_var_ptr, "ObSysVarSyncMasterInfo");
      break;
    }
    case share::SYS_VAR_TABLE_OPEN_CACHE: {
      ret = create_one_sys_var<ObSysVarTableOpenCache>(allocator_, sys_var_ptr, "ObSysVarTableOpenCache");
      break;
    }
    case share::SYS_VAR_INNODB_MONITOR_DISABLE: {
      ret = create_one_sys_var<ObSysVarInnodbMonitorDisable>(allocator_, sys_var_ptr, "ObSysVarInnodbMonitorDisable");
      break;
    }
    case share::SYS_VAR_INNODB_MONITOR_ENABLE: {
      ret = create_one_sys_var<ObSysVarInnodbMonitorEnable>(allocator_, sys_var_ptr, "ObSysVarInnodbMonitorEnable");
      break;
    }
    case share::SYS_VAR_INNODB_MONITOR_RESET: {
      ret = create_one_sys_var<ObSysVarInnodbMonitorReset>(allocator_, sys_var_ptr, "ObSysVarInnodbMonitorReset");
      break;
    }
    case share::SYS_VAR_INNODB_MONITOR_RESET_ALL: {
      ret = create_one_sys_var<ObSysVarInnodbMonitorResetAll>(allocator_, sys_var_ptr, "ObSysVarInnodbMonitorResetAll");
      break;
    }
    case share::SYS_VAR_INNODB_OLD_BLOCKS_PCT: {
      ret = create_one_sys_var<ObSysVarInnodbOldBlocksPct>(allocator_, sys_var_ptr, "ObSysVarInnodbOldBlocksPct");
      break;
    }
    case share::SYS_VAR_INNODB_OLD_BLOCKS_TIME: {
      ret = create_one_sys_var<ObSysVarInnodbOldBlocksTime>(allocator_, sys_var_ptr, "ObSysVarInnodbOldBlocksTime");
      break;
    }
    case share::SYS_VAR_INNODB_PURGE_BATCH_SIZE: {
      ret = create_one_sys_var<ObSysVarInnodbPurgeBatchSize>(allocator_, sys_var_ptr, "ObSysVarInnodbPurgeBatchSize");
      break;
    }
    case share::SYS_VAR_INNODB_PURGE_RSEG_TRUNCATE_FREQUENCY: {
      ret = create_one_sys_var<ObSysVarInnodbPurgeRsegTruncateFrequency>(allocator_, sys_var_ptr, "ObSysVarInnodbPurgeRsegTruncateFrequency");
      break;
    }
    case share::SYS_VAR_INNODB_RANDOM_READ_AHEAD: {
      ret = create_one_sys_var<ObSysVarInnodbRandomReadAhead>(allocator_, sys_var_ptr, "ObSysVarInnodbRandomReadAhead");
      break;
    }
    case share::SYS_VAR_INNODB_READ_AHEAD_THRESHOLD: {
      ret = create_one_sys_var<ObSysVarInnodbReadAheadThreshold>(allocator_, sys_var_ptr, "ObSysVarInnodbReadAheadThreshold");
      break;
    }
    case share::SYS_VAR_INNODB_ROLLBACK_SEGMENTS: {
      ret = create_one_sys_var<ObSysVarInnodbRollbackSegments>(allocator_, sys_var_ptr, "ObSysVarInnodbRollbackSegments");
      break;
    }
    case share::SYS_VAR_INNODB_SPIN_WAIT_DELAY: {
      ret = create_one_sys_var<ObSysVarInnodbSpinWaitDelay>(allocator_, sys_var_ptr, "ObSysVarInnodbSpinWaitDelay");
      break;
    }
    case share::SYS_VAR_INNODB_STATUS_OUTPUT: {
      ret = create_one_sys_var<ObSysVarInnodbStatusOutput>(allocator_, sys_var_ptr, "ObSysVarInnodbStatusOutput");
      break;
    }
    case share::SYS_VAR_INNODB_STATUS_OUTPUT_LOCKS: {
      ret = create_one_sys_var<ObSysVarInnodbStatusOutputLocks>(allocator_, sys_var_ptr, "ObSysVarInnodbStatusOutputLocks");
      break;
    }
    case share::SYS_VAR_INNODB_SYNC_SPIN_LOOPS: {
      ret = create_one_sys_var<ObSysVarInnodbSyncSpinLoops>(allocator_, sys_var_ptr, "ObSysVarInnodbSyncSpinLoops");
      break;
    }
    case share::SYS_VAR_INTERNAL_TMP_DISK_STORAGE_ENGINE: {
      ret = create_one_sys_var<ObSysVarInternalTmpDiskStorageEngine>(allocator_, sys_var_ptr, "ObSysVarInternalTmpDiskStorageEngine");
      break;
    }
    case share::SYS_VAR_KEEP_FILES_ON_CREATE: {
      ret = create_one_sys_var<ObSysVarKeepFilesOnCreate>(allocator_, sys_var_ptr, "ObSysVarKeepFilesOnCreate");
      break;
    }
    case share::SYS_VAR_MAX_HEAP_TABLE_SIZE: {
      ret = create_one_sys_var<ObSysVarMaxHeapTableSize>(allocator_, sys_var_ptr, "ObSysVarMaxHeapTableSize");
      break;
    }
    case share::SYS_VAR_BULK_INSERT_BUFFER_SIZE: {
      ret = create_one_sys_var<ObSysVarBulkInsertBufferSize>(allocator_, sys_var_ptr, "ObSysVarBulkInsertBufferSize");
      break;
    }
    case share::SYS_VAR_HOST_CACHE_SIZE: {
      ret = create_one_sys_var<ObSysVarHostCacheSize>(allocator_, sys_var_ptr, "ObSysVarHostCacheSize");
      break;
    }
    case share::SYS_VAR_INIT_SLAVE: {
      ret = create_one_sys_var<ObSysVarInitSlave>(allocator_, sys_var_ptr, "ObSysVarInitSlave");
      break;
    }
    case share::SYS_VAR_INNODB_FAST_SHUTDOWN: {
      ret = create_one_sys_var<ObSysVarInnodbFastShutdown>(allocator_, sys_var_ptr, "ObSysVarInnodbFastShutdown");
      break;
    }
    case share::SYS_VAR_INNODB_IO_CAPACITY: {
      ret = create_one_sys_var<ObSysVarInnodbIoCapacity>(allocator_, sys_var_ptr, "ObSysVarInnodbIoCapacity");
      break;
    }
    case share::SYS_VAR_INNODB_IO_CAPACITY_MAX: {
      ret = create_one_sys_var<ObSysVarInnodbIoCapacityMax>(allocator_, sys_var_ptr, "ObSysVarInnodbIoCapacityMax");
      break;
    }
    case share::SYS_VAR_INNODB_THREAD_CONCURRENCY: {
      ret = create_one_sys_var<ObSysVarInnodbThreadConcurrency>(allocator_, sys_var_ptr, "ObSysVarInnodbThreadConcurrency");
      break;
    }
    case share::SYS_VAR_INNODB_THREAD_SLEEP_DELAY: {
      ret = create_one_sys_var<ObSysVarInnodbThreadSleepDelay>(allocator_, sys_var_ptr, "ObSysVarInnodbThreadSleepDelay");
      break;
    }
    case share::SYS_VAR_LOG_ERROR_VERBOSITY: {
      ret = create_one_sys_var<ObSysVarLogErrorVerbosity>(allocator_, sys_var_ptr, "ObSysVarLogErrorVerbosity");
      break;
    }
    case share::SYS_VAR_LOG_OUTPUT: {
      ret = create_one_sys_var<ObSysVarLogOutput>(allocator_, sys_var_ptr, "ObSysVarLogOutput");
      break;
    }
    case share::SYS_VAR_LOG_QUERIES_NOT_USING_INDEXES: {
      ret = create_one_sys_var<ObSysVarLogQueriesNotUsingIndexes>(allocator_, sys_var_ptr, "ObSysVarLogQueriesNotUsingIndexes");
      break;
    }
    case share::SYS_VAR_LOG_SLOW_ADMIN_STATEMENTS: {
      ret = create_one_sys_var<ObSysVarLogSlowAdminStatements>(allocator_, sys_var_ptr, "ObSysVarLogSlowAdminStatements");
      break;
    }
    case share::SYS_VAR_LOG_SLOW_SLAVE_STATEMENTS: {
      ret = create_one_sys_var<ObSysVarLogSlowSlaveStatements>(allocator_, sys_var_ptr, "ObSysVarLogSlowSlaveStatements");
      break;
    }
    case share::SYS_VAR_LOG_STATEMENTS_UNSAFE_FOR_BINLOG: {
      ret = create_one_sys_var<ObSysVarLogStatementsUnsafeForBinlog>(allocator_, sys_var_ptr, "ObSysVarLogStatementsUnsafeForBinlog");
      break;
    }
    case share::SYS_VAR_LOG_SYSLOG: {
      ret = create_one_sys_var<ObSysVarLogSyslog>(allocator_, sys_var_ptr, "ObSysVarLogSyslog");
      break;
    }
    case share::SYS_VAR_LOG_SYSLOG_FACILITY: {
      ret = create_one_sys_var<ObSysVarLogSyslogFacility>(allocator_, sys_var_ptr, "ObSysVarLogSyslogFacility");
      break;
    }
    case share::SYS_VAR_LOG_SYSLOG_INCLUDE_PID: {
      ret = create_one_sys_var<ObSysVarLogSyslogIncludePid>(allocator_, sys_var_ptr, "ObSysVarLogSyslogIncludePid");
      break;
    }
    case share::SYS_VAR_LOG_SYSLOG_TAG: {
      ret = create_one_sys_var<ObSysVarLogSyslogTag>(allocator_, sys_var_ptr, "ObSysVarLogSyslogTag");
      break;
    }
    case share::SYS_VAR_LOG_THROTTLE_QUERIES_NOT_USING_INDEXES: {
      ret = create_one_sys_var<ObSysVarLogThrottleQueriesNotUsingIndexes>(allocator_, sys_var_ptr, "ObSysVarLogThrottleQueriesNotUsingIndexes");
      break;
    }
    case share::SYS_VAR_LOG_TIMESTAMPS: {
      ret = create_one_sys_var<ObSysVarLogTimestamps>(allocator_, sys_var_ptr, "ObSysVarLogTimestamps");
      break;
    }
    case share::SYS_VAR_LOG_WARNINGS: {
      ret = create_one_sys_var<ObSysVarLogWarnings>(allocator_, sys_var_ptr, "ObSysVarLogWarnings");
      break;
    }
    case share::SYS_VAR_MAX_DELAYED_THREADS: {
      ret = create_one_sys_var<ObSysVarMaxDelayedThreads>(allocator_, sys_var_ptr, "ObSysVarMaxDelayedThreads");
      break;
    }
    case share::SYS_VAR_OFFLINE_MODE: {
      ret = create_one_sys_var<ObSysVarOfflineMode>(allocator_, sys_var_ptr, "ObSysVarOfflineMode");
      break;
    }
    case share::SYS_VAR_REQUIRE_SECURE_TRANSPORT: {
      ret = create_one_sys_var<ObSysVarRequireSecureTransport>(allocator_, sys_var_ptr, "ObSysVarRequireSecureTransport");
      break;
    }
    case share::SYS_VAR_SLOW_LAUNCH_TIME: {
      ret = create_one_sys_var<ObSysVarSlowLaunchTime>(allocator_, sys_var_ptr, "ObSysVarSlowLaunchTime");
      break;
    }
    case share::SYS_VAR_SQL_LOG_OFF: {
      ret = create_one_sys_var<ObSysVarSqlLogOff>(allocator_, sys_var_ptr, "ObSysVarSqlLogOff");
      break;
    }
    case share::SYS_VAR_THREAD_CACHE_SIZE: {
      ret = create_one_sys_var<ObSysVarThreadCacheSize>(allocator_, sys_var_ptr, "ObSysVarThreadCacheSize");
      break;
    }
    case share::SYS_VAR_THREAD_POOL_HIGH_PRIORITY_CONNECTION: {
      ret = create_one_sys_var<ObSysVarThreadPoolHighPriorityConnection>(allocator_, sys_var_ptr, "ObSysVarThreadPoolHighPriorityConnection");
      break;
    }
    case share::SYS_VAR_THREAD_POOL_MAX_UNUSED_THREADS: {
      ret = create_one_sys_var<ObSysVarThreadPoolMaxUnusedThreads>(allocator_, sys_var_ptr, "ObSysVarThreadPoolMaxUnusedThreads");
      break;
    }
    case share::SYS_VAR_THREAD_POOL_PRIO_KICKUP_TIMER: {
      ret = create_one_sys_var<ObSysVarThreadPoolPrioKickupTimer>(allocator_, sys_var_ptr, "ObSysVarThreadPoolPrioKickupTimer");
      break;
    }
    case share::SYS_VAR_THREAD_POOL_STALL_LIMIT: {
      ret = create_one_sys_var<ObSysVarThreadPoolStallLimit>(allocator_, sys_var_ptr, "ObSysVarThreadPoolStallLimit");
      break;
    }
    case share::SYS_VAR_HAVE_STATEMENT_TIMEOUT: {
      ret = create_one_sys_var<ObSysVarHaveStatementTimeout>(allocator_, sys_var_ptr, "ObSysVarHaveStatementTimeout");
      break;
    }
    case share::SYS_VAR_MYSQLX_BIND_ADDRESS: {
      ret = create_one_sys_var<ObSysVarMysqlxBindAddress>(allocator_, sys_var_ptr, "ObSysVarMysqlxBindAddress");
      break;
    }
    case share::SYS_VAR_MYSQLX_PORT: {
      ret = create_one_sys_var<ObSysVarMysqlxPort>(allocator_, sys_var_ptr, "ObSysVarMysqlxPort");
      break;
    }
    case share::SYS_VAR_MYSQLX_PORT_OPEN_TIMEOUT: {
      ret = create_one_sys_var<ObSysVarMysqlxPortOpenTimeout>(allocator_, sys_var_ptr, "ObSysVarMysqlxPortOpenTimeout");
      break;
    }
    case share::SYS_VAR_MYSQLX_SOCKET: {
      ret = create_one_sys_var<ObSysVarMysqlxSocket>(allocator_, sys_var_ptr, "ObSysVarMysqlxSocket");
      break;
    }
    case share::SYS_VAR_MYSQLX_SSL_CA: {
      ret = create_one_sys_var<ObSysVarMysqlxSslCa>(allocator_, sys_var_ptr, "ObSysVarMysqlxSslCa");
      break;
    }
    case share::SYS_VAR_MYSQLX_SSL_CAPATH: {
      ret = create_one_sys_var<ObSysVarMysqlxSslCapath>(allocator_, sys_var_ptr, "ObSysVarMysqlxSslCapath");
      break;
    }
    case share::SYS_VAR_MYSQLX_SSL_CERT: {
      ret = create_one_sys_var<ObSysVarMysqlxSslCert>(allocator_, sys_var_ptr, "ObSysVarMysqlxSslCert");
      break;
    }
    case share::SYS_VAR_MYSQLX_SSL_CIPHER: {
      ret = create_one_sys_var<ObSysVarMysqlxSslCipher>(allocator_, sys_var_ptr, "ObSysVarMysqlxSslCipher");
      break;
    }
    case share::SYS_VAR_MYSQLX_SSL_CRL: {
      ret = create_one_sys_var<ObSysVarMysqlxSslCrl>(allocator_, sys_var_ptr, "ObSysVarMysqlxSslCrl");
      break;
    }
    case share::SYS_VAR_MYSQLX_SSL_CRLPATH: {
      ret = create_one_sys_var<ObSysVarMysqlxSslCrlpath>(allocator_, sys_var_ptr, "ObSysVarMysqlxSslCrlpath");
      break;
    }
    case share::SYS_VAR_MYSQLX_SSL_KEY: {
      ret = create_one_sys_var<ObSysVarMysqlxSslKey>(allocator_, sys_var_ptr, "ObSysVarMysqlxSslKey");
      break;
    }
    case share::SYS_VAR_OLD: {
      ret = create_one_sys_var<ObSysVarOld>(allocator_, sys_var_ptr, "ObSysVarOld");
      break;
    }
    case share::SYS_VAR_PERFORMANCE_SCHEMA_ACCOUNTS_SIZE: {
      ret = create_one_sys_var<ObSysVarPerformanceSchemaAccountsSize>(allocator_, sys_var_ptr, "ObSysVarPerformanceSchemaAccountsSize");
      break;
    }
    case share::SYS_VAR_PERFORMANCE_SCHEMA_DIGESTS_SIZE: {
      ret = create_one_sys_var<ObSysVarPerformanceSchemaDigestsSize>(allocator_, sys_var_ptr, "ObSysVarPerformanceSchemaDigestsSize");
      break;
    }
    case share::SYS_VAR_PERFORMANCE_SCHEMA_EVENTS_STAGES_HISTORY_LONG_SIZE: {
      ret = create_one_sys_var<ObSysVarPerformanceSchemaEventsStagesHistoryLongSize>(allocator_, sys_var_ptr, "ObSysVarPerformanceSchemaEventsStagesHistoryLongSize");
      break;
    }
    case share::SYS_VAR_PERFORMANCE_SCHEMA_EVENTS_STAGES_HISTORY_SIZE: {
      ret = create_one_sys_var<ObSysVarPerformanceSchemaEventsStagesHistorySize>(allocator_, sys_var_ptr, "ObSysVarPerformanceSchemaEventsStagesHistorySize");
      break;
    }
    case share::SYS_VAR_PERFORMANCE_SCHEMA_EVENTS_STATEMENTS_HISTORY_LONG_SIZE: {
      ret = create_one_sys_var<ObSysVarPerformanceSchemaEventsStatementsHistoryLongSize>(allocator_, sys_var_ptr, "ObSysVarPerformanceSchemaEventsStatementsHistoryLongSize");
      break;
    }
    case share::SYS_VAR_PERFORMANCE_SCHEMA_EVENTS_STATEMENTS_HISTORY_SIZE: {
      ret = create_one_sys_var<ObSysVarPerformanceSchemaEventsStatementsHistorySize>(allocator_, sys_var_ptr, "ObSysVarPerformanceSchemaEventsStatementsHistorySize");
      break;
    }
    case share::SYS_VAR_PERFORMANCE_SCHEMA_EVENTS_TRANSACTIONS_HISTORY_LONG_SIZE: {
      ret = create_one_sys_var<ObSysVarPerformanceSchemaEventsTransactionsHistoryLongSize>(allocator_, sys_var_ptr, "ObSysVarPerformanceSchemaEventsTransactionsHistoryLongSize");
      break;
    }
    case share::SYS_VAR_PERFORMANCE_SCHEMA_EVENTS_TRANSACTIONS_HISTORY_SIZE: {
      ret = create_one_sys_var<ObSysVarPerformanceSchemaEventsTransactionsHistorySize>(allocator_, sys_var_ptr, "ObSysVarPerformanceSchemaEventsTransactionsHistorySize");
      break;
    }
    case share::SYS_VAR_PERFORMANCE_SCHEMA_EVENTS_WAITS_HISTORY_LONG_SIZE: {
      ret = create_one_sys_var<ObSysVarPerformanceSchemaEventsWaitsHistoryLongSize>(allocator_, sys_var_ptr, "ObSysVarPerformanceSchemaEventsWaitsHistoryLongSize");
      break;
    }
    case share::SYS_VAR_PERFORMANCE_SCHEMA_EVENTS_WAITS_HISTORY_SIZE: {
      ret = create_one_sys_var<ObSysVarPerformanceSchemaEventsWaitsHistorySize>(allocator_, sys_var_ptr, "ObSysVarPerformanceSchemaEventsWaitsHistorySize");
      break;
    }
    case share::SYS_VAR_PERFORMANCE_SCHEMA_HOSTS_SIZE: {
      ret = create_one_sys_var<ObSysVarPerformanceSchemaHostsSize>(allocator_, sys_var_ptr, "ObSysVarPerformanceSchemaHostsSize");
      break;
    }
    case share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_COND_CLASSES: {
      ret = create_one_sys_var<ObSysVarPerformanceSchemaMaxCondClasses>(allocator_, sys_var_ptr, "ObSysVarPerformanceSchemaMaxCondClasses");
      break;
    }
    case share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_COND_INSTANCES: {
      ret = create_one_sys_var<ObSysVarPerformanceSchemaMaxCondInstances>(allocator_, sys_var_ptr, "ObSysVarPerformanceSchemaMaxCondInstances");
      break;
    }
    case share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_DIGEST_LENGTH: {
      ret = create_one_sys_var<ObSysVarPerformanceSchemaMaxDigestLength>(allocator_, sys_var_ptr, "ObSysVarPerformanceSchemaMaxDigestLength");
      break;
    }
    case share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_FILE_CLASSES: {
      ret = create_one_sys_var<ObSysVarPerformanceSchemaMaxFileClasses>(allocator_, sys_var_ptr, "ObSysVarPerformanceSchemaMaxFileClasses");
      break;
    }
    case share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_FILE_HANDLES: {
      ret = create_one_sys_var<ObSysVarPerformanceSchemaMaxFileHandles>(allocator_, sys_var_ptr, "ObSysVarPerformanceSchemaMaxFileHandles");
      break;
    }
    case share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_FILE_INSTANCES: {
      ret = create_one_sys_var<ObSysVarPerformanceSchemaMaxFileInstances>(allocator_, sys_var_ptr, "ObSysVarPerformanceSchemaMaxFileInstances");
      break;
    }
    case share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_INDEX_STAT: {
      ret = create_one_sys_var<ObSysVarPerformanceSchemaMaxIndexStat>(allocator_, sys_var_ptr, "ObSysVarPerformanceSchemaMaxIndexStat");
      break;
    }
    case share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_MEMORY_CLASSES: {
      ret = create_one_sys_var<ObSysVarPerformanceSchemaMaxMemoryClasses>(allocator_, sys_var_ptr, "ObSysVarPerformanceSchemaMaxMemoryClasses");
      break;
    }
    case share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_METADATA_LOCKS: {
      ret = create_one_sys_var<ObSysVarPerformanceSchemaMaxMetadataLocks>(allocator_, sys_var_ptr, "ObSysVarPerformanceSchemaMaxMetadataLocks");
      break;
    }
    case share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_MUTEX_CLASSES: {
      ret = create_one_sys_var<ObSysVarPerformanceSchemaMaxMutexClasses>(allocator_, sys_var_ptr, "ObSysVarPerformanceSchemaMaxMutexClasses");
      break;
    }
    case share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_MUTEX_INSTANCES: {
      ret = create_one_sys_var<ObSysVarPerformanceSchemaMaxMutexInstances>(allocator_, sys_var_ptr, "ObSysVarPerformanceSchemaMaxMutexInstances");
      break;
    }
    case share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_PREPARED_STATEMENTS_INSTANCES: {
      ret = create_one_sys_var<ObSysVarPerformanceSchemaMaxPreparedStatementsInstances>(allocator_, sys_var_ptr, "ObSysVarPerformanceSchemaMaxPreparedStatementsInstances");
      break;
    }
    case share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_PROGRAM_INSTANCES: {
      ret = create_one_sys_var<ObSysVarPerformanceSchemaMaxProgramInstances>(allocator_, sys_var_ptr, "ObSysVarPerformanceSchemaMaxProgramInstances");
      break;
    }
    case share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_RWLOCK_CLASSES: {
      ret = create_one_sys_var<ObSysVarPerformanceSchemaMaxRwlockClasses>(allocator_, sys_var_ptr, "ObSysVarPerformanceSchemaMaxRwlockClasses");
      break;
    }
    case share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_RWLOCK_INSTANCES: {
      ret = create_one_sys_var<ObSysVarPerformanceSchemaMaxRwlockInstances>(allocator_, sys_var_ptr, "ObSysVarPerformanceSchemaMaxRwlockInstances");
      break;
    }
    case share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_SOCKET_CLASSES: {
      ret = create_one_sys_var<ObSysVarPerformanceSchemaMaxSocketClasses>(allocator_, sys_var_ptr, "ObSysVarPerformanceSchemaMaxSocketClasses");
      break;
    }
    case share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_SOCKET_INSTANCES: {
      ret = create_one_sys_var<ObSysVarPerformanceSchemaMaxSocketInstances>(allocator_, sys_var_ptr, "ObSysVarPerformanceSchemaMaxSocketInstances");
      break;
    }
    case share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_SQL_TEXT_LENGTH: {
      ret = create_one_sys_var<ObSysVarPerformanceSchemaMaxSqlTextLength>(allocator_, sys_var_ptr, "ObSysVarPerformanceSchemaMaxSqlTextLength");
      break;
    }
    case share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_STAGE_CLASSES: {
      ret = create_one_sys_var<ObSysVarPerformanceSchemaMaxStageClasses>(allocator_, sys_var_ptr, "ObSysVarPerformanceSchemaMaxStageClasses");
      break;
    }
    case share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_STATEMENT_CLASSES: {
      ret = create_one_sys_var<ObSysVarPerformanceSchemaMaxStatementClasses>(allocator_, sys_var_ptr, "ObSysVarPerformanceSchemaMaxStatementClasses");
      break;
    }
    case share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_STATEMENT_STACK: {
      ret = create_one_sys_var<ObSysVarPerformanceSchemaMaxStatementStack>(allocator_, sys_var_ptr, "ObSysVarPerformanceSchemaMaxStatementStack");
      break;
    }
    case share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_TABLE_HANDLES: {
      ret = create_one_sys_var<ObSysVarPerformanceSchemaMaxTableHandles>(allocator_, sys_var_ptr, "ObSysVarPerformanceSchemaMaxTableHandles");
      break;
    }
    case share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_TABLE_INSTANCES: {
      ret = create_one_sys_var<ObSysVarPerformanceSchemaMaxTableInstances>(allocator_, sys_var_ptr, "ObSysVarPerformanceSchemaMaxTableInstances");
      break;
    }
    case share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_TABLE_LOCK_STAT: {
      ret = create_one_sys_var<ObSysVarPerformanceSchemaMaxTableLockStat>(allocator_, sys_var_ptr, "ObSysVarPerformanceSchemaMaxTableLockStat");
      break;
    }
    case share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_THREAD_CLASSES: {
      ret = create_one_sys_var<ObSysVarPerformanceSchemaMaxThreadClasses>(allocator_, sys_var_ptr, "ObSysVarPerformanceSchemaMaxThreadClasses");
      break;
    }
    case share::SYS_VAR_PERFORMANCE_SCHEMA_MAX_THREAD_INSTANCES: {
      ret = create_one_sys_var<ObSysVarPerformanceSchemaMaxThreadInstances>(allocator_, sys_var_ptr, "ObSysVarPerformanceSchemaMaxThreadInstances");
      break;
    }
    case share::SYS_VAR_PERFORMANCE_SCHEMA_SESSION_CONNECT_ATTRS_SIZE: {
      ret = create_one_sys_var<ObSysVarPerformanceSchemaSessionConnectAttrsSize>(allocator_, sys_var_ptr, "ObSysVarPerformanceSchemaSessionConnectAttrsSize");
      break;
    }
    case share::SYS_VAR_PERFORMANCE_SCHEMA_SETUP_ACTORS_SIZE: {
      ret = create_one_sys_var<ObSysVarPerformanceSchemaSetupActorsSize>(allocator_, sys_var_ptr, "ObSysVarPerformanceSchemaSetupActorsSize");
      break;
    }
    case share::SYS_VAR_PERFORMANCE_SCHEMA_SETUP_OBJECTS_SIZE: {
      ret = create_one_sys_var<ObSysVarPerformanceSchemaSetupObjectsSize>(allocator_, sys_var_ptr, "ObSysVarPerformanceSchemaSetupObjectsSize");
      break;
    }
    case share::SYS_VAR_PERFORMANCE_SCHEMA_USERS_SIZE: {
      ret = create_one_sys_var<ObSysVarPerformanceSchemaUsersSize>(allocator_, sys_var_ptr, "ObSysVarPerformanceSchemaUsersSize");
      break;
    }
    case share::SYS_VAR_VERSION_TOKENS_SESSION_NUMBER: {
      ret = create_one_sys_var<ObSysVarVersionTokensSessionNumber>(allocator_, sys_var_ptr, "ObSysVarVersionTokensSessionNumber");
      break;
    }
    case share::SYS_VAR_BACK_LOG: {
      ret = create_one_sys_var<ObSysVarBackLog>(allocator_, sys_var_ptr, "ObSysVarBackLog");
      break;
    }
    case share::SYS_VAR_BASEDIR: {
      ret = create_one_sys_var<ObSysVarBasedir>(allocator_, sys_var_ptr, "ObSysVarBasedir");
      break;
    }
    case share::SYS_VAR_BIND_ADDRESS: {
      ret = create_one_sys_var<ObSysVarBindAddress>(allocator_, sys_var_ptr, "ObSysVarBindAddress");
      break;
    }
    case share::SYS_VAR_CORE_FILE: {
      ret = create_one_sys_var<ObSysVarCoreFile>(allocator_, sys_var_ptr, "ObSysVarCoreFile");
      break;
    }
    case share::SYS_VAR_HAVE_COMPRESS: {
      ret = create_one_sys_var<ObSysVarHaveCompress>(allocator_, sys_var_ptr, "ObSysVarHaveCompress");
      break;
    }
    case share::SYS_VAR_IGNORE_DB_DIRS: {
      ret = create_one_sys_var<ObSysVarIgnoreDbDirs>(allocator_, sys_var_ptr, "ObSysVarIgnoreDbDirs");
      break;
    }
    case share::SYS_VAR_INIT_FILE: {
      ret = create_one_sys_var<ObSysVarInitFile>(allocator_, sys_var_ptr, "ObSysVarInitFile");
      break;
    }
    case share::SYS_VAR_INNODB_OPEN_FILES: {
      ret = create_one_sys_var<ObSysVarInnodbOpenFiles>(allocator_, sys_var_ptr, "ObSysVarInnodbOpenFiles");
      break;
    }
    case share::SYS_VAR_INNODB_PAGE_CLEANERS: {
      ret = create_one_sys_var<ObSysVarInnodbPageCleaners>(allocator_, sys_var_ptr, "ObSysVarInnodbPageCleaners");
      break;
    }
    case share::SYS_VAR_INNODB_PURGE_THREADS: {
      ret = create_one_sys_var<ObSysVarInnodbPurgeThreads>(allocator_, sys_var_ptr, "ObSysVarInnodbPurgeThreads");
      break;
    }
    case share::SYS_VAR_INNODB_READ_IO_THREADS: {
      ret = create_one_sys_var<ObSysVarInnodbReadIoThreads>(allocator_, sys_var_ptr, "ObSysVarInnodbReadIoThreads");
      break;
    }
    case share::SYS_VAR_INNODB_SYNC_ARRAY_SIZE: {
      ret = create_one_sys_var<ObSysVarInnodbSyncArraySize>(allocator_, sys_var_ptr, "ObSysVarInnodbSyncArraySize");
      break;
    }
    case share::SYS_VAR_INNODB_USE_NATIVE_AIO: {
      ret = create_one_sys_var<ObSysVarInnodbUseNativeAio>(allocator_, sys_var_ptr, "ObSysVarInnodbUseNativeAio");
      break;
    }
    case share::SYS_VAR_INNODB_WRITE_IO_THREADS: {
      ret = create_one_sys_var<ObSysVarInnodbWriteIoThreads>(allocator_, sys_var_ptr, "ObSysVarInnodbWriteIoThreads");
      break;
    }
    case share::SYS_VAR_LARGE_FILES_SUPPORT: {
      ret = create_one_sys_var<ObSysVarLargeFilesSupport>(allocator_, sys_var_ptr, "ObSysVarLargeFilesSupport");
      break;
    }
    case share::SYS_VAR_LOCKED_IN_MEMORY: {
      ret = create_one_sys_var<ObSysVarLockedInMemory>(allocator_, sys_var_ptr, "ObSysVarLockedInMemory");
      break;
    }
    case share::SYS_VAR_LOG_ERROR: {
      ret = create_one_sys_var<ObSysVarLogError>(allocator_, sys_var_ptr, "ObSysVarLogError");
      break;
    }
    case share::SYS_VAR_NAMED_PIPE: {
      ret = create_one_sys_var<ObSysVarNamedPipe>(allocator_, sys_var_ptr, "ObSysVarNamedPipe");
      break;
    }
    case share::SYS_VAR_NAMED_PIPE_FULL_ACCESS_GROUP: {
      ret = create_one_sys_var<ObSysVarNamedPipeFullAccessGroup>(allocator_, sys_var_ptr, "ObSysVarNamedPipeFullAccessGroup");
      break;
    }
    case share::SYS_VAR_OPEN_FILES_LIMIT: {
      ret = create_one_sys_var<ObSysVarOpenFilesLimit>(allocator_, sys_var_ptr, "ObSysVarOpenFilesLimit");
      break;
    }
    case share::SYS_VAR_REPORT_HOST: {
      ret = create_one_sys_var<ObSysVarReportHost>(allocator_, sys_var_ptr, "ObSysVarReportHost");
      break;
    }
    case share::SYS_VAR_REPORT_PASSWORD: {
      ret = create_one_sys_var<ObSysVarReportPassword>(allocator_, sys_var_ptr, "ObSysVarReportPassword");
      break;
    }
    case share::SYS_VAR_REPORT_PORT: {
      ret = create_one_sys_var<ObSysVarReportPort>(allocator_, sys_var_ptr, "ObSysVarReportPort");
      break;
    }
    case share::SYS_VAR_REPORT_USER: {
      ret = create_one_sys_var<ObSysVarReportUser>(allocator_, sys_var_ptr, "ObSysVarReportUser");
      break;
    }
    case share::SYS_VAR_SERVER_ID_BITS: {
      ret = create_one_sys_var<ObSysVarServerIdBits>(allocator_, sys_var_ptr, "ObSysVarServerIdBits");
      break;
    }
    case share::SYS_VAR_SHARED_MEMORY: {
      ret = create_one_sys_var<ObSysVarSharedMemory>(allocator_, sys_var_ptr, "ObSysVarSharedMemory");
      break;
    }
    case share::SYS_VAR_SHARED_MEMORY_BASE_NAME: {
      ret = create_one_sys_var<ObSysVarSharedMemoryBaseName>(allocator_, sys_var_ptr, "ObSysVarSharedMemoryBaseName");
      break;
    }
    case share::SYS_VAR_SKIP_NAME_RESOLVE: {
      ret = create_one_sys_var<ObSysVarSkipNameResolve>(allocator_, sys_var_ptr, "ObSysVarSkipNameResolve");
      break;
    }
    case share::SYS_VAR_SKIP_NETWORKING: {
      ret = create_one_sys_var<ObSysVarSkipNetworking>(allocator_, sys_var_ptr, "ObSysVarSkipNetworking");
      break;
    }
    case share::SYS_VAR_THREAD_HANDLING: {
      ret = create_one_sys_var<ObSysVarThreadHandling>(allocator_, sys_var_ptr, "ObSysVarThreadHandling");
      break;
    }
    case share::SYS_VAR_THREAD_POOL_ALGORITHM: {
      ret = create_one_sys_var<ObSysVarThreadPoolAlgorithm>(allocator_, sys_var_ptr, "ObSysVarThreadPoolAlgorithm");
      break;
    }
    case share::SYS_VAR_THREAD_POOL_SIZE: {
      ret = create_one_sys_var<ObSysVarThreadPoolSize>(allocator_, sys_var_ptr, "ObSysVarThreadPoolSize");
      break;
    }
    case share::SYS_VAR_THREAD_STACK: {
      ret = create_one_sys_var<ObSysVarThreadStack>(allocator_, sys_var_ptr, "ObSysVarThreadStack");
      break;
    }
    case share::SYS_VAR_BINLOG_GTID_SIMPLE_RECOVERY: {
      ret = create_one_sys_var<ObSysVarBinlogGtidSimpleRecovery>(allocator_, sys_var_ptr, "ObSysVarBinlogGtidSimpleRecovery");
      break;
    }
    case share::SYS_VAR_INNODB_API_ENABLE_BINLOG: {
      ret = create_one_sys_var<ObSysVarInnodbApiEnableBinlog>(allocator_, sys_var_ptr, "ObSysVarInnodbApiEnableBinlog");
      break;
    }
    case share::SYS_VAR_INNODB_LOCKS_UNSAFE_FOR_BINLOG: {
      ret = create_one_sys_var<ObSysVarInnodbLocksUnsafeForBinlog>(allocator_, sys_var_ptr, "ObSysVarInnodbLocksUnsafeForBinlog");
      break;
    }
    case share::SYS_VAR_INNODB_LOG_BUFFER_SIZE: {
      ret = create_one_sys_var<ObSysVarInnodbLogBufferSize>(allocator_, sys_var_ptr, "ObSysVarInnodbLogBufferSize");
      break;
    }
    case share::SYS_VAR_INNODB_LOG_FILES_IN_GROUP: {
      ret = create_one_sys_var<ObSysVarInnodbLogFilesInGroup>(allocator_, sys_var_ptr, "ObSysVarInnodbLogFilesInGroup");
      break;
    }
    case share::SYS_VAR_INNODB_LOG_FILE_SIZE: {
      ret = create_one_sys_var<ObSysVarInnodbLogFileSize>(allocator_, sys_var_ptr, "ObSysVarInnodbLogFileSize");
      break;
    }
    case share::SYS_VAR_INNODB_LOG_GROUP_HOME_DIR: {
      ret = create_one_sys_var<ObSysVarInnodbLogGroupHomeDir>(allocator_, sys_var_ptr, "ObSysVarInnodbLogGroupHomeDir");
      break;
    }
    case share::SYS_VAR_INNODB_UNDO_DIRECTORY: {
      ret = create_one_sys_var<ObSysVarInnodbUndoDirectory>(allocator_, sys_var_ptr, "ObSysVarInnodbUndoDirectory");
      break;
    }
    case share::SYS_VAR_INNODB_UNDO_TABLESPACES: {
      ret = create_one_sys_var<ObSysVarInnodbUndoTablespaces>(allocator_, sys_var_ptr, "ObSysVarInnodbUndoTablespaces");
      break;
    }
    case share::SYS_VAR_LOG_BIN_BASENAME: {
      ret = create_one_sys_var<ObSysVarLogBinBasename>(allocator_, sys_var_ptr, "ObSysVarLogBinBasename");
      break;
    }
    case share::SYS_VAR_LOG_BIN_INDEX: {
      ret = create_one_sys_var<ObSysVarLogBinIndex>(allocator_, sys_var_ptr, "ObSysVarLogBinIndex");
      break;
    }
    case share::SYS_VAR_LOG_SLAVE_UPDATES: {
      ret = create_one_sys_var<ObSysVarLogSlaveUpdates>(allocator_, sys_var_ptr, "ObSysVarLogSlaveUpdates");
      break;
    }
    case share::SYS_VAR_RELAY_LOG: {
      ret = create_one_sys_var<ObSysVarRelayLog>(allocator_, sys_var_ptr, "ObSysVarRelayLog");
      break;
    }
    case share::SYS_VAR_RELAY_LOG_BASENAME: {
      ret = create_one_sys_var<ObSysVarRelayLogBasename>(allocator_, sys_var_ptr, "ObSysVarRelayLogBasename");
      break;
    }
    case share::SYS_VAR_RELAY_LOG_INDEX: {
      ret = create_one_sys_var<ObSysVarRelayLogIndex>(allocator_, sys_var_ptr, "ObSysVarRelayLogIndex");
      break;
    }
    case share::SYS_VAR_RELAY_LOG_INFO_FILE: {
      ret = create_one_sys_var<ObSysVarRelayLogInfoFile>(allocator_, sys_var_ptr, "ObSysVarRelayLogInfoFile");
      break;
    }
    case share::SYS_VAR_RELAY_LOG_RECOVERY: {
      ret = create_one_sys_var<ObSysVarRelayLogRecovery>(allocator_, sys_var_ptr, "ObSysVarRelayLogRecovery");
      break;
    }
    case share::SYS_VAR_RELAY_LOG_SPACE_LIMIT: {
      ret = create_one_sys_var<ObSysVarRelayLogSpaceLimit>(allocator_, sys_var_ptr, "ObSysVarRelayLogSpaceLimit");
      break;
    }
    case share::SYS_VAR_DELAY_KEY_WRITE: {
      ret = create_one_sys_var<ObSysVarDelayKeyWrite>(allocator_, sys_var_ptr, "ObSysVarDelayKeyWrite");
      break;
    }
    case share::SYS_VAR_INNODB_LARGE_PREFIX: {
      ret = create_one_sys_var<ObSysVarInnodbLargePrefix>(allocator_, sys_var_ptr, "ObSysVarInnodbLargePrefix");
      break;
    }
    case share::SYS_VAR_KEY_BUFFER_SIZE: {
      ret = create_one_sys_var<ObSysVarKeyBufferSize>(allocator_, sys_var_ptr, "ObSysVarKeyBufferSize");
      break;
    }
    case share::SYS_VAR_KEY_CACHE_AGE_THRESHOLD: {
      ret = create_one_sys_var<ObSysVarKeyCacheAgeThreshold>(allocator_, sys_var_ptr, "ObSysVarKeyCacheAgeThreshold");
      break;
    }
    case share::SYS_VAR_KEY_CACHE_DIVISION_LIMIT: {
      ret = create_one_sys_var<ObSysVarKeyCacheDivisionLimit>(allocator_, sys_var_ptr, "ObSysVarKeyCacheDivisionLimit");
      break;
    }
    case share::SYS_VAR_MAX_SEEKS_FOR_KEY: {
      ret = create_one_sys_var<ObSysVarMaxSeeksForKey>(allocator_, sys_var_ptr, "ObSysVarMaxSeeksForKey");
      break;
    }
    case share::SYS_VAR_OLD_ALTER_TABLE: {
      ret = create_one_sys_var<ObSysVarOldAlterTable>(allocator_, sys_var_ptr, "ObSysVarOldAlterTable");
      break;
    }
    case share::SYS_VAR_TABLE_DEFINITION_CACHE: {
      ret = create_one_sys_var<ObSysVarTableDefinitionCache>(allocator_, sys_var_ptr, "ObSysVarTableDefinitionCache");
      break;
    }
    case share::SYS_VAR_INNODB_SORT_BUFFER_SIZE: {
      ret = create_one_sys_var<ObSysVarInnodbSortBufferSize>(allocator_, sys_var_ptr, "ObSysVarInnodbSortBufferSize");
      break;
    }
    case share::SYS_VAR_KEY_CACHE_BLOCK_SIZE: {
      ret = create_one_sys_var<ObSysVarKeyCacheBlockSize>(allocator_, sys_var_ptr, "ObSysVarKeyCacheBlockSize");
      break;
    }
    case share::SYS_VAR_CHARACTER_SETS_DIR: {
      ret = create_one_sys_var<ObSysVarCharacterSetsDir>(allocator_, sys_var_ptr, "ObSysVarCharacterSetsDir");
      break;
    }
    case share::SYS_VAR_DATE_FORMAT: {
      ret = create_one_sys_var<ObSysVarDateFormat>(allocator_, sys_var_ptr, "ObSysVarDateFormat");
      break;
    }
    case share::SYS_VAR_DATETIME_FORMAT: {
      ret = create_one_sys_var<ObSysVarDatetimeFormat>(allocator_, sys_var_ptr, "ObSysVarDatetimeFormat");
      break;
    }
    case share::SYS_VAR_DISCONNECT_ON_EXPIRED_PASSWORD: {
      ret = create_one_sys_var<ObSysVarDisconnectOnExpiredPassword>(allocator_, sys_var_ptr, "ObSysVarDisconnectOnExpiredPassword");
      break;
    }
    case share::SYS_VAR_EXTERNAL_USER: {
      ret = create_one_sys_var<ObSysVarExternalUser>(allocator_, sys_var_ptr, "ObSysVarExternalUser");
      break;
    }
    case share::SYS_VAR_HAVE_CRYPT: {
      ret = create_one_sys_var<ObSysVarHaveCrypt>(allocator_, sys_var_ptr, "ObSysVarHaveCrypt");
      break;
    }
    case share::SYS_VAR_LANGUAGE: {
      ret = create_one_sys_var<ObSysVarLanguage>(allocator_, sys_var_ptr, "ObSysVarLanguage");
      break;
    }
    case share::SYS_VAR_LC_MESSAGES_DIR: {
      ret = create_one_sys_var<ObSysVarLcMessagesDir>(allocator_, sys_var_ptr, "ObSysVarLcMessagesDir");
      break;
    }
    case share::SYS_VAR_LOWER_CASE_FILE_SYSTEM: {
      ret = create_one_sys_var<ObSysVarLowerCaseFileSystem>(allocator_, sys_var_ptr, "ObSysVarLowerCaseFileSystem");
      break;
    }
    case share::SYS_VAR_MAX_DIGEST_LENGTH: {
      ret = create_one_sys_var<ObSysVarMaxDigestLength>(allocator_, sys_var_ptr, "ObSysVarMaxDigestLength");
      break;
    }
    case share::SYS_VAR_NDBINFO_DATABASE: {
      ret = create_one_sys_var<ObSysVarNdbinfoDatabase>(allocator_, sys_var_ptr, "ObSysVarNdbinfoDatabase");
      break;
    }
    case share::SYS_VAR_NDBINFO_TABLE_PREFIX: {
      ret = create_one_sys_var<ObSysVarNdbinfoTablePrefix>(allocator_, sys_var_ptr, "ObSysVarNdbinfoTablePrefix");
      break;
    }
    case share::SYS_VAR_NDBINFO_VERSION: {
      ret = create_one_sys_var<ObSysVarNdbinfoVersion>(allocator_, sys_var_ptr, "ObSysVarNdbinfoVersion");
      break;
    }
    case share::SYS_VAR_NDB_BATCH_SIZE: {
      ret = create_one_sys_var<ObSysVarNdbBatchSize>(allocator_, sys_var_ptr, "ObSysVarNdbBatchSize");
      break;
    }
    case share::SYS_VAR_NDB_CLUSTER_CONNECTION_POOL: {
      ret = create_one_sys_var<ObSysVarNdbClusterConnectionPool>(allocator_, sys_var_ptr, "ObSysVarNdbClusterConnectionPool");
      break;
    }
    case share::SYS_VAR_NDB_CLUSTER_CONNECTION_POOL_NODEIDS: {
      ret = create_one_sys_var<ObSysVarNdbClusterConnectionPoolNodeids>(allocator_, sys_var_ptr, "ObSysVarNdbClusterConnectionPoolNodeids");
      break;
    }
    case share::SYS_VAR_NDB_LOG_APPLY_STATUS: {
      ret = create_one_sys_var<ObSysVarNdbLogApplyStatus>(allocator_, sys_var_ptr, "ObSysVarNdbLogApplyStatus");
      break;
    }
    case share::SYS_VAR_NDB_LOG_BIN: {
      ret = create_one_sys_var<ObSysVarNdbLogBin>(allocator_, sys_var_ptr, "ObSysVarNdbLogBin");
      break;
    }
    case share::SYS_VAR_NDB_LOG_FAIL_TERMINATE: {
      ret = create_one_sys_var<ObSysVarNdbLogFailTerminate>(allocator_, sys_var_ptr, "ObSysVarNdbLogFailTerminate");
      break;
    }
    case share::SYS_VAR_NDB_LOG_ORIG: {
      ret = create_one_sys_var<ObSysVarNdbLogOrig>(allocator_, sys_var_ptr, "ObSysVarNdbLogOrig");
      break;
    }
    case share::SYS_VAR_NDB_LOG_TRANSACTION_ID: {
      ret = create_one_sys_var<ObSysVarNdbLogTransactionId>(allocator_, sys_var_ptr, "ObSysVarNdbLogTransactionId");
      break;
    }
    case share::SYS_VAR_NDB_OPTIMIZED_NODE_SELECTION: {
      ret = create_one_sys_var<ObSysVarNdbOptimizedNodeSelection>(allocator_, sys_var_ptr, "ObSysVarNdbOptimizedNodeSelection");
      break;
    }
    case share::SYS_VAR_NDB_SYSTEM_NAME: {
      ret = create_one_sys_var<ObSysVarNdbSystemName>(allocator_, sys_var_ptr, "ObSysVarNdbSystemName");
      break;
    }
    case share::SYS_VAR_NDB_USE_COPYING_ALTER_TABLE: {
      ret = create_one_sys_var<ObSysVarNdbUseCopyingAlterTable>(allocator_, sys_var_ptr, "ObSysVarNdbUseCopyingAlterTable");
      break;
    }
    case share::SYS_VAR_NDB_VERSION_STRING: {
      ret = create_one_sys_var<ObSysVarNdbVersionString>(allocator_, sys_var_ptr, "ObSysVarNdbVersionString");
      break;
    }
    case share::SYS_VAR_NDB_WAIT_CONNECTED: {
      ret = create_one_sys_var<ObSysVarNdbWaitConnected>(allocator_, sys_var_ptr, "ObSysVarNdbWaitConnected");
      break;
    }
    case share::SYS_VAR_NDB_WAIT_SETUP: {
      ret = create_one_sys_var<ObSysVarNdbWaitSetup>(allocator_, sys_var_ptr, "ObSysVarNdbWaitSetup");
      break;
    }
    case share::SYS_VAR_PROXY_USER: {
      ret = create_one_sys_var<ObSysVarProxyUser>(allocator_, sys_var_ptr, "ObSysVarProxyUser");
      break;
    }
    case share::SYS_VAR_SHA256_PASSWORD_AUTO_GENERATE_RSA_KEYS: {
      ret = create_one_sys_var<ObSysVarSha256PasswordAutoGenerateRsaKeys>(allocator_, sys_var_ptr, "ObSysVarSha256PasswordAutoGenerateRsaKeys");
      break;
    }
    case share::SYS_VAR_SHA256_PASSWORD_PRIVATE_KEY_PATH: {
      ret = create_one_sys_var<ObSysVarSha256PasswordPrivateKeyPath>(allocator_, sys_var_ptr, "ObSysVarSha256PasswordPrivateKeyPath");
      break;
    }
    case share::SYS_VAR_SHA256_PASSWORD_PUBLIC_KEY_PATH: {
      ret = create_one_sys_var<ObSysVarSha256PasswordPublicKeyPath>(allocator_, sys_var_ptr, "ObSysVarSha256PasswordPublicKeyPath");
      break;
    }
    case share::SYS_VAR_SKIP_SHOW_DATABASE: {
      ret = create_one_sys_var<ObSysVarSkipShowDatabase>(allocator_, sys_var_ptr, "ObSysVarSkipShowDatabase");
      break;
    }
    case share::SYS_VAR_BIG_TABLES: {
      ret = create_one_sys_var<ObSysVarBigTables>(allocator_, sys_var_ptr, "ObSysVarBigTables");
      break;
    }
    case share::SYS_VAR_CHECK_PROXY_USERS: {
      ret = create_one_sys_var<ObSysVarCheckProxyUsers>(allocator_, sys_var_ptr, "ObSysVarCheckProxyUsers");
      break;
    }
    case share::SYS_VAR_DEFAULT_WEEK_FORMAT: {
      ret = create_one_sys_var<ObSysVarDefaultWeekFormat>(allocator_, sys_var_ptr, "ObSysVarDefaultWeekFormat");
      break;
    }
    case share::SYS_VAR_DELAYED_INSERT_TIMEOUT: {
      ret = create_one_sys_var<ObSysVarDelayedInsertTimeout>(allocator_, sys_var_ptr, "ObSysVarDelayedInsertTimeout");
      break;
    }
    case share::SYS_VAR_DELAYED_QUEUE_SIZE: {
      ret = create_one_sys_var<ObSysVarDelayedQueueSize>(allocator_, sys_var_ptr, "ObSysVarDelayedQueueSize");
      break;
    }
    case share::SYS_VAR_EQ_RANGE_INDEX_DIVE_LIMIT: {
      ret = create_one_sys_var<ObSysVarEqRangeIndexDiveLimit>(allocator_, sys_var_ptr, "ObSysVarEqRangeIndexDiveLimit");
      break;
    }
    case share::SYS_VAR_INNODB_STATS_AUTO_RECALC: {
      ret = create_one_sys_var<ObSysVarInnodbStatsAutoRecalc>(allocator_, sys_var_ptr, "ObSysVarInnodbStatsAutoRecalc");
      break;
    }
    case share::SYS_VAR_INNODB_STATS_INCLUDE_DELETE_MARKED: {
      ret = create_one_sys_var<ObSysVarInnodbStatsIncludeDeleteMarked>(allocator_, sys_var_ptr, "ObSysVarInnodbStatsIncludeDeleteMarked");
      break;
    }
    case share::SYS_VAR_INNODB_STATS_METHOD: {
      ret = create_one_sys_var<ObSysVarInnodbStatsMethod>(allocator_, sys_var_ptr, "ObSysVarInnodbStatsMethod");
      break;
    }
    case share::SYS_VAR_INNODB_STATS_ON_METADATA: {
      ret = create_one_sys_var<ObSysVarInnodbStatsOnMetadata>(allocator_, sys_var_ptr, "ObSysVarInnodbStatsOnMetadata");
      break;
    }
    case share::SYS_VAR_VERSION_TOKENS_SESSION: {
      ret = create_one_sys_var<ObSysVarVersionTokensSession>(allocator_, sys_var_ptr, "ObSysVarVersionTokensSession");
      break;
    }
    case share::SYS_VAR_INNODB_STATS_PERSISTENT_SAMPLE_PAGES: {
      ret = create_one_sys_var<ObSysVarInnodbStatsPersistentSamplePages>(allocator_, sys_var_ptr, "ObSysVarInnodbStatsPersistentSamplePages");
      break;
    }
    case share::SYS_VAR_INNODB_STATS_SAMPLE_PAGES: {
      ret = create_one_sys_var<ObSysVarInnodbStatsSamplePages>(allocator_, sys_var_ptr, "ObSysVarInnodbStatsSamplePages");
      break;
    }
    case share::SYS_VAR_INNODB_STATS_TRANSIENT_SAMPLE_PAGES: {
      ret = create_one_sys_var<ObSysVarInnodbStatsTransientSamplePages>(allocator_, sys_var_ptr, "ObSysVarInnodbStatsTransientSamplePages");
      break;
    }
    case share::SYS_VAR_OPTIMIZER_SWITCH: {
      ret = create_one_sys_var<ObSysVarOptimizerSwitch>(allocator_, sys_var_ptr, "ObSysVarOptimizerSwitch");
      break;
    }
    case share::SYS_VAR_MAX_CONNECT_ERRORS: {
      ret = create_one_sys_var<ObSysVarMaxConnectErrors>(allocator_, sys_var_ptr, "ObSysVarMaxConnectErrors");
      break;
    }
    case share::SYS_VAR_MYSQL_FIREWALL_MODE: {
      ret = create_one_sys_var<ObSysVarMysqlFirewallMode>(allocator_, sys_var_ptr, "ObSysVarMysqlFirewallMode");
      break;
    }
    case share::SYS_VAR_MYSQL_FIREWALL_TRACE: {
      ret = create_one_sys_var<ObSysVarMysqlFirewallTrace>(allocator_, sys_var_ptr, "ObSysVarMysqlFirewallTrace");
      break;
    }
    case share::SYS_VAR_MYSQL_NATIVE_PASSWORD_PROXY_USERS: {
      ret = create_one_sys_var<ObSysVarMysqlNativePasswordProxyUsers>(allocator_, sys_var_ptr, "ObSysVarMysqlNativePasswordProxyUsers");
      break;
    }
    case share::SYS_VAR_NET_RETRY_COUNT: {
      ret = create_one_sys_var<ObSysVarNetRetryCount>(allocator_, sys_var_ptr, "ObSysVarNetRetryCount");
      break;
    }
    case share::SYS_VAR_NEW: {
      ret = create_one_sys_var<ObSysVarNew>(allocator_, sys_var_ptr, "ObSysVarNew");
      break;
    }
    case share::SYS_VAR_OLD_PASSWORDS: {
      ret = create_one_sys_var<ObSysVarOldPasswords>(allocator_, sys_var_ptr, "ObSysVarOldPasswords");
      break;
    }
    case share::SYS_VAR_OPTIMIZER_PRUNE_LEVEL: {
      ret = create_one_sys_var<ObSysVarOptimizerPruneLevel>(allocator_, sys_var_ptr, "ObSysVarOptimizerPruneLevel");
      break;
    }
    case share::SYS_VAR_OPTIMIZER_SEARCH_DEPTH: {
      ret = create_one_sys_var<ObSysVarOptimizerSearchDepth>(allocator_, sys_var_ptr, "ObSysVarOptimizerSearchDepth");
      break;
    }
    case share::SYS_VAR_OPTIMIZER_TRACE: {
      ret = create_one_sys_var<ObSysVarOptimizerTrace>(allocator_, sys_var_ptr, "ObSysVarOptimizerTrace");
      break;
    }
    case share::SYS_VAR_OPTIMIZER_TRACE_FEATURES: {
      ret = create_one_sys_var<ObSysVarOptimizerTraceFeatures>(allocator_, sys_var_ptr, "ObSysVarOptimizerTraceFeatures");
      break;
    }
    case share::SYS_VAR_OPTIMIZER_TRACE_LIMIT: {
      ret = create_one_sys_var<ObSysVarOptimizerTraceLimit>(allocator_, sys_var_ptr, "ObSysVarOptimizerTraceLimit");
      break;
    }
    case share::SYS_VAR_OPTIMIZER_TRACE_MAX_MEM_SIZE: {
      ret = create_one_sys_var<ObSysVarOptimizerTraceMaxMemSize>(allocator_, sys_var_ptr, "ObSysVarOptimizerTraceMaxMemSize");
      break;
    }
    case share::SYS_VAR_OPTIMIZER_TRACE_OFFSET: {
      ret = create_one_sys_var<ObSysVarOptimizerTraceOffset>(allocator_, sys_var_ptr, "ObSysVarOptimizerTraceOffset");
      break;
    }
    case share::SYS_VAR_PARSER_MAX_MEM_SIZE: {
      ret = create_one_sys_var<ObSysVarParserMaxMemSize>(allocator_, sys_var_ptr, "ObSysVarParserMaxMemSize");
      break;
    }
    case share::SYS_VAR_RAND_SEED1: {
      ret = create_one_sys_var<ObSysVarRandSeed1>(allocator_, sys_var_ptr, "ObSysVarRandSeed1");
      break;
    }
    case share::SYS_VAR_RAND_SEED2: {
      ret = create_one_sys_var<ObSysVarRandSeed2>(allocator_, sys_var_ptr, "ObSysVarRandSeed2");
      break;
    }
    case share::SYS_VAR_RANGE_ALLOC_BLOCK_SIZE: {
      ret = create_one_sys_var<ObSysVarRangeAllocBlockSize>(allocator_, sys_var_ptr, "ObSysVarRangeAllocBlockSize");
      break;
    }
    case share::SYS_VAR_RANGE_OPTIMIZER_MAX_MEM_SIZE: {
      ret = create_one_sys_var<ObSysVarRangeOptimizerMaxMemSize>(allocator_, sys_var_ptr, "ObSysVarRangeOptimizerMaxMemSize");
      break;
    }
    case share::SYS_VAR_REWRITER_ENABLED: {
      ret = create_one_sys_var<ObSysVarRewriterEnabled>(allocator_, sys_var_ptr, "ObSysVarRewriterEnabled");
      break;
    }
    case share::SYS_VAR_REWRITER_VERBOSE: {
      ret = create_one_sys_var<ObSysVarRewriterVerbose>(allocator_, sys_var_ptr, "ObSysVarRewriterVerbose");
      break;
    }
    case share::SYS_VAR_SECURE_AUTH: {
      ret = create_one_sys_var<ObSysVarSecureAuth>(allocator_, sys_var_ptr, "ObSysVarSecureAuth");
      break;
    }
    case share::SYS_VAR_SHA256_PASSWORD_PROXY_USERS: {
      ret = create_one_sys_var<ObSysVarSha256PasswordProxyUsers>(allocator_, sys_var_ptr, "ObSysVarSha256PasswordProxyUsers");
      break;
    }
    case share::SYS_VAR_SHOW_COMPATIBILITY_56: {
      ret = create_one_sys_var<ObSysVarShowCompatibility56>(allocator_, sys_var_ptr, "ObSysVarShowCompatibility56");
      break;
    }
    case share::SYS_VAR_SHOW_CREATE_TABLE_VERBOSITY: {
      ret = create_one_sys_var<ObSysVarShowCreateTableVerbosity>(allocator_, sys_var_ptr, "ObSysVarShowCreateTableVerbosity");
      break;
    }
    case share::SYS_VAR_SHOW_OLD_TEMPORALS: {
      ret = create_one_sys_var<ObSysVarShowOldTemporals>(allocator_, sys_var_ptr, "ObSysVarShowOldTemporals");
      break;
    }
    case share::SYS_VAR_SQL_BIG_SELECTS: {
      ret = create_one_sys_var<ObSysVarSqlBigSelects>(allocator_, sys_var_ptr, "ObSysVarSqlBigSelects");
      break;
    }
    case share::SYS_VAR_UPDATABLE_VIEWS_WITH_LIMIT: {
      ret = create_one_sys_var<ObSysVarUpdatableViewsWithLimit>(allocator_, sys_var_ptr, "ObSysVarUpdatableViewsWithLimit");
      break;
    }
    case share::SYS_VAR_VALIDATE_PASSWORD_DICTIONARY_FILE: {
      ret = create_one_sys_var<ObSysVarValidatePasswordDictionaryFile>(allocator_, sys_var_ptr, "ObSysVarValidatePasswordDictionaryFile");
      break;
    }
    case share::SYS_VAR_DELAYED_INSERT_LIMIT: {
      ret = create_one_sys_var<ObSysVarDelayedInsertLimit>(allocator_, sys_var_ptr, "ObSysVarDelayedInsertLimit");
      break;
    }
    case share::SYS_VAR_NDB_VERSION: {
      ret = create_one_sys_var<ObSysVarNdbVersion>(allocator_, sys_var_ptr, "ObSysVarNdbVersion");
      break;
    }
    case share::SYS_VAR_AUTO_GENERATE_CERTS: {
      ret = create_one_sys_var<ObSysVarAutoGenerateCerts>(allocator_, sys_var_ptr, "ObSysVarAutoGenerateCerts");
      break;
    }
    case share::SYS_VAR__OPTIMIZER_COST_BASED_TRANSFORMATION: {
      ret = create_one_sys_var<ObSysVarOptimizerCostBasedTransformation>(allocator_, sys_var_ptr, "ObSysVarOptimizerCostBasedTransformation");
      break;
    }
    case share::SYS_VAR_RANGE_INDEX_DIVE_LIMIT: {
      ret = create_one_sys_var<ObSysVarRangeIndexDiveLimit>(allocator_, sys_var_ptr, "ObSysVarRangeIndexDiveLimit");
      break;
    }
    case share::SYS_VAR_PARTITION_INDEX_DIVE_LIMIT: {
      ret = create_one_sys_var<ObSysVarPartitionIndexDiveLimit>(allocator_, sys_var_ptr, "ObSysVarPartitionIndexDiveLimit");
      break;
    }
    case share::SYS_VAR_PID_FILE: {
      ret = create_one_sys_var<ObSysVarPidFile>(allocator_, sys_var_ptr, "ObSysVarPidFile");
      break;
    }
    case share::SYS_VAR_PORT: {
      ret = create_one_sys_var<ObSysVarPort>(allocator_, sys_var_ptr, "ObSysVarPort");
      break;
    }
    case share::SYS_VAR_SOCKET: {
      ret = create_one_sys_var<ObSysVarSocket>(allocator_, sys_var_ptr, "ObSysVarSocket");
      break;
    }
    case share::SYS_VAR_ENABLE_OPTIMIZER_ROWGOAL: {
      ret = create_one_sys_var<ObSysVarEnableOptimizerRowgoal>(allocator_, sys_var_ptr, "ObSysVarEnableOptimizerRowgoal");
      break;
    }
    case share::SYS_VAR_OB_IVF_NPROBES: {
      ret = create_one_sys_var<ObSysVarObIvfNprobes>(allocator_, sys_var_ptr, "ObSysVarObIvfNprobes");
      break;
    }
    case share::SYS_VAR_OB_HNSW_EXTRA_INFO_MAX_SIZE: {
      ret = create_one_sys_var<ObSysVarObHnswExtraInfoMaxSize>(allocator_, sys_var_ptr, "ObSysVarObHnswExtraInfoMaxSize");
      break;
    }
    case share::SYS_VAR__PUSH_JOIN_PREDICATE: {
      ret = create_one_sys_var<ObSysVarPushJoinPredicate>(allocator_, sys_var_ptr, "ObSysVarPushJoinPredicate");
      break;
    }
    case share::SYS_VAR_OB_SPARSE_DROP_RATIO_SEARCH: {
      ret = create_one_sys_var<ObSysVarObSparseDropRatioSearch>(allocator_, sys_var_ptr, "ObSysVarObSparseDropRatioSearch");
      break;
    }

    default: {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("invalid system variable id", K(ret), K(sys_var_id));
      break;
    }
  }
  return ret;
}

int ObSysVarFactory::create_sys_var(share::ObSysVarClassType sys_var_id, ObBasicSysVar *&sys_var, int64_t store_idx)
{
  int ret = OB_SUCCESS;
  ObBasicSysVar *sys_var_ptr = NULL;
  if (OB_FAIL(try_init_store_mem())) {
    LOG_WARN("fail to init", K(ret));
  } else if (-1 == store_idx && OB_FAIL(share::ObSysVarMeta::calc_sys_var_store_idx(sys_var_id, store_idx))) {
    LOG_WARN("fail to calc sys var store idx", K(ret), K(sys_var_id));
  } else if (store_idx < 0 || store_idx >= share::ObSysVarMeta::ALL_SYS_VARS_COUNT) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected store idx", K(ret), K(store_idx), K(sys_var_id));
  } else if (OB_NOT_NULL(store_[store_idx])) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("store ptr shoule be null", K(ret), K(store_idx), K(sys_var_id));
  } else {
    if (OB_NOT_NULL(store_buf_[store_idx])) {
      sys_var_ptr = store_buf_[store_idx];
      store_buf_[store_idx] = nullptr;
    }
  }
  if (OB_SUCC(ret) && OB_ISNULL(sys_var_ptr)) {
    if (OB_FAIL(create_sys_var(allocator_, sys_var_id, sys_var_ptr))) {
      LOG_WARN("fail to calc sys var", K(ret), K(sys_var_id));
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_ISNULL(sys_var_ptr)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("ret is OB_SUCCESS, but sys_var_ptr is NULL", K(ret), K(sys_var_id));
    } else if (OB_NOT_NULL(store_[store_idx])) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("store_[store_idx] should be NULL", K(ret), K(sys_var_id));
    } else {
      store_[store_idx] = sys_var_ptr;
      sys_var = sys_var_ptr;
    }
  }
  if (OB_FAIL(ret) && sys_var_ptr != nullptr) {
    sys_var_ptr->~ObBasicSysVar();
    sys_var_ptr = NULL;
  }
  return ret;
}

}
}
