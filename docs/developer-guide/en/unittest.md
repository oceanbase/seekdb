# Write and Run Unit Tests

## How to Build and Run All Unit Tests

[OceanBase SeekDB](https://github.com/oceanbase/seekdb) has two unittest directories.

- `unittest`: These are the main unit test cases, and they test the code in the `src` directory.

- `deps/oblib/unittest`: Test cases for oblib.

First, you should build `unittest`. Enter the `unittest` directory in the build directory and build explicitly. When you build the SeekDB project, it doesn't build the unit tests by default. For example:

```bash
bash build.sh --init --make # init and build a debug mode project
cd build_debug/unittest  # or cd build_debug/deps/oblib/unittest
make -j4 # build unittest
```

Then you can execute the script file `run_tests.sh` to run all test cases.

## How to Build and Run a Single Unit Test

You can also build and test a single unit test case. Enter the `build_debug` directory and execute `make case-name` to build the specific case and run the built binary file. For example:

```bash
cd build_debug
# **NOTE**: don't enter the unittest directory
make -j4 test_chunk_row_store
find . -name "test_chunk_row_store"
# got ./unittest/sql/engine/basic/test_chunk_row_store
./unittest/sql/engine/basic/test_chunk_row_store
```

## How to Write Unit Tests

As a C++ project, [OceanBase SeekDB](https://github.com/oceanbase/seekdb) uses [Google Test](https://github.com/google/googletest) as the unit test framework.

SeekDB uses `test_xxx.cpp` as the unit test file name. Create a `test_xxx.cpp` file and add the file name to the specific `CMakeLists.txt` file.

In the `test_xxx.cpp` file, add the header file `#include <gtest/gtest.h>` and the main function below.

```cpp
int main(int argc, char **argv)
{
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
```

You can then add functions to test different scenarios. Below is an example from `test_ra_row_store_projector.cpp`.

```cpp
///
/// TEST is a google test macro.
/// You can use it to create a new test function
///
/// RARowStore is the test suite name and alloc_project_fail
/// is the test name.
///
TEST(RARowStore, alloc_project_fail)
{
  ObEmptyAlloc alloc;
  ObRARowStore rs(&alloc, true);

  /// ASSERT_XXX are testing macros that help us determine whether the results
  /// are expected, and they will terminate the test if failed.
  ///
  /// There are some other testing macros beginning with `EXPECT_` which
  /// don't terminate the test if failed.
  ///
  ASSERT_EQ(OB_SUCCESS, rs.init(100 << 20));
  const int64_t OBJ_CNT = 3;
  ObObj objs[OBJ_CNT];
  ObNewRow r;
  r.cells_ = objs;
  r.count_ = OBJ_CNT;
  int64_t val = 0;
  for (int64_t i = 0; i < OBJ_CNT; i++) {
    objs[i].set_int(val);
    val++;
  }

  int32_t projector[] = {0, 2};
  r.projector_ = projector;
  r.projector_size_ = ARRAYSIZEOF(projector);

  ASSERT_EQ(OB_ALLOCATE_MEMORY_FAILED, rs.add_row(r));
}
```

Please refer to the [Google Test documentation](https://google.github.io/googletest/) for more details about `TEST`, `ASSERT`, and `EXPECT`.

## Unit Tests on GitHub CI

Before a pull request is merged, the CI will test your pull request. The `Farm` will test the `mysql test` and `unittest`. You can see the details by following the `Details` link as shown below.

![GitHub CI](images/unittest-github-ci.png)

![GitHub CI Farm Details](images/unittest-ci-details.png)

![Farm Unittest](images/unittest-unittest.png)
