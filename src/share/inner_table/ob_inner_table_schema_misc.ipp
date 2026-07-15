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


#ifdef CLUSTER_PRIVATE_TABLE_SWITCH


#endif


#ifdef SYS_INDEX_TABLE_ID_SWITCH

case OB_ALL_TABLE_IDX_DATA_TABLE_ID_TID:
case OB_ALL_TABLE_IDX_DB_TB_NAME_TID:
case OB_ALL_TABLE_IDX_TB_NAME_TID:
case OB_ALL_COLUMN_IDX_TB_COLUMN_NAME_TID:
case OB_ALL_COLUMN_IDX_COLUMN_NAME_TID:
case OB_ALL_DDL_OPERATION_IDX_DDL_TYPE_TID:
case OB_ALL_TABLE_HISTORY_IDX_DATA_TABLE_ID_TID:
case OB_ALL_DDL_TASK_STATUS_IDX_TASK_KEY_TID:
case OB_ALL_USER_IDX_UR_NAME_TID:
case OB_ALL_DATABASE_IDX_DB_NAME_TID:
case OB_ALL_TABLEGROUP_IDX_TG_NAME_TID:
case OB_ALL_ROOTSERVICE_EVENT_HISTORY_IDX_RS_MODULE_TID:
case OB_ALL_ROOTSERVICE_EVENT_HISTORY_IDX_RS_EVENT_TID:
case OB_ALL_RECYCLEBIN_IDX_RECYCLEBIN_DB_TYPE_TID:
case OB_ALL_PART_IDX_PART_NAME_TID:
case OB_ALL_SUB_PART_IDX_SUB_PART_NAME_TID:
case OB_ALL_DEF_SUB_PART_IDX_DEF_SUB_PART_NAME_TID:
case OB_ALL_FOREIGN_KEY_IDX_FK_CHILD_TID_TID:
case OB_ALL_FOREIGN_KEY_IDX_FK_PARENT_TID_TID:
case OB_ALL_FOREIGN_KEY_IDX_FK_NAME_TID:
case OB_ALL_FOREIGN_KEY_HISTORY_IDX_FK_HIS_CHILD_TID_TID:
case OB_ALL_FOREIGN_KEY_HISTORY_IDX_FK_HIS_PARENT_TID_TID:
case OB_ALL_DDL_CHECKSUM_IDX_DDL_CHECKSUM_TASK_TID:
case OB_ALL_ROUTINE_IDX_DB_ROUTINE_NAME_TID:
case OB_ALL_ROUTINE_IDX_ROUTINE_NAME_TID:
case OB_ALL_ROUTINE_IDX_ROUTINE_PKG_ID_TID:
case OB_ALL_ROUTINE_PARAM_IDX_ROUTINE_PARAM_NAME_TID:
case OB_ALL_PACKAGE_IDX_DB_PKG_NAME_TID:
case OB_ALL_PACKAGE_IDX_PKG_NAME_TID:
case OB_ALL_ACQUIRED_SNAPSHOT_IDX_SNAPSHOT_TABLET_TID:
case OB_ALL_CONSTRAINT_IDX_CST_NAME_TID:
case OB_ALL_ROLE_GRANTEE_MAP_IDX_GRANTEE_ROLE_ID_TID:
case OB_ALL_ROLE_GRANTEE_MAP_HISTORY_IDX_GRANTEE_HIS_ROLE_ID_TID:
case OB_ALL_TRIGGER_IDX_TRIGGER_BASE_OBJ_ID_TID:
case OB_ALL_TRIGGER_IDX_DB_TRIGGER_NAME_TID:
case OB_ALL_TRIGGER_IDX_TRIGGER_NAME_TID:
case OB_ALL_TRIGGER_HISTORY_IDX_TRIGGER_HIS_BASE_OBJ_ID_TID:
case OB_ALL_OBJAUTH_IDX_OBJAUTH_GRANTOR_TID:
case OB_ALL_OBJAUTH_IDX_OBJAUTH_GRANTEE_TID:
case OB_ALL_DEPENDENCY_IDX_DEPENDENCY_REF_OBJ_TID:
case OB_ALL_DDL_ERROR_MESSAGE_IDX_DDL_ERROR_OBJECT_TID:
case OB_ALL_TABLE_STAT_HISTORY_IDX_TABLE_STAT_HIS_SAVTIME_TID:
case OB_ALL_COLUMN_STAT_HISTORY_IDX_COLUMN_STAT_HIS_SAVTIME_TID:
case OB_ALL_HISTOGRAM_STAT_HISTORY_IDX_HISTOGRAM_STAT_HIS_SAVTIME_TID:
case OB_ALL_TABLET_TO_LS_IDX_TABLET_TO_TABLE_ID_TID:
case OB_ALL_PENDING_TRANSACTION_IDX_PENDING_TX_ID_TID:
case OB_ALL_CONTEXT_IDX_CTX_NAMESPACE_TID:
case OB_ALL_DIRECTORY_IDX_DIRECTORY_NAME_TID:
case OB_ALL_JOB_IDX_JOB_POWNER_TID:
case OB_ALL_SEQUENCE_OBJECT_IDX_SEQ_OBJ_DB_NAME_TID:
case OB_ALL_SEQUENCE_OBJECT_IDX_SEQ_OBJ_NAME_TID:
case OB_ALL_RECYCLEBIN_IDX_RECYCLEBIN_ORI_NAME_TID:
case OB_ALL_TABLE_PRIVILEGE_IDX_TB_PRIV_DB_NAME_TID:
case OB_ALL_TABLE_PRIVILEGE_IDX_TB_PRIV_TB_NAME_TID:
case OB_ALL_DATABASE_PRIVILEGE_IDX_DB_PRIV_DB_NAME_TID:
case OB_ALL_DBMS_LOCK_ALLOCATED_IDX_DBMS_LOCK_ALLOCATED_LOCKHANDLE_TID:
case OB_ALL_DBMS_LOCK_ALLOCATED_IDX_DBMS_LOCK_ALLOCATED_EXPIRATION_TID:
case OB_ALL_TABLET_REORGANIZE_HISTORY_IDX_TABLET_HIS_TABLE_ID_SRC_TID:
case OB_ALL_MVIEW_REFRESH_RUN_STATS_IDX_MVIEW_REFRESH_RUN_STATS_NUM_MVS_CURRENT_TID:
case OB_ALL_MVIEW_REFRESH_STATS_IDX_MVIEW_REFRESH_STATS_END_TIME_TID:
case OB_ALL_MVIEW_REFRESH_STATS_IDX_MVIEW_REFRESH_STATS_MVIEW_END_TIME_TID:
case OB_ALL_CLIENT_TO_SERVER_SESSION_INFO_IDX_CLIENT_TO_SERVER_SESSION_INFO_CLIENT_SESSION_ID_TID:
case OB_ALL_COLUMN_PRIVILEGE_IDX_COLUMN_PRIVILEGE_NAME_TID:
case OB_ALL_SCHEDULER_JOB_RUN_DETAIL_V2_IDX_SCHEDULER_JOB_RUN_DETAIL_V2_TIME_TID:
case OB_ALL_SCHEDULER_JOB_RUN_DETAIL_V2_IDX_SCHEDULER_JOB_RUN_DETAIL_V2_JOB_CLASS_TIME_TID:
case OB_ALL_PKG_TYPE_IDX_PKG_DB_TYPE_NAME_TID:
case OB_ALL_PKG_TYPE_IDX_PKG_TYPE_NAME_TID:
case OB_ALL_PKG_TYPE_ATTR_IDX_PKG_TYPE_ATTR_NAME_TID:
case OB_ALL_PKG_TYPE_ATTR_IDX_PKG_TYPE_ATTR_ID_TID:
case OB_ALL_PKG_COLL_TYPE_IDX_PKG_COLL_NAME_TYPE_TID:
case OB_ALL_PKG_COLL_TYPE_IDX_PKG_COLL_NAME_ID_TID:
case OB_ALL_CATALOG_IDX_CATALOG_NAME_TID:
case OB_ALL_CATALOG_PRIVILEGE_IDX_CATALOG_PRIV_CATALOG_NAME_TID:
case OB_ALL_CCL_RULE_IDX_CCL_RULE_ID_TID:
case OB_ALL_AI_MODEL_ENDPOINT_IDX_ENDPOINT_NAME_TID:
case OB_ALL_AI_MODEL_ENDPOINT_IDX_AI_MODEL_NAME_TID:
case OB_ALL_LOCATION_IDX_LOCATION_NAME_TID:
case OB_ALL_OBJAUTH_MYSQL_IDX_OBJAUTH_MYSQL_USER_ID_TID:
case OB_ALL_OBJAUTH_MYSQL_IDX_OBJAUTH_MYSQL_OBJ_NAME_TID:
case OB_ALL_TABLET_REORGANIZE_HISTORY_IDX_TABLET_HIS_TABLE_ID_DEST_TID:

#endif


#ifdef SYS_INDEX_DATA_TABLE_ID_SWITCH

case OB_ALL_TABLE_TID:
case OB_ALL_COLUMN_TID:
case OB_ALL_DDL_OPERATION_TID:
case OB_ALL_TABLE_HISTORY_TID:
case OB_ALL_DDL_TASK_STATUS_TID:
case OB_ALL_USER_TID:
case OB_ALL_DATABASE_TID:
case OB_ALL_TABLEGROUP_TID:
case OB_ALL_ROOTSERVICE_EVENT_HISTORY_TID:
case OB_ALL_RECYCLEBIN_TID:
case OB_ALL_PART_TID:
case OB_ALL_SUB_PART_TID:
case OB_ALL_DEF_SUB_PART_TID:
case OB_ALL_FOREIGN_KEY_TID:
case OB_ALL_FOREIGN_KEY_HISTORY_TID:
case OB_ALL_DDL_CHECKSUM_TID:
case OB_ALL_ROUTINE_TID:
case OB_ALL_ROUTINE_PARAM_TID:
case OB_ALL_PACKAGE_TID:
case OB_ALL_ACQUIRED_SNAPSHOT_TID:
case OB_ALL_CONSTRAINT_TID:
case OB_ALL_ROLE_GRANTEE_MAP_TID:
case OB_ALL_ROLE_GRANTEE_MAP_HISTORY_TID:
case OB_ALL_TRIGGER_TID:
case OB_ALL_TRIGGER_HISTORY_TID:
case OB_ALL_OBJAUTH_TID:
case OB_ALL_DEPENDENCY_TID:
case OB_ALL_DDL_ERROR_MESSAGE_TID:
case OB_ALL_TABLE_STAT_HISTORY_TID:
case OB_ALL_COLUMN_STAT_HISTORY_TID:
case OB_ALL_HISTOGRAM_STAT_HISTORY_TID:
case OB_ALL_TABLET_TO_LS_TID:
case OB_ALL_PENDING_TRANSACTION_TID:
case OB_ALL_CONTEXT_TID:
case OB_ALL_DIRECTORY_TID:
case OB_ALL_JOB_TID:
case OB_ALL_SEQUENCE_OBJECT_TID:
case OB_ALL_TABLE_PRIVILEGE_TID:
case OB_ALL_DATABASE_PRIVILEGE_TID:
case OB_ALL_DBMS_LOCK_ALLOCATED_TID:
case OB_ALL_TABLET_REORGANIZE_HISTORY_TID:
case OB_ALL_MVIEW_REFRESH_RUN_STATS_TID:
case OB_ALL_MVIEW_REFRESH_STATS_TID:
case OB_ALL_CLIENT_TO_SERVER_SESSION_INFO_TID:
case OB_ALL_COLUMN_PRIVILEGE_TID:
case OB_ALL_SCHEDULER_JOB_RUN_DETAIL_V2_TID:
case OB_ALL_PKG_TYPE_TID:
case OB_ALL_PKG_TYPE_ATTR_TID:
case OB_ALL_PKG_COLL_TYPE_TID:
case OB_ALL_CATALOG_TID:
case OB_ALL_CATALOG_PRIVILEGE_TID:
case OB_ALL_CCL_RULE_TID:
case OB_ALL_AI_MODEL_ENDPOINT_TID:
case OB_ALL_LOCATION_TID:
case OB_ALL_OBJAUTH_MYSQL_TID:

#endif


#ifdef SYS_INDEX_DATA_TABLE_ID_TO_INDEX_IDS_SWITCH

case OB_ALL_TABLE_TID: {
  if (FAILEDx(index_tids.push_back(OB_ALL_TABLE_IDX_DATA_TABLE_ID_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  if (FAILEDx(index_tids.push_back(OB_ALL_TABLE_IDX_DB_TB_NAME_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  if (FAILEDx(index_tids.push_back(OB_ALL_TABLE_IDX_TB_NAME_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  break;
}
case OB_ALL_COLUMN_TID: {
  if (FAILEDx(index_tids.push_back(OB_ALL_COLUMN_IDX_TB_COLUMN_NAME_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  if (FAILEDx(index_tids.push_back(OB_ALL_COLUMN_IDX_COLUMN_NAME_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  break;
}
case OB_ALL_DDL_OPERATION_TID: {
  if (FAILEDx(index_tids.push_back(OB_ALL_DDL_OPERATION_IDX_DDL_TYPE_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  break;
}
case OB_ALL_TABLE_HISTORY_TID: {
  if (FAILEDx(index_tids.push_back(OB_ALL_TABLE_HISTORY_IDX_DATA_TABLE_ID_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  break;
}
case OB_ALL_DDL_TASK_STATUS_TID: {
  if (FAILEDx(index_tids.push_back(OB_ALL_DDL_TASK_STATUS_IDX_TASK_KEY_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  break;
}
case OB_ALL_USER_TID: {
  if (FAILEDx(index_tids.push_back(OB_ALL_USER_IDX_UR_NAME_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  break;
}
case OB_ALL_DATABASE_TID: {
  if (FAILEDx(index_tids.push_back(OB_ALL_DATABASE_IDX_DB_NAME_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  break;
}
case OB_ALL_TABLEGROUP_TID: {
  if (FAILEDx(index_tids.push_back(OB_ALL_TABLEGROUP_IDX_TG_NAME_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  break;
}
case OB_ALL_ROOTSERVICE_EVENT_HISTORY_TID: {
  if (FAILEDx(index_tids.push_back(OB_ALL_ROOTSERVICE_EVENT_HISTORY_IDX_RS_MODULE_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  if (FAILEDx(index_tids.push_back(OB_ALL_ROOTSERVICE_EVENT_HISTORY_IDX_RS_EVENT_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  break;
}
case OB_ALL_RECYCLEBIN_TID: {
  if (FAILEDx(index_tids.push_back(OB_ALL_RECYCLEBIN_IDX_RECYCLEBIN_DB_TYPE_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  if (FAILEDx(index_tids.push_back(OB_ALL_RECYCLEBIN_IDX_RECYCLEBIN_ORI_NAME_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  break;
}
case OB_ALL_PART_TID: {
  if (FAILEDx(index_tids.push_back(OB_ALL_PART_IDX_PART_NAME_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  break;
}
case OB_ALL_SUB_PART_TID: {
  if (FAILEDx(index_tids.push_back(OB_ALL_SUB_PART_IDX_SUB_PART_NAME_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  break;
}
case OB_ALL_DEF_SUB_PART_TID: {
  if (FAILEDx(index_tids.push_back(OB_ALL_DEF_SUB_PART_IDX_DEF_SUB_PART_NAME_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  break;
}
case OB_ALL_FOREIGN_KEY_TID: {
  if (FAILEDx(index_tids.push_back(OB_ALL_FOREIGN_KEY_IDX_FK_CHILD_TID_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  if (FAILEDx(index_tids.push_back(OB_ALL_FOREIGN_KEY_IDX_FK_PARENT_TID_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  if (FAILEDx(index_tids.push_back(OB_ALL_FOREIGN_KEY_IDX_FK_NAME_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  break;
}
case OB_ALL_FOREIGN_KEY_HISTORY_TID: {
  if (FAILEDx(index_tids.push_back(OB_ALL_FOREIGN_KEY_HISTORY_IDX_FK_HIS_CHILD_TID_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  if (FAILEDx(index_tids.push_back(OB_ALL_FOREIGN_KEY_HISTORY_IDX_FK_HIS_PARENT_TID_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  break;
}
case OB_ALL_DDL_CHECKSUM_TID: {
  if (FAILEDx(index_tids.push_back(OB_ALL_DDL_CHECKSUM_IDX_DDL_CHECKSUM_TASK_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  break;
}
case OB_ALL_ROUTINE_TID: {
  if (FAILEDx(index_tids.push_back(OB_ALL_ROUTINE_IDX_DB_ROUTINE_NAME_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  if (FAILEDx(index_tids.push_back(OB_ALL_ROUTINE_IDX_ROUTINE_NAME_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  if (FAILEDx(index_tids.push_back(OB_ALL_ROUTINE_IDX_ROUTINE_PKG_ID_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  break;
}
case OB_ALL_ROUTINE_PARAM_TID: {
  if (FAILEDx(index_tids.push_back(OB_ALL_ROUTINE_PARAM_IDX_ROUTINE_PARAM_NAME_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  break;
}
case OB_ALL_PACKAGE_TID: {
  if (FAILEDx(index_tids.push_back(OB_ALL_PACKAGE_IDX_DB_PKG_NAME_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  if (FAILEDx(index_tids.push_back(OB_ALL_PACKAGE_IDX_PKG_NAME_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  break;
}
case OB_ALL_ACQUIRED_SNAPSHOT_TID: {
  if (FAILEDx(index_tids.push_back(OB_ALL_ACQUIRED_SNAPSHOT_IDX_SNAPSHOT_TABLET_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  break;
}
case OB_ALL_CONSTRAINT_TID: {
  if (FAILEDx(index_tids.push_back(OB_ALL_CONSTRAINT_IDX_CST_NAME_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  break;
}
case OB_ALL_ROLE_GRANTEE_MAP_TID: {
  if (FAILEDx(index_tids.push_back(OB_ALL_ROLE_GRANTEE_MAP_IDX_GRANTEE_ROLE_ID_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  break;
}
case OB_ALL_ROLE_GRANTEE_MAP_HISTORY_TID: {
  if (FAILEDx(index_tids.push_back(OB_ALL_ROLE_GRANTEE_MAP_HISTORY_IDX_GRANTEE_HIS_ROLE_ID_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  break;
}
case OB_ALL_TRIGGER_TID: {
  if (FAILEDx(index_tids.push_back(OB_ALL_TRIGGER_IDX_TRIGGER_BASE_OBJ_ID_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  if (FAILEDx(index_tids.push_back(OB_ALL_TRIGGER_IDX_DB_TRIGGER_NAME_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  if (FAILEDx(index_tids.push_back(OB_ALL_TRIGGER_IDX_TRIGGER_NAME_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  break;
}
case OB_ALL_TRIGGER_HISTORY_TID: {
  if (FAILEDx(index_tids.push_back(OB_ALL_TRIGGER_HISTORY_IDX_TRIGGER_HIS_BASE_OBJ_ID_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  break;
}
case OB_ALL_OBJAUTH_TID: {
  if (FAILEDx(index_tids.push_back(OB_ALL_OBJAUTH_IDX_OBJAUTH_GRANTOR_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  if (FAILEDx(index_tids.push_back(OB_ALL_OBJAUTH_IDX_OBJAUTH_GRANTEE_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  break;
}
case OB_ALL_DEPENDENCY_TID: {
  if (FAILEDx(index_tids.push_back(OB_ALL_DEPENDENCY_IDX_DEPENDENCY_REF_OBJ_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  break;
}
case OB_ALL_DDL_ERROR_MESSAGE_TID: {
  if (FAILEDx(index_tids.push_back(OB_ALL_DDL_ERROR_MESSAGE_IDX_DDL_ERROR_OBJECT_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  break;
}
case OB_ALL_TABLE_STAT_HISTORY_TID: {
  if (FAILEDx(index_tids.push_back(OB_ALL_TABLE_STAT_HISTORY_IDX_TABLE_STAT_HIS_SAVTIME_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  break;
}
case OB_ALL_COLUMN_STAT_HISTORY_TID: {
  if (FAILEDx(index_tids.push_back(OB_ALL_COLUMN_STAT_HISTORY_IDX_COLUMN_STAT_HIS_SAVTIME_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  break;
}
case OB_ALL_HISTOGRAM_STAT_HISTORY_TID: {
  if (FAILEDx(index_tids.push_back(OB_ALL_HISTOGRAM_STAT_HISTORY_IDX_HISTOGRAM_STAT_HIS_SAVTIME_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  break;
}
case OB_ALL_TABLET_TO_LS_TID: {
  if (FAILEDx(index_tids.push_back(OB_ALL_TABLET_TO_LS_IDX_TABLET_TO_TABLE_ID_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  break;
}
case OB_ALL_PENDING_TRANSACTION_TID: {
  if (FAILEDx(index_tids.push_back(OB_ALL_PENDING_TRANSACTION_IDX_PENDING_TX_ID_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  break;
}
case OB_ALL_CONTEXT_TID: {
  if (FAILEDx(index_tids.push_back(OB_ALL_CONTEXT_IDX_CTX_NAMESPACE_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  break;
}
case OB_ALL_DIRECTORY_TID: {
  if (FAILEDx(index_tids.push_back(OB_ALL_DIRECTORY_IDX_DIRECTORY_NAME_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  break;
}
case OB_ALL_JOB_TID: {
  if (FAILEDx(index_tids.push_back(OB_ALL_JOB_IDX_JOB_POWNER_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  break;
}
case OB_ALL_SEQUENCE_OBJECT_TID: {
  if (FAILEDx(index_tids.push_back(OB_ALL_SEQUENCE_OBJECT_IDX_SEQ_OBJ_DB_NAME_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  if (FAILEDx(index_tids.push_back(OB_ALL_SEQUENCE_OBJECT_IDX_SEQ_OBJ_NAME_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  break;
}
case OB_ALL_TABLE_PRIVILEGE_TID: {
  if (FAILEDx(index_tids.push_back(OB_ALL_TABLE_PRIVILEGE_IDX_TB_PRIV_DB_NAME_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  if (FAILEDx(index_tids.push_back(OB_ALL_TABLE_PRIVILEGE_IDX_TB_PRIV_TB_NAME_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  break;
}
case OB_ALL_DATABASE_PRIVILEGE_TID: {
  if (FAILEDx(index_tids.push_back(OB_ALL_DATABASE_PRIVILEGE_IDX_DB_PRIV_DB_NAME_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  break;
}
case OB_ALL_DBMS_LOCK_ALLOCATED_TID: {
  if (FAILEDx(index_tids.push_back(OB_ALL_DBMS_LOCK_ALLOCATED_IDX_DBMS_LOCK_ALLOCATED_LOCKHANDLE_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  if (FAILEDx(index_tids.push_back(OB_ALL_DBMS_LOCK_ALLOCATED_IDX_DBMS_LOCK_ALLOCATED_EXPIRATION_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  break;
}
case OB_ALL_TABLET_REORGANIZE_HISTORY_TID: {
  if (FAILEDx(index_tids.push_back(OB_ALL_TABLET_REORGANIZE_HISTORY_IDX_TABLET_HIS_TABLE_ID_SRC_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  if (FAILEDx(index_tids.push_back(OB_ALL_TABLET_REORGANIZE_HISTORY_IDX_TABLET_HIS_TABLE_ID_DEST_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  break;
}
case OB_ALL_MVIEW_REFRESH_RUN_STATS_TID: {
  if (FAILEDx(index_tids.push_back(OB_ALL_MVIEW_REFRESH_RUN_STATS_IDX_MVIEW_REFRESH_RUN_STATS_NUM_MVS_CURRENT_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  break;
}
case OB_ALL_MVIEW_REFRESH_STATS_TID: {
  if (FAILEDx(index_tids.push_back(OB_ALL_MVIEW_REFRESH_STATS_IDX_MVIEW_REFRESH_STATS_END_TIME_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  if (FAILEDx(index_tids.push_back(OB_ALL_MVIEW_REFRESH_STATS_IDX_MVIEW_REFRESH_STATS_MVIEW_END_TIME_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  break;
}
case OB_ALL_CLIENT_TO_SERVER_SESSION_INFO_TID: {
  if (FAILEDx(index_tids.push_back(OB_ALL_CLIENT_TO_SERVER_SESSION_INFO_IDX_CLIENT_TO_SERVER_SESSION_INFO_CLIENT_SESSION_ID_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  break;
}
case OB_ALL_COLUMN_PRIVILEGE_TID: {
  if (FAILEDx(index_tids.push_back(OB_ALL_COLUMN_PRIVILEGE_IDX_COLUMN_PRIVILEGE_NAME_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  break;
}
case OB_ALL_SCHEDULER_JOB_RUN_DETAIL_V2_TID: {
  if (FAILEDx(index_tids.push_back(OB_ALL_SCHEDULER_JOB_RUN_DETAIL_V2_IDX_SCHEDULER_JOB_RUN_DETAIL_V2_TIME_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  if (FAILEDx(index_tids.push_back(OB_ALL_SCHEDULER_JOB_RUN_DETAIL_V2_IDX_SCHEDULER_JOB_RUN_DETAIL_V2_JOB_CLASS_TIME_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  break;
}
case OB_ALL_PKG_TYPE_TID: {
  if (FAILEDx(index_tids.push_back(OB_ALL_PKG_TYPE_IDX_PKG_DB_TYPE_NAME_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  if (FAILEDx(index_tids.push_back(OB_ALL_PKG_TYPE_IDX_PKG_TYPE_NAME_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  break;
}
case OB_ALL_PKG_TYPE_ATTR_TID: {
  if (FAILEDx(index_tids.push_back(OB_ALL_PKG_TYPE_ATTR_IDX_PKG_TYPE_ATTR_NAME_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  if (FAILEDx(index_tids.push_back(OB_ALL_PKG_TYPE_ATTR_IDX_PKG_TYPE_ATTR_ID_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  break;
}
case OB_ALL_PKG_COLL_TYPE_TID: {
  if (FAILEDx(index_tids.push_back(OB_ALL_PKG_COLL_TYPE_IDX_PKG_COLL_NAME_TYPE_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  if (FAILEDx(index_tids.push_back(OB_ALL_PKG_COLL_TYPE_IDX_PKG_COLL_NAME_ID_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  break;
}
case OB_ALL_CATALOG_TID: {
  if (FAILEDx(index_tids.push_back(OB_ALL_CATALOG_IDX_CATALOG_NAME_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  break;
}
case OB_ALL_CATALOG_PRIVILEGE_TID: {
  if (FAILEDx(index_tids.push_back(OB_ALL_CATALOG_PRIVILEGE_IDX_CATALOG_PRIV_CATALOG_NAME_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  break;
}
case OB_ALL_CCL_RULE_TID: {
  if (FAILEDx(index_tids.push_back(OB_ALL_CCL_RULE_IDX_CCL_RULE_ID_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  break;
}
case OB_ALL_AI_MODEL_ENDPOINT_TID: {
  if (FAILEDx(index_tids.push_back(OB_ALL_AI_MODEL_ENDPOINT_IDX_ENDPOINT_NAME_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  if (FAILEDx(index_tids.push_back(OB_ALL_AI_MODEL_ENDPOINT_IDX_AI_MODEL_NAME_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  break;
}
case OB_ALL_LOCATION_TID: {
  if (FAILEDx(index_tids.push_back(OB_ALL_LOCATION_IDX_LOCATION_NAME_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  break;
}
case OB_ALL_OBJAUTH_MYSQL_TID: {
  if (FAILEDx(index_tids.push_back(OB_ALL_OBJAUTH_MYSQL_IDX_OBJAUTH_MYSQL_USER_ID_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  if (FAILEDx(index_tids.push_back(OB_ALL_OBJAUTH_MYSQL_IDX_OBJAUTH_MYSQL_OBJ_NAME_TID))) {
    LOG_WARN("fail to push back index tid", KR(ret));
  }
  break;
}

#endif


#ifdef SYS_INDEX_DATA_TABLE_ID_TO_INDEX_SCHEMAS_SWITCH

case OB_ALL_TABLE_TID: {
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_table_idx_data_table_id_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_table_idx_db_tb_name_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_table_idx_tb_name_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  break;
}
case OB_ALL_COLUMN_TID: {
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_column_idx_tb_column_name_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_column_idx_column_name_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  break;
}
case OB_ALL_DDL_OPERATION_TID: {
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_ddl_operation_idx_ddl_type_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  break;
}
case OB_ALL_TABLE_HISTORY_TID: {
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_table_history_idx_data_table_id_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  break;
}
case OB_ALL_DDL_TASK_STATUS_TID: {
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_ddl_task_status_idx_task_key_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  break;
}
case OB_ALL_USER_TID: {
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_user_idx_ur_name_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  break;
}
case OB_ALL_DATABASE_TID: {
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_database_idx_db_name_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  break;
}
case OB_ALL_TABLEGROUP_TID: {
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_tablegroup_idx_tg_name_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  break;
}
case OB_ALL_ROOTSERVICE_EVENT_HISTORY_TID: {
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_rootservice_event_history_idx_rs_module_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_rootservice_event_history_idx_rs_event_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  break;
}
case OB_ALL_RECYCLEBIN_TID: {
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_recyclebin_idx_recyclebin_db_type_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_recyclebin_idx_recyclebin_ori_name_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  break;
}
case OB_ALL_PART_TID: {
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_part_idx_part_name_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  break;
}
case OB_ALL_SUB_PART_TID: {
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_sub_part_idx_sub_part_name_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  break;
}
case OB_ALL_DEF_SUB_PART_TID: {
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_def_sub_part_idx_def_sub_part_name_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  break;
}
case OB_ALL_FOREIGN_KEY_TID: {
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_foreign_key_idx_fk_child_tid_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_foreign_key_idx_fk_parent_tid_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_foreign_key_idx_fk_name_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  break;
}
case OB_ALL_FOREIGN_KEY_HISTORY_TID: {
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_foreign_key_history_idx_fk_his_child_tid_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_foreign_key_history_idx_fk_his_parent_tid_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  break;
}
case OB_ALL_DDL_CHECKSUM_TID: {
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_ddl_checksum_idx_ddl_checksum_task_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  break;
}
case OB_ALL_ROUTINE_TID: {
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_routine_idx_db_routine_name_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_routine_idx_routine_name_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_routine_idx_routine_pkg_id_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  break;
}
case OB_ALL_ROUTINE_PARAM_TID: {
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_routine_param_idx_routine_param_name_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  break;
}
case OB_ALL_PACKAGE_TID: {
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_package_idx_db_pkg_name_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_package_idx_pkg_name_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  break;
}
case OB_ALL_ACQUIRED_SNAPSHOT_TID: {
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_acquired_snapshot_idx_snapshot_tablet_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  break;
}
case OB_ALL_CONSTRAINT_TID: {
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_constraint_idx_cst_name_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  break;
}
case OB_ALL_ROLE_GRANTEE_MAP_TID: {
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_role_grantee_map_idx_grantee_role_id_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  break;
}
case OB_ALL_ROLE_GRANTEE_MAP_HISTORY_TID: {
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_role_grantee_map_history_idx_grantee_his_role_id_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  break;
}
case OB_ALL_TRIGGER_TID: {
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_trigger_idx_trigger_base_obj_id_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_trigger_idx_db_trigger_name_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_trigger_idx_trigger_name_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  break;
}
case OB_ALL_TRIGGER_HISTORY_TID: {
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_trigger_history_idx_trigger_his_base_obj_id_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  break;
}
case OB_ALL_OBJAUTH_TID: {
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_objauth_idx_objauth_grantor_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_objauth_idx_objauth_grantee_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  break;
}
case OB_ALL_DEPENDENCY_TID: {
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_dependency_idx_dependency_ref_obj_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  break;
}
case OB_ALL_DDL_ERROR_MESSAGE_TID: {
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_ddl_error_message_idx_ddl_error_object_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  break;
}
case OB_ALL_TABLE_STAT_HISTORY_TID: {
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_table_stat_history_idx_table_stat_his_savtime_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  break;
}
case OB_ALL_COLUMN_STAT_HISTORY_TID: {
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_column_stat_history_idx_column_stat_his_savtime_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  break;
}
case OB_ALL_HISTOGRAM_STAT_HISTORY_TID: {
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_histogram_stat_history_idx_histogram_stat_his_savtime_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  break;
}
case OB_ALL_TABLET_TO_LS_TID: {
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_tablet_to_ls_idx_tablet_to_table_id_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  break;
}
case OB_ALL_PENDING_TRANSACTION_TID: {
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_pending_transaction_idx_pending_tx_id_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  break;
}
case OB_ALL_CONTEXT_TID: {
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_context_idx_ctx_namespace_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  break;
}
case OB_ALL_DIRECTORY_TID: {
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_directory_idx_directory_name_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  break;
}
case OB_ALL_JOB_TID: {
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_job_idx_job_powner_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  break;
}
case OB_ALL_SEQUENCE_OBJECT_TID: {
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_sequence_object_idx_seq_obj_db_name_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_sequence_object_idx_seq_obj_name_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  break;
}
case OB_ALL_TABLE_PRIVILEGE_TID: {
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_table_privilege_idx_tb_priv_db_name_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_table_privilege_idx_tb_priv_tb_name_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  break;
}
case OB_ALL_DATABASE_PRIVILEGE_TID: {
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_database_privilege_idx_db_priv_db_name_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  break;
}
case OB_ALL_DBMS_LOCK_ALLOCATED_TID: {
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_dbms_lock_allocated_idx_dbms_lock_allocated_lockhandle_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_dbms_lock_allocated_idx_dbms_lock_allocated_expiration_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  break;
}
case OB_ALL_TABLET_REORGANIZE_HISTORY_TID: {
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_tablet_reorganize_history_idx_tablet_his_table_id_src_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_tablet_reorganize_history_idx_tablet_his_table_id_dest_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  break;
}
case OB_ALL_MVIEW_REFRESH_RUN_STATS_TID: {
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_mview_refresh_run_stats_idx_mview_refresh_run_stats_num_mvs_current_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  break;
}
case OB_ALL_MVIEW_REFRESH_STATS_TID: {
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_mview_refresh_stats_idx_mview_refresh_stats_end_time_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_mview_refresh_stats_idx_mview_refresh_stats_mview_end_time_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  break;
}
case OB_ALL_CLIENT_TO_SERVER_SESSION_INFO_TID: {
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_client_to_server_session_info_idx_client_to_server_session_info_client_session_id_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  break;
}
case OB_ALL_COLUMN_PRIVILEGE_TID: {
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_column_privilege_idx_column_privilege_name_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  break;
}
case OB_ALL_SCHEDULER_JOB_RUN_DETAIL_V2_TID: {
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_scheduler_job_run_detail_v2_idx_scheduler_job_run_detail_v2_time_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_scheduler_job_run_detail_v2_idx_scheduler_job_run_detail_v2_job_class_time_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  break;
}
case OB_ALL_PKG_TYPE_TID: {
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_pkg_type_idx_pkg_db_type_name_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_pkg_type_idx_pkg_type_name_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  break;
}
case OB_ALL_PKG_TYPE_ATTR_TID: {
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_pkg_type_attr_idx_pkg_type_attr_name_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_pkg_type_attr_idx_pkg_type_attr_id_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  break;
}
case OB_ALL_PKG_COLL_TYPE_TID: {
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_pkg_coll_type_idx_pkg_coll_name_type_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_pkg_coll_type_idx_pkg_coll_name_id_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  break;
}
case OB_ALL_CATALOG_TID: {
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_catalog_idx_catalog_name_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  break;
}
case OB_ALL_CATALOG_PRIVILEGE_TID: {
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_catalog_privilege_idx_catalog_priv_catalog_name_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  break;
}
case OB_ALL_CCL_RULE_TID: {
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_ccl_rule_idx_ccl_rule_id_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  break;
}
case OB_ALL_AI_MODEL_ENDPOINT_TID: {
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_ai_model_endpoint_idx_endpoint_name_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_ai_model_endpoint_idx_ai_model_name_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  break;
}
case OB_ALL_LOCATION_TID: {
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_location_idx_location_name_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  break;
}
case OB_ALL_OBJAUTH_MYSQL_TID: {
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_objauth_mysql_idx_objauth_mysql_user_id_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  index_schema.reset();
  if (FAILEDx(ObInnerTableSchema::all_objauth_mysql_idx_objauth_mysql_obj_name_schema(index_schema))) {
    LOG_WARN("fail to create index schema", KR(ret), K(data_table_id));
  } else if (OB_FAIL(append_table_(index_schema, tables))) {
    LOG_WARN("fail to append", KR(ret), K(data_table_id));
  }
  break;
}

#endif


#ifdef ADD_SYS_INDEX_ID

  } else if (OB_FAIL(table_ids.push_back(OB_ALL_TABLE_IDX_DATA_TABLE_ID_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_TABLE_IDX_DB_TB_NAME_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_TABLE_IDX_TB_NAME_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_COLUMN_IDX_TB_COLUMN_NAME_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_COLUMN_IDX_COLUMN_NAME_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_DDL_OPERATION_IDX_DDL_TYPE_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_TABLE_HISTORY_IDX_DATA_TABLE_ID_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_DDL_TASK_STATUS_IDX_TASK_KEY_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_USER_IDX_UR_NAME_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_DATABASE_IDX_DB_NAME_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_TABLEGROUP_IDX_TG_NAME_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_ROOTSERVICE_EVENT_HISTORY_IDX_RS_MODULE_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_ROOTSERVICE_EVENT_HISTORY_IDX_RS_EVENT_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_RECYCLEBIN_IDX_RECYCLEBIN_DB_TYPE_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_PART_IDX_PART_NAME_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_SUB_PART_IDX_SUB_PART_NAME_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_DEF_SUB_PART_IDX_DEF_SUB_PART_NAME_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_FOREIGN_KEY_IDX_FK_CHILD_TID_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_FOREIGN_KEY_IDX_FK_PARENT_TID_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_FOREIGN_KEY_IDX_FK_NAME_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_FOREIGN_KEY_HISTORY_IDX_FK_HIS_CHILD_TID_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_FOREIGN_KEY_HISTORY_IDX_FK_HIS_PARENT_TID_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_DDL_CHECKSUM_IDX_DDL_CHECKSUM_TASK_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_ROUTINE_IDX_DB_ROUTINE_NAME_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_ROUTINE_IDX_ROUTINE_NAME_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_ROUTINE_IDX_ROUTINE_PKG_ID_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_ROUTINE_PARAM_IDX_ROUTINE_PARAM_NAME_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_PACKAGE_IDX_DB_PKG_NAME_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_PACKAGE_IDX_PKG_NAME_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_ACQUIRED_SNAPSHOT_IDX_SNAPSHOT_TABLET_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_CONSTRAINT_IDX_CST_NAME_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_ROLE_GRANTEE_MAP_IDX_GRANTEE_ROLE_ID_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_ROLE_GRANTEE_MAP_HISTORY_IDX_GRANTEE_HIS_ROLE_ID_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_TRIGGER_IDX_TRIGGER_BASE_OBJ_ID_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_TRIGGER_IDX_DB_TRIGGER_NAME_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_TRIGGER_IDX_TRIGGER_NAME_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_TRIGGER_HISTORY_IDX_TRIGGER_HIS_BASE_OBJ_ID_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_OBJAUTH_IDX_OBJAUTH_GRANTOR_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_OBJAUTH_IDX_OBJAUTH_GRANTEE_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_DEPENDENCY_IDX_DEPENDENCY_REF_OBJ_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_DDL_ERROR_MESSAGE_IDX_DDL_ERROR_OBJECT_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_TABLE_STAT_HISTORY_IDX_TABLE_STAT_HIS_SAVTIME_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_COLUMN_STAT_HISTORY_IDX_COLUMN_STAT_HIS_SAVTIME_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_HISTOGRAM_STAT_HISTORY_IDX_HISTOGRAM_STAT_HIS_SAVTIME_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_TABLET_TO_LS_IDX_TABLET_TO_TABLE_ID_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_PENDING_TRANSACTION_IDX_PENDING_TX_ID_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_CONTEXT_IDX_CTX_NAMESPACE_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_DIRECTORY_IDX_DIRECTORY_NAME_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_JOB_IDX_JOB_POWNER_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_SEQUENCE_OBJECT_IDX_SEQ_OBJ_DB_NAME_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_SEQUENCE_OBJECT_IDX_SEQ_OBJ_NAME_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_RECYCLEBIN_IDX_RECYCLEBIN_ORI_NAME_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_TABLE_PRIVILEGE_IDX_TB_PRIV_DB_NAME_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_TABLE_PRIVILEGE_IDX_TB_PRIV_TB_NAME_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_DATABASE_PRIVILEGE_IDX_DB_PRIV_DB_NAME_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_DBMS_LOCK_ALLOCATED_IDX_DBMS_LOCK_ALLOCATED_LOCKHANDLE_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_DBMS_LOCK_ALLOCATED_IDX_DBMS_LOCK_ALLOCATED_EXPIRATION_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_TABLET_REORGANIZE_HISTORY_IDX_TABLET_HIS_TABLE_ID_SRC_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_MVIEW_REFRESH_RUN_STATS_IDX_MVIEW_REFRESH_RUN_STATS_NUM_MVS_CURRENT_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_MVIEW_REFRESH_STATS_IDX_MVIEW_REFRESH_STATS_END_TIME_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_MVIEW_REFRESH_STATS_IDX_MVIEW_REFRESH_STATS_MVIEW_END_TIME_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_CLIENT_TO_SERVER_SESSION_INFO_IDX_CLIENT_TO_SERVER_SESSION_INFO_CLIENT_SESSION_ID_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_COLUMN_PRIVILEGE_IDX_COLUMN_PRIVILEGE_NAME_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_SCHEDULER_JOB_RUN_DETAIL_V2_IDX_SCHEDULER_JOB_RUN_DETAIL_V2_TIME_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_SCHEDULER_JOB_RUN_DETAIL_V2_IDX_SCHEDULER_JOB_RUN_DETAIL_V2_JOB_CLASS_TIME_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_PKG_TYPE_IDX_PKG_DB_TYPE_NAME_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_PKG_TYPE_IDX_PKG_TYPE_NAME_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_PKG_TYPE_ATTR_IDX_PKG_TYPE_ATTR_NAME_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_PKG_TYPE_ATTR_IDX_PKG_TYPE_ATTR_ID_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_PKG_COLL_TYPE_IDX_PKG_COLL_NAME_TYPE_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_PKG_COLL_TYPE_IDX_PKG_COLL_NAME_ID_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_CATALOG_IDX_CATALOG_NAME_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_CATALOG_PRIVILEGE_IDX_CATALOG_PRIV_CATALOG_NAME_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_CCL_RULE_IDX_CCL_RULE_ID_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_AI_MODEL_ENDPOINT_IDX_ENDPOINT_NAME_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_AI_MODEL_ENDPOINT_IDX_AI_MODEL_NAME_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_LOCATION_IDX_LOCATION_NAME_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_OBJAUTH_MYSQL_IDX_OBJAUTH_MYSQL_USER_ID_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_OBJAUTH_MYSQL_IDX_OBJAUTH_MYSQL_OBJ_NAME_TID))) {
    LOG_WARN("add index id failed", KR(ret));
  } else if (OB_FAIL(table_ids.push_back(OB_ALL_TABLET_REORGANIZE_HISTORY_IDX_TABLET_HIS_TABLE_ID_DEST_TID))) {
    LOG_WARN("add index id failed", KR(ret));

#endif


#ifdef SQLITE_CREATE_TABLE_STATEMENTS
#ifndef SQLITE_CREATE_TABLE_STATEMENTS_DEFINED
#define SQLITE_CREATE_TABLE_STATEMENTS_DEFINED
// Auto-generated SQLite CREATE TABLE statements
// DO NOT EDIT THIS FILE MANUALLY
// Usage: Include this file and use the constant strings in your .cpp file

// __all_merge_info
inline const char *SQLITE_CREATE_TABLE_MERGE_INFO = 
  "CREATE TABLE IF NOT EXISTS __all_merge_info (\n"
  "  id INTEGER NOT NULL DEFAULT 0,\n"
  "  frozen_scn INTEGER NOT NULL,\n"
  "  global_broadcast_scn INTEGER NOT NULL,\n"
  "  is_merge_error INTEGER NOT NULL,\n"
  "  last_merged_scn INTEGER NOT NULL,\n"
  "  merge_status INTEGER NOT NULL,\n"
  "  error_type INTEGER NOT NULL,\n"
  "  suspend_merging INTEGER NOT NULL,\n"
  "  merge_start_time INTEGER NOT NULL,\n"
  "  last_merged_time INTEGER NOT NULL,\n"
  "  PRIMARY KEY (id)\n"
  ");";

// __all_zone_merge_info
inline const char *SQLITE_CREATE_TABLE_ZONE_MERGE_INFO = 
  "CREATE TABLE IF NOT EXISTS __all_zone_merge_info (\n"
  "  id INTEGER NOT NULL DEFAULT 0,\n"
  "  all_merged_scn INTEGER NOT NULL,\n"
  "  broadcast_scn INTEGER NOT NULL,\n"
  "  frozen_scn INTEGER NOT NULL,\n"
  "  is_merging INTEGER NOT NULL,\n"
  "  last_merged_time INTEGER NOT NULL,\n"
  "  last_merged_scn INTEGER NOT NULL,\n"
  "  merge_start_time INTEGER NOT NULL,\n"
  "  merge_status INTEGER NOT NULL,\n"
  "  PRIMARY KEY (id)\n"
  ");";

// __all_reserved_snapshot
inline const char *SQLITE_CREATE_TABLE_RESERVED_SNAPSHOT = 
  "CREATE TABLE IF NOT EXISTS __all_reserved_snapshot (\n"
  "  snapshot_type INTEGER NOT NULL,\n"
  "  create_time INTEGER NOT NULL,\n"
  "  snapshot_version INTEGER NOT NULL,\n"
  "  status INTEGER NOT NULL,\n"
  "  PRIMARY KEY (snapshot_type)\n"
  ");";

// __all_server_event_history
inline const char *SQLITE_CREATE_TABLE_SERVER_EVENT_HISTORY = 
  "CREATE TABLE IF NOT EXISTS __all_server_event_history (\n"
  "  gmt_create INTEGER NOT NULL,\n"
  "  event_type INTEGER NOT NULL,\n"
  "  module TEXT NOT NULL,\n"
  "  event TEXT NOT NULL,\n"
  "  name1 TEXT NULL,\n"
  "  value1 TEXT NULL,\n"
  "  name2 TEXT NULL,\n"
  "  value2 TEXT NULL,\n"
  "  name3 TEXT NULL,\n"
  "  value3 TEXT NULL,\n"
  "  name4 TEXT NULL,\n"
  "  value4 TEXT NULL,\n"
  "  name5 TEXT NULL,\n"
  "  value5 TEXT NULL,\n"
  "  name6 TEXT NULL,\n"
  "  value6 TEXT NULL,\n"
  "  extra_info TEXT NULL,\n"
  "  PRIMARY KEY (event_type, gmt_create)\n"
  ");";

// __all_column_checksum_error_info
inline const char *SQLITE_CREATE_TABLE_COLUMN_CHECKSUM_ERROR_INFO = 
  "CREATE TABLE IF NOT EXISTS __all_column_checksum_error_info (\n"
  "  frozen_scn INTEGER NOT NULL,\n"
  "  index_type INTEGER NOT NULL,\n"
  "  data_table_id INTEGER NOT NULL,\n"
  "  index_table_id INTEGER NOT NULL,\n"
  "  data_tablet_id INTEGER NOT NULL,\n"
  "  index_tablet_id INTEGER NOT NULL,\n"
  "  column_id INTEGER NOT NULL,\n"
  "  data_column_checksum INTEGER NOT NULL,\n"
  "  index_column_checksum INTEGER NOT NULL,\n"
  "  PRIMARY KEY (frozen_scn, index_type, data_table_id, index_table_id, data_tablet_id, index_tablet_id)\n"
  ");";

// __all_deadlock_event_history
inline const char *SQLITE_CREATE_TABLE_DEADLOCK_EVENT_HISTORY = 
  "CREATE TABLE IF NOT EXISTS __all_deadlock_event_history (\n"
  "  event_id INTEGER NOT NULL,\n"
  "  detector_id INTEGER NOT NULL,\n"
  "  report_time INTEGER NOT NULL,\n"
  "  cycle_idx INTEGER NOT NULL,\n"
  "  cycle_size INTEGER NOT NULL,\n"
  "  role TEXT NULL,\n"
  "  priority_level TEXT NULL,\n"
  "  priority INTEGER NOT NULL,\n"
  "  create_time INTEGER NOT NULL,\n"
  "  start_delay INTEGER NOT NULL,\n"
  "  module TEXT NULL,\n"
  "  visitor TEXT NULL,\n"
  "  object TEXT NULL,\n"
  "  extra_name1 TEXT NULL,\n"
  "  extra_value1 TEXT NULL,\n"
  "  extra_name2 TEXT NULL,\n"
  "  extra_value2 TEXT NULL,\n"
  "  extra_name3 TEXT NULL,\n"
  "  extra_value3 TEXT NULL,\n"
  "  PRIMARY KEY (event_id, detector_id)\n"
  ");";

// __all_tablet_meta_table
inline const char *SQLITE_CREATE_TABLE_TABLET_META_TABLE = 
  "CREATE TABLE IF NOT EXISTS __all_tablet_meta_table (\n"
  "  gmt_create INTEGER NULL,\n"
  "  gmt_modified INTEGER NULL,\n"
  "  tablet_id INTEGER NOT NULL,\n"
  "  compaction_scn INTEGER NOT NULL,\n"
  "  data_size INTEGER NOT NULL,\n"
  "  required_size INTEGER NOT NULL DEFAULT 0,\n"
  "  report_scn INTEGER NOT NULL DEFAULT 0,\n"
  "  status INTEGER NOT NULL DEFAULT 0,\n"
  "  PRIMARY KEY (tablet_id)\n"
  ");";

// __all_tablet_replica_checksum
inline const char *SQLITE_CREATE_TABLE_TABLET_REPLICA_CHECKSUM = 
  "CREATE TABLE IF NOT EXISTS __all_tablet_replica_checksum (\n"
  "  tablet_id INTEGER NOT NULL,\n"
  "  compaction_scn INTEGER NOT NULL,\n"
  "  row_count INTEGER NOT NULL,\n"
  "  data_checksum INTEGER NOT NULL,\n"
  "  column_checksums TEXT NULL,\n"
  "  b_column_checksums BLOB NULL,\n"
  "  data_checksum_type INTEGER NOT NULL DEFAULT 0,\n"
  "  co_base_snapshot_version INTEGER NOT NULL,\n"
  "  PRIMARY KEY (tablet_id)\n"
  ");";

// __all_sys_parameter
inline const char *SQLITE_CREATE_TABLE_SYS_PARAMETER = 
  "CREATE TABLE IF NOT EXISTS __all_sys_parameter (\n"
  "  gmt_create INTEGER NULL,\n"
  "  gmt_modified INTEGER NULL,\n"
  "  name TEXT NOT NULL,\n"
  "  data_type TEXT NULL,\n"
  "  value TEXT NOT NULL,\n"
  "  value_strict TEXT NULL,\n"
  "  info TEXT NULL,\n"
  "  need_reboot INTEGER NULL,\n"
  "  section TEXT NULL,\n"
  "  visible_level TEXT NULL,\n"
  "  config_version INTEGER NOT NULL,\n"
  "  scope TEXT NULL,\n"
  "  source TEXT NULL,\n"
  "  edit_level TEXT NULL,\n"
  "  PRIMARY KEY (name)\n"
  ");";

// __all_event_history
inline const char *SQLITE_CREATE_TABLE_EVENT_HISTORY = 
  "CREATE TABLE IF NOT EXISTS __all_event_history (\n"
  "  gmt_create INTEGER NOT NULL,\n"
  "  module TEXT NOT NULL,\n"
  "  event TEXT NOT NULL,\n"
  "  name1 TEXT NULL,\n"
  "  value1 TEXT NULL,\n"
  "  name2 TEXT NULL,\n"
  "  value2 TEXT NULL,\n"
  "  name3 TEXT NULL,\n"
  "  value3 TEXT NULL,\n"
  "  name4 TEXT NULL,\n"
  "  value4 TEXT NULL,\n"
  "  name5 TEXT NULL,\n"
  "  value5 TEXT NULL,\n"
  "  name6 TEXT NULL,\n"
  "  value6 TEXT NULL,\n"
  "  extra_info TEXT NULL,\n"
  "  trace_id TEXT NULL,\n"
  "  cost_time INTEGER NULL,\n"
  "  ret_code INTEGER NULL,\n"
  "  error_msg TEXT NULL,\n"
  "  PRIMARY KEY (gmt_create)\n"
  ");";

// __all_rootservice_job
inline const char *SQLITE_CREATE_TABLE_ROOTSERVICE_JOB = 
  "CREATE TABLE IF NOT EXISTS __all_rootservice_job (\n"
  "  job_id INTEGER NOT NULL DEFAULT 0,\n"
  "  gmt_create INTEGER NULL,\n"
  "  gmt_modified INTEGER NULL,\n"
  "  job_type TEXT NOT NULL DEFAULT '',\n"
  "  job_status TEXT NOT NULL DEFAULT '',\n"
  "  result_code INTEGER NULL,\n"
  "  PRIMARY KEY (job_id)\n"
  ");";

// __all_kv_table
inline const char *SQLITE_CREATE_TABLE_KV_TABLE = 
  "CREATE TABLE IF NOT EXISTS __all_kv_table (\n"
  "  key TEXT NOT NULL,\n"
  "  value TEXT NOT NULL DEFAULT '',\n"
  "  gmt_create INTEGER NOT NULL,\n"
  "  gmt_modified INTEGER NOT NULL,\n"
  "  PRIMARY KEY (key)\n"
  ");";

#endif // SQLITE_CREATE_TABLE_STATEMENTS_DEFINED
#endif // SQLITE_CREATE_TABLE_STATEMENTS


#ifdef SQLITE_VIRTUAL_TABLE_CREATE_ITER

  // All SQLite virtual tables are defined in ob_all_virtual_sqlite_tables.h
  // Include it in ob_virtual_table_iterator_factory.cpp:
  // (sqlite virtual tables header is pulled in by the factory cpp, not here)

  BEGIN_CREATE_VT_ITER_SWITCH_LAMBDA
    case OB_ALL_VIRTUAL_SYS_PARAMETER_TID: {
      ObAllVirtualSysParameter *oballvirtualsysparameter = NULL;
      if (OB_FAIL(NEW_VIRTUAL_TABLE(ObAllVirtualSysParameter, oballvirtualsysparameter))) {
        SERVER_LOG(ERROR, "ObAllVirtualSysParameter construct failed", K(ret));
      } else {
        vt_iter = static_cast<ObVirtualTableIterator *>(oballvirtualsysparameter);
      }
      break;
    }
    case OB_ALL_VIRTUAL_DEADLOCK_EVENT_HISTORY_TID: {
      ObAllVirtualDeadlockEventHistory *oballvirtualdeadlockeventhistory = NULL;
      if (OB_FAIL(NEW_VIRTUAL_TABLE(ObAllVirtualDeadlockEventHistory, oballvirtualdeadlockeventhistory))) {
        SERVER_LOG(ERROR, "ObAllVirtualDeadlockEventHistory construct failed", K(ret));
      } else {
        vt_iter = static_cast<ObVirtualTableIterator *>(oballvirtualdeadlockeventhistory);
      }
      break;
    }
    case OB_ALL_VIRTUAL_TABLET_META_TABLE_TID: {
      ObAllVirtualTabletMetaTable *oballvirtualtabletmetatable = NULL;
      if (OB_FAIL(NEW_VIRTUAL_TABLE(ObAllVirtualTabletMetaTable, oballvirtualtabletmetatable))) {
        SERVER_LOG(ERROR, "ObAllVirtualTabletMetaTable construct failed", K(ret));
      } else {
        vt_iter = static_cast<ObVirtualTableIterator *>(oballvirtualtabletmetatable);
      }
      break;
    }
    case OB_ALL_VIRTUAL_TABLET_REPLICA_CHECKSUM_TID: {
      ObAllVirtualTabletReplicaChecksum *oballvirtualtabletreplicachecksum = NULL;
      if (OB_FAIL(NEW_VIRTUAL_TABLE(ObAllVirtualTabletReplicaChecksum, oballvirtualtabletreplicachecksum))) {
        SERVER_LOG(ERROR, "ObAllVirtualTabletReplicaChecksum construct failed", K(ret));
      } else {
        vt_iter = static_cast<ObVirtualTableIterator *>(oballvirtualtabletreplicachecksum);
      }
      break;
    }
    case OB_ALL_VIRTUAL_ZONE_MERGE_INFO_TID: {
      ObAllVirtualZoneMergeInfo *oballvirtualzonemergeinfo = NULL;
      if (OB_FAIL(NEW_VIRTUAL_TABLE(ObAllVirtualZoneMergeInfo, oballvirtualzonemergeinfo))) {
        SERVER_LOG(ERROR, "ObAllVirtualZoneMergeInfo construct failed", K(ret));
      } else {
        vt_iter = static_cast<ObVirtualTableIterator *>(oballvirtualzonemergeinfo);
      }
      break;
    }
    case OB_ALL_VIRTUAL_MERGE_INFO_TID: {
      ObAllVirtualMergeInfo *oballvirtualmergeinfo = NULL;
      if (OB_FAIL(NEW_VIRTUAL_TABLE(ObAllVirtualMergeInfo, oballvirtualmergeinfo))) {
        SERVER_LOG(ERROR, "ObAllVirtualMergeInfo construct failed", K(ret));
      } else {
        vt_iter = static_cast<ObVirtualTableIterator *>(oballvirtualmergeinfo);
      }
      break;
    }
    case OB_ALL_VIRTUAL_COLUMN_CHECKSUM_ERROR_INFO_TID: {
      ObAllVirtualColumnChecksumErrorInfo *oballvirtualcolumnchecksumerrorinfo = NULL;
      if (OB_FAIL(NEW_VIRTUAL_TABLE(ObAllVirtualColumnChecksumErrorInfo, oballvirtualcolumnchecksumerrorinfo))) {
        SERVER_LOG(ERROR, "ObAllVirtualColumnChecksumErrorInfo construct failed", K(ret));
      } else {
        vt_iter = static_cast<ObVirtualTableIterator *>(oballvirtualcolumnchecksumerrorinfo);
      }
      break;
    }
    case OB_ALL_VIRTUAL_RESERVED_SNAPSHOT_TID: {
      ObAllVirtualReservedSnapshot *oballvirtualreservedsnapshot = NULL;
      if (OB_FAIL(NEW_VIRTUAL_TABLE(ObAllVirtualReservedSnapshot, oballvirtualreservedsnapshot))) {
        SERVER_LOG(ERROR, "ObAllVirtualReservedSnapshot construct failed", K(ret));
      } else {
        vt_iter = static_cast<ObVirtualTableIterator *>(oballvirtualreservedsnapshot);
      }
      break;
    }
    case OB_ALL_VIRTUAL_SERVER_EVENT_HISTORY_TID: {
      ObAllVirtualServerEventHistory *oballvirtualservereventhistory = NULL;
      if (OB_FAIL(NEW_VIRTUAL_TABLE(ObAllVirtualServerEventHistory, oballvirtualservereventhistory))) {
        SERVER_LOG(ERROR, "ObAllVirtualServerEventHistory construct failed", K(ret));
      } else {
        vt_iter = static_cast<ObVirtualTableIterator *>(oballvirtualservereventhistory);
      }
      break;
    }
    case OB_ALL_VIRTUAL_EVENT_HISTORY_TID: {
      ObAllVirtualEventHistory *oballvirtualeventhistory = NULL;
      if (OB_FAIL(NEW_VIRTUAL_TABLE(ObAllVirtualEventHistory, oballvirtualeventhistory))) {
        SERVER_LOG(ERROR, "ObAllVirtualEventHistory construct failed", K(ret));
      } else {
        vt_iter = static_cast<ObVirtualTableIterator *>(oballvirtualeventhistory);
      }
      break;
    }
    case OB_ALL_VIRTUAL_ROOTSERVICE_JOB_TID: {
      ObAllVirtualRootserviceJob *oballvirtualrootservicejob = NULL;
      if (OB_FAIL(NEW_VIRTUAL_TABLE(ObAllVirtualRootserviceJob, oballvirtualrootservicejob))) {
        SERVER_LOG(ERROR, "ObAllVirtualRootserviceJob construct failed", K(ret));
      } else {
        vt_iter = static_cast<ObVirtualTableIterator *>(oballvirtualrootservicejob);
      }
      break;
    }
  END_CREATE_VT_ITER_SWITCH_LAMBDA

#endif // SQLITE_VIRTUAL_TABLE_CREATE_ITER
