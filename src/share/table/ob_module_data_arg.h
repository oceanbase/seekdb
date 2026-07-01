#ifndef OCEANBASE_SHARE_TABLE_OB_MODULE_DATA_ARG_H_
#define OCEANBASE_SHARE_TABLE_OB_MODULE_DATA_ARG_H_
#include "lib/ob_define.h"
#include "lib/string/ob_string.h"
#include "lib/utility/ob_print_utils.h"
namespace oceanbase
{
namespace table
{
struct ObModuleDataArg
{
public:
  enum ObInfoOpType {
    INVALID_OP = -1,
    LOAD_INFO,
    CHECK_INFO,
    MAX_OP
  };
  enum ObExecModule {
    INVALID_MOD = -1,
    REDIS,
    TIMEZONE,
    GIS,
    MAX_MOD
  };
  ObModuleDataArg() : 
    op_(ObInfoOpType::INVALID_OP),
    module_(ObExecModule::INVALID_MOD),
    file_path_()
  {}
  virtual ~ObModuleDataArg() {}
  bool is_valid() const;
  TO_STRING_KV(K_(op), K_(module), K_(file_path));

  ObInfoOpType op_; // enum ObInfoOpType
  ObExecModule module_; // ObExecModule
  ObString file_path_;
};
}  // namespace table
}  // namespace oceanbase
#endif
