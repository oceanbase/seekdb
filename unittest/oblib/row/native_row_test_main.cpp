#include "common/row/ob_row.h"
#include "lib/charset/ob_charset.h"
#include "lib/allocator/ob_allocator.h"
#include "common/wide_integer/ob_wide_integer_helper.h"
#include <gtest/gtest.h>
#include <fstream>
#include <cstdlib>
int main(int argc,char **argv) {
 using namespace oceanbase::common;
 if(ObCharset::init_charset()!=OB_SUCCESS)return 2;
 static ObArenaAllocator constants;
 if(wide::ObDecimalIntConstValue::init_const_values(constants)!=OB_SUCCESS)return 3;
 const char *maps=getenv("NATIVE_ROW_MAPS");
 if(maps){std::ifstream input("/proc/self/maps");std::ofstream output(maps);output<<input.rdbuf();}
 ::testing::InitGoogleTest(&argc,argv);return RUN_ALL_TESTS();
}
