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
// ################################### Use multi-source transaction compatibility placeholder notes ##################################
// # Placeholder code needs to be written within the macro definition block [NEED_GENERATE_MDS_FRAME_CODE_FOR_TRANSACTION]
// # Placeholder method: Through [comment] placeholder, placeholder requires information: Helper type name/Ctx type name/Enum value/Enum naming
// #
// # Note:
// # 0. Place a placeholder before the 'reserved position'
// # 1. Always placeholder on master first, ensure the master branch is a superset of all other branches
// # 2. After the master placeholder is occupied, the corresponding information in the registration macro on the development branch cannot be modified, otherwise FARM will consider it a placeholder conflict. If this scenario occurs, the master placeholder needs to be modified first
// # 3. All type names are written starting from the global namespace '::oceanbase'
// # 4. Enum values use an incremental approach for placeholders
// # 5. Enum naming cannot be the same as previous text
// # 6. Since the placeholder is done using comments, therefore [do not need] to write the corresponding type definition and include it in [NEED_MDS_REGISTER_DEFINE]
// ############################################################################################
// ################################### Use multi-source data compatibility placeholder notes ##################################
// # Placeholder code needs to be written within the macro definition block [GENERATE_MDS_UNIT], further:
// # 1. If you want to add tablet level metadata, then add the placeholder information to [GENERATE_NORMAL_MDS_TABLE]
// # 2. If you want to add log stream level metadata, then add the placeholder information to [GENERATE_LS_INNER_MDS_TABLE]
// # Placeholder method: Through [definition] placeholder, placeholder requires information: Key type name/Value type name/Multi-version semantic support
// #
// # Note:
// # 0. Place a placeholder before the 'reserved position'
// # 1. Always placeholder on master first, to ensure the master branch is a superset of all other branches
// # 2. After the master placeholder is occupied, the corresponding information in the registration macro on the development branch cannot be modified, otherwise FARM will consider it a placeholder conflict. If this scenario occurs, the master placeholder needs to be modified first
// # 3. All type names are written starting from the global namespace '::oceanbase'
// # 4. If the Key type name is not '::oceanbase::storage::mds::DummyKey', then the corresponding Key type definition needs to be provided, and the corresponding header file should be included in [NEED_MDS_REGISTER_DEFINE]
// # 5. Need to provide the corresponding Value type definition, and include the corresponding header file in [NEED_MDS_REGISTER_DEFINE]
// # 6. The type of Key/Value only needs to be declared, without defining member methods and variables, but it needs to implement the interfaces required by the framework to pass the framework's compile-time checks, including (implemented as empty when placeholder):
// #    a. print function: [int64_t T::to_string(char *, const int64_t) const]
// #    b. Implementation of some comparison functions, for example: [bool T::operator==(const T &) const] and [bool T::operator<(const T &) const]
// #    c. Implementation of some serialization functions, for example: [int serialize(char *, const int64_t, int64_t &) const] and [int deserialize(const char *, const int64_t, int64_t &)] and [int64_t get_serialize_size() const]
// #    d. Copy/move function implementation, for example: [int T::assign(const T &)]
// ############################################################################################

// the MDS FRAME must know the defination of some class type to generate legal CPP codes, including:
// 1. DATA type defination if you need multi source data support.
//    1.a. KEY type defination if you need multi source data support with multi key support.
// 2. HELPER FUNCTION type and inherited class of BufferCtx definations if you need multi source
//    transaction support.
// inlcude those classes definations header file in below MACRO BLOCK
// CAUTION: MAKE SURE your header file is as CLEAN as possible to avoid recursive dependencies!
#if defined (NEED_MDS_REGISTER_DEFINE) && !defined (NEED_GENERATE_MDS_FRAME_CODE_FOR_TRANSACTION)
  #include "src/storage/ddl/ob_ddl_change_tablet_to_table_helper.h"
  #include "src/storage/multi_data_source/compile_utility/mds_dummy_key.h"
  #include "src/storage/multi_data_source/mds_ctx.h"
  #include "src/storage/multi_data_source/test/example_user_data_define.h"
  #include "src/storage/multi_data_source/test/example_user_helper_define.h"
  #include "src/storage/tablet/ob_tablet_create_delete_mds_user_data.h"
  #include "src/storage/tablet/ob_tablet_create_mds_helper.h"
  #include "src/storage/tablet/ob_tablet_delete_mds_helper.h"
  #include "src/storage/tablet/ob_tablet_binding_helper.h"
  #include "src/storage/tablet/ob_tablet_binding_mds_user_data.h"
  #include "src/storage/tablet/ob_tablet_split_mds_helper.h"
  #include "src/storage/tablet/ob_tablet_split_mds_user_data.h"
  #include "src/storage/tablet/ob_tablet_fork_mds_helper.h"
  #include "src/share/ob_tablet_autoincrement_param.h"
  #include "src/storage/compaction/ob_medium_compaction_info.h"
  #include "src/storage/tablet/ob_tablet_start_transfer_mds_helper.h"
  #include "src/storage/tablet/ob_tablet_finish_transfer_mds_helper.h"
  #include "src/share/balance/ob_balance_task_table_operator.h"
  #include "src/storage/tablet/ob_tablet_transfer_tx_ctx.h"
  #include "src/storage/multi_data_source/ob_tablet_create_mds_ctx.h"
  #include "src/share/ob_standby_upgrade.h"
  #include "src/storage/tablet/ob_tablet_abort_transfer_mds_helper.h"
  #include "src/storage/multi_data_source/ob_tablet_create_mds_ctx.h"
  #include "src/storage/multi_data_source/ob_start_transfer_in_mds_ctx.h"
  #include "src/storage/multi_data_source/ob_finish_transfer_in_mds_ctx.h"
  #include "src/storage/multi_data_source/ob_abort_transfer_in_mds_ctx.h"
  #include "src/share/ob_standby_upgrade.h"
  #include "src/storage/mview/ob_major_mv_merge_info.h"
  #include "src/storage/truncate_info/ob_truncate_info.h"
  #include "src/storage/truncate_info/ob_truncate_info_mds_helper.h"
  #include "src/storage/mview/ob_mview_mds.h"
  #include "src/storage/tablet/ob_tablet_ddl_complete_mds_helper.h"
  #include "src/storage/tablet/ob_tablet_ddl_complete_mds_data.h"
#endif
/**************************************************************************************************/

/**********************generate mds frame code with multi source transaction***********************/
// GENERATE_MDS_FRAME_CODE_FOR_TRANSACTION(HELPER_CLASS, BUFFER_CTX_TYPE, ID, ENUM_NAME) is an
// interface MACRO to transfer necessary information to MDS FRAME to generate codes in transaction
// layer, mostly in ob_multi_data_source.cpp and ob_trans_part_ctx.cpp.
//
// @param HELPER_CLASS the class must has two static method signatures(or COMPILE ERROR):
//                     1. static int on_register(const char* buf,
//                                               const int64_t len,
//                                               storage::mds::BufferCtx &ctx);// on leader
//                     2. static int on_replay(const char* buf,
//                                             const int64_t len,
//                                             const share::SCN &scn,
//                                             storage::mds::BufferCtx &ctx);// on follower
//                     the actual ctx's type is BUFFER_CTX_TYPE user registered.
// @param BUFFER_CTX_TYPE must inherited from storage::mds::BufferCtx.
// @param ID for FRAME code reflection reason and compat reason, write the number by INC logic.
// @param ENUM_NAME transaction layer needed, will be defined in enum class
//                  ObTxDataSourceType(ob_multi_data_source.h)
#define GENERATE_MDS_FRAME_CODE_FOR_TRANSACTION(HELPER_CLASS, BUFFER_CTX_TYPE, ID, ENUM_NAME) \
_GENERATE_MDS_FRAME_CODE_FOR_TRANSACTION_(HELPER_CLASS, BUFFER_CTX_TYPE, ID, ENUM_NAME)
#ifdef NEED_GENERATE_MDS_FRAME_CODE_FOR_TRANSACTION
  GENERATE_MDS_FRAME_CODE_FOR_TRANSACTION(::oceanbase::unittest::ExampleUserHelperFunction1,\
                                          ::oceanbase::storage::mds::MdsCtx,\
                                          14,\
                                          TEST1)
  GENERATE_MDS_FRAME_CODE_FOR_TRANSACTION(::oceanbase::unittest::ExampleUserHelperFunction2,\
                                          ::oceanbase::storage::mds::MdsCtx,\
                                          15,\
                                          TEST2)
  GENERATE_MDS_FRAME_CODE_FOR_TRANSACTION(::oceanbase::unittest::ExampleUserHelperFunction3,\
                                          ::oceanbase::unittest::ExampleUserHelperCtx,\
                                          16,\
                                          TEST3)
  GENERATE_MDS_FRAME_CODE_FOR_TRANSACTION(::oceanbase::storage::ObTabletCreateMdsHelper,\
                                          ::oceanbase::storage::mds::ObTabletCreateMdsCtx,\
                                          3,\
                                          CREATE_TABLET_NEW_MDS)
  GENERATE_MDS_FRAME_CODE_FOR_TRANSACTION(::oceanbase::storage::ObTabletDeleteMdsHelper,\
                                          ::oceanbase::storage::mds::MdsCtx,\
                                          4,\
                                          DELETE_TABLET_NEW_MDS)
  GENERATE_MDS_FRAME_CODE_FOR_TRANSACTION(::oceanbase::storage::ObTabletUnbindMdsHelper,\
                                          ::oceanbase::storage::mds::MdsCtx,\
                                          7,\
                                          UNBIND_TABLET_NEW_MDS)
  GENERATE_MDS_FRAME_CODE_FOR_TRANSACTION(::oceanbase::storage::ObTabletStartTransferOutHelper,\
                                          ::oceanbase::storage::mds::MdsCtx,\
                                          20,\
                                          START_TRANSFER_OUT)
  GENERATE_MDS_FRAME_CODE_FOR_TRANSACTION(::oceanbase::storage::ObTabletStartTransferInHelper,\
                                          ::oceanbase::storage::mds::ObStartTransferInMdsCtx,\
                                          21,\
                                          START_TRANSFER_IN)
  GENERATE_MDS_FRAME_CODE_FOR_TRANSACTION(::oceanbase::storage::ObTabletFinishTransferOutHelper,\
                                          ::oceanbase::storage::mds::MdsCtx,\
                                          22,\
                                          FINISH_TRANSFER_OUT)
  GENERATE_MDS_FRAME_CODE_FOR_TRANSACTION(::oceanbase::storage::ObTabletFinishTransferInHelper,\
                                          ::oceanbase::storage::mds::ObFinishTransferInMdsCtx,\
                                          23,\
                                          FINISH_TRANSFER_IN)
  GENERATE_MDS_FRAME_CODE_FOR_TRANSACTION(::oceanbase::share::ObBalanceTaskMDSHelper,\
                                          ::oceanbase::storage::mds::MdsCtx,\
                                          24,\
                                          TRANSFER_TASK)
  GENERATE_MDS_FRAME_CODE_FOR_TRANSACTION(::oceanbase::storage::ObTabletStartTransferOutPrepareHelper,\
                                          ::oceanbase::storage::mds::MdsCtx,\
                                          25,\
                                          START_TRANSFER_OUT_PREPARE)
  GENERATE_MDS_FRAME_CODE_FOR_TRANSACTION(::oceanbase::storage::ObTabletStartTransferOutV2Helper,\
                                          ::oceanbase::storage::ObTransferOutTxCtx,\
                                          26,\
                                          START_TRANSFER_OUT_V2)
  GENERATE_MDS_FRAME_CODE_FOR_TRANSACTION(::oceanbase::storage::ObStartTransferMoveTxHelper,\
                                          ::oceanbase::storage::ObTransferMoveTxCtx,\
                                          27,\
                                          TRANSFER_MOVE_TX_CTX)
  GENERATE_MDS_FRAME_CODE_FOR_TRANSACTION(::oceanbase::storage::ObStartTransferDestPrepareHelper,\
                                          ::oceanbase::storage::ObTransferDestPrepareTxCtx,\
                                          28,\
                                          TRANSFER_DEST_PREPARE)
  GENERATE_MDS_FRAME_CODE_FOR_TRANSACTION(::oceanbase::storage::ObTabletUnbindLobMdsHelper,\
                                          ::oceanbase::storage::mds::MdsCtx,\
                                          29,\
                                          UNBIND_LOB_TABLET)
  GENERATE_MDS_FRAME_CODE_FOR_TRANSACTION(::oceanbase::storage::ObChangeTabletToTableHelper,\
                                          ::oceanbase::storage::mds::MdsCtx,\
                                          30,\
                                          CHANGE_TABLET_TO_TABLE_MDS)
  GENERATE_MDS_FRAME_CODE_FOR_TRANSACTION(::oceanbase::storage::ObTabletSplitMdsHelper,\
                                           ::oceanbase::storage::mds::MdsCtx,\
                                           31,\
                                           TABLET_SPLIT)
  GENERATE_MDS_FRAME_CODE_FOR_TRANSACTION(::oceanbase::storage::ObTabletAbortTransferHelper,\
                                          ::oceanbase::storage::mds::ObAbortTransferInMdsCtx,\
                                          32,\
                                          TRANSFER_IN_ABORTED)
  GENERATE_MDS_FRAME_CODE_FOR_TRANSACTION(::oceanbase::share::ObUpgradeDataVersionMDSHelper, \
                                          ::oceanbase::storage::mds::MdsCtx, \
                                          33,\
                                          STANDBY_UPGRADE_DATA_VERSION)
  GENERATE_MDS_FRAME_CODE_FOR_TRANSACTION(::oceanbase::storage::ObTabletBindingMdsHelper,\
                                          ::oceanbase::storage::mds::MdsCtx,\
                                          34,\
                                          TABLET_BINDING)
  GENERATE_MDS_FRAME_CODE_FOR_TRANSACTION(::oceanbase::storage::ObMVPublishSCNHelper,\
                                          ::oceanbase::storage::ObUnUseCtx, \
                                          35,\
                                          MV_PUBLISH_SCN)
  GENERATE_MDS_FRAME_CODE_FOR_TRANSACTION(::oceanbase::storage::ObMVNoticeSafeHelper,\
                                          ::oceanbase::storage::ObUnUseCtx, \
                                          36,\
                                          MV_NOTICE_SAFE)
  GENERATE_MDS_FRAME_CODE_FOR_TRANSACTION(::oceanbase::storage::ObMVUpdateSCNHelper,\
                                          ::oceanbase::storage::ObUnUseCtx, \
                                          37,\
                                          MV_UPDATE_SCN)
  GENERATE_MDS_FRAME_CODE_FOR_TRANSACTION(::oceanbase::storage::ObTruncateInfoMdsHelper,\
                                          ::oceanbase::storage::mds::MdsCtx, \
                                          38,\
                                          SYNC_TRUNCATE_INFO)
  GENERATE_MDS_FRAME_CODE_FOR_TRANSACTION(::oceanbase::storage::ObMVMergeSCNHelper,\
                                          ::oceanbase::storage::ObUnUseCtx, \
                                          39,\
                                          MV_MERGE_SCN)
  GENERATE_MDS_FRAME_CODE_FOR_TRANSACTION(::oceanbase::storage::ObMViewMdsOpHelper,\
                                          ::oceanbase::storage::ObMViewMdsOpCtx, \
                                          40,\
                                          MVIEW_MDS_OP)
  GENERATE_MDS_FRAME_CODE_FOR_TRANSACTION(::oceanbase::storage::ObTabletDDLCompleteMdsHelper,\
                                          ::oceanbase::storage::mds::MdsCtx,\
                                          41,\
                                          DDL_COMPLETE_MDS)
  GENERATE_MDS_FRAME_CODE_FOR_TRANSACTION(::oceanbase::storage::ObTabletForkMdsHelper,\
                                          ::oceanbase::storage::mds::MdsCtx,\
                                          42,\
                                          TABLET_FORK)
  // GENERATE_MDS_FRAME_CODE_FOR_TRANSACTION(::oceanbase::storage::ObTabletDDLCompleteMdsHelper,\
  //                                         ::oceanbase::storage::mds::MdsCtx,\
  //                                         41,\
  //                                         DDL_COMPLETE_MDS)
  // GENERATE_MDS_FRAME_CODE_FOR_TRANSACTION(::oceanbase::storage::ObTTLFilterInfoMdsHelper,\
  //                                         ::oceanbase::storage::mds::MdsCtx, \
  //                                         43,\
  //                                         SYNC_TTL_FILTER_INFO)
  // # Reserved position (placeholder before this line)
#undef GENERATE_MDS_FRAME_CODE_FOR_TRANSACTION
#endif
/**************************************************************************************************/

/**********************generate mds frame code with multi source data******************************/
// GENERATE_MDS_UNIT(KEY_TYPE, VALUE_TYPE, NEED_MULTI_VERSION) is an
// interface MACRO to transfer necessary information to MDS FRAME to generate codes on MdsTable.
// 1. There are some requirements about class type KEY_TYPE and VALUE_TYPE, if your type is not
//    satisfied with below requirments, COMPILE ERROR will occured.
// 2. If you want store your data on normal tablet, write register macro in
//    GENERATE_NORMAL_MDS_TABLE block, if you want store your data on LS INNER tablet, write
//    register macro in GENERATE_LS_INNER_MDS_TABLE block.
//
// @param KEY_TYPE if you do not need multi row specfied by key, use DummyKey, otherwise, your Key
//                 type must support:
//                 1. has both [bool T::operator==(const T &)] and [bool T::operator<(const T &)],
//                    or has [int T::compare(const Key &, bool &)]
//                 2. be able to_string: [int64_t T::to_string(char *, const int64_t)]
//                 3. serializeable(the classic 3 serialize methods)
//                 4. copiable, Frame code will try call(ordered):
//                    a. copy construction: [T::T(const T &)]
//                    b. assign operator: [T &T::operator=(const T &)]
//                    c. assign method: [int T::assign(const T &)]
//                    d. COMPILE ERROR if neither of above method exists.
// @param VALUE_TYPE must support:
//                   1. be able to string: [int64_t to_string(char *, const int64_t)]
//                   2. serializeable(the classic 3 serialize methods)
//                   3. copiable as KEY_TYPE
//                   4. (optional) if has [void on_redo(const share::SCN&)] will be called.
//                      (optional) if has [void on_prepare(const share::SCN&)] will be called.
//                      (optional) if has [void on_commit(const share::SCN&)] will be called.
//                      (optional) if has [void on_abort()] will be called.
//                      CAUTION: DO NOT do heavy action in these optinal functions!
//                   5. (optional) optimized if VALUE_TYPE support rvalue sematic.
// @param NEED_MULTI_VERSION if setted true, will remain multi version data,
//                           or just remain uncommitted and the last committed data.
#define GENERATE_MDS_UNIT(KEY_TYPE, VALUE_TYPE, NEED_MULTI_VERSION) \
_GENERATE_MDS_UNIT_(KEY_TYPE, VALUE_TYPE, NEED_MULTI_VERSION)
#ifdef GENERATE_TEST_MDS_TABLE
  GENERATE_MDS_UNIT(::oceanbase::storage::mds::DummyKey,\
                    ::oceanbase::unittest::ExampleUserData1,\
                    true) // no need multi row, need multi version
                          // (complicated data with rvalue optimized)
  GENERATE_MDS_UNIT(::oceanbase::storage::mds::DummyKey,\
                    ::oceanbase::unittest::ExampleUserData2,\
                    true) // no need multi row, need multi version(simple data)
  GENERATE_MDS_UNIT(::oceanbase::unittest::ExampleUserKey,\
                    ::oceanbase::unittest::ExampleUserData1,\
                    false)// need multi row, no need multi version
  GENERATE_MDS_UNIT(::oceanbase::unittest::ExampleUserKey,\
                    ::oceanbase::unittest::ExampleUserData2,\
                    true)// need multi row & need multi version
#endif

#ifdef GENERATE_NORMAL_MDS_TABLE
  GENERATE_MDS_UNIT(::oceanbase::storage::mds::DummyKey,\
                    ::oceanbase::storage::ObTabletCreateDeleteMdsUserData,\
                    false)
  GENERATE_MDS_UNIT(::oceanbase::storage::mds::DummyKey,\
                    ::oceanbase::storage::ObTabletBindingMdsUserData,\
                    false)
  GENERATE_MDS_UNIT(::oceanbase::storage::mds::DummyKey,\
                    ::oceanbase::share::ObTabletAutoincSeq,\
                    false)
  GENERATE_MDS_UNIT(::oceanbase::compaction::ObMediumCompactionInfoKey,\
                    ::oceanbase::compaction::ObMediumCompactionInfo,\
                    false)
  GENERATE_MDS_UNIT(::oceanbase::storage::mds::DummyKey,\
                    ::oceanbase::storage::ObTabletSplitMdsUserData,\
                    false)
  GENERATE_MDS_UNIT(::oceanbase::storage::ObTruncateInfoKey,\
                    ::oceanbase::storage::ObTruncateInfo,\
                    false)
  GENERATE_MDS_UNIT(::oceanbase::storage::mds::DummyKey,\
                    ::oceanbase::storage::ObTabletDDLCompleteMdsUserData,\
                    false)
  // # reserved position (this line is for placeholder)
#endif

#ifdef GENERATE_LS_INNER_MDS_TABLE
  GENERATE_MDS_UNIT(::oceanbase::storage::mds::DummyKey,\
                    ::oceanbase::unittest::ExampleUserData1,\
                    true) // replace this line if you are the first user to register LS INNER TABLET
  // # reserved position (this line is for placeholder)
#endif
/**************************************************************************************************/
