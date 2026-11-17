/*
 * Copyright (c) 2026 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *     http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "sql/engine/basic/file_sql/file_sql_csv.h"
#include <cstdlib>
#include <iostream>
#include <sys/resource.h>
#include <unistd.h>

using namespace oceanbase::sql::file_sql;
namespace {
size_t checks = 0;
void require(bool value,const char *expr,int line)
{
  ++checks;
  if (!value) { std::cerr << "FAIL line " << line << ": " << expr << '\n'; std::exit(1); }
}
}
#define CHECK(x) require((x),#x,__LINE__)
namespace {
class TempFile
{
public:
  explicit TempFile(const std::string &data = {}) : f(tmpfile())
  {
    CHECK(f != nullptr); CHECK(fwrite(data.data(),1,data.size(),f) == data.size());
    CHECK(fflush(f) == 0); rewind(f);
  }
  ~TempFile() { fclose(f); }
  TempFile(const TempFile &) = delete;
  TempFile &operator=(const TempFile &) = delete;
  FILE *f;
};
void rejects(const std::string &input,FileCode code,size_t column = 0)
{
  TempFile f(input); CsvTableScan scan; FileStatus s = scan.prepare(f.f);
  CHECK(s.code == code);
  if (column) { CHECK(s.column == column && s.record > 0 && s.line > 0); }
  std::vector<ScalarRow> rows;
  CHECK(scan.next_batch(2,rows).code == FileCode::INVALID_ARGUMENT && rows.empty());
}
void basic_fixture(const char *path)
{
  FILE *f = fopen(path,"rb"); CHECK(f != nullptr);
  CsvTableScan scan; FileStatus s = scan.prepare(f);
  if (!s.ok()) { std::cerr << s.message << '\n'; }
  CHECK(s.ok()); CHECK(scan.row_count() == 3); CHECK(scan.columns().size() == 8);
  const ScalarType types[] = {ScalarType::BIGINT,ScalarType::VARCHAR,ScalarType::DOUBLE,
    ScalarType::BOOLEAN,ScalarType::DATE,ScalarType::DATETIME,ScalarType::VARCHAR,ScalarType::BIGINT};
  for (size_t i=0; i<8; ++i) {
    CHECK(scan.columns()[i].type == types[i]);
    CHECK(scan.columns()[i].nullable == (i==4 || i==5));
  }
  CHECK(scan.columns()[7].name == "id__2");
  std::vector<ScalarRow> rows;
  CHECK(scan.next_batch(2,rows).ok() && rows.size() == 2);
  CHECK(rows[0][0].integer == 1 && rows[0][2].real == 98.5);
  CHECK(rows[0][6].text == "hello,\nworld");
  CHECK(rows[1][4].is_null && rows[1][5].is_null && rows[1][3].integer == 0);
  CHECK(scan.next_batch(2,rows).ok() && rows.size() == 1);
  CHECK(rows[0][5].text == "2026-08-03 12:13:14" && rows[0][6].text == "say \"hi\"");
  CHECK(scan.next_batch(2,rows).code == FileCode::END && rows.empty());
  CHECK(scan.rescan().ok()); CHECK(scan.next_batch(10,rows).ok() && rows.size() == 3);
  CHECK(rows[2][7].integer == 103); fclose(f);
}
void csv_cases()
{
  {
    TempFile f("\xef\xbb\xbf" "id,name,id\r\n1,\"中文🙂\",2\r\n2,\"a\r\nb\",3");
    CsvTableScan scan; CHECK(scan.prepare(f.f).ok()); CHECK(scan.columns()[2].name == "id__2");
    std::vector<ScalarRow> rows; CHECK(scan.next_batch(10,rows).ok() && rows.size()==2);
    CHECK(rows[0][1].text == "中文🙂" && rows[1][1].text == "a\r\nb");
  }
  {
    TempFile f("id,id,id__2,ID,,\n1,2,3,4,5,6\n"); CsvTableScan scan;
    CHECK(scan.prepare(f.f).ok());
    const std::vector<std::string> expected = {"id","id__3","id__2","ID__4","column4","column5"};
    for (size_t i=0; i<expected.size(); ++i) { CHECK(scan.columns()[i].name == expected[i]); }
  }
  {
    TempFile f("a,b\n,\"\"\n"); CsvTableScan scan; CHECK(scan.prepare(f.f).ok());
    CHECK(scan.columns()[0].type == ScalarType::NULL_VALUE && scan.columns()[0].nullable);
    CHECK(scan.columns()[1].type == ScalarType::VARCHAR && !scan.columns()[1].nullable);
    std::vector<ScalarRow> rows; CHECK(scan.next_batch(1,rows).ok());
    CHECK(rows[0][0].is_null && !rows[0][1].is_null && rows[0][1].text.empty());
  }
  {
    TempFile f("1;\"a\\\"b\";NULL\n2;\"NULL\";\"\"\n"); CsvOptions o;
    o.header=false; o.delimiter=';'; o.escape='\\'; o.null_string="NULL";
    CsvTableScan scan; CHECK(scan.prepare(f.f,o).ok()); CHECK(scan.columns()[0].name == "column0");
    std::vector<ScalarRow> rows; CHECK(scan.next_batch(8,rows).ok());
    CHECK(rows[0][1].text == "a\"b" && rows[0][2].is_null);
    CHECK(rows[1][1].text == "NULL" && !rows[1][2].is_null);
  }
  {
    TempFile f("a,b\n"); CsvTableScan scan; CHECK(scan.prepare(f.f).ok() && scan.row_count()==0);
    std::vector<ScalarRow> rows; CHECK(scan.next_batch(4,rows).code==FileCode::END);
    CHECK(scan.prepare(f.f,{},10).code==FileCode::INVALID_ARGUMENT);
  }
  rejects("",FileCode::INVALID_DATA);
  rejects("a,b\n1\n",FileCode::INVALID_DATA,2);
  rejects("a,b\n1,2,3\n",FileCode::INVALID_DATA,3);
  rejects("a\n\"unterminated",FileCode::INVALID_DATA,1);
  rejects("a\n\"closed\"x\n",FileCode::INVALID_DATA,1);
  rejects("a\nunquoted\"\n",FileCode::INVALID_DATA,1);
  rejects("a\n\xc0\xaf\n",FileCode::INVALID_DATA,1);
  rejects(std::string("a\nx\0y\n",6),FileCode::INVALID_DATA,1);
  {
    TempFile f("a,b\n1,2\n"); CsvOptions o; o.max_columns=1; CsvTableScan scan;
    CHECK(scan.prepare(f.f,o).code==FileCode::RESOURCE_LIMIT);
    o={}; o.max_record_bytes=3; CHECK(scan.prepare(f.f,o).code==FileCode::RESOURCE_LIMIT);
    o={}; o.delimiter=o.quote; CHECK(scan.prepare(f.f,o).code==FileCode::INVALID_ARGUMENT);
  }
  {
    TempFile f("a\n1\n2\n"); CsvTableScan scan; CHECK(scan.prepare(f.f).ok());
    CHECK(fseeko(f.f,0,SEEK_END)==0); CHECK(fwrite("3\n",1,2,f.f)==2); CHECK(fflush(f.f)==0);
    std::vector<ScalarRow> rows; CHECK(scan.next_batch(1,rows).code==FileCode::FILE_CHANGED && rows.empty());
    CHECK(scan.rescan().code==FileCode::FILE_CHANGED);
  }
  {
    TempFile f("a\n1\n"); CsvTableScan scan; CHECK(scan.prepare(f.f).ok());
    CHECK(ftruncate(fileno(f.f),2)==0);
    std::vector<ScalarRow> rows; CHECK(scan.next_batch(1,rows).code==FileCode::FILE_CHANGED);
  }
  {
    TempFile f("a\n1\n"); CsvTableScan scan; CHECK(scan.prepare(f.f).ok());
    CHECK(pwrite(fileno(f.f),"2",1,2)==1);
    // Force an observable timestamp even on filesystems with coarse time granularity.
    struct stat st {}; CHECK(fstat(fileno(f.f),&st)==0);
    timespec times[2] = {st.st_atim,st.st_mtim}; ++times[1].tv_sec;
    CHECK(futimens(fileno(f.f),times)==0);
    std::vector<ScalarRow> rows; CHECK(scan.next_batch(1,rows).code==FileCode::FILE_CHANGED);
  }
}
void full_inference()
{
  TempFile f;
  CHECK(fputs("n,allnull\n",f.f)>=0);
  for (int i=0; i<3000; ++i) { CHECK(fputs("1,\n",f.f)>=0); }
  CHECK(fputs("2.5,\n",f.f)>=0); CHECK(fflush(f.f)==0); rewind(f.f);
  CsvTableScan scan; CHECK(scan.prepare(f.f).ok());
  CHECK(scan.columns()[0].type==ScalarType::DOUBLE && scan.row_count()==3001);
  CHECK(scan.columns()[1].type==ScalarType::NULL_VALUE && scan.columns()[1].nullable);
}
void roundtrip()
{
  const TextRow data = {{"",true},{"",false},{"NULL",false},{"a,b",false},{"say \"hi\"\n下一行",false}};
  for (char escape : {'"','\\'}) {
    TempFile f; CsvOptions o; o.null_string="NULL"; o.escape=escape;
    CsvWriter writer; CHECK(writer.init(f.f,{"a","b","c","d","e"},o).ok());
    CHECK(writer.write_row(data).ok()); CHECK(writer.finish().ok()); rewind(f.f);
    CsvReader reader; CHECK(reader.init(f.f,o).ok()); TextRow row;
    CHECK(reader.next_row(row).ok()); CHECK(reader.next_row(row).ok()); CHECK(row.size()==data.size());
    for (size_t i=0; i<row.size(); ++i) { CHECK(row[i].text==data[i].text || data[i].is_null); CHECK(row[i].is_null==data[i].is_null); }
    CHECK(reader.next_row(row).code==FileCode::END);
  }
  TempFile f; CsvOptions o; o.header=false; CsvWriter writer;
  CHECK(writer.init(f.f,{"a"},o).ok()); CHECK(writer.write_row({{"",false}}).ok()); CHECK(writer.finish().ok());
  rewind(f.f); CsvTableScan scan; CHECK(scan.prepare(f.f,o).ok());
  CHECK(scan.columns()[0].type==ScalarType::VARCHAR && scan.row_count()==1);
  {
    TempFile null_file; CsvWriter null_writer;
    CHECK(null_writer.init(null_file.f,{"a"}).ok());
    CHECK(null_writer.write_row({{"",true}}).ok()); CHECK(null_writer.finish().ok()); rewind(null_file.f);
    CsvTableScan null_scan; CHECK(null_scan.prepare(null_file.f).ok()); CHECK(null_scan.row_count()==1);
    std::vector<ScalarRow> null_rows; CHECK(null_scan.next_batch(1,null_rows).ok() && null_rows[0][0].is_null);
  }
  FILE *full = fopen("/dev/full","wb"); CHECK(full!=nullptr); setvbuf(full,nullptr,_IONBF,0);
  CHECK(writer.init(full,{"a"},o).ok()); CHECK(writer.write_row({{"x",false}}).code==FileCode::IO_ERROR);
  CHECK(writer.finish().code==FileCode::IO_ERROR); fclose(full);
  TempFile invalid_output;
  CHECK(writer.init(invalid_output.f,{"a"},o).ok());
  CHECK(writer.write_row({{"one",false},{"extra",false}}).code==FileCode::INVALID_DATA);
  CHECK(writer.finish().code==FileCode::INVALID_DATA);
}
void streaming()
{
  TempFile f;
  CHECK(fputs("id,value\n",f.f)>=0);
  const std::string payload(220,'x');
  for (int i=0; i<500000; ++i) { CHECK(fprintf(f.f,"%d,%s\n",i,payload.c_str())>0); }
  CHECK(fflush(f.f)==0); CHECK(ftello(f.f)>100*1024*1024); rewind(f.f);
  CsvTableScan scan; CHECK(scan.prepare(f.f).ok()); CHECK(scan.row_count()==500000);
  std::vector<ScalarRow> rows; int64_t count=0,sum=0;
  for (;;) {
    const FileStatus s=scan.next_batch(257,rows);
    if (s.code==FileCode::END) { break; }
    CHECK(s.ok() && rows.size()<=257);
    for (const auto &r:rows) { CHECK(r[0].integer==count++); sum+=r[0].integer; }
  }
  CHECK(count==500000 && sum==499999LL*500000/2);
  rusage usage {}; CHECK(getrusage(RUSAGE_SELF,&usage)==0);
  std::cout<<"Streaming 500000 rows, peak RSS KiB="<<usage.ru_maxrss<<'\n';
  // Run without sanitizers for this check: ASan's allocator quarantine is intentionally large.
  if (std::getenv("FILE_SQL_CHECK_RSS")) { CHECK(usage.ru_maxrss<128*1024); }
}
} // namespace
int main(int argc,char **argv)
{
  if (argc>1 && std::string(argv[1])=="--streaming") { streaming(); }
  else { if (argc>1) { basic_fixture(argv[1]); } csv_cases(); full_inference(); roundtrip(); }
  std::cout<<"PASS CSV: "<<checks<<" checks\n";
}
