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
#ifndef OCEANBASE_STORAGE_OB_STORAGE_RPC_ARG_H_
#define OCEANBASE_STORAGE_OB_STORAGE_RPC_ARG_H_
// moved from share/ob_rpc_struct.h:RPC arguments that embed storage types by value
// (owner=storage;ns obrpc unchanged;RPC declaration remains in share proxy,vertical split for task 5)
#include "share/ob_ls_id.h"
#include "share/ob_rpc_struct.h"
#include "storage/tablet/ob_tablet_binding_mds_user_data.h"
#include "storage/tablet/ob_tablet_create_delete_mds_user_data.h"
#include "storage/tablet/ob_tablet_split_mds_user_data.h"
#include "storage/tx/ob_multi_data_source.h"
#include "storage/tx/ob_trans_define.h"
#include "storage/ddl/ob_ddl_struct.h"
#include "storage/ob_i_table.h"
#include "storage/blocksstable/ob_block_sstable_struct.h"
namespace oceanbase
{
namespace transaction { class ObTransService; }
namespace obcall
{
struct ObRpcRemoteWriteDDLCommitLogArg final
{
  OB_UNIS_VERSION(1);
public:
  ObRpcRemoteWriteDDLCommitLogArg();
  ~ObRpcRemoteWriteDDLCommitLogArg() = default;
  int init(const share::ObLSID &ls_id,
           const storage::ObITable::TableKey &table_key,
           const share::SCN &start_scn);
  bool is_valid() const
  {
    return 1UL != OB_INVALID_ID && ls_id_.is_valid() && table_key_.is_valid() && start_scn_.is_valid_and_not_min();
  }
  TO_STRING_KV(K_(ls_id), K_(table_key), K_(start_scn), K_(table_id),
               K_(execution_id), K_(ddl_task_id));
public:
  share::ObLSID ls_id_;
  storage::ObITable::TableKey table_key_;
  share::SCN start_scn_;
  int64_t table_id_; // depercated
  int64_t execution_id_; // depercated
  int64_t ddl_task_id_; // depercated
private:
  DISALLOW_COPY_AND_ASSIGN(ObRpcRemoteWriteDDLCommitLogArg);
};

struct ObRpcRemoteWriteDDLRedoLogArg
{
  OB_UNIS_VERSION(1);
public:
  ObRpcRemoteWriteDDLRedoLogArg();
  ~ObRpcRemoteWriteDDLRedoLogArg() = default;
  int init(const share::ObLSID &ls_id,
           const storage::ObDDLMacroBlockRedoInfo &redo_info,
           const int64_t task_id);
  bool is_valid() const { return 1UL != OB_INVALID_ID && ls_id_.is_valid() && redo_info_.is_valid() && task_id_ != 0; }
  TO_STRING_KV(K(ls_id_), K_(redo_info), K(task_id_));
public:
  share::ObLSID ls_id_;
  storage::ObDDLMacroBlockRedoInfo redo_info_;
  int64_t task_id_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObRpcRemoteWriteDDLRedoLogArg);
};

struct ObRpcRemoteWriteDDLIncCommitLogArg final
{
  OB_UNIS_VERSION(1);
public:
  ObRpcRemoteWriteDDLIncCommitLogArg();
  ~ObRpcRemoteWriteDDLIncCommitLogArg();
  int init(const share::ObLSID &ls_id,
           const common::ObTabletID tablet_id,
           const common::ObTabletID lob_meta_tablet_id,
           transaction::ObTxDesc *tx_desc);
  int release();
  bool is_valid() const
  {
    return 1UL != OB_INVALID_ID && ls_id_.is_valid() && tablet_id_.is_valid() &&
           OB_NOT_NULL(tx_desc_) && tx_desc_->is_valid();
  }
  TO_STRING_KV(K_(ls_id), K_(tablet_id), K_(lob_meta_tablet_id), KP_(tx_desc));
public:
  share::ObLSID ls_id_;
  common::ObTabletID tablet_id_;
  common::ObTabletID lob_meta_tablet_id_;
  transaction::ObTxDesc *tx_desc_;
  bool need_release_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObRpcRemoteWriteDDLIncCommitLogArg);
};

struct ObRpcRemoteWriteDDLIncCommitLogRes final
{
  OB_UNIS_VERSION(1);
public:
  ObRpcRemoteWriteDDLIncCommitLogRes() : tx_result_() {}
  ~ObRpcRemoteWriteDDLIncCommitLogRes() {}
  TO_STRING_KV(K_(tx_result));
public:
  transaction::ObTxExecResult tx_result_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObRpcRemoteWriteDDLIncCommitLogRes);
};

#ifdef OB_BUILD_SHARED_STORAGE
struct ObRpcRemoteWriteDDLFinishLogArg final
{
  OB_UNIS_VERSION(1);
public:
  ObRpcRemoteWriteDDLFinishLogArg();
  ~ObRpcRemoteWriteDDLFinishLogArg() = default;
  int init(const storage::ObDDLFinishLogInfo &log);
  bool is_valid() const
  {
    return log_info_.is_valid();
  }
  TO_STRING_KV(K_(log_info));
public:
  storage::ObDDLFinishLogInfo log_info_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObRpcRemoteWriteDDLFinishLogArg);
};
#endif

struct ObRegisterTxDataArg
{
  OB_UNIS_VERSION(1);

public:
  ObRegisterTxDataArg();
  ~ObRegisterTxDataArg() = default;
  void inc_request_id(const int64_t base_request_id);
  int init(const transaction::ObTxDesc &tx_desc,
           const share::ObLSID &ls_id,
           const transaction::ObTxDataSourceType &type,
           const common::ObString &buf,
           const transaction::ObTxSEQ seq_no,
           const int64_t base_request_id,
           const transaction::ObRegisterMdsFlag &register_flag);
  TO_STRING_KV(KPC_(tx_desc),
               K_(ls_id),
               K_(type),
               KP(buf_.length()),
               K_(seq_no),
               K_(request_id),
               K_(register_flag));

public:
  transaction::ObTxDesc *tx_desc_;
  share::ObLSID ls_id_;
  transaction::ObTxDataSourceType type_;
  common::ObString buf_;
  transaction::ObTxSEQ seq_no_;
  int64_t request_id_;

  transaction::ObRegisterMdsFlag register_flag_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObRegisterTxDataArg);
};

struct ObBatchGetTabletBindingRes final
{
  OB_UNIS_VERSION(1);
public:
  ObBatchGetTabletBindingRes() : binding_datas_() {}
  ~ObBatchGetTabletBindingRes() {}
public:
  bool is_valid() const { return binding_datas_.count() > 0; }
  TO_STRING_KV(K_(binding_datas));
public:
  common::ObSArray<storage::ObTabletBindingMdsUserData> binding_datas_;
};

struct ObBatchGetTabletSplitRes final
{
  OB_UNIS_VERSION(1);
public:
  ObBatchGetTabletSplitRes() : split_datas_() {}
  ~ObBatchGetTabletSplitRes() {}
public:
  bool is_valid() const { return split_datas_.count() > 0; }
  TO_STRING_KV(K_(split_datas));
public:
  common::ObSArray<storage::ObTabletSplitMdsUserData> split_datas_;
};
#ifdef OB_BUILD_SHARED_STORAGE

struct ObGetSSMacroBlockArg final
{
  OB_UNIS_VERSION(1);
public:
  ObGetSSMacroBlockArg() : tenant_id_(OB_INVALID_TENANT_ID), macro_id_(), offset_(0), size_(0) {}
  ~ObGetSSMacroBlockArg() { tenant_id_ = OB_INVALID_TENANT_ID; }
  bool is_valid() const
  {
    return OB_INVALID_TENANT_ID != tenant_id_ && macro_id_.is_valid();
  }
  TO_STRING_KV(K_(tenant_id), K_(macro_id), K_(offset), K_(size));

public:
  uint64_t tenant_id_;
  blocksstable::MacroBlockId macro_id_;
  int64_t offset_;
  int64_t size_;
  DISALLOW_COPY_AND_ASSIGN(ObGetSSMacroBlockArg);
};

struct ObGetSSMacroBlockResult final
{
  OB_UNIS_VERSION(1);
public:
  ObGetSSMacroBlockResult() : macro_buf_(), allocator_() {}
  ~ObGetSSMacroBlockResult()
  {
    allocator_.clear();
  }
  void reset()
  {
    macro_buf_.reset();
  }
  TO_STRING_KV(K_(macro_buf));

public:
  common::ObString macro_buf_;
  common::ObArenaAllocator allocator_;
};

struct ObGetSSPhyBlockInfoArg final
{
  OB_UNIS_VERSION(1);
public:
  ObGetSSPhyBlockInfoArg() : tenant_id_(OB_INVALID_TENANT_ID), phy_block_idx_(0) {}
  ~ObGetSSPhyBlockInfoArg() {}
  bool is_valid() const
  {
    return OB_INVALID_TENANT_ID != tenant_id_ && phy_block_idx_ >= 0;
  }
  TO_STRING_KV(K_(tenant_id), K_(phy_block_idx));
public:
  uint64_t tenant_id_;
  int64_t phy_block_idx_;
};

struct ObGetSSPhyBlockInfoResult final
{
  OB_UNIS_VERSION(1);
public:
  ObGetSSPhyBlockInfoResult() : ss_phy_block_info_(), ret_(common::OB_ERROR) {}
  ~ObGetSSPhyBlockInfoResult() {}
  TO_STRING_KV(K_(ss_phy_block_info), K_(ret));
public:
  ObSSPhysicalBlock ss_phy_block_info_;
  int ret_;
};

struct ObGetSSMicroBlockMetaArg final
{
  OB_UNIS_VERSION(1);
public:
  ObGetSSMicroBlockMetaArg() : tenant_id_(OB_INVALID_TENANT_ID), micro_key_() {}
  ~ObGetSSMicroBlockMetaArg() {}
  bool is_valid() const
  {
    return  OB_INVALID_TENANT_ID != tenant_id_ && micro_key_.is_valid();
  }
  TO_STRING_KV(K_(tenant_id), K_(micro_key));
public:
  uint64_t tenant_id_;
  storage::ObSSMicroBlockCacheKey micro_key_;
};

struct ObSSMicroMetaInfo
{
  OB_UNIS_VERSION(1);
public:
  ObSSMicroMetaInfo() : reuse_version_(0), data_dest_(0), access_time_(0), length_(0), is_in_l1_(0),
    is_in_ghost_(0), is_persisted_(0), is_reorganizing_(0), ref_cnt_(0), crc_(0), micro_key_() {}
  ~ObSSMicroMetaInfo() {}
  TO_STRING_KV(K_(reuse_version), K_(data_dest), K_(access_time), K_(length), K_(is_in_l1), K_(is_in_ghost),
      K_(is_persisted), K_(is_reorganizing), K_(ref_cnt), K_(crc), K_(micro_key));
public:
  uint64_t reuse_version_;
  uint64_t data_dest_;
  uint64_t access_time_;
  uint64_t length_;
  uint64_t is_in_l1_;
  uint64_t is_in_ghost_;
  uint64_t is_persisted_;
  uint64_t is_reorganizing_;
  uint32_t ref_cnt_;
  uint32_t crc_;
  ObSSMicroBlockCacheKey micro_key_;
};

struct ObGetSSMicroBlockMetaResult final
{
  OB_UNIS_VERSION(1);
public:
  ObGetSSMicroBlockMetaResult() : micro_meta_info_(), ret_(common::OB_ERROR) {}
  ~ObGetSSMicroBlockMetaResult() {}
  TO_STRING_KV(K_(micro_meta_info), K_(ret));
public:
  ObSSMicroMetaInfo micro_meta_info_;
  int ret_;
};

struct ObGetSSMacroBlockByURIArg final
{
  OB_UNIS_VERSION(1);
public:
  ObGetSSMacroBlockByURIArg() : tenant_id_(OB_INVALID_TENANT_ID), offset_(0), size_(0) {}
  ~ObGetSSMacroBlockByURIArg() { tenant_id_ = OB_INVALID_TENANT_ID; }
  bool is_valid() const
  {
    return OB_INVALID_TENANT_ID != tenant_id_ && STRLEN(uri_) > 0;
  }
  TO_STRING_KV(K_(tenant_id), K_(uri));

public:
  uint64_t tenant_id_;
  int64_t offset_;
  int64_t size_;
  char uri_[common::OB_MAX_URI_LENGTH] = {0};
  DISALLOW_COPY_AND_ASSIGN(ObGetSSMacroBlockByURIArg);
};

struct ObGetSSMacroBlockByURIResult final
{
  OB_UNIS_VERSION(1);
public:
  ObGetSSMacroBlockByURIResult() : macro_buf_(), allocator_() {}
  ~ObGetSSMacroBlockByURIResult()
  {
    allocator_.clear();
  }
  void reset()
  {
    macro_buf_.reset();
  }
  TO_STRING_KV(K_(macro_buf));

public:
  common::ObString macro_buf_;
  common::ObArenaAllocator allocator_;
};

struct ObDelSSTabletMetaArg final
{
  OB_UNIS_VERSION(1);
public:
  ObDelSSTabletMetaArg() : tenant_id_(OB_INVALID_TENANT_ID), macro_id_() {}
  ~ObDelSSTabletMetaArg() { tenant_id_ = OB_INVALID_TENANT_ID; }
  bool is_valid() const
  {
    return OB_INVALID_TENANT_ID != tenant_id_ && macro_id_.is_valid();
  }
  TO_STRING_KV(K_(tenant_id), K_(macro_id));

public:
  uint64_t tenant_id_;
  blocksstable::MacroBlockId macro_id_;
  DISALLOW_COPY_AND_ASSIGN(ObDelSSTabletMetaArg);
};

struct ObEnableSSMicroCacheArg final
{
  OB_UNIS_VERSION(1);
public:
  ObEnableSSMicroCacheArg() : tenant_id_(OB_INVALID_TENANT_ID), is_enabled_(true) {}
  ~ObEnableSSMicroCacheArg() {}
  bool is_valid() const
  {
    return OB_INVALID_TENANT_ID != tenant_id_ ;
  }
  TO_STRING_KV(K_(tenant_id), K_(is_enabled));
public:
  uint64_t tenant_id_;
  bool is_enabled_;
};

struct ObGetSSMicroCacheInfoArg final
{
  OB_UNIS_VERSION(1);
public:
  ObGetSSMicroCacheInfoArg() : tenant_id_(OB_INVALID_TENANT_ID) {}
  ~ObGetSSMicroCacheInfoArg() {}
  bool is_valid() const
  {
    return OB_INVALID_TENANT_ID != tenant_id_ ;
  }
  TO_STRING_KV(K_(tenant_id));
public:
  uint64_t tenant_id_;
};

struct ObGetSSMicroCacheInfoResult final
{
  OB_UNIS_VERSION(1);
public:
  ObGetSSMicroCacheInfoResult() : micro_cache_stat_(), super_block_(), arc_info_() {}
  ~ObGetSSMicroCacheInfoResult() {}
  TO_STRING_KV(K_(micro_cache_stat), K_(super_block), K_(arc_info));
public:
  ObSSMicroCacheStat micro_cache_stat_;
  ObSSMicroCacheSuperBlock super_block_;
  ObSSARCInfo arc_info_;
};

struct ObClearSSMicroCacheArg final
{
  OB_UNIS_VERSION(1);

public:
  ObClearSSMicroCacheArg() : tenant_id_(OB_INVALID_TENANT_ID) {}
  ~ObClearSSMicroCacheArg() {}
  bool is_valid() const
  {
    return is_valid_tenant_id(tenant_id_);
  }
  TO_STRING_KV(K_(tenant_id));

public:
  uint64_t tenant_id_;
};

struct ObDelSSLocalTmpFileArg final
{
  OB_UNIS_VERSION(1);
public:
  ObDelSSLocalTmpFileArg() : tenant_id_(OB_INVALID_TENANT_ID), macro_id_() {}
  ~ObDelSSLocalTmpFileArg() {}
  bool is_valid() const
  {
    return OB_INVALID_TENANT_ID != tenant_id_ && macro_id_.is_valid();
  }
  TO_STRING_KV(K_(tenant_id), K_(macro_id));
public:
  int64_t tenant_id_;
  blocksstable::MacroBlockId macro_id_;
};

struct ObDelSSLocalMajorArg final
{
  OB_UNIS_VERSION(1);
public:
  ObDelSSLocalMajorArg() : tenant_id_(OB_INVALID_TENANT_ID) {}
  ~ObDelSSLocalMajorArg() {}
  bool is_valid() const
  {
    return OB_INVALID_TENANT_ID != tenant_id_;
  }
  TO_STRING_KV(K_(tenant_id));
public:
  int64_t tenant_id_;
};

struct ObDelSSTabletMicroArg final
{
  OB_UNIS_VERSION(1);
public:
  ObDelSSTabletMicroArg() : tenant_id_(OB_INVALID_TENANT_ID), tablet_id_() {}
  ~ObDelSSTabletMicroArg() {}
  bool is_valid() const
  {
    return OB_INVALID_TENANT_ID != tenant_id_ && tablet_id_.is_valid();
  }
  TO_STRING_KV(K_(tenant_id), K_(tablet_id));
public:
  int64_t tenant_id_;
  ObTabletID tablet_id_;
};

struct ObSetSSCkptCompressorArg final
{
  OB_UNIS_VERSION(1);
public:
  ObSetSSCkptCompressorArg()
      : tenant_id_(OB_INVALID_TENANT_ID),
        block_type_(ObSSPhyBlockType::SS_INVALID_BLK_TYPE),
        compressor_type_(common::ObCompressorType::INVALID_COMPRESSOR)
  {}
  ~ObSetSSCkptCompressorArg()
  {}
  bool is_valid() const
  {
    return OB_INVALID_TENANT_ID != tenant_id_ && compressor_type_ != common::ObCompressorType::INVALID_COMPRESSOR &&
           is_ckpt_block_type(block_type_);
  }
  TO_STRING_KV(K_(tenant_id), K_(block_type), K_(compressor_type));
public:
  int64_t tenant_id_;
  ObSSPhyBlockType block_type_;
  common::ObCompressorType compressor_type_;
};

struct ObSetSSCacheSizeRatioArg final
{
  OB_UNIS_VERSION(1);
public:
  ObSetSSCacheSizeRatioArg()
      : tenant_id_(OB_INVALID_TENANT_ID),
        micro_cache_size_ratio_(0),
        macro_cache_size_ratio_(0)
  {}
  ~ObSetSSCacheSizeRatioArg()
  {}
  bool is_valid() const
  {
    return (OB_INVALID_TENANT_ID != tenant_id_) &&
           (micro_cache_size_ratio_ > 0 && micro_cache_size_ratio_ < 100) &&
           (macro_cache_size_ratio_ > 0 && macro_cache_size_ratio_ < 100);
  }
  TO_STRING_KV(K_(tenant_id), K_(micro_cache_size_ratio), K_(macro_cache_size_ratio));
public:
  int64_t tenant_id_;
  int64_t micro_cache_size_ratio_;
  int64_t macro_cache_size_ratio_;
};

struct ObCalibrateSSDiskSpaceArg final
{
  OB_UNIS_VERSION(1);
public:
  ObCalibrateSSDiskSpaceArg() : tenant_id_(OB_INVALID_TENANT_ID){}
  ~ObCalibrateSSDiskSpaceArg() {}
  bool is_valid() const
  {
    return OB_INVALID_TENANT_ID != tenant_id_;
  }
  TO_STRING_KV(K_(tenant_id));
public:
  int64_t tenant_id_;
};
#endif
#ifdef OB_BUILD_SHARED_STORAGE
struct ObLSSyncHotMicroKeyArg
{
  OB_UNIS_VERSION(1);
public:
  ObLSSyncHotMicroKeyArg() : tenant_id_(OB_INVALID_TENANT_ID), ls_id_(share::ObLSID::INVALID_LS_ID), leader_addr_(), micro_keys_() {}
  ~ObLSSyncHotMicroKeyArg() {}
  int assign(const ObLSSyncHotMicroKeyArg &other);
  bool is_valid() const;
  // return reserved serialize size besides ObSSMicroBlockCacheKeyMeta elements (including
  // OB_UNIS_VERSION, tenant_id_, ls_id_, leader_addr_.get_serialize_size(), count of ObSArray,
  // and NS_::OB_SERIALIZE_SIZE_NEED_BYTES), which is smaller than 4KB.
  OB_INLINE int64_t get_reserved_serialize_size() const { return 4096; }
  TO_STRING_KV(K_(tenant_id), K_(ls_id), K_(leader_addr), "micro_keys_cnt", micro_keys_.count());

public:
  uint64_t tenant_id_;
  int64_t ls_id_;
  ObAddr leader_addr_;
  ObSArray<ObSSMicroBlockCacheKeyMeta> micro_keys_;
};
#endif
struct ObDDLBuildSingleReplicaRequestArg final
{
  OB_UNIS_VERSION(1);
public:
  ObDDLBuildSingleReplicaRequestArg() :
      rowkey_allocator_("SplitRangeRPC"),
      ls_id_(), source_tablet_id_(), dest_tablet_id_(),
      source_table_id_(OB_INVALID_ID), dest_schema_id_(OB_INVALID_ID),
      schema_version_(0), snapshot_version_(0), ddl_type_(0), task_id_(0), parallelism_(0), execution_id_(-1), tablet_task_id_(0),
      data_format_version_(0), dest_ls_id_(), dest_schema_version_(0),
      compaction_scn_(0), can_reuse_macro_block_(false), split_sstable_type_(share::ObSplitSSTableType::SPLIT_BOTH),
      lob_col_idxs_(), parallel_datum_rowkey_list_(), is_no_logging_(false),
      min_split_start_scn_()
  {}
  bool is_valid() const;
  int assign(const ObDDLBuildSingleReplicaRequestArg &other);
  TO_STRING_KV(K_(ls_id), K_(source_tablet_id), K_(dest_tablet_id),
    K_(source_table_id), K_(dest_schema_id), K_(schema_version), K_(snapshot_version), K_(ddl_type),
    K_(task_id), K_(parallelism), K_(execution_id), K_(tablet_task_id), K_(data_format_version),
    K_(dest_ls_id), K_(dest_schema_version),
    K_(compaction_scn), K_(can_reuse_macro_block), K_(split_sstable_type), K_(lob_col_idxs),
    K_(parallel_datum_rowkey_list), K_(is_no_logging), K_(min_split_start_scn));
public:
  common::ObArenaAllocator rowkey_allocator_; // alloc buf for datum rowkey.
  share::ObLSID ls_id_;
  ObTabletID source_tablet_id_;
  ObTabletID dest_tablet_id_;
  int64_t source_table_id_;
  int64_t dest_schema_id_;
  int64_t schema_version_;
  int64_t snapshot_version_;
  int64_t ddl_type_;
  int64_t task_id_;
  int64_t parallelism_;
  int64_t execution_id_;
  int64_t tablet_task_id_;
  uint64_t data_format_version_;
  share::ObLSID dest_ls_id_;
  int64_t dest_schema_version_;
  int64_t compaction_scn_;
  bool can_reuse_macro_block_;
  share::ObSplitSSTableType split_sstable_type_;
  ObSArray<uint64_t> lob_col_idxs_;
  common::ObSArray<blocksstable::ObDatumRowkey> parallel_datum_rowkey_list_;
  bool is_no_logging_;
  share::SCN min_split_start_scn_;
};

struct ObDDLBuildSingleReplicaRequestResult final
{
  OB_UNIS_VERSION(1);
public:
  ObDDLBuildSingleReplicaRequestResult()
    : ret_code_(OB_SUCCESS), row_inserted_(0), row_scanned_(0), physical_row_count_(0)
  {}
  ~ObDDLBuildSingleReplicaRequestResult() = default;
  TO_STRING_KV(K_(ret_code), K_(row_inserted), K_(row_scanned), K_(physical_row_count))
public:
  int64_t ret_code_;
  int64_t row_inserted_;
  int64_t row_scanned_;
  int64_t physical_row_count_;
};

struct ObPrepareSplitRangesArg final
{
  OB_UNIS_VERSION(1);
public:
  ObPrepareSplitRangesArg()
    : ls_id_(),
      tablet_id_(),
      user_parallelism_(0),
      schema_tablet_size_(0),
      ddl_type_(share::ObDDLType::DDL_INVALID)
  {}
  ~ObPrepareSplitRangesArg() {}
  bool is_valid() const
  {
    return ls_id_.is_valid() && tablet_id_.is_valid() && share::ObDDLType::DDL_INVALID != ddl_type_;
  }
  TO_STRING_KV(K(ls_id_), K(tablet_id_), K_(user_parallelism), K_(schema_tablet_size), K_(ddl_type));
public:
  share::ObLSID ls_id_;
  ObTabletID tablet_id_;
  int64_t user_parallelism_;
  int64_t schema_tablet_size_;
  share::ObDDLType ddl_type_;
DISALLOW_COPY_AND_ASSIGN(ObPrepareSplitRangesArg);
};

struct ObPrepareSplitRangesRes final
{
  OB_UNIS_VERSION(1);
public:
  ObPrepareSplitRangesRes()
    : rowkey_allocator_("SplitRangeRPC"),
      parallel_datum_rowkey_list_()
  {}
  ~ObPrepareSplitRangesRes() = default;
  TO_STRING_KV(K_(parallel_datum_rowkey_list));
public:
  common::ObArenaAllocator rowkey_allocator_; // alloc buf for datum rowkey.
  common::ObSArray<blocksstable::ObDatumRowkey> parallel_datum_rowkey_list_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObPrepareSplitRangesRes);
};

struct ObTabletSplitArg final
{
  OB_UNIS_VERSION(1);
public:
  ObTabletSplitArg()
    : rowkey_allocator_("SplitRangeRPC"),
      ls_id_(), table_id_(OB_INVALID_ID), lob_table_id_(OB_INVALID_ID),
      schema_version_(0), task_id_(0), source_tablet_id_(),
      dest_tablets_id_(), compaction_scn_(0), data_format_version_(0),
      can_reuse_macro_block_(false), split_sstable_type_(share::ObSplitSSTableType::SPLIT_BOTH),
      lob_col_idxs_(), parallel_datum_rowkey_list_(), min_split_start_scn_()
  {}
  ~ObTabletSplitArg() = default;
  bool is_valid() const;
  int assign(const ObTabletSplitArg &other);
  TO_STRING_KV(K_(ls_id), K_(table_id), K_(lob_table_id),
               K_(schema_version), K_(task_id), K_(source_tablet_id),
               K_(dest_tablets_id), K_(compaction_scn), K_(data_format_version),
               K_(can_reuse_macro_block), K_(split_sstable_type),
               K_(lob_col_idxs), K_(parallel_datum_rowkey_list), K_(min_split_start_scn));
public:
  common::ObArenaAllocator rowkey_allocator_; // alloc buf for datum rowkey.
  share::ObLSID ls_id_;
  uint64_t table_id_; // scan rows needed.
  uint64_t lob_table_id_; // scan rows needed.
  int64_t schema_version_; // report replica build status needed.
  int64_t task_id_; // report replica build status needed.
  common::ObTabletID source_tablet_id_;
  common::ObSArray<common::ObTabletID> dest_tablets_id_;
  int64_t compaction_scn_;
  int64_t data_format_version_;
  bool can_reuse_macro_block_;
  share::ObSplitSSTableType split_sstable_type_;
  common::ObSEArray<uint64_t, 16> lob_col_idxs_;
  common::ObSArray<blocksstable::ObDatumRowkey> parallel_datum_rowkey_list_;
  share::SCN min_split_start_scn_;
};
struct ObTabletSplitStartArg final
{
  OB_UNIS_VERSION(1);
public:
  ObTabletSplitStartArg()
    : split_info_array_()
    {}
  ~ObTabletSplitStartArg() = default;
  bool is_valid() const;
  TO_STRING_KV(K_(split_info_array));
public:
  common::ObSArray<ObTabletSplitArg> split_info_array_;
};

struct ObTabletSplitStartResult final
{
    OB_UNIS_VERSION(1);
public:
  ObTabletSplitStartResult()
    : ret_codes_(), min_split_start_scn_()
  {}
  ~ObTabletSplitStartResult() = default;
  TO_STRING_KV(K_(ret_codes), K_(min_split_start_scn));
public:
  common::ObSArray<int> ret_codes_;
  share::SCN min_split_start_scn_;
};

struct ObTabletSplitFinishArg final
{
  OB_UNIS_VERSION(1);
public:
  ObTabletSplitFinishArg()
    : split_info_array_()
    {}
  ~ObTabletSplitFinishArg() = default;
  bool is_valid() const;
  TO_STRING_KV(K_(split_info_array));
public:
  common::ObSArray<ObTabletSplitArg> split_info_array_;
};

struct ObTabletSplitFinishResult final
{
    OB_UNIS_VERSION(1);
public:
  ObTabletSplitFinishResult()
    : ret_codes_()
  {}
  ~ObTabletSplitFinishResult() = default;
  TO_STRING_KV(K_(ret_codes));
public:
  common::ObSArray<int> ret_codes_;
  uint64_t dest_tenant_id_;
  share::ObLSID dest_ls_id_;
  int64_t dest_schema_version_;
  common::ObAddr server_addr_;
};

struct ObCallRemoteWriteDDLRedoLogArg
{
  OB_UNIS_VERSION(1);
public:
  ObCallRemoteWriteDDLRedoLogArg();
  ~ObCallRemoteWriteDDLRedoLogArg() = default;
  int init(const share::ObLSID &ls_id,
           const storage::ObDDLMacroBlockRedoInfo &redo_info,
           const int64_t task_id);
  bool is_valid() const { return 1UL != OB_INVALID_ID && ls_id_.is_valid() && redo_info_.is_valid() && task_id_ != 0; }
  TO_STRING_KV(K(ls_id_), K_(redo_info), K(task_id_));
public:
  share::ObLSID ls_id_;
  storage::ObDDLMacroBlockRedoInfo redo_info_;
  int64_t task_id_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObCallRemoteWriteDDLRedoLogArg);
};

struct ObCallRemoteWriteDDLCommitLogArg final
{
  OB_UNIS_VERSION(1);
public:
  ObCallRemoteWriteDDLCommitLogArg();
  ~ObCallRemoteWriteDDLCommitLogArg() = default;
  int init(const share::ObLSID &ls_id,
           const storage::ObITable::TableKey &table_key,
           const share::SCN &start_scn);
  bool is_valid() const
  {
    return 1UL != OB_INVALID_ID && ls_id_.is_valid() && table_key_.is_valid() && start_scn_.is_valid_and_not_min();
  }
  TO_STRING_KV(K_(ls_id), K_(table_key), K_(start_scn), K_(table_id),
               K_(execution_id), K_(ddl_task_id));
public:
  share::ObLSID ls_id_;
  storage::ObITable::TableKey table_key_;
  share::SCN start_scn_;
  int64_t table_id_; // depercated
  int64_t execution_id_; // depercated
  int64_t ddl_task_id_; // depercated
private:
  DISALLOW_COPY_AND_ASSIGN(ObCallRemoteWriteDDLCommitLogArg);
};
struct ObCallRemoteWriteDDLIncCommitLogArg final
{
  OB_UNIS_VERSION(1);
public:
  ObCallRemoteWriteDDLIncCommitLogArg();
  ~ObCallRemoteWriteDDLIncCommitLogArg();
  int init(const share::ObLSID &ls_id,
           const common::ObTabletID tablet_id,
           const common::ObTabletID lob_meta_tablet_id,
           transaction::ObTxDesc *tx_desc);
  int release();
  bool is_valid() const
  {
    return 1UL != OB_INVALID_ID && ls_id_.is_valid() && tablet_id_.is_valid() &&
           OB_NOT_NULL(tx_desc_) && tx_desc_->is_valid();
  }
  TO_STRING_KV(K_(ls_id), K_(tablet_id), K_(lob_meta_tablet_id), KP_(tx_desc));
public:
  share::ObLSID ls_id_;
  common::ObTabletID tablet_id_;
  common::ObTabletID lob_meta_tablet_id_;
  transaction::ObTxDesc *tx_desc_;
  bool need_release_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObCallRemoteWriteDDLIncCommitLogArg);
};

struct ObCallRemoteWriteDDLIncCommitLogRes final
{
  OB_UNIS_VERSION(1);
public:
  ObCallRemoteWriteDDLIncCommitLogRes() : tx_result_() {}
  ~ObCallRemoteWriteDDLIncCommitLogRes() {}
  TO_STRING_KV(K_(tx_result));
public:
  transaction::ObTxExecResult tx_result_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObCallRemoteWriteDDLIncCommitLogRes);
};

}  // namespace obcall
}  // namespace oceanbase
#endif
