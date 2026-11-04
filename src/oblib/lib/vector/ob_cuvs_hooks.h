#pragma once
// Optional CUVS hooks; declarations only, no change to existing public ABI.
namespace oceanbase { namespace common { namespace obvsag {
void mark_cuvs_index(void *key);
void unmark_cuvs_index(void *key);
}}}
