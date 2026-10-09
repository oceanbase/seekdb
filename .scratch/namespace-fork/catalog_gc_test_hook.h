// Local test binary only: one synchronous boundary in the real collector.
#pragma once
#include <functional>
namespace oceanbase { namespace rootserver { namespace catalog_gc_test {
inline thread_local std::function<int()> after_scan;
} } }
