/*
 * Experimental native file-table SQL adapter for seekdb 1.4.
 *
 * The adapter rewrites FROM/JOIN read_csv/read_json/read_ndjson/glob into
 * JSON_TABLE source nodes, so relational operations remain inside seekdb.
 * It is deliberately bounded (2 MiB JSON materialization), not the final
 * streaming/Parquet/COPY implementation.
 *
 * Access is opt-in: all resolved files must be below SEEKDB_FILE_SQL_ROOT,
 * or /tmp/seekdb-file-sql when the variable is unset. Canonical paths are
 * checked with realpath() to block symlink and .. escapes.
 */
#ifndef OCEANBASE_SQL_PARSER_OB_FILE_SQL_REWRITE_H_
#define OCEANBASE_SQL_PARSER_OB_FILE_SQL_REWRITE_H_

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <glob.h>
#include <limits.h>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <vector>
#include <cstdint>
#include <cstdio>

namespace oceanbase {
namespace sql {
namespace native_file_sql {

static const size_t MAX_FILE_BYTES = 2 * 1024 * 1024;
static const size_t MAX_RESULT_BYTES = 2 * 1024 * 1024;
static const size_t MAX_FILE_ROWS = 10000;

inline bool space(const char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}
inline std::string lower(const std::string &x) {
  std::string out=x;
  for (size_t i=0; i<out.size(); ++i) {
    if (out[i]>='A' && out[i]<='Z') out[i]=static_cast<char>(out[i]-'A'+'a');
  }
  return out;
}
inline std::string trim(const std::string &x) {
  size_t a=0,b=x.size();
  while (a<b && space(x[a])) ++a;
  while (b>a && space(x[b-1])) --b;
  return x.substr(a,b-a);
}
inline bool id_start(const char c) {
  return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_';
}
inline bool id_char(const char c) {
  return id_start(c)||(c>='0'&&c<='9');
}
inline std::string json_quote(const std::string &x) {
  static const char *hex="0123456789abcdef";
  std::string out="\"";
  for (size_t i=0; i<x.size(); ++i) {
    unsigned char c=static_cast<unsigned char>(x[i]);
    switch(c) {
      case '"': out+="\\\""; break;
      case '\\': out+="\\\\"; break;
      case '\n': out+="\\n"; break;
      case '\r': out+="\\r"; break;
      case '\t': out+="\\t"; break;
      default:
        if (c<32) {
          out+="\\u00";
          out.push_back(hex[c>>4]);
          out.push_back(hex[c&15]);
        } else {
          out.push_back(static_cast<char>(c));
        }
    }
  }
  out.push_back('"');
  return out;
}
inline std::string sql_quote(const std::string &x) {
  // Parser uses MySQL SQL mode, in which backslash must be escaped.
  std::string out="'";
  for (size_t i=0; i<x.size(); ++i) {
    char c=x[i];
    if (c=='\'') out+="''";
    else if (c=='\\') out+="\\\\";
    else out.push_back(c);
  }
  out.push_back('\'');
  return out;
}
inline std::string sql_identifier(const std::string &x) {
  std::string out="`";
  for (size_t i=0;i<x.size();++i) {
    if (x[i]=='`') out+="``";
    else out.push_back(x[i]);
  }
  out.push_back('`');
  return out;
}

// SQL-session-bound allowlist. Standalone unit tests keep the historical
// environment root, but real server parses install a session's secure_file_priv
// root for the duration of SQL parsing. The parser must reject file SQL before
// entering this code unless enable_file_sql is explicitly set in its session.
inline std::string &session_secure_file_root()
{
  static thread_local std::string root;
  return root;
}
class ScopedFileSqlSecurity
{
public:
  explicit ScopedFileSqlSecurity(const std::string &allowed_root)
    : previous_(session_secure_file_root())
  {
    session_secure_file_root() = allowed_root;
  }
  ~ScopedFileSqlSecurity()
  {
    session_secure_file_root() = previous_;
  }
private:
  const std::string previous_;
  ScopedFileSqlSecurity(const ScopedFileSqlSecurity &) = delete;
  ScopedFileSqlSecurity &operator=(const ScopedFileSqlSecurity &) = delete;
};
inline bool secured_file_path(const std::string &real_path)
{
  const std::string &allowed = session_secure_file_root();
  return allowed.empty() || allowed == "/" ||
         (real_path.size()>allowed.size() &&
          real_path.compare(0,allowed.size(),allowed)==0 &&
          real_path[allowed.size()]=='/');
}
// Extract only exact SET SESSION enable_file_sql controls. All other SET
// statements are passed unchanged. The backing @ variable is session-local:
// this is an interim compatibility shim, not an actual system variable.
inline bool file_sql_set_statement(const std::string &input,std::string &out)
{
  std::string sql=trim(input);
  if(!sql.empty() && sql.back()==';')sql=trim(sql.substr(0,sql.size()-1));
  const std::string low=lower(sql);
  size_t at=0;
  auto word=[&](const char *value)->bool{
    const size_t n=std::strlen(value);
    while(at<low.size() && space(low[at]))++at;
    if(low.compare(at,n,value)!=0 ||
       (at+n<low.size() && id_char(low[at+n])))return false;
    at+=n;
    return true;
  };
  if(!word("set"))return false;
  const size_t save=at;
  if(!word("session"))at=save;
  if(!word("enable_file_sql"))return false;
  while(at<low.size() && space(low[at]))++at;
  if(at>=low.size() || low[at]!='=')return false;
  ++at;
  while(at<low.size() && space(low[at]))++at;
  std::string value=trim(low.substr(at));
  if(value=="on" || value=="true" || value=="1")
    out="SET @__seekdb_file_sql_enabled = 1";
  else if(value=="off" || value=="false" || value=="0")
    out="SET @__seekdb_file_sql_enabled = 0";
  else return false;
  return true;
}
// Replace @@session.enable_file_sql only in source text, not in strings,
// backtick identifiers, or SQL comments. The actual value is read from the
// same MySQL connection's @__seekdb_file_sql_enabled user variable.
inline bool file_sql_show_statement(const std::string &sql,std::string &out)
{
  const std::string target="@@session.enable_file_sql";
  out.clear();
  bool changed=false;
  enum State { CODE, SQ, DQ, BT, LINE, BLOCK } state=CODE;
  for(size_t i=0;i<sql.size();) {
    const char c=sql[i];
    if(state==LINE) {
      out.push_back(c);++i;
      if(c=='\n')state=CODE;
    } else if(state==BLOCK) {
      if(c=='*' && i+1<sql.size() && sql[i+1]=='/') {
        out+="*/";i+=2;state=CODE;
      }else{out.push_back(c);++i;}
    } else if(state==SQ || state==DQ || state==BT) {
      const char end=state==SQ?'\'':(state==DQ?'"':'`');
      out.push_back(c);++i;
      if(c=='\\' && i<sql.size()){out.push_back(sql[i]);++i;}
      else if(c==end && i<sql.size() && sql[i]==end) {
        out.push_back(sql[i]);++i;
      } else if(c==end)state=CODE;
    }else if(c=='\'' || c=='"' || c=='`') {
      state=c=='\''?SQ:(c=='"'?DQ:BT);
      out.push_back(c);++i;
    }else if(c=='#' || (c=='-' && i+2<sql.size() && sql[i+1]=='-' && space(sql[i+2]))) {
      state=LINE;out.push_back(c);++i;
    }else if(c=='/' && i+1<sql.size() && sql[i+1]=='*'){
      state=BLOCK;out+="/*";i+=2;
    }else if(i+target.size()<=sql.size() &&
             lower(sql.substr(i,target.size()))==target &&
             (i+target.size()==sql.size() ||
             !id_char(sql[i+target.size()]))) {
      out+="COALESCE(@__seekdb_file_sql_enabled, 0)";
      i+=target.size();
      changed=true;
    }else {
      out.push_back(c);++i;
    }
  }
  return changed;
}
inline bool file_sql_statement_detected(const std::string &sql)
{
  const std::string normalized=lower(trim(sql));
  if(normalized.size()>=4 && normalized.compare(0,4,"copy")==0 &&
     (normalized.size()==4 || !id_char(normalized[4])))return true;
  const char *fn[]={"read_csv","read_json","read_ndjson","read_parquet","glob"};
  enum State { CODE, SQ, DQ, BT, LINE, BLOCK } state=CODE;
  for(size_t i=0;i<sql.size();) {
    const char c=sql[i];
    if(state==LINE) {
      if(c=='\n')state=CODE;
      ++i;
    }else if(state==BLOCK) {
      if(c=='*' && i+1<sql.size() && sql[i+1]=='/') {
        i+=2;state=CODE;
      }else ++i;
    }else if(state==SQ || state==DQ || state==BT) {
      const char end=state==SQ?'\'':(state==DQ?'"':'`');
      ++i;
      if(c=='\\' && i<sql.size())++i;
      else if(c==end && i<sql.size() && sql[i]==end)++i;
      else if(c==end)state=CODE;
    }else if(c=='\'' || c=='"' || c=='`') {
      state=c=='\''?SQ:(c=='"'?DQ:BT);++i;
    }else if(c=='#' || (c=='-' && i+2<sql.size() && sql[i+1]=='-' && space(sql[i+2]))) {
      state=LINE;++i;
    }else if(c=='/' && i+1<sql.size() && sql[i+1]=='*') {
      state=BLOCK;i+=2;
    }else if(id_start(c)) {
      const size_t start=i++;
      while(i<sql.size() && id_char(sql[i]))++i;
      const std::string token=lower(sql.substr(start,i-start));
      size_t p=i;
      while(p<sql.size() && space(sql[p]))++p;
      for(size_t j=0;j<sizeof(fn)/sizeof(fn[0]);++j)
        if(token==fn[j] && p<sql.size() && sql[p]=='(')return true;
    }else ++i;
  }
  return false;
}

inline bool parse_quoted_path(const std::string &sql,size_t &pos,std::string &path) {
  if (pos>=sql.size() || sql[pos]!='\'') return false;
  ++pos;
  for (;pos<sql.size();++pos) {
    char c=sql[pos];
    if (c=='\'') {
      if (pos+1<sql.size() && sql[pos+1]=='\'') {
        path.push_back('\'');
        ++pos;
      } else {
        ++pos;
        return true;
      }
    } else if (c=='\\' && pos+1<sql.size()) {
      // Handle escaped quote/backslash and standard MySQL path literal.
      path.push_back(sql[++pos]);
    } else {
      path.push_back(c);
    }
  }
  return false;
}
inline bool permitted_root(std::string &root,std::string &err) {
  const char *env=std::getenv("SEEKDB_FILE_SQL_ROOT");
  const char *want=env && *env ? env :
      (session_secure_file_root().empty() ?
       "/tmp/seekdb-file-sql" : session_secure_file_root().c_str());
  char resolved[PATH_MAX];
  if (::realpath(want,resolved)==nullptr) {
    err=std::string("file_sql_root does not exist: ")+want;
    return false;
  }
  struct stat st;
  if (::stat(resolved,&st)!=0 || !S_ISDIR(st.st_mode)) {
    err="file_sql_root is not a directory";
    return false;
  }
  root=resolved;
  return true;
}
inline bool under_root(const std::string &root,const std::string &candidate) {
  return candidate.size()>root.size() &&
      candidate.compare(0,root.size(),root)==0 &&
      (root=="/" || candidate[root.size()]=='/');
}
inline bool resolve_file(const std::string &input,std::string &path,std::string &err) {
  std::string root;
  if (!permitted_root(root,err)) return false;
  // No remote URLs, named pipes, directories, or special devices.
  if (input.empty() || input.find('\0')!=std::string::npos ||
      input.find("://")!=std::string::npos) {
    err="invalid local file path";
    return false;
  }
  const std::string original=input[0]=='/'?input:(root+"/"+input);
  char resolved[PATH_MAX];
  if (::realpath(original.c_str(),resolved)==nullptr) {
    err="file cannot be resolved within file_sql_root";
    return false;
  }
  path=resolved;
  struct stat st;
  if (!under_root(root,path) || !secured_file_path(path) ||
      ::stat(path.c_str(),&st)!=0 ||
      !S_ISREG(st.st_mode) || st.st_size<0 ||
      static_cast<size_t>(st.st_size)>MAX_FILE_BYTES) {
    err="read requires a regular file smaller than 2 MiB inside file_sql_root";
    return false;
  }
  return true;
}
inline bool read_text(const std::string &input,std::string &text,std::string &err) {
  std::string path;
  if (!resolve_file(input,path,err)) return false;
  std::ifstream file(path.c_str(),std::ios::binary);
  if (!file.good()) { err="cannot open file"; return false; }
  std::ostringstream buffer;
  buffer<<file.rdbuf();
  text=buffer.str();
  if (text.size()>MAX_FILE_BYTES) {err="file exceeds size limit"; return false;}
  return file.good() || file.eof();
}
inline bool json_balanced_object(const std::string &x,size_t a,size_t &b) {
  if (a>=x.size() || x[a]!='{') return false;
  bool quoted=false,escape=false;
  int depth=0;
  for (size_t i=a;i<x.size();++i) {
    char c=x[i];
    if (quoted) {
      if (escape) escape=false;
      else if (c=='\\') escape=true;
      else if (c=='"') quoted=false;
    } else if(c=='"') quoted=true;
    else if(c=='{' || c=='[') ++depth;
    else if(c=='}' || c==']') {
      --depth;
      if (depth==0) {b=i+1;return true;}
      if (depth<0) return false;
    }
  }
  return false;
}
inline bool json_string_at(const std::string &x,size_t &p,std::string &value) {
  if (p>=x.size() || x[p]!='"') return false;
  ++p;
  bool escape=false;
  for (;p<x.size();++p) {
    char c=x[p];
    if (escape) {
      if (c=='n') value.push_back('\n');
      else if (c=='r') value.push_back('\r');
      else if (c=='t') value.push_back('\t');
      else value.push_back(c);
      escape=false;
    } else if (c=='\\') escape=true;
    else if(c=='"') {++p;return true;}
    else value.push_back(c);
  }
  return false;
}
inline bool json_object_keys(const std::string &obj,std::vector<std::string> &keys) {
  if (obj.empty() || obj[0]!='{') return false;
  int depth=0;
  bool quote=false, escape=false;
  for (size_t i=0;i<obj.size();++i) {
    char c=obj[i];
    if (quote) {
      if (escape) escape=false;
      else if(c=='\\') escape=true;
      else if(c=='"') quote=false;
      continue;
    }
    if(c=='{') {++depth;continue;}
    if(c=='}') {--depth;continue;}
    if(c=='[') {++depth;continue;}
    if(c==']') {--depth;continue;}
    if(c=='"' && depth==1) {
      size_t end=i;
      std::string name;
      if (!json_string_at(obj,end,name)) return false;
      size_t after=end;
      while(after<obj.size() && space(obj[after]))++after;
      if (after<obj.size() && obj[after]==':') {
        if (std::find(keys.begin(),keys.end(),name)==keys.end()) keys.push_back(name);
      }
      i=end-1;
      continue;
    }
    if (c=='"') quote=true;
  }
  return !keys.empty() && depth==0;
}
inline bool parse_json_records(const std::string &input,bool ndjson,
                               std::vector<std::string> &objects,
                               std::vector<std::string> &names,std::string &err) {
  if (ndjson) {
    std::istringstream lines(input);
    std::string line;
    while(std::getline(lines,line)) {
      line=trim(line);
      if (line.empty())continue;
      size_t e=0;
      if (!json_balanced_object(line,0,e) || e!=line.size()) {
        err="NDJSON requires one JSON object per line";
        return false;
      }
      objects.push_back(line);
    }
  } else {
    std::string doc=trim(input);
    if (doc.empty()) {err="JSON input is empty";return false;}
    if (doc[0]=='{') {
      size_t end=0;
      if (!json_balanced_object(doc,0,end) || end!=doc.size()) {
        err="malformed JSON object";return false;
      }
      objects.push_back(doc);
    } else if(doc[0]=='[' && doc.back()==']') {
      size_t pos=1;
      while(pos+1<doc.size()) {
        while (pos+1<doc.size() && (space(doc[pos]) || doc[pos]==',')) ++pos;
        if (pos+1>=doc.size())break;
        size_t end=0;
        if (!json_balanced_object(doc,pos,end)) {
          err="JSON arrays must contain objects";return false;
        }
        objects.push_back(doc.substr(pos,end-pos));
        pos=end;
        while(pos+1<doc.size() && space(doc[pos]))++pos;
        if(pos+1<doc.size() && doc[pos]!=',') {
          err="invalid JSON array separator";return false;
        }
      }
    } else {err="read_json expects JSON object or array";return false;}
  }
  if (objects.size()>MAX_FILE_ROWS) {err="too many JSON rows";return false;}
  for (size_t i=0;i<objects.size();++i) {
    if (!json_object_keys(objects[i],names)) {
      err="cannot infer JSON object keys";
      return false;
    }
  }
  if (names.empty()) names.push_back("value");
  return true;
}
inline bool csv_rows(const std::string &data,
                     std::vector<std::vector<std::string> > &rows,std::string &err) {
  bool quote=false,at_start=true;
  std::string cell;
  std::vector<std::string> row;
  size_t start=(data.size()>=3 &&
       static_cast<unsigned char>(data[0])==0xef &&
       static_cast<unsigned char>(data[1])==0xbb &&
       static_cast<unsigned char>(data[2])==0xbf)?3:0;
  for (size_t i=start;i<data.size();++i) {
    char c=data[i];
    if(quote) {
      if(c=='"') {
        if(i+1<data.size() && data[i+1]=='"') {cell.push_back('"');++i;}
        else quote=false;
      } else cell.push_back(c);
    } else if(c=='"' && at_start) {
      quote=true;
      at_start=false;
    } else if(c==',') {
      row.push_back(cell);cell.clear();at_start=true;
    } else if(c=='\n' || c=='\r') {
      if(c=='\r' && i+1<data.size() && data[i+1]=='\n')++i;
      row.push_back(cell); cell.clear();at_start=true;
      rows.push_back(row);row.clear();
      if(rows.size()>MAX_FILE_ROWS+1){err="too many CSV rows";return false;}
    } else {
      cell.push_back(c);at_start=false;
    }
  }
  if(quote){err="unterminated CSV quote";return false;}
  if(!cell.empty() || !row.empty()){
    row.push_back(cell);rows.push_back(row);
  }
  return true;
}
inline bool is_number(const std::string &value) {
  // RFC 8259 number grammar. In particular, 001, +1 and 1. must be
  // represented as strings, not emitted as invalid JSON numeric tokens.
  size_t i=0;
  if (i<value.size() && value[i]=='-') ++i;
  if (i==value.size()) return false;
  if (value[i]=='0') {
    ++i;
  } else if (value[i]>='1' && value[i]<='9') {
    do {++i;} while(i<value.size() && value[i]>='0' && value[i]<='9');
  } else return false;
  if (i<value.size() && value[i]=='.') {
    ++i;
    size_t start=i;
    while(i<value.size() && value[i]>='0' && value[i]<='9')++i;
    if(i==start)return false;
  }
  if (i<value.size() && (value[i]=='e' || value[i]=='E')) {
    ++i;
    if(i<value.size() && (value[i]=='+' || value[i]=='-'))++i;
    size_t start=i;
    while(i<value.size() && value[i]>='0' && value[i]<='9')++i;
    if(i==start)return false;
  }
  if(i!=value.size())return false;
  errno=0;
  char *end=nullptr;
  const double n=std::strtod(value.c_str(),&end);
  return errno==0 && end==value.c_str()+value.size() && std::isfinite(n);
}

// Deterministic bounded schema inference for file-backed JSON_TABLE sources.
// The type lattice makes inference independent of sampling and row order.
enum FileScalarType {
  FILE_NULL=0, FILE_BOOL=1, FILE_INT=2, FILE_DOUBLE=3,
  FILE_DATE=4, FILE_DATETIME=5, FILE_TEXT=6
};
inline bool date_digits(const std::string &s,size_t at,size_t count) {
  if(at+count>s.size())return false;
  for(size_t i=0;i<count;++i)
    if(s[at+i]<'0' || s[at+i]>'9')return false;
  return true;
}
inline int date_number(const std::string &s,size_t at,size_t count) {
  int n=0;
  for(size_t i=0;i<count;++i)n=n*10+(s[at+i]-'0');
  return n;
}
inline bool valid_date_value(const std::string &s) {
  if(s.size()!=10 || s[4]!='-' || s[7]!='-' ||
     !date_digits(s,0,4) || !date_digits(s,5,2) || !date_digits(s,8,2))
    return false;
  const int y=date_number(s,0,4),m=date_number(s,5,2),d=date_number(s,8,2);
  if(y<1 || m<1 || m>12 || d<1)return false;
  static const int lengths[12]={31,28,31,30,31,30,31,31,30,31,30,31};
  const bool leap=(y%4==0 && (y%100!=0 || y%400==0));
  return d<=lengths[m-1]+((m==2 && leap)?1:0);
}
inline bool valid_datetime_value(const std::string &s) {
  if(s.size()<19 || !valid_date_value(s.substr(0,10)) ||
     (s[10]!='T' && s[10]!=' ') || s[13]!=':' || s[16]!=':' ||
     !date_digits(s,11,2) || !date_digits(s,14,2) ||
     !date_digits(s,17,2))
    return false;
  const int h=date_number(s,11,2);
  const int minute=date_number(s,14,2);
  const int sec=date_number(s,17,2);
  if(h>23 || minute>59 || sec>59)return false;
  if(s.size()==19)return true;
  if(s[19]!='.' || s.size()>26 || s.size()==20)return false;
  return date_digits(s,20,s.size()-20);
}
inline bool integer_value(const std::string &value) {
  if(!is_number(value) || value.find_first_of(".eE")!=std::string::npos)
    return false;
  errno=0;
  char *end=nullptr;
  (void)std::strtoll(value.c_str(),&end,10);
  return errno==0 && end==value.c_str()+value.size();
}
inline FileScalarType detect_scalar(const std::string &value,
                                     bool allow_numeric_and_bool=true) {
  if(value.empty())return FILE_NULL;
  const std::string l=lower(value);
  if(allow_numeric_and_bool && (l=="true" || l=="false"))
    return FILE_BOOL;
  if(valid_date_value(value))return FILE_DATE;
  if(valid_datetime_value(value))return FILE_DATETIME;
  if(allow_numeric_and_bool && integer_value(value))return FILE_INT;
  if(allow_numeric_and_bool && is_number(value)) {
    if(value.find_first_of(".eE")==std::string::npos)return FILE_TEXT;
    return FILE_DOUBLE;
  }
  return FILE_TEXT;
}
inline FileScalarType widen_file_type(FileScalarType old_t,FileScalarType new_t) {
  if(old_t==FILE_NULL)return new_t;
  if(new_t==FILE_NULL || old_t==new_t)return old_t;
  if((old_t==FILE_INT && new_t==FILE_DOUBLE) ||
     (old_t==FILE_DOUBLE && new_t==FILE_INT))return FILE_DOUBLE;
  if((old_t==FILE_DATE && new_t==FILE_DATETIME) ||
     (old_t==FILE_DATETIME && new_t==FILE_DATE))return FILE_DATETIME;
  return FILE_TEXT;
}
inline std::string file_sql_type(FileScalarType t) {
  switch(t) {
    case FILE_BOOL:return "TINYINT";
    case FILE_INT:return "BIGINT";
    case FILE_DOUBLE:return "DOUBLE";
    case FILE_DATE:return "DATE";
    case FILE_DATETIME:return "DATETIME";
    case FILE_NULL:case FILE_TEXT:return "VARCHAR(4096)";
  }
  return "VARCHAR(4096)";
}
inline void uniquify_file_names(std::vector<std::string> &names) {
  std::vector<std::string> seen;
  for(size_t i=0;i<names.size();++i) {
    std::string base=names[i].empty()?
      "column_"+std::to_string(i+1):names[i];
    std::string candidate=base;
    for(unsigned suffix=2;
        std::find(seen.begin(),seen.end(),candidate)!=seen.end();++suffix)
      candidate=base+"__"+std::to_string(suffix);
    seen.push_back(candidate);
    names[i]=candidate;
  }
}
inline bool typed_csv_json(const std::string &data,bool has_header,
                           std::string &doc,std::vector<std::string> &names,
                           std::vector<std::string> &types,std::string &err) {
  std::vector<std::vector<std::string> > rows;
  if(!csv_rows(data,rows,err))return false;
  if(rows.empty()){err="CSV has no records";return false;}
  const size_t count=rows[0].size();
  if(count==0 || count>256){err="CSV column count exceeds limit";return false;}
  if(has_header)names=rows[0];
  else for(size_t i=0;i<count;++i)names.push_back("column"+std::to_string(i));
  uniquify_file_names(names);
  if(rows.size()>MAX_FILE_ROWS+static_cast<size_t>(has_header)){
    err="CSV row count exceeds limit";return false;
  }
  const size_t start=has_header?1:0;
  std::vector<FileScalarType> inferred(count,FILE_NULL);
  for(size_t i=start;i<rows.size();++i) {
    if(rows[i].size()!=count) {
      err="CSV row "+std::to_string(i+1)+" has wrong column count";
      return false;
    }
    for(size_t c=0;c<count;++c) {
      if(rows[i][c].size()>4096) {
        err="CSV field exceeds VARCHAR(4096) limit";return false;
      }
      inferred[c]=widen_file_type(inferred[c],detect_scalar(rows[i][c]));
    }
  }
  for(size_t i=0;i<count;++i)types.push_back(file_sql_type(inferred[i]));
  doc="[";
  for(size_t i=start;i<rows.size();++i) {
    if(i>start)doc+=",";
    doc+="{";
    for(size_t c=0;c<count;++c) {
      if(c)doc+=",";
      const std::string &value=rows[i][c];
      doc+=json_quote(names[c])+":";
      if(value.empty())doc+="null";
      else if(inferred[c]==FILE_BOOL)doc+=lower(value);
      else if(inferred[c]==FILE_INT || inferred[c]==FILE_DOUBLE)
        doc+=value;
      else if(inferred[c]==FILE_DATETIME && valid_datetime_value(value)
              && value[10]=='T') {
        std::string converted=value;
        converted[10]=' ';
        doc+=json_quote(converted);
      } else doc+=json_quote(value);
    }
    doc+="}";
    if(doc.size()>MAX_RESULT_BYTES){err="CSV materialization exceeds 2 MiB";return false;}
  }
  doc+="]";
  return true;
}
inline bool append_utf8(uint32_t cp,std::string &out) {
  if(cp==0 || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff))return false;
  if(cp<0x80)out.push_back(static_cast<char>(cp));
  else if(cp<0x800){
    out.push_back(static_cast<char>(0xc0|(cp>>6)));
    out.push_back(static_cast<char>(0x80|(cp&0x3f)));
  } else if(cp<0x10000){
    out.push_back(static_cast<char>(0xe0|(cp>>12)));
    out.push_back(static_cast<char>(0x80|((cp>>6)&0x3f)));
    out.push_back(static_cast<char>(0x80|(cp&0x3f)));
  } else {
    out.push_back(static_cast<char>(0xf0|(cp>>18)));
    out.push_back(static_cast<char>(0x80|((cp>>12)&0x3f)));
    out.push_back(static_cast<char>(0x80|((cp>>6)&0x3f)));
    out.push_back(static_cast<char>(0x80|(cp&0x3f)));
  }
  return true;
}
inline int hex_digit(char c) {
  if(c>='0' && c<='9')return c-'0';
  if(c>='a' && c<='f')return c-'a'+10;
  if(c>='A' && c<='F')return c-'A'+10;
  return -1;
}
inline bool json_escape_codepoint(const std::string &s,size_t &at,
                                  uint32_t &cp) {
  if(at+4>s.size())return false;
  cp=0;
  for(size_t i=0;i<4;++i) {
    const int h=hex_digit(s[at+i]);
    if(h<0)return false;
    cp=(cp<<4)|static_cast<uint32_t>(h);
  }
  at+=4;
  return true;
}
inline bool strict_json_string(const std::string &s,size_t &p,
                               std::string &decoded) {
  if(p>=s.size() || s[p]!='"')return false;
  ++p;
  while(p<s.size()) {
    const unsigned char c=static_cast<unsigned char>(s[p++]);
    if(c=='"')return true;
    if(c<0x20)return false;
    if(c!='\\') {
      decoded.push_back(static_cast<char>(c));
      continue;
    }
    if(p>=s.size())return false;
    const char e=s[p++];
    if(e=='u') {
      uint32_t cp=0;
      if(!json_escape_codepoint(s,p,cp))return false;
      if(cp>=0xd800 && cp<=0xdbff) {
        if(p+2>s.size() || s[p++]!='\\' || s[p++]!='u')return false;
        uint32_t low=0;
        if(!json_escape_codepoint(s,p,low) || low<0xdc00 || low>0xdfff)
          return false;
        cp=0x10000+((cp-0xd800)<<10)+(low-0xdc00);
      }
      if(!append_utf8(cp,decoded))return false;
    } else {
      switch(e) {
        case '"':decoded.push_back('"');break;
        case '\\':decoded.push_back('\\');break;
        case '/':decoded.push_back('/');break;
        case 'b':decoded.push_back('\b');break;
        case 'f':decoded.push_back('\f');break;
        case 'n':decoded.push_back('\n');break;
        case 'r':decoded.push_back('\r');break;
        case 't':decoded.push_back('\t');break;
        default:return false;
      }
    }
  }
  return false;
}
inline bool typed_json_schema(const std::vector<std::string> &objects,
                              std::vector<std::string> &names,
                              std::vector<std::string> &types,
                              std::string &err) {
  std::vector<FileScalarType> inferred;
  for(size_t i=0;i<objects.size();++i) {
    const std::string &row=objects[i];
    size_t p=0;
    while(p<row.size() && space(row[p]))++p;
    if(p>=row.size() || row[p]!='{'){
      err="JSON object expected at row "+std::to_string(i+1);return false;
    }
    ++p;
    std::vector<std::string> local;
    bool first=true;
    for(;;) {
      while(p<row.size() && space(row[p]))++p;
      if(p>=row.size()){err="unterminated JSON object";return false;}
      if(row[p]=='}'){++p;break;}
      if(!first) {
        if(row[p]!=','){err="missing JSON comma";return false;}
        ++p;
        while(p<row.size() && space(row[p]))++p;
        if(p>=row.size() || row[p]=='}'){
          err="JSON trailing comma";return false;
        }
      }
      first=false;
      std::string name;
      if(!strict_json_string(row,p,name) || name.empty() || name.size()>256){
        err="invalid JSON property name at row "+std::to_string(i+1);
        return false;
      }
      if(std::find(local.begin(),local.end(),name)!=local.end()){
        err="duplicate JSON object key "+name;return false;
      }
      local.push_back(name);
      while(p<row.size() && space(row[p]))++p;
      if(p>=row.size() || row[p]!=':'){err="missing JSON colon";return false;}
      ++p;
      while(p<row.size() && space(row[p]))++p;
      if(p>=row.size()){err="JSON value missing";return false;}
      FileScalarType t=FILE_NULL;
      if(row[p]=='"') {
        std::string value;
        if(!strict_json_string(row,p,value)){err="invalid JSON string";return false;}
        if(value.size()>4096){err="JSON text exceeds 4096";return false;}
        // JSON quoted numerics and booleans remain text. ISO dates are typed.
        t=value.empty()?FILE_TEXT:detect_scalar(value,false);
      } else if(row[p]=='[' || row[p]=='{') {
        err="nested JSON arrays/objects are not supported at row "+
            std::to_string(i+1);return false;
      } else {
        const size_t start=p;
        while(p<row.size() && !space(row[p]) &&
              row[p]!=',' && row[p]!='}')++p;
        const std::string token=row.substr(start,p-start);
        if(token=="null")t=FILE_NULL;
        else if(token=="true" || token=="false")t=FILE_BOOL;
        else if(integer_value(token))t=FILE_INT;
        else if(is_number(token))t=FILE_DOUBLE;
        else {
          err="invalid JSON scalar at row "+std::to_string(i+1);
          return false;
        }
      }
      size_t at=0;
      while(at<names.size() && names[at]!=name)++at;
      if(at==names.size()) {
        if(names.size()>=256) {
          err="JSON schema has too many columns";return false;
        }
        names.push_back(name);
        inferred.push_back(FILE_NULL);
      }
      inferred[at]=widen_file_type(inferred[at],t);
    }
    while(p<row.size() && space(row[p]))++p;
    if(p!=row.size()){err="unexpected tokens after JSON object";return false;}
  }
  if(names.empty()){err="cannot infer empty JSON schema";return false;}
  for(size_t i=0;i<names.size();++i)
    types.push_back(file_sql_type(inferred[i]));
  return true;
}

inline bool csv_to_json(const std::string &data,std::string &doc,
                        std::vector<std::string> &names,
                        std::vector<std::string> &types,std::string &err) {
  std::vector<std::vector<std::string> > rows;
  if(!csv_rows(data,rows,err))return false;
  if(rows.empty()) {err="CSV header is missing";return false;}
  names=rows.front();
  if(names.empty()) {err="CSV column list is empty";return false;}
  for(size_t i=0;i<names.size();++i) {
    if(names[i].empty()) names[i]="column_"+std::to_string(i+1);
    // Deduplicate names deterministically while preserving their SQL order.
    const std::string base=names[i];
    unsigned n=2;
    for(size_t j=0;j<i;++j) {
      if(names[j]==names[i]) {
        names[i]=base+"_"+std::to_string(n++);
        j=static_cast<size_t>(-1);
      }
    }
  }
  types.assign(names.size(),"BIGINT");
  for(size_t r=1;r<rows.size();++r) {
    if(rows[r].size()!=names.size()) {
      err="CSV row has different column count";return false;
    }
    for(size_t c=0;c<names.size();++c) {
      const std::string &v=rows[r][c];
      if(v.empty()) continue;
      if(!is_number(v)) types[c]="VARCHAR(4096)";
      else if(types[c]=="BIGINT" && v.find_first_of(".eE")!=std::string::npos)
        types[c]="DOUBLE";
    }
  }
  doc="[";
  for(size_t r=1;r<rows.size();++r) {
    if(r>1)doc+=",";
    doc+="{";
    for(size_t c=0;c<names.size();++c) {
      if(c)doc+=",";
      doc+=json_quote(names[c])+":";
      const std::string &value=rows[r][c];
      if(value.empty())doc+="null";
      else if(types[c]!="VARCHAR(4096)" && is_number(value))doc+=value;
      else doc+=json_quote(value);
    }
    doc+="}";
    if(doc.size()>MAX_RESULT_BYTES){err="JSON result exceeds 2 MiB";return false;}
  }
  doc+="]";
  return true;
}
inline bool glob_to_json(const std::string &input,std::string &doc,std::string &err) {
  std::string root;
  if(!permitted_root(root,err))return false;
  if(input.find("..")!=std::string::npos || input.find("://")!=std::string::npos) {
    err="glob patterns cannot traverse outside file_sql_root";return false;
  }
  const std::string pattern=input.empty()?"":(input[0]=='/'?input:root+"/"+input);
  if(pattern.empty() || !under_root(root,pattern) ||
     !secured_file_path(pattern)) {
    err="glob pattern is outside file_sql_root";return false;
  }
  glob_t matches={};
  int rc=::glob(pattern.c_str(),0,nullptr,&matches);
  if(rc!=0 && rc!=GLOB_NOMATCH) {
    ::globfree(&matches);err="glob() failed";return false;
  }
  if(matches.gl_pathc>MAX_FILE_ROWS) {
    ::globfree(&matches);err="too many glob matches";return false;
  }
  doc="[";
  for(size_t i=0;i<matches.gl_pathc;++i) {
    std::string resolved;
    std::string path_error;
    if(!resolve_file(matches.gl_pathv[i],resolved,path_error)) {
      ::globfree(&matches);err="glob result violates file_sql_root";return false;
    }
    if(i)doc+=",";
    doc+="{\"file\":"+json_quote(resolved)+"}";
    if(doc.size()>MAX_RESULT_BYTES){
      ::globfree(&matches);err="glob result exceeds 2 MiB";return false;
    }
  }
  ::globfree(&matches);
  doc+="]";
  return true;
}

// Native bounded Parquet data-page decoder. This is an experimental
// subset of Apache Parquet (TCompactProtocol metadata, flat primitives,
// PLAIN / RLE_DICTIONARY, page V1/V2, UNCOMPRESSED / SNAPPY). Unsupported
// codecs/encodings abort with an error, never silently invent results.
// Data is currently materialized into JSON_TABLE so this does NOT yet
// satisfy the competition's streaming-scan requirement.
namespace parquet_native {

struct Reader {
  const unsigned char *data;
  size_t len;
  size_t pos;
  bool ok;
  Reader(const char *src,size_t length)
      :data(reinterpret_cast<const unsigned char *>(src)),
       len(length),pos(0),ok(true){}
  bool can(size_t count) const {
    return count<=len && pos<=len-count;
  }
  unsigned char byte() {
    if(!can(1)){ok=false;return 0;}
    return data[pos++];
  }
  uint64_t uvar() {
    uint64_t v=0;
    for(int shift=0;shift<70;shift+=7) {
      if(!can(1)){ok=false;return 0;}
      const unsigned char c=byte();
      if(shift==63 && (c&0x7e)!=0){ok=false;return 0;}
      if(shift>63 && (c&0x7f)!=0){ok=false;return 0;}
      v|=uint64_t(c&0x7f)<<shift;
      if(!(c&0x80))return v;
    }
    ok=false;return 0;
  }
  int64_t zig() {
    uint64_t n=uvar();
    return static_cast<int64_t>((n>>1) ^ (~(n&1)+1));
  }
  uint64_t le(size_t width) {
    if(width>8 || !can(width)){ok=false;return 0;}
    uint64_t result=0;
    for(size_t i=0;i<width;++i) result|=uint64_t(byte())<<(i*8);
    return result;
  }
  std::string binary() {
    const uint64_t n=uvar();
    if(n>MAX_FILE_BYTES || !can(static_cast<size_t>(n))){
      ok=false;return std::string();
    }
    const std::string result(reinterpret_cast<const char*>(data+pos),
                             static_cast<size_t>(n));
    pos+=static_cast<size_t>(n);
    return result;
  }
  bool field(int &last,int &id,int &type) {
    if(!ok || !can(1)) {ok=false;return false;}
    const int c=byte();
    type=c&0x0f;
    if(type==0)return false;
    const int delta=c>>4;
    const int64_t v=delta?last+delta:zig();
    if(v<1 || v>32767){ok=false;return false;}
    id=static_cast<int>(v);last=id;
    return true;
  }
  bool list(int &type,size_t &count) {
    if(!can(1)){ok=false;return false;}
    const int v=byte();
    type=v&15;
    uint64_t n=(v>>4)==15?uvar():static_cast<uint64_t>(v>>4);
    if(n>MAX_FILE_ROWS*128){ok=false;return false;}
    count=static_cast<size_t>(n);
    return ok;
  }
  bool skip(int type,int depth=0) {
    if(depth>32){ok=false;return false;}
    switch(type) {
      case 0: case 1: case 2: break;
      case 3: if(!can(1))ok=false;else ++pos;break;
      case 4:case 5:case 6: uvar();break;
      case 7: if(!can(8))ok=false;else pos+=8;break;
      case 8: binary();break;
      case 9:case 10: {
        int subtype;size_t count;
        if(!list(subtype,count))return false;
        for(size_t i=0;i<count && ok;++i)skip(subtype,depth+1);
        break;
      }
      case 11: {
        const uint64_t n=uvar();
        if(n>MAX_FILE_ROWS*128){ok=false;return false;}
        if(n>0){
          const int types=byte();
          for(size_t i=0;i<n && ok;++i){
            skip(types>>4,depth+1);
            skip(types&0xf,depth+1);
          }
        }
        break;
      }
      case 12: {
        int last=0,id=0,subtype=0;
        while(ok && field(last,id,subtype))skip(subtype,depth+1);
        break;
      }
      default: ok=false;
    }
    return ok;
  }
};

struct Schema {
  std::string name;
  int type;
  int repetition;
  int children;
  int converted;
  Schema():type(-1),repetition(0),children(0),converted(-1){}
};
struct Column {
  int type;
  int codec;
  uint64_t num_values;
  uint64_t data_offset;
  uint64_t dictionary_offset;
  bool has_dictionary;
  Column():type(-1),codec(-1),num_values(0),data_offset(0),
           dictionary_offset(0),has_dictionary(false){}
};
struct RowGroup {
  uint64_t rows;
  std::vector<Column> columns;
  RowGroup():rows(0){}
};
struct FileMeta {
  std::vector<Schema> schema;
  std::vector<RowGroup> groups;
  uint64_t num_rows;
  FileMeta():num_rows(0){}
};
struct Page {
  int type;
  uint64_t compressed_size,raw_size;
  int encoding,level_encoding,rep_encoding;
  uint64_t values;
  uint64_t def_length,rep_length;
  bool values_compressed;
  Page():type(-1),compressed_size(0),raw_size(0),
         encoding(-1),level_encoding(3),rep_encoding(3),
         values(0),def_length(0),rep_length(0),values_compressed(true){}
};

inline bool parse_schema(Reader &r,Schema &s) {
  int last=0,id=0,type=0;
  while(r.ok && r.field(last,id,type)) {
    if(id==1 && type==5)s.type=static_cast<int>(r.zig());
    else if(id==3 && type==5)s.repetition=static_cast<int>(r.zig());
    else if(id==4 && type==8)s.name=r.binary();
    else if(id==5 && type==5)s.children=static_cast<int>(r.zig());
    else if(id==6 && type==5)s.converted=static_cast<int>(r.zig());
    else r.skip(type);
  }
  return r.ok && !s.name.empty();
}
inline bool parse_colmeta(Reader &r,Column &c) {
  int last=0,id=0,type=0;
  while(r.ok && r.field(last,id,type)) {
    if(id==1 && type==5)c.type=static_cast<int>(r.zig());
    else if(id==4 && type==5)c.codec=static_cast<int>(r.zig());
    else if(id==5 && type==6)c.num_values=static_cast<uint64_t>(r.zig());
    else if(id==9 && type==6)c.data_offset=static_cast<uint64_t>(r.zig());
    else if(id==11 && type==6) {
      c.dictionary_offset=static_cast<uint64_t>(r.zig());
      c.has_dictionary=true;
    } else r.skip(type);
  }
  return r.ok && c.type>=0 && c.codec>=0;
}
inline bool parse_column(Reader &r,Column &column) {
  int last=0,id=0,type=0;
  bool found=false;
  while(r.ok && r.field(last,id,type)) {
    if(id==1 && type==8) {
      // External column chunks are not supported: paths must stay inside
      // the single resolved file, not be followed as auxiliary inputs.
      const std::string other=r.binary();
      if(!other.empty())return false;
    } else if(id==3 && type==12) {
      found=parse_colmeta(r,column);
      if(!found)return false;
    }else r.skip(type);
  }
  return r.ok && found;
}
inline bool parse_rowgroup(Reader &r,RowGroup &group) {
  int last=0,id=0,type=0;
  while(r.ok && r.field(last,id,type)) {
    if(id==1 && type==9) {
      int child=0;size_t count=0;
      if(!r.list(child,count) || child!=12 || count>256)return false;
      for(size_t i=0;i<count;++i) {
        Column column;
        if(!parse_column(r,column))return false;
        group.columns.push_back(column);
      }
    } else if(id==3 && type==6)group.rows=static_cast<uint64_t>(r.zig());
    else r.skip(type);
  }
  return r.ok && group.columns.size()>0 && group.rows<=MAX_FILE_ROWS;
}
inline bool parse_filemeta(Reader &r,FileMeta &meta) {
  int last=0,id=0,type=0;
  while(r.ok && r.field(last,id,type)) {
    if(id==2 && type==9) {
      int subtype=0;size_t count=0;
      if(!r.list(subtype,count) || subtype!=12 || count>257)return false;
      for(size_t i=0;i<count;++i) {
        Schema s;
        if(!parse_schema(r,s))return false;
        meta.schema.push_back(s);
      }
    } else if(id==3 && type==6)meta.num_rows=static_cast<uint64_t>(r.zig());
    else if(id==4 && type==9) {
      int subtype=0;size_t count=0;
      if(!r.list(subtype,count) || subtype!=12 || count>MAX_FILE_ROWS)return false;
      for(size_t i=0;i<count;++i) {
        RowGroup g;
        if(!parse_rowgroup(r,g))return false;
        meta.groups.push_back(g);
      }
    } else r.skip(type);
  }
  return r.ok && meta.schema.size()>=2 &&
         meta.num_rows<=MAX_FILE_ROWS && !meta.groups.empty();
}
inline bool parse_page(Reader &r,Page &page) {
  int last=0,id=0,type=0;
  while(r.ok && r.field(last,id,type)) {
    if(id==1 && type==5)page.type=static_cast<int>(r.zig());
    else if(id==2 && type==5)page.raw_size=static_cast<uint64_t>(r.zig());
    else if(id==3 && type==5)page.compressed_size=static_cast<uint64_t>(r.zig());
    else if(id==5 && type==12) {
      int inner_last=0,inner_id=0,inner_type=0;
      while(r.ok && r.field(inner_last,inner_id,inner_type)) {
        if(inner_id==1 && inner_type==5)page.values=static_cast<uint64_t>(r.zig());
        else if(inner_id==2 && inner_type==5)page.encoding=static_cast<int>(r.zig());
        else if(inner_id==3 && inner_type==5)page.level_encoding=static_cast<int>(r.zig());
        else if(inner_id==4 && inner_type==5)page.rep_encoding=static_cast<int>(r.zig());
        else r.skip(inner_type);
      }
    } else if(id==7 && type==12) {
      int inner_last=0,inner_id=0,inner_type=0;
      while(r.ok && r.field(inner_last,inner_id,inner_type)) {
        if(inner_id==1 && inner_type==5)page.values=static_cast<uint64_t>(r.zig());
        else if(inner_id==2 && inner_type==5)page.encoding=static_cast<int>(r.zig());
        else r.skip(inner_type);
      }
    } else if(id==8 && type==12) {
      int inner_last=0,inner_id=0,inner_type=0;
      while(r.ok && r.field(inner_last,inner_id,inner_type)) {
        if(inner_id==1 && inner_type==5)page.values=static_cast<uint64_t>(r.zig());
        else if(inner_id==4 && inner_type==5)page.encoding=static_cast<int>(r.zig());
        else if(inner_id==5 && inner_type==5)page.def_length=static_cast<uint64_t>(r.zig());
        else if(inner_id==6 && inner_type==5)page.rep_length=static_cast<uint64_t>(r.zig());
        else if(inner_id==7 && (inner_type==1 || inner_type==2))
          page.values_compressed=inner_type==1;
        else r.skip(inner_type);
      }
    } else r.skip(type);
  }
  return r.ok && page.type>=0 &&
         page.compressed_size<=MAX_FILE_BYTES &&
         page.raw_size<=MAX_FILE_BYTES && page.values<=MAX_FILE_ROWS;
}

inline bool snappy_decode(const std::string &src,
                          size_t limit,std::string &out) {
  Reader r(src.data(),src.size());
  const uint64_t declared=r.uvar();
  if(!r.ok || declared>limit)return false;
  out.clear();
  out.reserve(static_cast<size_t>(declared));
  while(r.ok && r.pos<r.len && out.size()<declared) {
    const unsigned char tag=r.byte();
    const unsigned kind=tag&3;
    if(kind==0) {
      size_t n=(tag>>2)+1;
      if((tag>>2)>=60) {
        const size_t width=(tag>>2)-59;
        n=static_cast<size_t>(r.le(width))+1;
      }
      if(!r.ok || !r.can(n) || n>declared-out.size())return false;
      out.append(reinterpret_cast<const char*>(r.data+r.pos),n);
      r.pos+=n;
    } else {
      size_t n=0;
      size_t offset=0;
      if(kind==1) {
        n=4+((tag>>2)&7);
        offset=(static_cast<size_t>(tag&0xe0)<<3)|r.byte();
      } else if(kind==2) {
        n=1+(tag>>2);
        offset=static_cast<size_t>(r.le(2));
      } else {
        n=1+(tag>>2);
        offset=static_cast<size_t>(r.le(4));
      }
      if(!r.ok || offset==0 || offset>out.size() ||
         n>declared-out.size())return false;
      for(size_t i=0;i<n;++i)
        out.push_back(out[out.size()-offset]);
    }
  }
  return r.ok && r.pos==r.len && out.size()==declared;
}
inline bool decompressed(const std::string &src,int codec,size_t raw,
                          std::string &out) {
  if(raw>MAX_FILE_BYTES || src.size()>MAX_FILE_BYTES)return false;
  if(codec==0)out=src;
  else if(codec==1) {
    if(!snappy_decode(src,MAX_FILE_BYTES,out))return false;
  }else return false;
  return out.size()==raw;
}
inline bool hybrid_decode(const std::string &src,unsigned width,
                          size_t count,std::vector<uint32_t> &out) {
  if(width>32 || count>MAX_FILE_ROWS)return false;
  Reader r(src.data(),src.size());
  out.clear();
  while(r.ok && r.pos<r.len && out.size()<count) {
    const uint64_t header=r.uvar();
    if(!r.ok || header==0)return false;
    if((header&1)==0) {
      const uint64_t n=header>>1;
      if(n==0 || n>MAX_FILE_ROWS || n>count-out.size())return false;
      const uint32_t value=static_cast<uint32_t>(r.le((width+7)/8));
      if(!r.ok || (width<32 && value >= (uint64_t(1)<<width)))return false;
      out.insert(out.end(),static_cast<size_t>(n),value);
    }else {
      const uint64_t groups=header>>1;
      if(groups==0 || groups>MAX_FILE_ROWS)return false;
      const size_t bytes=static_cast<size_t>(groups)*width;
      if(!r.can(bytes))return false;
      const size_t n=std::min(static_cast<size_t>(groups)*8,count-out.size());
      for(size_t k=0;k<n;++k) {
        const size_t bit=k*width;
        uint32_t value=0;
        for(unsigned j=0;j<width;++j) {
          if((r.data[r.pos+(bit+j)/8]>>((bit+j)%8))&1)
            value|=(uint32_t(1)<<j);
        }
        out.push_back(value);
      }
      r.pos+=bytes;
    }
  }
  return r.ok && out.size()==count;
}
inline std::string iso_date(int64_t days) {
  days+=719468;
  const int64_t era=days>=0?days/146097:(days-146096)/146097;
  const uint64_t doe=static_cast<uint64_t>(days-era*146097);
  const uint64_t yoe=(doe-doe/1460+doe/36524-doe/146096)/365;
  int64_t year=static_cast<int64_t>(yoe)+era*400;
  const uint64_t doy=doe-(365*yoe+yoe/4-yoe/100);
  const uint64_t mp=(5*doy+2)/153;
  const uint64_t day=doy-(153*mp+2)/5+1;
  const uint64_t month=mp+(mp<10?3:-9);
  year+=(month<=2);
  char buffer[50];
  std::snprintf(buffer,sizeof(buffer),"%04lld-%02llu-%02llu",
                static_cast<long long>(year),
                static_cast<unsigned long long>(month),
                static_cast<unsigned long long>(day));
  return buffer;
}
inline std::string iso_datetime(int64_t millis) {
  int64_t days=millis/86400000LL;
  int64_t rem=millis%86400000LL;
  if(rem<0){rem+=86400000LL;--days;}
  char buffer[64];
  std::snprintf(buffer,sizeof(buffer),"%02lld:%02lld:%02lld",
    static_cast<long long>(rem/3600000),
    static_cast<long long>((rem/60000)%60),
    static_cast<long long>((rem/1000)%60));
  return iso_date(days)+" "+buffer;
}
inline std::string parquet_sql_type(const Schema &s) {
  if(s.converted==6)return "DATE";
  if(s.converted==9)return "DATETIME";
  if(s.type==0)return "TINYINT";
  if(s.type==1 || s.type==2)return "BIGINT";
  if(s.type==4 || s.type==5)return "DOUBLE";
  return "VARCHAR(4096)";
}
inline bool plain_read(Reader &r,int type,int converted,
                       uint64_t &bool_position,std::string &value) {
  if(type==0) {
    // Plain BOOLEAN uses one bit/value, 8 values per byte.
    // Bool state is tracked by bit index; byte must be consumed only for
    // first bit of each octet.
    if((bool_position%8)==0) {
      if(!r.can(1))return false;
    }
    if(r.pos==0)return false;
    // Use the next data byte without advancing until all 8 bits consumed.
    const unsigned char byte=r.data[r.pos];
    value=((byte>>(bool_position%8))&1)?"1":"0";
    ++bool_position;
    if((bool_position%8)==0)++r.pos;
    return true;
  } else if(type==1) {
    value=std::to_string(static_cast<int32_t>(r.le(4)));
    if(converted==6)value=json_quote(iso_date(static_cast<int32_t>(
                              std::strtol(value.c_str(),nullptr,10))));
    return r.ok;
  } else if(type==2) {
    const int64_t n=static_cast<int64_t>(r.le(8));
    value=converted==9?json_quote(iso_datetime(n)):std::to_string(n);
    return r.ok;
  } else if(type==4 || type==5) {
    const uint64_t bits=r.le(type==4?4:8);
    if(!r.ok)return false;
    double f=0;
    if(type==4) {
      const uint32_t b=static_cast<uint32_t>(bits);
      float f32=0;std::memcpy(&f32,&b,sizeof(f32));
      f=f32;
    } else std::memcpy(&f,&bits,sizeof(f));
    if(!std::isfinite(f))value="null";
    else {
      char buffer[64];
      std::snprintf(buffer,sizeof(buffer),type==4?"%.9g":"%.17g",f);
      value=buffer;
    }
    return true;
  } else if(type==6) {
    const uint64_t len=r.le(4);
    if(!r.ok || len>MAX_FILE_BYTES || !r.can(static_cast<size_t>(len)))
      return false;
    std::string str(reinterpret_cast<const char*>(r.data+r.pos),
                    static_cast<size_t>(len));
    r.pos+=static_cast<size_t>(len);
    value=json_quote(str);
    return true;
  }
  return false;
}
inline bool decode_values(const std::string &data,int physical,int converted,
                          size_t count,std::vector<std::string> &out) {
  Reader r(data.data(),data.size());
  uint64_t bool_index=0;
  for(size_t i=0;i<count;++i) {
    std::string value;
    if(physical==0) {
      // Boolean bit-packed values need per-byte offset handling.
      if((i%8)==0 && !r.can(1))return false;
      value=((r.data[r.pos]>>(i%8))&1)?"1":"0";
      if(i%8==7 || i+1==count)++r.pos;
    } else if(!plain_read(r,physical,converted,bool_index,value))
      return false;
    out.push_back(value);
  }
  return r.ok;
}
inline bool page_values(const std::string &bytes,const Page &page,
                        const Column &column,const Schema &schema,
                        const std::vector<std::string> &dictionary,
                        std::vector<std::string> &values,
                        std::string &err) {
  std::string raw;
  const bool v2=page.type==3;
  if(v2 && page.def_length+page.rep_length<=bytes.size()) {
    const size_t levels_length=static_cast<size_t>(page.def_length+page.rep_length);
    const std::string initial=bytes.substr(0,levels_length);
    const std::string encoded=bytes.substr(levels_length);
    std::string decoded;
    if(page.values_compressed) {
      if(!decompressed(encoded,column.codec,
                       static_cast<size_t>(page.raw_size-levels_length),
                       decoded)) {
        err="Parquet v2 page decompression failed";return false;
      }
    } else decoded=encoded;
    raw=initial+decoded;
  } else if(!decompressed(bytes,column.codec,
                          static_cast<size_t>(page.raw_size),raw)) {
    err="Parquet page decompression failed";return false;
  }
  if(raw.size()!=page.raw_size){err="Parquet page length mismatch";return false;}
  size_t offset=0;
  std::vector<uint32_t> levels;
  if(schema.repetition==1) {
    std::string encoded;
    if(v2) {
      if(page.rep_length!=0 || page.def_length>raw.size()) {
        err="unsupported Parquet nested/repeated levels";return false;
      }
      encoded=raw.substr(0,static_cast<size_t>(page.def_length));
      offset=static_cast<size_t>(page.def_length);
    } else {
      Reader r(raw.data(),raw.size());
      const uint64_t length=r.le(4);
      if(!r.ok || length>raw.size()-r.pos) {
        err="malformed Parquet definition levels";return false;
      }
      encoded=raw.substr(r.pos,static_cast<size_t>(length));
      offset=r.pos+static_cast<size_t>(length);
    }
    if(!hybrid_decode(encoded,1,static_cast<size_t>(page.values),levels)){
      err="invalid Parquet definition levels";return false;
    }
  } else if(v2)offset=static_cast<size_t>(page.def_length+page.rep_length);
  size_t defined=static_cast<size_t>(page.values);
  if(schema.repetition==1) {
    defined=0;
    for(size_t i=0;i<levels.size();++i)defined+=levels[i]==1;
  }
  const std::string body=raw.substr(offset);
  std::vector<std::string> decoded;
  if(page.encoding==0) {
    if(!decode_values(body,column.type,schema.converted,defined,decoded)){
      err="unsupported or malformed Parquet PLAIN values";return false;
    }
  } else if(page.encoding==2 || page.encoding==8) {
    if(dictionary.empty() && defined>0) {
      err="Parquet dictionary is missing";return false;
    }
    if(body.empty() && defined>0)return false;
    std::vector<uint32_t> ids;
    if(defined>0 && !hybrid_decode(body.substr(1),
           static_cast<unsigned char>(body[0]),defined,ids)){
      err="Parquet dictionary indexes invalid";return false;
    }
    for(size_t i=0;i<ids.size();++i) {
      if(ids[i]>=dictionary.size()){err="Parquet bad dictionary index";return false;}
      decoded.push_back(dictionary[ids[i]]);
    }
  } else {
    err="unsupported Parquet page encoding";return false;
  }
  size_t j=0;
  for(size_t i=0;i<page.values;++i){
    if(schema.repetition==1 && levels[i]==0)values.push_back("null");
    else {
      if(j>=decoded.size()){err="Parquet value count mismatch";return false;}
      values.push_back(decoded[j++]);
    }
  }
  return true;
}
inline bool decode_chunk(const std::string &file, const Column &column,
                         const Schema &schema,size_t expected_rows,
                         std::vector<std::string> &values,std::string &err) {
  if(column.num_values!=expected_rows) {
    err="Parquet repeated/nested columns are not supported";return false;
  }
  const uint64_t offset=column.has_dictionary?
      std::min(column.dictionary_offset,column.data_offset):column.data_offset;
  if(offset>=file.size() || offset<4) {
    err="invalid Parquet page offset";return false;
  }
  size_t at=static_cast<size_t>(offset),pages=0;
  std::vector<std::string> dictionary;
  while(values.size()<expected_rows && at<file.size()-8 && pages++<MAX_FILE_ROWS){
    Reader r(file.data()+at,file.size()-at);
    Page page;
    if(!parse_page(r,page)) {
      err="invalid Parquet page header";return false;
    }
    const size_t payload=at+r.pos;
    if(page.compressed_size>file.size()-payload) {
      err="invalid Parquet compressed page size";return false;
    }
    const std::string bytes=file.substr(payload,
                                        static_cast<size_t>(page.compressed_size));
    at=payload+static_cast<size_t>(page.compressed_size);
    if(page.type==2) {
      std::string raw;
      if(page.encoding!=0 ||
         !decompressed(bytes,column.codec,static_cast<size_t>(page.raw_size),raw) ||
         !decode_values(raw,column.type,schema.converted,
                        static_cast<size_t>(page.values),dictionary)){
        err="invalid Parquet dictionary page";return false;
      }
    } else if(page.type==0 || page.type==3) {
      if(page.values>expected_rows-values.size()) {
        err="Parquet page contains excess rows";return false;
      }
      if(!page_values(bytes,page,column,schema,dictionary,values,err))
        return false;
    } // skip index and other non-data pages
  }
  if(values.size()!=expected_rows) {
    err="Parquet column data is incomplete";return false;
  }
  return true;
}
inline bool read_parquet(const std::string &file,std::string &doc,
                         std::vector<std::string> &names,
                         std::vector<std::string> &types,
                         std::string &err) {
  if(file.size()<12 || file.compare(0,4,"PAR1")!=0 ||
     file.compare(file.size()-4,4,"PAR1")!=0) {
    err="invalid Parquet magic";return false;
  }
  Reader trailer(file.data()+file.size()-8,4);
  const uint64_t footer=trailer.le(4);
  if(!trailer.ok || footer>file.size()-12) {
    err="invalid Parquet footer size";return false;
  }
  const size_t start=file.size()-8-static_cast<size_t>(footer);
  Reader r(file.data()+start,static_cast<size_t>(footer));
  FileMeta meta;
  if(!parse_filemeta(r,meta)){
    err="invalid/unsupported Parquet footer metadata";return false;
  }
  const size_t column_count=meta.schema.size()-1;
  if(meta.schema[0].children!=static_cast<int>(column_count) ||
     column_count>256){
    err="only flat Parquet schema is supported";return false;
  }
  for(size_t i=1;i<meta.schema.size();++i){
    const Schema &s=meta.schema[i];
    if(s.children!=0 || s.repetition>1 || s.type<0 ||
       s.type==3 || s.type==7 || s.type>6){
      err="unsupported nested or specialized Parquet column";return false;
    }
    names.push_back(s.name);
    types.push_back(parquet_sql_type(s));
  }
  std::vector<std::vector<std::string> > cols(column_count);
  size_t total_rows=0;
  for(size_t g=0;g<meta.groups.size();++g) {
    const RowGroup &group=meta.groups[g];
    if(group.columns.size()!=column_count ||
       group.rows>MAX_FILE_ROWS-total_rows) {
      err="Parquet row group columns/row count mismatch";return false;
    }
    total_rows+=static_cast<size_t>(group.rows);
    for(size_t j=0;j<column_count;++j) {
      if(group.columns[j].type!=meta.schema[j+1].type) {
        err="Parquet physical type metadata mismatch";return false;
      }
      std::vector<std::string> values;
      if(!decode_chunk(file,group.columns[j],meta.schema[j+1],
                       static_cast<size_t>(group.rows),values,err))
        return false;
      cols[j].insert(cols[j].end(),values.begin(),values.end());
    }
  }
  if(total_rows!=meta.num_rows) {
    err="Parquet file/rowgroup row count mismatch";return false;
  }
  doc="[";
  for(size_t row=0;row<total_rows;++row) {
    if(row)doc+=",";
    doc+="{";
    for(size_t col=0;col<column_count;++col) {
      if(col)doc+=",";
      doc+=json_quote(names[col])+":"+cols[col][row];
    }
    doc+="}";
    if(doc.size()>MAX_RESULT_BYTES) {
      err="Parquet result exceeds 2 MiB; streaming scanner required";
      return false;
    }
  }
  doc+="]";
  return true;
}

} // namespace parquet_native

struct FileReadOptions {
  bool csv_header;
  bool json_newlines;
  FileReadOptions():csv_header(true),json_newlines(false){}
};
inline bool file_options(const std::string &sql,size_t &at,
                         const std::string &function,
                         FileReadOptions &opts,std::string &err) {
  std::vector<std::string> seen;
  while(at<sql.size()) {
    while(at<sql.size() && space(sql[at]))++at;
    if(at>=sql.size())break;
    if(sql[at]==')')return true;
    if(sql[at]!=',') {
      err="expected comma or closing parenthesis in file table arguments";
      return false;
    }
    ++at;
    while(at<sql.size() && space(sql[at]))++at;
    const size_t key_begin=at;
    if(at>=sql.size() || !id_start(sql[at])){
      err="file table function options must be named";return false;
    }
    while(at<sql.size() && id_char(sql[at]))++at;
    const std::string key=lower(sql.substr(key_begin,at-key_begin));
    while(at<sql.size() && space(sql[at]))++at;
    if(at>=sql.size() || sql[at]!='='){
      err="file table argument requires key = value";return false;
    }
    ++at;
    while(at<sql.size() && space(sql[at]))++at;
    std::string value;
    if(at<sql.size() && sql[at]=='\''){
      if(!parse_quoted_path(sql,at,value)) {
        err="invalid quoted file option value";return false;
      }
    } else {
      const size_t start=at;
      if(at<sql.size() && (sql[at]=='+' || sql[at]=='-'))++at;
      while(at<sql.size() && id_char(sql[at]))++at;
      value=sql.substr(start,at-start);
    }
    const std::string v=lower(value);
    if(std::find(seen.begin(),seen.end(),key)!=seen.end()) {
      err="duplicate file option: "+key;return false;
    }
    seen.push_back(key);
    if(key=="header" && function=="read_csv"){
      if(v=="true" || v=="on" || v=="1")opts.csv_header=true;
      else if(v=="false" || v=="off" || v=="0")opts.csv_header=false;
      else {
        err="header must be true or false";return false;
      }
    } else if(key=="sample_size" &&
              (function=="read_csv" || function=="read_json" ||
               function=="read_ndjson")) {
      if(v!="-1"){
        err="file schema inference requires sample_size = -1";
        return false;
      }
    } else if(key=="format" && (function=="read_json" ||
                                function=="read_ndjson")) {
      if(v=="newline_delimited" || v=="jsonl" || v=="ndjson")
        opts.json_newlines=true;
      else if(v=="auto" || v=="json" || v=="array")
        opts.json_newlines=false;
      else {
        err="unsupported JSON file format";return false;
      }
    } else {
      err="unsupported option "+key+" for "+function;
      return false;
    }
  }
  err="unterminated file table function";
  return false;
}

inline bool source_sql(const std::string &type,const std::string &path,
                       const FileReadOptions &opts,
                       std::string &target,std::string &err,
                       std::vector<std::string> *returned_names=nullptr,
                       std::vector<std::string> *returned_types=nullptr) {
  std::string doc;
  std::vector<std::string> names,types;
  if(type=="glob") {
    if(!glob_to_json(path,doc,err))return false;
    names.push_back("file");
    types.push_back("VARCHAR(4096)");
  } else {
    std::string data;
    if(!read_text(path,data,err))return false;
    if(type=="read_csv") {
      if(!typed_csv_json(data,opts.csv_header,doc,names,types,err))return false;
    } else if(type=="read_json" || type=="read_ndjson") {
      std::vector<std::string> objects;
      const std::string suffix=lower(path);
      const bool newline_json=type=="read_ndjson" || opts.json_newlines ||
          (suffix.size()>=6 && suffix.substr(suffix.size()-6)==".jsonl") ||
          (suffix.size()>=7 && suffix.substr(suffix.size()-7)==".ndjson");
      std::vector<std::string> discard;
      if(!parse_json_records(data,newline_json,objects,discard,err))
        return false;
      if(!typed_json_schema(objects,names,types,err))return false;
      doc="[";
      for(size_t i=0;i<objects.size();++i) {
        if(i)doc+=",";
        doc+=objects[i];
        if(doc.size()>MAX_RESULT_BYTES) {
          err="JSON result exceeds 2 MiB";return false;
        }
      }
      doc+="]";
    } else if(type=="read_parquet") {
      if(!parquet_native::read_parquet(data,doc,names,types,err))return false;
    } else {
      err="unsupported file table function";return false;
    }
  }
  if(doc.size()>MAX_RESULT_BYTES){err="source result exceeds 2 MiB";return false;}
  if(returned_names!=nullptr)*returned_names=names;
  if(returned_types!=nullptr)*returned_types=types;
  target="JSON_TABLE("+sql_quote(doc)+", '$[*]' COLUMNS (";
  for(size_t i=0;i<names.size();++i) {
    if(i)target+=", ";
    // "$.quoted-key" is a valid JSON path in OceanBase's JSON_TABLE.
    std::string path_expr="$."+json_quote(names[i]);
    target+=sql_identifier(names[i])+" "+types[i]+" PATH "+sql_quote(path_expr);
  }
  target+="))";
  return true;
}

// After rewriting a file table function to JSON_TABLE, supply a generated
// table alias only when the original query omitted one. JSON_TABLE requires it.
inline bool file_table_needs_alias(const std::string &sql, size_t pos) {
  for (;;) {
    while (pos < sql.size() && space(sql[pos])) ++pos;
    if (pos + 1 < sql.size() && sql[pos]=='/' && sql[pos+1]=='*') {
      const size_t end = sql.find("*/", pos+2);
      if (end == std::string::npos) return false;
      pos = end+2;
    } else if (pos + 1 < sql.size() && sql[pos]=='-' && sql[pos+1]=='-' &&
               (pos+2==sql.size() || space(sql[pos+2]))) {
      const size_t end=sql.find('\n',pos+2);
      pos = end==std::string::npos?sql.size():end+1;
    } else if (pos < sql.size() && sql[pos]=='#') {
      const size_t end=sql.find('\n',pos+1);
      pos = end==std::string::npos?sql.size():end+1;
    } else {
      break;
    }
  }
  if (pos==sql.size()) return true;
  if (sql[pos]==';' || sql[pos]==',' || sql[pos]==')') return true;
  if (!id_start(sql[pos])) return false;
  size_t end=pos+1;
  while(end<sql.size() && id_char(sql[end])) ++end;
  const std::string word=lower(sql.substr(pos,end-pos));
  const char *keywords[]={
    "where", "group", "order", "having", "limit", "union", "intersect",
    "except", "join", "inner", "left", "right", "full", "outer", "cross",
    "natural", "straight_join", "on", "using", "window", "qualify", "for", "lock"
  };
  for (size_t k=0;k<sizeof(keywords)/sizeof(keywords[0]);++k) {
    if (word==keywords[k]) return true;
  }
  return false;
}


// COPY is deliberately lowered to the seekdb native SELECT INTO OUTFILE /
// INSERT ... SELECT operators, never to a separate file-writing SQL engine.
// Unlike the file reader, output must not exist and its resolved PARENT
// directory must stay within the configured allowed directory.
inline bool copy_output_path(const std::string &input,std::string &output,
                             std::string &err) {
  std::string root;
  if (!permitted_root(root,err)) return false;
  if (input.empty() || input.find('\0')!=std::string::npos ||
      input.find("://")!=std::string::npos) {
    err="COPY TO requires a local file path";
    return false;
  }
  const std::string absolute=input[0]=='/'?input:root+"/"+input;
  const size_t sep=absolute.rfind('/');
  if (sep==std::string::npos || sep+1>=absolute.size()) {
    err="COPY TO requires a named file";
    return false;
  }
  char parent[PATH_MAX];
  const std::string dirname=sep==0?"/":absolute.substr(0,sep);
  if (::realpath(dirname.c_str(),parent)==nullptr) {
    err="COPY TO parent directory cannot be resolved";
    return false;
  }
  const std::string resolved_parent(parent);
  if (resolved_parent!=root && !under_root(root,resolved_parent)) {
    err="COPY TO path is outside file_sql_root";
    return false;
  }
  const std::string basename=absolute.substr(sep+1);
  if (basename=="." || basename==".." || basename.find('/')!=std::string::npos) {
    err="invalid COPY TO filename";
    return false;
  }
  output=resolved_parent+(resolved_parent=="/"?"":"/")+basename;
  if(!secured_file_path(output)) {
    err="COPY TO path is outside secure_file_priv";
    return false;
  }
  // Reject existing files, including symlinks and special files. The SQL
  // executor must ALSO use an atomic create-without-replacement operation;
  // this preflight check alone cannot eliminate rename races.
  struct stat st;
  if (::lstat(output.c_str(),&st)==0) {
    err="COPY TO refuses to overwrite an existing file";
    return false;
  }
  if (errno!=ENOENT) {
    err="COPY TO cannot inspect target";
    return false;
  }
  return true;
}
inline void copy_skip_ws(const std::string &s,size_t &p) {
  while(p<s.size() && space(s[p])) ++p;
}
inline bool copy_word(const std::string &s,size_t &p,const char *literal) {
  copy_skip_ws(s,p);
  const size_t n=std::strlen(literal);
  if (p+n>s.size() || lower(s.substr(p,n))!=literal ||
      (p+n<s.size() && id_char(s[p+n]))) return false;
  p+=n;
  return true;
}
inline bool copy_parenthesized_query(const std::string &s,size_t &p,
                                     std::string &query,std::string &err) {
  if(p>=s.size() || s[p]!='(') {
    err="COPY expected parenthesized query";
    return false;
  }
  const size_t start=++p;
  int depth=1;
  char quote=0;
  for(;p<s.size();++p) {
    const char c=s[p];
    if(quote!=0) {
      if(c=='\\' && p+1<s.size()) ++p;
      else if(c==quote && p+1<s.size() && s[p+1]==quote) ++p;
      else if(c==quote) quote=0;
    } else if(c=='\'' || c=='"' || c=='`') quote=c;
    else if(c=='(') ++depth;
    else if(c==')') {
      if(--depth==0) {
        query=trim(s.substr(start,p-start));
        ++p;
        const std::string ql=lower(query);
        if((ql.compare(0,6,"select")!=0 || (query.size()>6 && id_char(query[6]))) &&
           (ql.compare(0,4,"with")!=0 || (query.size()>4 && id_char(query[4])))) {
          err="COPY accepts only SELECT / WITH queries";
          return false;
        }
        if(query.find(';')!=std::string::npos) {
          err="COPY cannot contain SQL statement separators";
          return false;
        }
        return true;
      }
    }
  }
  err="unbalanced COPY query";
  return false;
}
inline bool copy_table_name(const std::string &s,size_t &p,
                            std::string &table,std::string &err) {
  const size_t start=p;
  bool in_quote=false;
  while(p<s.size()) {
    const char c=s[p];
    if(c=='`') {
      if(in_quote && p+1<s.size() && s[p+1]=='`') {p+=2;continue;}
      in_quote=!in_quote;
      ++p;
    } else if(!in_quote && space(c))break;
    else if(!in_quote && !(id_char(c) || c=='.'))break;
    else ++p;
  }
  table=s.substr(start,p-start);
  if(table.empty() || in_quote || table.find(';')!=std::string::npos) {
    err="invalid COPY table identifier";
    return false;
  }
  return true;
}

struct CopyCsvOptions {
  bool header;
  bool nullstr;
  std::string null_text;
  CopyCsvOptions():header(false),nullstr(false),null_text(){}
};
inline bool copy_options(const std::string &s,size_t &p,CopyCsvOptions &options,
                         std::string &err) {
  copy_skip_ws(s,p);
  if(copy_word(s,p,"with"))copy_skip_ws(s,p);
  if(p<s.size() && s[p]=='(') {
    ++p;
    std::vector<std::string> seen;
    for(;;) {
      copy_skip_ws(s,p);
      if(p<s.size() && s[p]==')') {++p;break;}
      const size_t start=p;
      while(p<s.size() && id_char(s[p]))++p;
      const std::string name=lower(s.substr(start,p-start));
      if(name.empty()) {
        err="invalid COPY option";return false;
      }
      if(std::find(seen.begin(),seen.end(),name)!=seen.end()) {
        err="duplicate COPY option: "+name;return false;
      }
      seen.push_back(name);
      copy_skip_ws(s,p);
      if(p<s.size() && s[p]=='=') {++p;copy_skip_ws(s,p);}
      if(name=="format") {
        if(!copy_word(s,p,"csv")) {
          err="COPY supports CSV only";return false;
        }
      } else if(name=="header") {
        const size_t saved=p;
        if(copy_word(s,p,"false") || copy_word(s,p,"off") ||
           copy_word(s,p,"0"))options.header=false;
        else if(copy_word(s,p,"true") || copy_word(s,p,"on") ||
                copy_word(s,p,"1"))options.header=true;
        else {
          // SQL syntax (FORMAT CSV, HEADER) means HEADER TRUE.
          p=saved;
          options.header=true;
        }
      } else if(name=="nullstr") {
        if(!parse_quoted_path(s,p,options.null_text) ||
           options.null_text.size()>256) {
          err="COPY NULLSTR must be quoted and <= 256 bytes";return false;
        }
        options.nullstr=true;
      } else if(name=="delimiter") {
        std::string value;
        if(!parse_quoted_path(s,p,value) || value!=",") {
          err="COPY currently requires comma delimiter";return false;
        }
      } else {
        err="unsupported COPY option: "+name;return false;
      }
      copy_skip_ws(s,p);
      if(p<s.size() && s[p]==',')++p;
      else if(p<s.size() && s[p]!=')') {
        err="invalid COPY option separator";return false;
      }
    }
  } else if(copy_word(s,p,"format")) {
    if(!copy_word(s,p,"csv")) {
      err="COPY supports CSV only";return false;
    }
  }
  copy_skip_ws(s,p);
  if(p<s.size() && s[p]==';') {++p;copy_skip_ws(s,p);}
  if(p!=s.size()) {
    err="unsupported trailing COPY tokens";return false;
  }
  return true;
}
// Bounded projection extractor. Supports plain projected columns and
// explicit aliases; doesn't invent column names for arbitrary expressions.
inline bool copy_query_column_names(const std::string &query,
                                    std::vector<std::string> &names,
                                    std::string &err) {
  size_t pos=0;
  if(!copy_word(query,pos,"select")) {
    err="COPY with a CSV header requires a SELECT query";return false;
  }
  const size_t projection_start=pos;
  size_t end=query.size(),depth=0;
  char quote=0;
  bool is_from=false;
  for(size_t i=pos;i<query.size();++i) {
    const char c=query[i];
    if(quote) {
      if(c=='\\' && i+1<query.size())++i;
      else if(c==quote && i+1<query.size() && query[i+1]==quote)++i;
      else if(c==quote)quote=0;
    } else if(c=='\'' || c=='"' || c=='`') quote=c;
    else if(c=='(')++depth;
    else if(c==')' && depth>0)--depth;
    else if(depth==0 && i+4<=query.size() &&
            lower(query.substr(i,4))=="from" &&
            (i==0 || !id_char(query[i-1])) &&
            (i+4==query.size() || !id_char(query[i+4]))) {
      end=i;is_from=true;break;
    }
  }
  if(!is_from){err="COPY needs SELECT ... FROM";return false;}
  const std::string projected=query.substr(projection_start,end-projection_start);
  std::vector<std::string> items;
  size_t start=0;
  depth=0;quote=0;
  for(size_t i=0;i<=projected.size();++i) {
    if(i==projected.size() || (projected[i]==',' && depth==0 && quote==0)){
      items.push_back(trim(projected.substr(start,i-start)));
      start=i+1;continue;
    }
    const char c=projected[i];
    if(quote) {
      if(c=='\\' && i+1<projected.size())++i;
      else if(c==quote && i+1<projected.size() && projected[i+1]==quote)++i;
      else if(c==quote)quote=0;
    }else if(c=='\'' || c=='"' || c=='`')quote=c;
    else if(c=='(')++depth;
    else if(c==')' && depth>0)--depth;
  }
  if(items.empty() || items.size()>256){
    err="COPY CSV header projection count is unsupported";return false;
  }
  for(size_t i=0;i<items.size();++i) {
    const std::string field=items[i];
    std::string name=field;
    // Alias via a standalone "AS" token; otherwise use last dotted part.
    size_t as_pos=std::string::npos;
    const std::string low=lower(field);
    for(size_t at=0;at+2<=field.size();++at)
      if(low.compare(at,2,"as")==0 &&
         (at==0 || space(field[at-1])) &&
         (at+2==field.size() || space(field[at+2])))
        as_pos=at;
    if(as_pos!=std::string::npos)name=trim(field.substr(as_pos+2));
    else {
      const size_t dot=field.rfind('.');
      if(dot!=std::string::npos)name=trim(field.substr(dot+1));
    }
    if(name.size()>=2 && name.front()=='`' && name.back()=='`')
      name=name.substr(1,name.size()-2);
    if(name.empty() || !id_start(name[0])) {
      err="COPY CSV HEADER needs named SELECT columns";return false;
    }
    for(size_t j=1;j<name.size();++j)
      if(!id_char(name[j])){
        err="COPY CSV HEADER requires simple column names or explicit AS";
        return false;
      }
    names.push_back(name);
  }
  return true;
}
// COPY with CSV headers/NULL text uses the *native* SELECT INTO OUTFILE
// operator. Header is synthesized by UNION ALL; never writes files here.
inline bool copy_header_output(const std::string &query,const std::string &target,
                               const CopyCsvOptions &options,
                               std::string &rewritten,std::string &err) {
  std::vector<std::string> names;
  if(!copy_query_column_names(query,names,err))return false;
  std::string head="SELECT 0 AS __seekdb_copy_sort";
  std::string body="SELECT 1 AS __seekdb_copy_sort";
  std::string output_fields;
  for(size_t i=0;i<names.size();++i) {
    const std::string c="__seekdb_copy_c"+std::to_string(i);
    const std::string col="__seekdb_copy_src."+sql_identifier(names[i]);
    if(i>0)output_fields+=", ";
    output_fields+=sql_identifier(c);
    head+=", "+sql_quote(names[i])+" AS "+sql_identifier(c);
    // Use explicit NULL text when provided; for header-only mode retain
    // the native exporter’s default SQL NULL representation.
    body+=", ";
    if(options.nullstr)
      body+="COALESCE(CAST("+col+" AS CHAR),"+
            sql_quote(options.null_text)+")";
    else
      body+=col;
    body+=" AS "+sql_identifier(c);
  }
  body+=" FROM ("+query+") AS __seekdb_copy_src";
  const std::string inner=options.header?head+" UNION ALL "+body:body;
  rewritten="SELECT "+output_fields+
      " INTO OUTFILE "+sql_quote(target)+
      " FIELDS TERMINATED BY ',' OPTIONALLY ENCLOSED BY '\"' "
      "ESCAPED BY '\"' LINES TERMINATED BY '\\n' "
      "FROM ("+inner+") AS __seekdb_copy_rows";
  if(options.header)
    rewritten+=" ORDER BY __seekdb_copy_sort, __seekdb_copy_c0";
  return true;
}

inline bool rewrite_copy(const std::string &sql,std::string &rewritten,
                         std::string &err,bool &matched) {
  matched=false;
  size_t p=0;
  if(!copy_word(sql,p,"copy")) return true;
  matched=true;
  copy_skip_ws(sql,p);
  if(p>=sql.size()){err="incomplete COPY statement";return false;}
  const bool source_query=sql[p]=='(';
  std::string source;
  if(source_query) {
    if(!copy_parenthesized_query(sql,p,source,err))return false;
  } else if(!copy_table_name(sql,p,source,err)) return false;
  const bool output=copy_word(sql,p,"to");
  if(!output && !copy_word(sql,p,"from")) {
    err="COPY expects TO or FROM";
    return false;
  }
  if(!output && source_query) {
    err="COPY FROM requires a target table, not a SELECT query";
    return false;
  }
  copy_skip_ws(sql,p);
  std::string path;
  if(!parse_quoted_path(sql,p,path)) {
    err="COPY requires a single-quoted file path";
    return false;
  }
  CopyCsvOptions opts;
  if(!copy_options(sql,p,opts,err))return false;
  if(output) {
    std::string target;
    if(!copy_output_path(path,target,err))return false;
    if(opts.header || opts.nullstr) {
      if(!source_query) {
        err="COPY HEADER/NULLSTR requires a SELECT query in this implementation";
        return false;
      }
      if(!copy_header_output(source,target,opts,rewritten,err))
        return false;
    } else {
      const std::string from_expr=source_query?
          "("+source+") AS __seekdb_copy_src":source;
      // INTO OUTFILE must occur before FROM in the seekdb MySQL grammar.
      rewritten="SELECT * INTO OUTFILE "+sql_quote(target)+
          " FIELDS TERMINATED BY ',' OPTIONALLY ENCLOSED BY '\"' "
          "ESCAPED BY '\"' LINES TERMINATED BY '\\n' FROM "+
          from_expr;
    }
  } else {
    std::string source_file;
    if(!resolve_file(path,source_file,err))return false;
    const std::string lp=lower(source_file);
    const bool is_json=lp.size()>=5 && lp.substr(lp.size()-5)==".json";
    const bool is_ndjson=(lp.size()>=7 && lp.substr(lp.size()-7)==".ndjson") ||
                         (lp.size()>=6 && lp.substr(lp.size()-6)==".jsonl");
    const bool is_csv=lp.size()>=4 && lp.substr(lp.size()-4)==".csv";
    if(!is_csv && !is_json && !is_ndjson) {
      err="COPY FROM currently supports .csv, .json and .ndjson/.jsonl only";
      return false;
    }
    rewritten="INSERT INTO "+source+" SELECT * FROM "+
        (is_csv?"read_csv":(is_json?"read_json":"read_ndjson"))+
        "("+sql_quote(source_file)+")";
  }
  return true;
}


// DESCRIBE SELECT * FROM read_*() exposes the file's inferred columns
// as ordinary SQL rows. It never asks an external SQL engine to run the query.
inline bool rewrite_describe_file(const std::string &sql,std::string &out,
                                  std::string &err,bool &matched) {
  matched=false;
  size_t p=0;
  const bool prefix=copy_word(sql,p,"describe") || copy_word(sql,p,"desc");
  if(!prefix)return true;
  if(!copy_word(sql,p,"select"))return true; // preserve DESCRIBE table
  matched=true;
  copy_skip_ws(sql,p);
  if(p>=sql.size() || sql[p]!='*'){
    err="DESCRIBE SELECT supports SELECT * projection only";return false;
  }
  ++p;
  if(!copy_word(sql,p,"from")){
    err="DESCRIBE SELECT requires FROM";return false;
  }
  copy_skip_ws(sql,p);
  const size_t start=p;
  while(p<sql.size() && id_char(sql[p]))++p;
  const std::string function=lower(sql.substr(start,p-start));
  if(function!="read_csv" && function!="read_json" &&
     function!="read_ndjson" && function!="read_parquet" &&
     function!="glob"){
    err="DESCRIBE SELECT requires a supported file table function";
    return false;
  }
  copy_skip_ws(sql,p);
  if(p>=sql.size() || sql[p]!='('){
    err="DESCRIBE SELECT requires a file path";return false;
  }
  ++p;copy_skip_ws(sql,p);
  std::string path;
  if(!parse_quoted_path(sql,p,path)){
    err="DESCRIBE SELECT requires a constant local path";return false;
  }
  FileReadOptions options;
  if(!file_options(sql,p,function,options,err))return false;
  if(p>=sql.size() || sql[p]!=')'){
    err="DESCRIBE SELECT has unterminated file table function";
    return false;
  }
  ++p;copy_skip_ws(sql,p);
  if(copy_word(sql,p,"as"))copy_skip_ws(sql,p);
  // Optionally consume one table alias.
  if(p<sql.size() && id_start(sql[p])) {
    while(p<sql.size() && id_char(sql[p]))++p;
    copy_skip_ws(sql,p);
  }
  if(p<sql.size() && sql[p]==';'){++p;copy_skip_ws(sql,p);}
  if(p!=sql.size()) {
    err="DESCRIBE SELECT supports only a single file table source";
    return false;
  }
  std::string source;
  std::vector<std::string> names,types;
  if(!source_sql(function,path,options,source,err,&names,&types))return false;
  if(names.empty() || names.size()!=types.size()){
    err="file table has no describable columns";return false;
  }
  out.clear();
  for(size_t i=0;i<names.size();++i){
    if(i)out+=" UNION ALL ";
    out+="SELECT "+sql_quote(names[i])+" AS column_name, "+
      sql_quote(types[i])+" AS column_type, 'YES' AS `null`, "+
      "NULL AS `key`, NULL AS `default`, NULL AS `extra`";
  }
  return true;
}

inline bool rewrite(const std::string &sql,std::string &out,std::string &err) {
  out.clear();
  if(sql.size()>MAX_RESULT_BYTES){err="SQL exceeds file scan limit";return false;}
  bool is_describe=false;
  if(!rewrite_describe_file(sql,out,err,is_describe))return false;
  if(is_describe)return true;
  bool is_copy=false;
  std::string copy_sql;
  if(!rewrite_copy(sql,copy_sql,err,is_copy))return false;
  if(is_copy) {
    // COPY FROM introduces a read_csv/read_json table source; pass the
    // transformed SQL back through the existing table-source rewrite.
    return rewrite(copy_sql,out,err);
  }
  if(lower(sql).find("read_csv")==std::string::npos &&
     lower(sql).find("read_json")==std::string::npos &&
     lower(sql).find("read_ndjson")==std::string::npos &&
     lower(sql).find("read_parquet")==std::string::npos &&
     lower(sql).find("glob(")==std::string::npos) {
    out=sql;
    return true;
  }
  bool from=false,expect_table=false;
  int64_t generated_aliases=0;
  enum State { NORMAL, SQUOTE, DQUOTE, BACKTICK, LINE_COMMENT, BLOCK_COMMENT } state=NORMAL;
  for(size_t i=0;i<sql.size();) {
    char c=sql[i];
    if(state==LINE_COMMENT){
      out.push_back(c);++i;
      if(c=='\n')state=NORMAL;
    } else if(state==BLOCK_COMMENT){
      if(c=='*' && i+1<sql.size() && sql[i+1]=='/') {
        out+="*/";i+=2;state=NORMAL;
      }else{out.push_back(c);++i;}
    } else if(state==SQUOTE || state==DQUOTE || state==BACKTICK){
      out.push_back(c);
      char close=state==SQUOTE?'\'':(state==DQUOTE?'"':'`');
      if(c=='\\' && i+1<sql.size()){out.push_back(sql[i+1]);i+=2;}
      else if(c==close && i+1<sql.size() && sql[i+1]==close){
        out.push_back(sql[i+1]);i+=2;
      } else {++i;if(c==close)state=NORMAL;}
    } else if(c=='-' && i+1<sql.size() && sql[i+1]=='-' &&
              (i+2==sql.size() || space(sql[i+2]))) {
      state=LINE_COMMENT;out+="--";i+=2;
    } else if(c=='#') {state=LINE_COMMENT;out.push_back(c);++i;}
    else if(c=='/' && i+1<sql.size() && sql[i+1]=='*'){
      state=BLOCK_COMMENT;out+="/*";i+=2;
    }else if(c=='\''){state=SQUOTE;out.push_back(c);++i;}
    else if(c=='"'){state=DQUOTE;out.push_back(c);++i;}
    else if(c=='`'){state=BACKTICK;out.push_back(c);++i;}
    else if(id_start(c)) {
      size_t end=i+1;
      while(end<sql.size() && id_char(sql[end]))++end;
      std::string token=sql.substr(i,end-i);
      std::string word=lower(token);
      bool table_fn=(word=="read_csv" || word=="read_json" ||
                     word=="read_ndjson" || word=="read_parquet" || word=="glob");
      size_t paren=end;
      while(paren<sql.size() && space(sql[paren]))++paren;
      if(table_fn && expect_table && paren<sql.size() && sql[paren]=='('){
        size_t at=paren+1;
        while(at<sql.size() && space(sql[at]))++at;
        std::string path;
        if(!parse_quoted_path(sql,at,path)){
          err="file table function requires a constant single-quoted path";
          return false;
        }
        FileReadOptions opts;
        if(!file_options(sql,at,word,opts,err))return false;
        if(at>=sql.size() || sql[at]!=')'){
          err="unbalanced file table function arguments";
          return false;
        }
        ++at;
        std::string replacement;
        if(!source_sql(word,path,opts,replacement,err))return false;
        if(out.size()+replacement.size()>MAX_RESULT_BYTES*2){
          err="rewritten SQL exceeds size limit";return false;
        }
        out+=replacement;
        if (file_table_needs_alias(sql, at)) {
          out+=" AS __seekdb_file_"+std::to_string(++generated_aliases);
        }
        expect_table=false;
        i=at;
      }else{
        out+=token;
        i=end;
        if(word=="from" || word=="join"){expect_table=true;from=true;}
        else if(word=="where" || word=="group" || word=="having" ||
                word=="order" || word=="limit" || word=="union") {
          from=false;expect_table=false;
        } else if(expect_table && word!="lateral" && word!="as") expect_table=false;
      }
    }else{
      out.push_back(c);++i;
      if(c==',' && from)expect_table=true;
    }
  }
  return true;
}

} // namespace native_file_sql
} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_SQL_PARSER_OB_FILE_SQL_REWRITE_H_
