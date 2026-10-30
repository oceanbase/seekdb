/* Copyright (c) 2025 OceanBase. Licensed under the Apache License, Version 2.0. */
#include "common/rowkey/ob_rowkey.h"
#ifdef OCEANBASE_COMMON_OB_OBJECT_H_
#error "The native rowkey carrier must not require a complete legacy value"
#endif
#include "common/ob_range.h"
#include "common/datum/datum_parameter_codec.h"
#include <gtest/gtest.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
using namespace oceanbase::common;
namespace {
std::string unhex(const std::string &s) {
  std::string bytes;
  for(size_t i=0;i<s.size();i+=2)bytes.push_back(static_cast<char>(std::stoul(s.substr(i,2),nullptr,16)));
  return bytes;
}
std::string hex(const char *bytes,int64_t n) {
  std::string out;const char *digits="0123456789abcdef";
  for(int64_t i=0;i<n;++i){const unsigned char b=bytes[i];out+=digits[b>>4];out+=digits[b&15];}return out;
}
void serialized(const ObRowkey &key,std::ostringstream &out) {
  std::string bytes(key.get_serialize_size()+64,'\0');int64_t pos=0;
  const int ret=key.serialize(&bytes[0],bytes.size(),pos);
  out<<"key-wire\t"<<ret<<'\t'<<key.get_serialize_size()<<'\t'<<pos<<'\t'<<hex(bytes.data(),pos)<<'\n';
}
int decode(const std::string &wire,ObRowkey &key,DatumValue *reserved,
           ObIAllocator &owner,std::ostringstream &out) {
  const std::string bytes=unhex(wire);int64_t pos=0;key.assign(reserved,1024);
  const int ret=key.deserialize(owner,bytes.data(),bytes.size(),pos,false);
  out<<"decode\t"<<ret<<'\t'<<pos<<'\t'<<key.count()<<'\n';return ret;
}
struct Case { std::string command;std::vector<std::string> lines; };
std::vector<Case> fixture() {
  std::ifstream in("unittest/oblib/data/native_rowkey.golden");
  EXPECT_TRUE(in.good());std::vector<Case> cases;Case current;std::string line;
  while(std::getline(in,line)){
    if(line.rfind("begin\t",0)==0){current=Case();current.command=line.substr(6);}
    else if(line.rfind("end\t",0)==0){cases.push_back(current);}
    else if(!line.empty()&&line[0]!='#'){current.lines.push_back(line);}
  }return cases;
}
class RejectKeyAllocator final : public ObIAllocator {
public:
  void *alloc(int64_t) override{return nullptr;}
  void *alloc(int64_t,const ObMemAttr &) override{return nullptr;}
  void free(void *) override{}
};
}
TEST(NativeRowkey, actual_legacy_compare_prefix_equality_hash_checksum_and_wire) {
  const auto cases=fixture();ASSERT_GT(cases.size(),300U);
  for(const auto &c:cases){
    SCOPED_TRACE(c.command);std::istringstream args(c.command);std::string mode,lwire,rwire;
    args>>mode>>lwire>>rwire;DatumValue left_values[1024],right_values[1024];
    ObRowkey left,right;ObArenaAllocator owner;std::ostringstream actual,expected;
    ASSERT_EQ(OB_SUCCESS,decode(lwire,left,left_values,owner,actual));
    if(mode!="inspect"){
      if(rwire.empty()){right=left;}else{ASSERT_EQ(OB_SUCCESS,decode(rwire,right,right_values,owner,actual));}
      int cmp=19;bool equal=false;int64_t prefix=0;
      int ret=left.compare(right,cmp);actual<<"compare\t"<<ret<<'\t'<<cmp<<'\n';
      ret=left.compare_prefix(right,cmp);actual<<"prefix-compare\t"<<ret<<'\t'<<cmp<<'\n';
      ret=left.equal(right,equal);actual<<"equal\t"<<ret<<'\t'<<equal<<'\t'<<(mode!="key-no-simple"?left.simple_equal(right):false)<<'\n';
      ret=ObRowkey::get_common_prefix_length(left,right,prefix);actual<<"common-prefix\t"<<ret<<'\t'<<prefix<<'\n';
    }
    actual<<"hash\t"<<left.murmurhash(0)<<'\t'<<left.murmurhash(0x123456789abcdef0ULL)<<'\n';
    ObBatchChecksum checksum;const int ret=left.checksum(checksum);actual<<"checksum\t"<<ret<<'\t'<<checksum.calc()<<'\n';
    if(mode!="inspect"){actual<<"bounds\t"<<left.is_min_row()<<'\t'<<left.is_max_row()<<'\n';}
    serialized(left,actual);
    for(const auto &line:c.lines)if(line.rfind("print-",0)!=0)expected<<line<<'\n';
    EXPECT_EQ(expected.str(),actual.str());
  }
}
TEST(NativeRowkey, actual_legacy_diagnostic_printers) {
  for(const auto &c:fixture()){
    if(c.command.rfind("inspect ",0)!=0)continue;
    SCOPED_TRACE(c.command);DatumValue values[1024];ObRowkey key;ObArenaAllocator owner;std::ostringstream unused,actual,expected;
    ASSERT_EQ(OB_SUCCESS,decode(c.command.substr(8),key,values,owner,unused));
    char printed[32768];int64_t n=key.to_string(printed,sizeof(printed));actual<<"print-range\t"<<n<<'\t'<<hex(printed,n)<<'\n';
    n=key.to_smart_string(printed,sizeof(printed));actual<<"print-smart\t"<<n<<'\t'<<hex(printed,n)<<'\n';
    n=key.to_format_string(printed,sizeof(printed));actual<<"print-format\t"<<n<<'\t'<<hex(printed,n)<<'\n';
    n=key.to_plain_string(printed,sizeof(printed));actual<<"print-plain\t"<<n<<'\t'<<hex(printed,n)<<'\n';
    for(const auto &line:c.lines)if(line.rfind("print-",0)==0)expected<<line<<'\n';
    EXPECT_EQ(expected.str(),actual.str());
  }
}
TEST(NativeRowkey, owned_copy_survives_source_reset_and_oom_does_not_publish) {
  ObArenaAllocator source,destination;DatumValue cells[2],previous;
  ASSERT_EQ(OB_SUCCESS,cells[0].set_int(42,source));
  ASSERT_EQ(OB_SUCCESS,cells[1].set_varchar(ObString("customer"),source));
  cells[1].set_collation_type(CS_TYPE_UTF8MB4_BIN);
  ASSERT_EQ(OB_SUCCESS,previous.set_int(77,destination));
  ObRowkey key(cells,2),copy(&previous,1);RejectKeyAllocator reject;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED,key.deep_copy(copy,reject));
  EXPECT_EQ(&previous,copy.get_values());EXPECT_EQ(77,copy.get_cell(0).get_int());
  ASSERT_EQ(OB_SUCCESS,key.deep_copy(copy,destination));source.reset();cells[0].reset();cells[1].reset();
  EXPECT_EQ(42,copy.get_cell(0).get_int());EXPECT_EQ(ObString("customer"),copy.get_cell(1).get_string());
  EXPECT_EQ(CS_TYPE_UTF8MB4_BIN,copy.get_cell(1).get_collation_type());copy.destroy(destination);
}
TEST(NativeRowkey, reserved_decode_reuses_metadata_but_owns_new_payload) {
  ObArenaAllocator owner;DatumValue slot;ASSERT_EQ(OB_SUCCESS,slot.set_int(81,owner));
  slot.meta_.set_scale(7);ObRowkey key(&slot,1);int64_t pos=0;
  const char count_only[]={1};
  ASSERT_EQ(OB_SUCCESS,key.deserialize(owner,count_only,sizeof(count_only),pos,false));
  EXPECT_EQ(1,pos);EXPECT_EQ(1,key.count());EXPECT_EQ(ObIntType,key.get_cell(0).get_type());
  EXPECT_EQ(7,key.get_cell(0).get_scale());EXPECT_EQ(0,key.get_cell(0).get_int());
  DatumValue previous;ASSERT_EQ(OB_SUCCESS,previous.set_int(17,owner));slot=previous;pos=0;
  const std::string incoming=unhex("0116022dff08637573746f6d657200");RejectKeyAllocator reject;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED,key.deserialize(reject,incoming.data(),incoming.size(),pos,false));
  EXPECT_EQ(0,pos);EXPECT_EQ(17,key.get_cell(0).get_int());
}
TEST(NativeRowkey, bounded_buffer_copy_publishes_only_on_success) {
  ObArenaAllocator owner;DatumValue cell,old;ASSERT_EQ(OB_SUCCESS,cell.set_int(42,owner));
  ASSERT_EQ(OB_SUCCESS,old.set_int(71,owner));ObRowkey source(&cell,1),target(&old,1);
  std::vector<char> bytes(source.get_deep_copy_size());
  EXPECT_NE(OB_SUCCESS,source.deep_copy(target,bytes.data(),sizeof(DatumValue)-1));
  EXPECT_EQ(&old,target.get_values());EXPECT_EQ(71,target.get_cell(0).get_int());
  ASSERT_EQ(OB_SUCCESS,source.deep_copy(target,bytes.data(),bytes.size()));
  cell.reset();EXPECT_EQ(42,target.get_cell(0).get_int());
}

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  if (ObCharset::init_charset() != OB_SUCCESS) { return 1; }
  return RUN_ALL_TESTS();
}
