"""Execute the actual native retry method with deterministic transport boundaries.

No network requests or copied retry implementation: the method is extracted from
the source under review, compiled, and called with controlled lower-level results.
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
SOURCE = Path(os.environ.get('RETRY_SOURCE', str(ROOT / 'src/sql/engine/expr/ob_expr_ai/ob_ai_func_client.cpp')))

SCAFFOLD = r'''
#include <cstdint>
#include <cstdio>
#include <cstdlib>
const int OB_SUCCESS = 0, OB_CURL_ERROR = -4667, OB_TIMEOUT = -4012,
          OB_INVALID_ARGUMENT = -4002, OTHER_ERROR = -9000;
#define OB_SUCC(value) ((value) == OB_SUCCESS)
#define OB_FAIL(value) ((ret = (value)) != OB_SUCCESS)
static int sleeps = 0;
void ob_usleep(int64_t) { ++sleeps; }
namespace oceanbase { namespace common {
struct ObIAllocator {};
struct ObString {};
struct ObJsonObject {};
template <typename T> struct ObArray {
  int count_ = 1;
  bool empty() const { return count_ == 0; }
};
class ObAIFuncClient {
public:
  int64_t max_retry_times_ = 3;
  int outcomes[3] = {OB_SUCCESS, OB_SUCCESS, OB_SUCCESS};
  int attempts = 0;
  int init_result = OB_SUCCESS;
  bool timeout = false;
  int init(ObIAllocator &, const ObString &, ObArray<ObString> &) { return init_result; }
  bool is_timeout() { return timeout; }
  int send_post_batch(ObArray<ObJsonObject *> &, ObArray<ObJsonObject *> &)
  { return outcomes[attempts++]; }
  int send_post_batch(ObIAllocator &, const ObString &, ObArray<ObString> &,
                      ObArray<ObJsonObject *> &, ObArray<ObJsonObject *> &);
};
__METHOD__
}}
int main(int argc, char **argv) {
  using namespace oceanbase::common;
  if (argc != 2) return 2;
  const int scenario = std::atoi(argv[1]);
  ObAIFuncClient client;
  ObIAllocator allocator;
  ObString url;
  ObArray<ObString> headers;
  ObArray<ObJsonObject *> data, responses;
  int expected = OB_SUCCESS, attempts = 1, expected_sleeps = 0;
  switch (scenario) {
    case 0: break;
    case 1:
      client.outcomes[0] = client.outcomes[1] = client.outcomes[2] = OB_CURL_ERROR;
      expected = OB_CURL_ERROR; attempts = 3; expected_sleeps = 2; break;
    case 2:
      client.outcomes[0] = OB_CURL_ERROR;
      attempts = 2; expected_sleeps = 1; break;
    case 3:
      client.outcomes[0] = client.outcomes[1] = OB_CURL_ERROR;
      attempts = 3; expected_sleeps = 2; break;
    case 4:
      client.outcomes[0] = OTHER_ERROR;
      expected = OTHER_ERROR; break;
    case 5:
      client.outcomes[0] = OB_CURL_ERROR; client.timeout = true;
      expected = OB_TIMEOUT; break;
    case 6:
      client.init_result = OTHER_ERROR;
      expected = OTHER_ERROR; attempts = 0; break;
    case 7:
      data.count_ = 0;
      expected = OB_INVALID_ARGUMENT; attempts = 0; break;
    case 8:
      client.max_retry_times_ = 1; client.outcomes[0] = OB_CURL_ERROR;
      expected = OB_CURL_ERROR; break;
    default: return 2;
  }
  const int result = client.send_post_batch(allocator, url, headers, data, responses);
  if (result != expected || client.attempts != attempts || sleeps != expected_sleeps) {
    std::printf("scenario %d: result=%d expected=%d attempts=%d expected=%d sleeps=%d expected=%d\n",
        scenario, result, expected, client.attempts, attempts, sleeps, expected_sleeps);
    return 1;
  }
  return 0;
}
'''


class BatchRetryRegression(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        text = SOURCE.read_text(encoding='utf-8')
        signature = 'int ObAIFuncClient::send_post_batch(common::ObIAllocator &allocator,'
        start = text.rindex(signature)  # Skip the WebAssembly unsupported stub.
        opening = text.index('{', start)
        depth, end = 1, opening + 1
        while depth:
            depth += (text[end] == '{') - (text[end] == '}')
            end += 1
        method = text[start:end]
        cls.directory = tempfile.TemporaryDirectory(prefix='seekdb-batch-retry-', dir=os.environ.get('RETRY_TEST_TMPDIR'))
        directory = Path(cls.directory.name)
        source = directory / 'test_retry.cpp'
        source.write_text(SCAFFOLD.replace('__METHOD__', method), encoding='utf-8')
        cls.binary = directory / ('retry.exe' if os.name == 'nt' else 'retry')
        compiler = os.environ.get('CXX') or shutil.which('c++') or shutil.which('clang++')
        if not compiler:
            raise RuntimeError('Set CXX to a C++17 compiler.')
        command = [compiler, '-std=c++17', '-O0', str(source), '-o', str(cls.binary)]
        if os.name == 'nt':
            command += ['-fuse-ld=lld', '-D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH']
        environment = os.environ.copy()
        environment.update(TEMP=str(directory), TMP=str(directory))
        result = subprocess.run(command, capture_output=True, text=True, env=environment)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def run_case(self, scenario):
        result = subprocess.run([str(self.binary), str(scenario)], capture_output=True, text=True)
        self.assertEqual(0, result.returncode, result.stdout + result.stderr)

    def test_first_attempt_success(self): self.run_case(0)
    def test_exhaustion_returns_last_error(self): self.run_case(1)
    def test_recovery_on_second_attempt(self): self.run_case(2)
    def test_recovery_on_last_attempt(self): self.run_case(3)
    def test_non_transport_error_is_not_retried(self): self.run_case(4)
    def test_timeout_is_not_retried(self): self.run_case(5)
    def test_initialization_error_is_preserved(self): self.run_case(6)
    def test_empty_batch_is_rejected(self): self.run_case(7)
    def test_single_attempt_preserves_error_without_sleep(self): self.run_case(8)


if __name__ == '__main__':
    unittest.main(verbosity=2)
