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
// the MDS FRAME must know the defination of some class type to generate legal CPP codes, including:
// 1. DATA type defination if you need multi source data support.
//    1.a. KEY type defination if you need multi source data support with multi key support.
// 2. HELPER FUNCTION type and inherited class of BufferCtx definations if you need multi source
//    transaction support.
// inlcude those classes definations header file in below MACRO BLOCK
// CAUTION: MAKE SURE your header file is as CLEAN as possible to avoid recursive dependencies!
#if defined (NEED_MDS_REGISTER_DEFINE) && !defined (NEED_GENERATE_MDS_FRAME_CODE_FOR_TRANSACTION)
  #include "src/storage/multi_data_source/compile_utility/mds_dummy_key.h"
  #include "src/storage/multi_data_source/mds_ctx.h"
  #include "src/storage/tablet/ob_tablet_create_delete_mds_user_data.h"
  #include "src/storage/tablet/ob_tablet_create_mds_helper.h"
  #include "src/storage/tablet/ob_tablet_delete_mds_helper.h"
  #include "src/storage/tablet/ob_tablet_binding_helper.h"
  #include "src/storage/tablet/ob_tablet_binding_mds_user_data.h"
  #include "src/storage/tablet/ob_tablet_fork_mds_helper.h"
  #include "storage/tablet/ob_tablet_autoincrement_state.h"
  #include "src/storage/compaction/ob_medium_compaction_info.h"
  #include "src/storage/multi_data_source/ob_tablet_create_mds_ctx.h"
  #include "src/storage/truncate_info/ob_truncate_info.h"
  #include "src/storage/truncate_info/ob_truncate_info_mds_helper.h"
  #include "src/storage/tablet/ob_tablet_ddl_complete_mds_helper.h"
  #include "src/storage/tablet/ob_tablet_ddl_complete_mds_data.h"
#endif
/**************************************************************************************************/

/**********************generate mds frame code with multi source transaction***********************/
// GENERATE_MDS_FRAME_CODE_FOR_TRANSACTION(HELPER_CLASS, BUFFER_CTX_TYPE, ID, ENUM_NAME) is an
// interface MACRO to transfer necessary information to MDS FRAME to generate codes in transaction
// layer, mostly in ob_multi_data_source.cpp and ob_tx_ctx.cpp.
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
// @param ID is the contiguous current-format identifier used by the frame.
// @param ENUM_NAME transaction layer needed, will be defined in enum class
//                  ObTxDataSourceType(ob_multi_data_source.h)
#define GENERATE_MDS_FRAME_CODE_FOR_TRANSACTION(HELPER_CLASS, BUFFER_CTX_TYPE, ID, ENUM_NAME) \
_GENERATE_MDS_FRAME_CODE_FOR_TRANSACTION_(HELPER_CLASS, BUFFER_CTX_TYPE, ID, ENUM_NAME)
#ifdef NEED_GENERATE_MDS_FRAME_CODE_FOR_TRANSACTION
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
  GENERATE_MDS_FRAME_CODE_FOR_TRANSACTION(::oceanbase::storage::ObTabletUnbindLobMdsHelper,\
                                          ::oceanbase::storage::mds::MdsCtx,\
                                          29,\
                                          UNBIND_LOB_TABLET)
  // Transaction MDS type 30 was CHANGE_TABLET_TO_TABLE_MDS. Do not reuse.
  GENERATE_MDS_FRAME_CODE_FOR_TRANSACTION(::oceanbase::storage::ObTabletBindingMdsHelper,\
                                          ::oceanbase::storage::mds::MdsCtx,\
                                          34,\
                                          TABLET_BINDING)
  GENERATE_MDS_FRAME_CODE_FOR_TRANSACTION(::oceanbase::storage::ObTruncateInfoMdsHelper,\
                                          ::oceanbase::storage::mds::MdsCtx, \
                                          38,\
                                          SYNC_TRUNCATE_INFO)
  GENERATE_MDS_FRAME_CODE_FOR_TRANSACTION(::oceanbase::storage::ObTabletDDLCompleteMdsHelper,\
                                          ::oceanbase::storage::mds::MdsCtx,\
                                          41,\
                                          DDL_COMPLETE_MDS)
  GENERATE_MDS_FRAME_CODE_FOR_TRANSACTION(::oceanbase::storage::ObTabletForkMdsHelper,\
                                          ::oceanbase::storage::mds::MdsCtx,\
                                          42,\
                                          TABLET_FORK)
#undef GENERATE_MDS_FRAME_CODE_FOR_TRANSACTION
#endif
/**************************************************************************************************/

/**********************generate mds frame code with multi source data******************************/
// GENERATE_MDS_UNIT(KEY_TYPE, VALUE_TYPE, NEED_MULTI_VERSION) is an
// interface MACRO to transfer necessary information to MDS FRAME to generate codes on MdsTable.
// 1. There are some requirements about class type KEY_TYPE and VALUE_TYPE, if your type is not
//    satisfied with below requirments, COMPILE ERROR will occured.
// 2. Register tablet metadata in the GENERATE_NORMAL_MDS_TABLE block.
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
#ifdef GENERATE_NORMAL_MDS_TABLE
  GENERATE_MDS_UNIT(::oceanbase::storage::mds::DummyKey,\
                    ::oceanbase::storage::ObTabletCreateDeleteMdsUserData,\
                    false)
  GENERATE_MDS_UNIT(::oceanbase::storage::mds::DummyKey,\
                    ::oceanbase::storage::ObTabletBindingMdsUserData,\
                    false)
  GENERATE_MDS_UNIT(::oceanbase::storage::mds::DummyKey,\
                    ::oceanbase::storage::ObTabletAutoincSeq,\
                    false)
  GENERATE_MDS_UNIT(::oceanbase::compaction::ObMediumCompactionInfoKey,\
                    ::oceanbase::compaction::ObMediumCompactionInfo,\
                    false)
  GENERATE_MDS_UNIT(::oceanbase::storage::ObTruncateInfoKey,\
                    ::oceanbase::storage::ObTruncateInfo,\
                    false)
  GENERATE_MDS_UNIT(::oceanbase::storage::mds::DummyKey,\
                    ::oceanbase::storage::ObTabletDDLCompleteMdsUserData,\
                    false)
#endif

/**************************************************************************************************/
