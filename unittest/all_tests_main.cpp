// Combined test runner for Android: all TEST() macros auto-register globally.
// Individual test mains are disabled via -Dmain=disabled_main at compile time.
#undef main
#include "gtest/gtest.h"

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
