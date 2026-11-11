"""Windows bootstrap regression for issue #1544 using the public C driver.

Set SEEKDB_TEST_DRIVER to seekdb_driver.dll and SEEKDB_TEST_BINARY to the
server executable under test, then run this file with Python 3. The driver
ships in the official pylibseekdb Windows wheel; no Python SDK import is used.
Failed instance directories are retained and printed for diagnosis.
"""

import ctypes
import os
from pathlib import Path
import shutil
import sys
import tempfile
import time
import unittest


class Driver:
    def __init__(self, library, binary):
        self.dll_directory = os.add_dll_directory(str(library.parent))
        self.lib = ctypes.CDLL(str(library))
        signatures = {
            "seekdb_set_binary_path": ([ctypes.c_char_p], ctypes.c_int),
            "seekdb_open": ([ctypes.c_char_p, ctypes.POINTER(ctypes.c_char_p), ctypes.POINTER(ctypes.c_void_p)], ctypes.c_int),
            "seekdb_last_open_error": ([], ctypes.c_char_p),
            "seekdb_close": ([ctypes.c_void_p], ctypes.c_int),
            "seekdb_connect": ([ctypes.c_void_p, ctypes.c_char_p, ctypes.c_bool, ctypes.POINTER(ctypes.c_void_p)], ctypes.c_int),
            "seekdb_disconnect": ([ctypes.c_void_p], ctypes.c_int),
            "seekdb_query": ([ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int64, ctypes.POINTER(ctypes.c_void_p)], ctypes.c_int),
            "seekdb_last_error": ([ctypes.c_void_p, ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_char_p)], ctypes.c_int),
            "seekdb_result_free": ([ctypes.c_void_p], ctypes.c_int),
            "seekdb_result_next": ([ctypes.c_void_p], ctypes.c_int),
            "seekdb_result_get_int64": ([ctypes.c_void_p, ctypes.c_int64, ctypes.POINTER(ctypes.c_int64)], ctypes.c_int),
        }
        for name, (arguments, result) in signatures.items():
            function = getattr(self.lib, name)
            function.argtypes = arguments
            function.restype = result
        if self.lib.seekdb_set_binary_path(str(binary).encode("utf-8")) != 0:
            raise RuntimeError("Cannot select the server executable")

    def open(self, directory, rpc_timeout):
        pairs = [b"cpu_count", b"2"]
        if rpc_timeout is not None:
            pairs.extend([b"rpc_timeout", rpc_timeout.encode("ascii")])
        pairs.append(None)
        parameters = (ctypes.c_char_p * len(pairs))(*pairs)
        handle = ctypes.c_void_p()
        code = self.lib.seekdb_open(str(directory).encode("utf-8"), parameters, ctypes.byref(handle))
        if code != 0:
            error = self.lib.seekdb_last_open_error()
            message = error.decode("utf-8", errors="replace") if error else "unknown startup failure"
            if handle.value:
                self.lib.seekdb_close(handle)
            raise RuntimeError(f"seekdb_open failed ({code}): {message}")
        return handle

    def execute(self, handle, sql, scalar=False):
        connection = ctypes.c_void_p()
        if self.lib.seekdb_connect(handle, b"test", True, ctypes.byref(connection)) != 0:
            raise RuntimeError("Cannot connect to the started instance")
        result = ctypes.c_void_p()
        try:
            encoded = sql.encode("utf-8")
            if self.lib.seekdb_query(connection, encoded, len(encoded), ctypes.byref(result)) != 0:
                number, message = ctypes.c_int(), ctypes.c_char_p()
                self.lib.seekdb_last_error(connection, ctypes.byref(number), ctypes.byref(message))
                detail = message.value.decode("utf-8", errors="replace") if message.value else "unknown SQL error"
                raise RuntimeError(f"SQL error {number.value}: {detail}")
            if scalar:
                if not result.value or self.lib.seekdb_result_next(result) != 0:
                    raise RuntimeError("Expected one result row")
                value = ctypes.c_int64()
                if self.lib.seekdb_result_get_int64(result, 0, ctypes.byref(value)) != 0:
                    raise RuntimeError("Cannot read the integer result")
                return value.value
        finally:
            if result.value:
                self.lib.seekdb_result_free(result)
            self.lib.seekdb_disconnect(connection)


@unittest.skipUnless(sys.platform == "win32", "issue #1544 concerns native Windows bootstrap")
class BootstrapTimeoutTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        library = os.environ.get("SEEKDB_TEST_DRIVER")
        binary = os.environ.get("SEEKDB_TEST_BINARY")
        if not library or not binary:
            raise unittest.SkipTest("Set SEEKDB_TEST_DRIVER and SEEKDB_TEST_BINARY")
        library, binary = Path(library).resolve(strict=True), Path(binary).resolve(strict=True)
        cls.driver = Driver(library, binary)
        previous = os.environ.get("TELEMETRY_ENABLED")
        os.environ["TELEMETRY_ENABLED"] = "false"

        def restore_environment():
            if previous is None:
                os.environ.pop("TELEMETRY_ENABLED", None)
            else:
                os.environ["TELEMETRY_ENABLED"] = previous

        cls.addClassCleanup(restore_environment)
        cls.addClassCleanup(cls.driver.dll_directory.close)

    def check_lifecycle(self, rpc_timeout):
        directory = Path(tempfile.mkdtemp(prefix="seekdb-bootstrap-timeout-")).resolve()
        try:
            handle = self.driver.open(directory, rpc_timeout)
            try:
                self.driver.execute(handle, "CREATE TABLE bootstrap_probe (id INT PRIMARY KEY, value INT)")
                self.driver.execute(handle, "INSERT INTO bootstrap_probe VALUES (1, 1544)")
                self.assertEqual(1544, self.driver.execute(handle, "SELECT value FROM bootstrap_probe WHERE id=1", scalar=True))
            finally:
                self.assertEqual(0, self.driver.lib.seekdb_close(handle))
            handle = self.driver.open(directory, rpc_timeout)
            try:
                self.assertEqual(1544, self.driver.execute(handle, "SELECT value FROM bootstrap_probe WHERE id=1", scalar=True))
            finally:
                self.assertEqual(0, self.driver.lib.seekdb_close(handle))
        except BaseException:
            print(f"Retained failed instance: {directory}", file=sys.stderr)
            raise
        else:
            temporary_root = Path(tempfile.gettempdir()).resolve()
            if directory.parent != temporary_root or not directory.name.startswith("seekdb-bootstrap-timeout-"):
                raise RuntimeError("Unexpected temporary instance directory")
            # The Windows driver can return while the child's final log
            # handles are still closing. Give those handles a bounded drain.
            deadline = time.monotonic() + 10
            while True:
                try:
                    shutil.rmtree(directory)
                    break
                except PermissionError:
                    if time.monotonic() >= deadline:
                        raise
                    time.sleep(0.1)

    def test_default_rpc_timeout(self):
        self.check_lifecycle(None)

    def test_short_rpc_timeout(self):
        # init_global_stat must use internal_sql_execute_timeout, rather than
        # inheriting this intentionally short RPC budget in its stat proxy.
        self.check_lifecycle("50ms")


if __name__ == "__main__":
    unittest.main()
