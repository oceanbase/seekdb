/* Copyright (c) 2025 OceanBase. Licensed under the Apache License, Version 2.0. */
#include "storage/memtable/ob_memtable_read_row_util.h"
#include "storage/memtable/ob_memtable_mutator.h"
#include "storage/memtable/ob_memtable_util.h"
#include "storage/blocksstable/ob_row_writer.h"
#include "storage/lob/ob_lob_persistent_iterator.h"
#include "storage/tablet/ob_mds_scan_param_helper.h"
#include "storage/fts/ob_fts_doc_word_iterator.h"
#include "common/datum/datum_parameter_codec.h"
#include "common/datum/datum_value_projection.h"
#include <gtest/gtest.h>
#include <fstream>
#include <string>
#include <vector>
#include <cstring>
using namespace oceanbase::common;
using namespace oceanbase::storage;
using namespace oceanbase::memtable;
using namespace oceanbase::blocksstable;
namespace {
std::vector<std::string> split(const std::string &input,char delimiter='\t')
{
  std::vector<std::string> output;size_t pos=0;
  while(true) { auto end=input.find(delimiter,pos);output.push_back(input.substr(pos,end-pos));if(end==std::string::npos)break;pos=end+1; }
  return output;
}
std::string hex(const char *data,int64_t length)
{
  const char *digits="0123456789abcdef";std::string output;
  for(int64_t i=0;i<length;++i) {unsigned char c=data[i];output+=digits[c>>4];output+=digits[c&15];}
  return output.empty()?"-":output;
}
std::string unhex(const std::string &input)
{
  if(input=="-")return {};
  const char *digits="0123456789abcdef";std::string output;
  for(size_t i=0;i+1<input.size();i+=2) {
    auto *hi=std::strchr(digits,input[i]),*lo=std::strchr(digits,input[i+1]);
    if(hi==nullptr||lo==nullptr){ADD_FAILURE()<<"invalid hex";return {};}
    output+=static_cast<char>((hi-digits)*16+lo-digits);
  }
  return output;
}
std::vector<std::vector<std::string>> fixture(const char *kind)
{
  std::ifstream input("unittest/storage/data/storage_rowkey_consumers.golden");EXPECT_TRUE(input.good());
  std::vector<std::vector<std::string>> output;std::string line;
  while(std::getline(input,line)){const auto fields=split(line);if(!fields.empty()&&fields[0]==kind)output.push_back(fields);}
  return output;
}
template<typename T>std::string wire(const T &value)
{
  std::string bytes(value.get_serialize_size()+16,'\0');int64_t pos=0;
  const int ret=value.serialize(bytes.data(),bytes.size(),pos);
  return ret==0?hex(bytes.data(),pos):"ERROR"+std::to_string(ret);
}
std::string wire_value(const DatumValue &value)
{
  int64_t size=0;int ret=DatumParameterCodec::value_size(value.datum_,value.meta_,size);
  if(ret!=OB_SUCCESS)return "ERROR"+std::to_string(ret);
  std::string bytes(size,'\0');int64_t pos=0;
  ret=DatumParameterCodec::encode_value(value.datum_,value.meta_,bytes.data(),size,pos);
  return ret==0?hex(bytes.data(),pos):"ERROR"+std::to_string(ret);
}
DatumValue value_from(const std::string &input,ObIAllocator &owner)
{
  const auto bytes=unhex(input);DatumValue output;int64_t pos=0;
  EXPECT_EQ(OB_SUCCESS,DatumParameterCodec::decode_value(bytes.data(),bytes.size(),pos,owner,output.datum_,output.meta_));
  EXPECT_EQ(bytes.size(),pos);return output;
}
void key_from(const std::string &input,ObIAllocator &owner,ObStoreRowkey &output)
{
  const auto bytes=unhex(input);int64_t pos=0;
  ASSERT_EQ(OB_SUCCESS,output.deserialize(owner,bytes.data(),bytes.size(),pos));ASSERT_EQ(bytes.size(),pos);
}
std::string storage_value(const ObStorageDatum &value)
{
  return std::to_string(value.pack_)+":"+std::to_string(value.len_)+":"
      +(value.is_ext()?std::to_string(value.get_ext()):hex(value.ptr_,value.len_));
}
struct FtsBuildTag {using type=int(ObFTDocWordScanIterator::*)(uint64_t,const ObDatum &,ObIArray<ObNewRange>&);friend type access(FtsBuildTag);};
struct FtsTypeTag {using type=ObDocIDType ObFTDocWordScanIterator::*;friend type access(FtsTypeTag);};
template<typename Tag,typename Tag::type Member>struct PrivateAccess{friend typename Tag::type access(Tag){return Member;}};
template struct PrivateAccess<FtsBuildTag,&ObFTDocWordScanIterator::build_key_range>;
template struct PrivateAccess<FtsTypeTag,&ObFTDocWordScanIterator::docid_type_>;
class LobBuilder final:public ObLobMetaBaseIterator {
public:
  int range(ObLobAccessParam &param,ObNewRange &out){return build_rowkey_range(param,rowkey_values_,out);}
  int exact(ObLobAccessParam &param,ObString &seq,ObNewRange &out){return build_rowkey(param,rowkey_values_,seq,out);}
  int single(ObLobAccessParam &param,ObNewRange &out){return build_rowkey(param,rowkey_values_,out);}
};
TEST(NativeStorageRowkeyConsumers, read_key_matches_actual_old_storage_flags_bytes_and_partial_nop_error)
{
  const auto rows=fixture("READ");ASSERT_EQ(13,rows.size());
  for(const auto &fields:rows){ASSERT_EQ(5,fields.size());SCOPED_TRACE(fields[1]);ObArenaAllocator owner;
    ObStoreRowkey key;key_from(fields[2],owner,key);ObDatumRow row;ASSERT_EQ(OB_SUCCESS,row.init(owner,key.get_value_count()));
    EXPECT_EQ(std::stoi(fields[3]),ObReadRow::iterate_row_key(key,row));std::string actual;
    for(int64_t i=0;i<key.get_value_count();++i){if(i)actual+=';';actual+=storage_value(row.storage_datums_[i]);}
    EXPECT_EQ(fields[4],actual);
  }
}
TEST(NativeStorageRowkeyConsumers, lock_row_writer_matches_actual_old_full_physical_row_bytes)
{
  const auto rows=fixture("LOCK");ASSERT_EQ(12,rows.size());
  for(const auto &fields:rows){ASSERT_EQ(5,fields.size());SCOPED_TRACE(fields[1]);ObArenaAllocator owner;
    ObStoreRowkey key;key_from(fields[2],owner,key);ObRowWriter writer;char *bytes=nullptr;int64_t length=0;
    EXPECT_EQ(std::stoi(fields[3]),writer.write_lock_rowkey(key,bytes,length));EXPECT_EQ(fields[4],hex(bytes,length));
  }
}
TEST(NativeStorageRowkeyConsumers, mds_range_matches_actual_old_signed_tinyint_and_embedded_zero_keys)
{
  const auto rows=fixture("MDS");ASSERT_EQ(10,rows.size());
  for(const auto &fields:rows){ASSERT_EQ(6,fields.size());SCOPED_TRACE(fields[1]);ObArenaAllocator owner;ObNewRange range;
    char raw[]={0,1,'x',0};ObString udf(sizeof(raw),raw);const auto id=static_cast<uint8_t>(std::stoi(fields[2]));
    const int ret=std::stoi(fields[3])?ObMdsScanParamHelper::build_key_range(owner,42,id,udf,range)
        :ObMdsScanParamHelper::build_key_range(owner,42,id,range);
    EXPECT_EQ(std::stoi(fields[4]),ret);EXPECT_EQ(fields[5],wire(range));
  }
}
TEST(NativeStorageRowkeyConsumers, mds_owner_allocation_failure_keeps_both_previous_range_endpoints)
{
  ObWrapperAllocator reject;ObNewRange range;range.table_id_=99;range.set_whole_range();
  const std::string previous=wire(range);
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED,ObMdsScanParamHelper::build_key_range(
      reject,42,1,ObString::make_string("borrowed udf key"),range));
  EXPECT_EQ(previous,wire(range));
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED,ObMdsScanParamHelper::build_key_range(reject,42,1,range));
  EXPECT_EQ(previous,wire(range));
}
TEST(NativeStorageRowkeyConsumers, lob_range_and_binary_sequence_match_actual_old_and_use_stable_backing)
{
  const auto rows=fixture("LOB");ASSERT_EQ(3,rows.size());
  for(const auto &fields:rows){ASSERT_EQ(5,fields.size());SCOPED_TRACE(fields[1]);LobBuilder builder;ObLobAccessParam param;ObLobData data;
    data.id_.tablet_id_=42;data.id_.lob_id_=UINT64_MAX;param.lob_data_=&data;ObNewRange range;
    char raw[]={0,1,0,'x'};ObString seq(sizeof(raw),raw);const int action=std::stoi(fields[2]);
    const int ret=action==0?builder.range(param,range):action==1?builder.exact(param,seq,range):builder.single(param,range);
    EXPECT_EQ(std::stoi(fields[3]),ret);EXPECT_EQ(fields[4],wire(range));
  }
}
TEST(NativeStorageRowkeyConsumers, fts_hidden_and_sequence_docid_ranges_match_actual_old)
{
  const auto rows=fixture("FTS");ASSERT_EQ(4,rows.size());
  for(const auto &fields:rows){ASSERT_EQ(6,fields.size());SCOPED_TRACE(fields[1]);ObFTDocWordScanIterator iterator;
    const auto type=static_cast<ObDocIDType>(std::stoi(fields[2]));iterator.*access(FtsTypeTag{})=type;
    ObStorageDatum doc_id;if(type==ObDocIDType::TABLET_SEQUENCE)doc_id.set_string(ObString::make_string("binary-doc-id"));else doc_id.set_uint(UINT64_MAX);
    ObSEArray<ObNewRange,1> ranges;
    EXPECT_EQ(std::stoi(fields[4]),(iterator.*access(FtsBuildTag{}))(std::stoull(fields[3]),doc_id,ranges));
    EXPECT_EQ(fields[5],ranges.empty()?"-":wire(ranges.at(0)));
    if(!ranges.empty()&&type==ObDocIDType::HIDDEN_INC_PK){const std::string prior=wire(ranges.at(0));doc_id.set_int(0);EXPECT_EQ(prior,wire(ranges.at(0)));}
  }
}
TEST(NativeStorageRowkeyConsumers, mutator_full_wire_and_fixed_key_descriptors_match_actual_old)
{
  const auto rows=fixture("MUTATOR");ASSERT_EQ(3,rows.size());
  for(const auto &fields:rows){ASSERT_EQ(8,fields.size());SCOPED_TRACE(fields[1]);const auto bytes=unhex(fields[3]);
    ObMemtableMutatorRow value;int64_t pos=0;const int ret=value.deserialize(bytes.data(),bytes.size(),pos);
    EXPECT_EQ(std::stoi(fields[4]),ret);EXPECT_EQ(std::stoll(fields[5]),pos);
    if(ret==OB_SUCCESS){EXPECT_EQ(fields[2],wire(value.rowkey_));char buffer[32768];int64_t size=sizeof(buffer),end=0;
      EXPECT_EQ(std::stoi(fields[6]),value.serialize(buffer,size,end));EXPECT_EQ(fields[7],hex(buffer,end));}
  }
}
TEST(NativeStorageRowkeyConsumers, mutator_key_payloads_survive_input_overwrite_and_reset_rebinds_slots)
{
  const auto rows=fixture("MUTATOR");ASSERT_EQ(3,rows.size());ObMemtableMutatorRow value;
  for(int pass=0;pass<3;++pass){const auto &fields=rows.at(pass);std::string bytes=unhex(fields[3]);value.reset();
    EXPECT_EQ(OB_MAX_ROWKEY_COLUMN_NUMBER,value.rowkey_.get_value_count());int64_t pos=0;
    ASSERT_EQ(OB_SUCCESS,value.deserialize(bytes.data(),bytes.size(),pos));MEMSET(bytes.data(),0xa5,bytes.size());
    // New/old row-data intentionally still borrow the serialized frame; only
    // the native key has an independent owner. Do not read those row bodies.
    EXPECT_EQ(fields[2],wire(value.rowkey_));
  }
}
TEST(NativeStorageRowkeyConsumers, fake_table_lock_key_keeps_actual_old_null_declared_type)
{
  const auto rows=fixture("FAKE");ASSERT_EQ(1,rows.size());ObFakeStoreRowKey value("tbl",3);
  EXPECT_EQ(rows.at(0).at(2),wire(value.get_rowkey()));EXPECT_TRUE(value.get_rowkey().get_cell(0).meta_.is_null());
}
TEST(NativeStorageRowkeyConsumers, storage_plain_and_enhanced_projections_match_actual_old_metadata_and_errors)
{
  ObArenaAllocator owner;
  for(const char *kind:{"PLAIN","ENHANCE"}){const auto rows=fixture(kind);ASSERT_EQ(11,rows.size());
    for(const auto &fields:rows){ASSERT_EQ(7,fields.size());SCOPED_TRACE(std::string(kind)+fields[1]);
      const auto source=split(fields[2],':');ASSERT_EQ(3,source.size());ObStorageDatum datum;std::string bytes;
      const uint64_t pack=std::stoull(source[0]);datum.pack_=pack;
      if(datum.is_ext()){datum.set_ext_value(std::stoll(source[2]));}
      else if(datum.is_null()){datum.set_null();}
      else {bytes=unhex(source[2]);datum.set_string(bytes.data(),bytes.size());datum.pack_=pack;}
      const auto meta_bytes=unhex(fields[3]);ASSERT_EQ(sizeof(SqlTypeMeta),meta_bytes.size());SqlTypeMeta meta;MEMCPY(&meta,meta_bytes.data(),sizeof(meta));
      DatumValue output=value_from(fields[4],owner);const int ret=std::string(kind)=="ENHANCE"?datum.to_value_view(meta,output)
          :project_datum_value_view(datum,meta,ObDatum::get_sql_value_datum_map_type(meta.get_type()),output.meta_,output,true);
      EXPECT_EQ(std::stoi(fields[5]),ret);EXPECT_EQ(fields[6],wire_value(output));
    }
  }
}
} // namespace
