/* Copyright (c) 2025 OceanBase. Licensed under the Apache License, Version 2.0. */
#include "observer/virtual_table/ob_virtual_table_iterator.h"
#include "observer/virtual_table/ob_virtual_table_projector.h"
#include "share/schema/ob_table_schema.h"
#include "data_plane/access/ob_tablet_scan.h"
#include "common/datum/datum_parameter_codec.h"
#include "common/datum/datum_parameter_array.h"
#include <gtest/gtest.h>
#include <array>
#include <cstdlib>
#include <vector>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <type_traits>
using namespace oceanbase::common;
using namespace oceanbase::share::schema;
namespace {
constexpr int64_t COLUMN_COUNT = 7;
std::vector<std::string> fields(const std::string &line) {
  std::vector<std::string> output;std::istringstream input(line);std::string value;
  while(std::getline(input,value,'\t'))output.push_back(value);
  return output;
}
std::string wire(const DatumValue &value) {
  int64_t size=0,pos=0;EXPECT_EQ(OB_SUCCESS,DatumParameterCodec::value_size(value.datum_,value.meta_,size));
  std::string bytes(size+64,'\0');EXPECT_EQ(OB_SUCCESS,DatumParameterCodec::encode_value(value.datum_,value.meta_,bytes.data(),bytes.size(),pos));
  static const char *digits="0123456789abcdef";std::string output;
  for(int64_t i=0;i<pos;++i) { const uint8_t c=bytes[i];output+=digits[c>>4];output+=digits[c&15]; }
  return output.empty()?"-":output;
}
struct Fixture {
  std::array<std::array<std::string,COLUMN_COUNT>,2> values;
  std::array<std::string,COLUMN_COUNT> snapshot;
  std::map<std::string,int> events;
  std::array<std::array<int64_t,3>,2> oom;
  bool load() {
    std::ifstream input("unittest/observer/data/native_virtual_table_rows.golden");std::string line;int64_t count=0;
    if(!input)return false;
    while(std::getline(input,line)) {
      const auto f=fields(line);++count;
      if(f.size()==4&&f[0]=="V") { const auto row=std::stoi(f[1]),col=std::stoi(f[2]);if(row<0||row>=2||col<0||col>=COLUMN_COUNT)return false;values[row][col]=f[3]; }
      else if(f.size()==4&&f[0]=="S") { const auto col=std::stoi(f[2]);if(f[1]!="0"||col<0||col>=COLUMN_COUNT)return false;snapshot[col]=f[3]; }
      else if(f.size()==3&&f[0]=="E")events[f[1]]=std::stoi(f[2]);
      else if(f.size()==5&&f[0]=="O") { const auto index=std::stoi(f[1]);if(index<0||index>=2)return false;for(int c=0;c<3;++c)oom[index][c]=std::stoll(f[c+2]); }
      else return false;
    }
    return count==26&&events.size()==3;
  }
};
class Producer final : public ObVirtualTableIterator {
public:
  Producer():index_(0),varchar_(),text_(),number_() {}
  int inner_get_next_row(ObNewRow *&row) override {
    int ret=OB_SUCCESS;
    if(index_==2)return OB_ITER_END;
    varchar_.assign(index_==0?"a\0bcd":"x\0yzq",5);text_.assign(40,index_==0?'A':'B');
    if(OB_FAIL(number_.from(index_==0?"1234.125":"-99999.75",*allocator_))) {}
    else if(OB_FAIL(DatumValueBuffer::set_int(cur_row_.cells_,cur_row_.count_,0,index_==0?-17:555333))) {}
    else if(OB_FAIL(DatumValueBuffer::set_uint64(cur_row_.cells_,cur_row_.count_,1,index_==0?UINT64_MAX-7:(uint64_t(1)<<63)+17))) {}
    else if(OB_FAIL(DatumValueBuffer::set_varchar_view(cur_row_.cells_,cur_row_.count_,2,ObString(varchar_.size(),varchar_.data())))) {}
    else if(OB_FAIL(DatumValueBuffer::set_timestamp(cur_row_.cells_,cur_row_.count_,3,1700000000000000+index_))) {}
    else if(OB_FAIL(cur_row_.cells_[4].set_number(number_,row_calc_buf_))) {}
    else if(OB_FAIL(DatumValueBuffer::set_lob_view(cur_row_.cells_,cur_row_.count_,5,ObLongTextType,ObString(text_.size(),text_.data())))) {}
    else if(OB_FAIL(DatumValueBuffer::set_null(cur_row_.cells_,cur_row_.count_,6))) {}
    else {
      cur_row_.cells_[2].set_collation_type(CS_TYPE_UTF8MB4_BIN);cur_row_.cells_[5].set_collation_type(CS_TYPE_UTF8MB4_BIN);
      row=&cur_row_;++index_;
    }
    return ret;
  }
private:
  int64_t index_;std::string varchar_,text_;number::ObNumber number_;
};
class TrackingAllocator final : public ObIAllocator {
public:
  explicit TrackingAllocator(int64_t allowed):allowed_(allowed),calls_(0),frees_(0),foreign_frees_(0),live_() {}
  ~TrackingAllocator() { for(void *p:live_)std::free(p); }
  void *alloc(int64_t size) override { if(calls_++>=allowed_)return nullptr;void *p=std::malloc(size);if(p!=nullptr)live_.insert(p);return p; }
  void *alloc(int64_t size,const ObMemAttr &) override { return alloc(size); }
  void free(void *p) override { if(p!=nullptr) { if(live_.erase(p)==0)++foreign_frees_;else { ++frees_;std::free(p); } } }
  int64_t allowed_,calls_,frees_,foreign_frees_;std::set<void *> live_;
};
int schema_for(ObTableSchema &schema,int64_t count) {
  int ret=OB_SUCCESS;schema.set_table_id(200001);schema.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  if(OB_FAIL(schema.set_table_name("vt_native_oracle"))) {}
  const SqlValueType types[COLUMN_COUNT]={ObIntType,ObUInt64Type,ObVarcharType,ObTimestampType,ObNumberType,ObLongTextType,ObIntType};
  for(int64_t i=0;OB_SUCC(ret)&&i<count;++i) {
    ObColumnSchemaV2 column;column.set_table_id(schema.get_table_id());column.set_column_id(OB_APP_MIN_COLUMN_ID+i);
    column.set_data_type(types[i]);column.set_data_length(128);column.set_collation_type(CS_TYPE_UTF8MB4_BIN);column.set_nullable(true);
    const std::string name="c"+std::to_string(i);
    if(OB_FAIL(column.set_column_name(name.c_str()))) {}else if(OB_FAIL(schema.add_column(column))) {}
  }
  return ret;
}
int configure(ObVirtualTableIterator &producer,ObIAllocator &owner,ObTableSchema &schema,ObVTableScanParam &scan,int64_t count) {
  producer.set_allocator(&owner);producer.set_reserved_column_cnt(count);producer.set_table_schema(&schema);producer.set_scan_param(&scan);
  scan.timeout_=INT64_MAX;scan.output_exprs_=nullptr;scan.op_=nullptr;
  ObSEArray<uint64_t,COLUMN_COUNT> ids;
  for(int64_t i=0;i<count;++i) { const int ret=ids.push_back(OB_APP_MIN_COLUMN_ID+i);if(ret!=OB_SUCCESS)return ret; }
  return producer.set_output_column_ids(ids);
}
class BorrowedRowProducer final : public ObVirtualTableScannerIterator {
public:
  int inner_get_next_row(ObNewRow *&row) override {
    int ret=OB_SUCCESS;
    if(!start_to_read_) {
      if(OB_FAIL(DatumValueBuffer::set_int(cur_row_.cells_,cur_row_.count_,0,42))) {}
      else if(OB_FAIL(scanner_.add_row(cur_row_))) {}
      else { scanner_it_=scanner_.begin();start_to_read_=true; }
    }
    if(OB_SUCC(ret)&&OB_FAIL(scanner_it_.get_next_row(cur_row_))) {}
    else if(OB_SUCC(ret))row=&cur_row_;
    return ret;
  }
  const void *owned_allocation() const { return row_cells_; }
};
// This deliberately swaps the descriptor array to exercise the iterator's
// ownership contract separately from the compact scanner, which writes into
// the caller-provided descriptor array.
class ExplicitBorrowedRowProducer final : public ObVirtualTableIterator {
public:
  ExplicitBorrowedRowProducer():borrowed_owner_(), borrowed_cells_(nullptr) {}
  int inner_get_next_row(ObNewRow *&row) override {
    int ret=OB_SUCCESS;
    if (borrowed_cells_ == nullptr && OB_FAIL(DatumValueBuffer::allocate(borrowed_owner_, 1, borrowed_cells_))) {}
    else if (OB_FAIL(DatumValueBuffer::set_int(borrowed_cells_, 1, 0, 84))) {}
    else { cur_row_.cells_ = borrowed_cells_; row = &cur_row_; }
    return ret;
  }
  const void *owned_allocation() const { return row_cells_; }
  const DatumValue *borrowed_cells() const { return borrowed_cells_; }
private:
  ObArenaAllocator borrowed_owner_;
  DatumValue *borrowed_cells_;
};
class ColumnPeer : public ObVirtualTableProjector {
public:
  static void check_relocation() {
    static_assert(!std::is_trivial<Column>::value,"Array relocation must run native Column copy");
    ObArray<Column> columns;
    for(int64_t i=0;i<257;++i) {
      Column source;source.column_id_=i;
      ASSERT_EQ(OB_SUCCESS,DatumValueBuffer::set_int(&source.column_value_,1,0,-i-17));
      ASSERT_EQ(OB_SUCCESS,columns.push_back(source));MEMSET(source.fixed_payload_,0xff,sizeof(source.fixed_payload_));
    }
    ASSERT_EQ(OB_SUCCESS,columns.reserve(4096));
    for(int64_t i=0;i<columns.count();++i) { EXPECT_EQ(-i-17,columns.at(i).column_value_.get_int());EXPECT_EQ(columns.at(i).fixed_payload_,columns.at(i).column_value_.datum_.ptr_); }
    std::string bytes(48,'A');Column source;ASSERT_EQ(OB_SUCCESS,DatumValueBuffer::set_varchar_view(&source.column_value_,1,0,ObString(bytes.size(),bytes.data())));ASSERT_EQ(OB_SUCCESS,columns.push_back(source));
    const DatumValue &borrowed=columns.at(columns.count()-1).column_value_;EXPECT_EQ(bytes.data(),borrowed.datum_.ptr_);
    ObArenaAllocator owner;DatumValue owned;ASSERT_EQ(OB_SUCCESS,clone_typed_value(borrowed,owned,owner));
    bytes[0]='Z';EXPECT_EQ('Z',borrowed.get_string_ptr()[0]);columns.reset();EXPECT_EQ('A',owned.get_string_ptr()[0]);
  }
};
}
TEST(NativeVirtualTableRows, immutable_old_producer_wire_and_clone_survive_advance) {
  Fixture fixture;ASSERT_TRUE(fixture.load());THIS_WORKER.set_timeout_ts(INT64_MAX);
  ObTableSchema schema;ASSERT_EQ(OB_SUCCESS,schema_for(schema,COLUMN_COUNT));ObVTableScanParam scan;
  ObArenaAllocator owner,snapshot_owner;Producer producer;ASSERT_EQ(OB_SUCCESS,configure(producer,owner,schema,scan,COLUMN_COUNT));
  ASSERT_EQ(fixture.events.at("open"),producer.open());ObNewRow retained;
  for(int64_t row_index=0;row_index<2;++row_index) {
    ObNewRow *row=nullptr;ASSERT_EQ(OB_SUCCESS,producer.get_next_row(row));ASSERT_NE(nullptr,row);
    for(int64_t c=0;c<COLUMN_COUNT;++c)EXPECT_EQ(fixture.values[row_index][c],wire(row->get_cell(c)));
    if(row_index==0)ASSERT_EQ(OB_SUCCESS,row->clone(snapshot_owner,retained));
  }
  ObNewRow *end=nullptr;EXPECT_EQ(fixture.events.at("end"),producer.get_next_row(end));EXPECT_EQ(fixture.events.at("close"),producer.close());producer.reset();
  for(int64_t c=0;c<COLUMN_COUNT;++c)EXPECT_EQ(fixture.snapshot[c],wire(retained.get_cell(c)));
}
TEST(NativeVirtualTableRows, open_oom_preserves_old_errors_and_releases_owned_block) {
  Fixture fixture;ASSERT_TRUE(fixture.load());ObTableSchema schema;ASSERT_EQ(OB_SUCCESS,schema_for(schema,COLUMN_COUNT));
  for(int64_t allowed=0;allowed<2;++allowed) {
    TrackingAllocator owner(allowed);Producer producer;ObVTableScanParam scan;ASSERT_EQ(OB_SUCCESS,configure(producer,owner,schema,scan,COLUMN_COUNT));
    const int ret=producer.open();EXPECT_EQ(fixture.oom[allowed][0],ret);if(ret==OB_SUCCESS)EXPECT_EQ(OB_SUCCESS,producer.close());producer.reset();
    EXPECT_EQ(fixture.oom[allowed][1],owner.calls_);EXPECT_EQ(fixture.oom[allowed][2],owner.frees_);EXPECT_EQ(0,owner.foreign_frees_);EXPECT_TRUE(owner.live_.empty());
  }
}
TEST(NativeVirtualTableRows, scanner_writes_into_owned_descriptor_array) {
  THIS_WORKER.set_timeout_ts(INT64_MAX);TrackingAllocator owner(1);ObTableSchema schema;ASSERT_EQ(OB_SUCCESS,schema_for(schema,1));ObVTableScanParam scan;BorrowedRowProducer producer;
  ASSERT_EQ(OB_SUCCESS,configure(producer,owner,schema,scan,1));ASSERT_EQ(OB_SUCCESS,producer.open());ASSERT_TRUE(owner.live_.count(const_cast<void *>(producer.owned_allocation()))==1);
  ObNewRow *row=nullptr;ASSERT_EQ(OB_SUCCESS,producer.get_next_row(row));ASSERT_NE(nullptr,row);EXPECT_EQ(42,row->get_cell(0).get_int());EXPECT_EQ(producer.owned_allocation(),row->cells_);
  EXPECT_EQ(OB_SUCCESS,producer.close());producer.reset();EXPECT_EQ(1,owner.frees_);EXPECT_EQ(0,owner.foreign_frees_);EXPECT_TRUE(owner.live_.empty());
}
TEST(NativeVirtualTableRows, explicit_borrowed_cells_preserve_allocation_owner) {
  THIS_WORKER.set_timeout_ts(INT64_MAX);TrackingAllocator owner(1);ObTableSchema schema;ASSERT_EQ(OB_SUCCESS,schema_for(schema,1));ObVTableScanParam scan;ExplicitBorrowedRowProducer producer;
  ASSERT_EQ(OB_SUCCESS,configure(producer,owner,schema,scan,1));ASSERT_EQ(OB_SUCCESS,producer.open());
  const void *original=producer.owned_allocation();ASSERT_TRUE(owner.live_.count(const_cast<void *>(original))==1);
  ObNewRow *row=nullptr;ASSERT_EQ(OB_SUCCESS,producer.get_next_row(row));ASSERT_NE(nullptr,row);
  ASSERT_EQ(producer.borrowed_cells(),row->cells_);ASSERT_NE(original,row->cells_);EXPECT_EQ(84,row->get_cell(0).get_int());
  EXPECT_EQ(OB_SUCCESS,producer.close());producer.reset();EXPECT_EQ(1,owner.frees_);EXPECT_EQ(0,owner.foreign_frees_);EXPECT_TRUE(owner.live_.empty());
  EXPECT_EQ(84,producer.borrowed_cells()[0].get_int());
}
TEST(NativeVirtualTableRows, projector_column_relocation_rebinds_fixed_payloads) { ColumnPeer::check_relocation(); }
