#!/usr/bin/env python3
"""Local test only: exercise module stop before the ordinary process _Exit."""
import argparse,re
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('action',choices=('enable','disable'));a=p.parse_args()
root=Path(__file__).resolve().parents[2]
for rel in ('src/observer/ob_server.cpp','src/oblib/grpc/ob_grpc_server.cpp'):
    target=root/rel;s=target.read_text()
    s=re.sub(r'// LOCAL_GRPC_STOP_BEGIN\n.*?// LOCAL_GRPC_STOP_END\n','',s,flags=re.S)
    if a.action=='enable':
        if rel.endswith('/ob_server.cpp'):
            anchor='  _Exit(0);\n  return ret;\n}\n\nint ObServer::init_tz_info_mgr()'
            hook='// LOCAL_GRPC_STOP_BEGIN\n  if (getenv("SEEKDB_TEST_GRPC_STOP") != nullptr && standby_module_ != nullptr) {\n    fprintf(stderr, "GRPC_STOP_TEST_ENTER\\n");\n    standby_module_->stop();\n    fprintf(stderr, "GRPC_STOP_TEST_RETURN\\n");\n  }\n// LOCAL_GRPC_STOP_END\n'
            assert s.count(anchor)==1;s=s.replace(anchor,hook+anchor)
        else:
            anchor='    server_->Shutdown(std::chrono::system_clock::now());\n'
            hook='// LOCAL_GRPC_STOP_BEGIN\n    if (getenv("SEEKDB_TEST_LEGACY_GRPC_STOP") != nullptr) { server_->Shutdown(); }\n// LOCAL_GRPC_STOP_END\n'
            assert s.count(anchor)==1;s=s.replace(anchor,hook+anchor)
    if s!=target.read_text():target.write_text(s)
