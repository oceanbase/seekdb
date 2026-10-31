#!/usr/bin/env python3
"""Seekdb AI-function official-public-case-aligned smoke test (isolated port 2884).
This is a diagnostic regression, not the upstream mysqltest runner or official score.
No real AI credentials, no external network, no other databases accessed.
"""
import json
import os
import re
import socket
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT = Path("/workspaces/seekdb-core")
CLIENT = ROOT / "deps/3rd/u01/obclient/bin/obclient"
PORT = int(os.environ.get("SEEKDB_AI_PORT", "2884"))
DB = "ai_public_smoke_2026"
OUT = Path("/tmp/seekdb-ai-public-smoke-report.json")

class MockService(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):
        pass

    def do_POST(self):
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if length <= 0 or length > 2000000:
                raise ValueError("invalid content length")
            body = json.loads(self.rfile.read(length))
            path = self.path
            if not isinstance(body, dict):
                raise ValueError("request must be JSON object")
            if path.endswith("/v1/chat/completions"):
                messages = body.get("messages") or []
                content = messages[-1].get("content", "") if messages else ""
                if isinstance(content, list):
                    content = json.dumps(content, ensure_ascii=False)
                result = {"id":"mock-001","choices":[{"index":0,"message":{
                    "role":"assistant", "content":"OPENAI_OK:"+str(content)},"finish_reason":"stop"}]}
                count=1
            elif path.endswith("/api/generate"):
                if body.get("stream") is not False:
                    raise ValueError("Ollama generate requires stream=false")
                result = {"model":body.get("model","mock"),"response":"OLLAMA_OK:"+
                          str(body.get("prompt","")),"done":True}
                count=1
            elif path.endswith("/v1/embeddings") or path.endswith("/api/embed"):
                inp = body.get("input", [])
                if isinstance(inp,str):
                    inp = [inp]
                if not isinstance(inp,list) or not all(isinstance(v,str) for v in inp):
                    raise ValueError("embedding input must be string array")
                dim = body.get("dimensions", 4)
                if not isinstance(dim,int) or not 1 <= dim <= 2048:
                    raise ValueError("dimension must be between 1 and 2048")
                def emb(i):
                    return [round((i+1) * ((j%17)+1)/100.0,4) for j in range(dim)]
                if path.endswith("/api/embed"):
                    result = {"embeddings":[emb(i) for i in range(len(inp))]}
                else:
                    result = {"object":"list","data":[
                        {"object":"embedding","index":i,"embedding":emb(i)}
                        for i in reversed(range(len(inp)))
                    ]}
                count=len(inp)
            elif path.endswith("/v1/rerank"):
                docs=body.get("documents",[])
                if not isinstance(docs,list):
                    raise ValueError("documents must be array")
                # Intentionally return reverse-index order to exercise index mapping.
                result={"results":[{"index":i,"relevance_score":round(1.0-i*0.1,4)}
                          for i in reversed(range(len(docs)))]}
                count=len(docs)
            else:
                self.send_error(404,"Unknown mock path")
                return
            self.server.requests.append({"path":path,"count":count,
                "model":body.get("model"),"dimensions":body.get("dimensions"),
                "request":body})
            data=json.dumps(result,ensure_ascii=False).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type","application/json; charset=utf-8")
            self.send_header("Content-Length",str(len(data)))
            self.end_headers()
            self.wfile.write(data)
        except (BrokenPipeError,ConnectionResetError):
            pass
        except Exception as exc:
            data=json.dumps({"error":str(exc)}).encode("utf-8")
            try:
                self.send_response(400)
                self.send_header("Content-Type","application/json")
                self.send_header("Content-Length",str(len(data)))
                self.end_headers()
                self.wfile.write(data)
            except (BrokenPipeError,ConnectionResetError):
                pass

def main():
    if not CLIENT.is_file():
        print("ERROR: obclient not found:",CLIENT)
        return 2
    env=os.environ.copy()
    lib=ROOT/"deps/3rd/u01/obclient/lib"
    env["LD_LIBRARY_PATH"]=str(lib) + (":"+env["LD_LIBRARY_PATH"] if env.get("LD_LIBRARY_PATH") else "")
    client=[str(CLIENT),"-h127.0.0.1","-P"+str(PORT),"-uroot","-A","-N","-B"]
    if not re.fullmatch(r"[a-zA-Z_][a-zA-Z_0-9]*",DB):
        raise RuntimeError("database name is unsafe")
    def execute(statement, need_db=True):
        argv=client+ (["-D"+DB] if need_db else [])+["-e",statement]
        p=subprocess.run(argv,env=env,cwd=ROOT,capture_output=True,text=True,timeout=150)
        if p.returncode:
            raise RuntimeError("SQL error: "+(p.stderr.strip() or p.stdout.strip())[:1000])
        return p.stdout.strip()

    try:
        print("CONNECTION:",execute("SELECT @@version;",need_db=False)[:150])
    except Exception as exc:
        print("ERROR: patched seekdb instance at 127.0.0.1:{} unavailable: {}".format(PORT,exc))
        print("Use the existing 2884 phase-4 instance, NOT StepFun on 2881.")
        return 2
    server=ThreadingHTTPServer(("127.0.0.1",0),MockService)
    server.daemon_threads=True
    server.requests=[]
    thread=threading.Thread(target=server.serve_forever,daemon=True)
    thread.start()
    mock_port=server.server_address[1]
    tag=str(os.getpid())
    model_names={
       "complete":"pub_complete_"+tag,
       "embed":"pub_embed_"+tag,
       "rerank":"pub_rerank_"+tag,
       "ollama_complete":"pub_ollama_complete_"+tag,
       "ollama_embed":"pub_ollama_embed_"+tag
    }
    endpoint_names={key:"pub_ep_"+key+"_"+tag for key in model_names}
    table="pub_batch_"+tag
    lob_table="pub_lob_"+tag
    report={"port":PORT,"target":"localhost, native patched seekdb",
            "type":"public-test-aligned local Mock SQL regression",
            "cases":[],"mock_requests":[],"status":"RUNNING"}
    passed=0
    def case(label,sql,assert_result=lambda s:True):
        nonlocal passed
        try:
            output=execute(sql)
            if not assert_result(output):
                raise RuntimeError("unexpected SQL result: "+repr(output[:500]))
            passed+=1
            report["cases"].append({"name":label,"result":"PASS","output":output[:600]})
            print("PASS",label)
            if output: print("  ",output[:165].replace("\n"," | "))
            return output
        except Exception as e:
            report["cases"].append({"name":label,"result":"FAIL","error":str(e)[:1200]})
            print("FAIL",label,"\n  ",str(e)[:1100])
            raise
    def obj_json(config):
        return json.dumps(config,ensure_ascii=False,separators=(",",":"))
    def row_map(output):
        return {parts[0]: parts[1] for parts in
                (line.split("\t") for line in output.splitlines())
                if len(parts)>=2}
    def http_counts(from_index, suffix):
        return [r["count"] for r in server.requests[from_index:]
                if r["path"].endswith(suffix)]
    def post_endpoint(key,path,provider,access_key):
        opts={"ai_model_name":model_names[key],"scope":"all",
              "url":"http://127.0.0.1:{}{}".format(mock_port,path),
              "provider":provider}
        if access_key is not None:
            opts["access_key"]=access_key
        return "CALL DBMS_AI_SERVICE.CREATE_AI_MODEL_ENDPOINT('{}', '{}');".format(
            endpoint_names[key],obj_json(opts))
    def create_model(key,model_type,remote_model):
        return "CALL DBMS_AI_SERVICE.CREATE_AI_MODEL('{}', '{}');".format(
            model_names[key],obj_json({"type":model_type,"model_name":remote_model}))
    succeeded=False
    try:
        execute("CREATE DATABASE IF NOT EXISTS "+DB,need_db=False)
        case("P1: JSON args and repeated placeholders",
             "SELECT JSON_TYPE(JSON_EXTRACT(AI_PROMPT('{0}/{0}/{1}',JSON_ARRAY(1,true),JSON_OBJECT('k',2)),'$.args[0]'));",
             lambda out:out=="ARRAY")
        case("P2: JSON object preserved",
             "SELECT JSON_TYPE(JSON_EXTRACT(AI_PROMPT('{0}',JSON_OBJECT('k',2)),'$.args[0]'));",
             lambda out:out=="OBJECT")
        case("P3: LONGTEXT/TEXT table columns",
             "CREATE TABLE {}(t LONGTEXT,a TEXT);".format(lob_table))
        case("P4: insert LOB template data",
             "INSERT INTO {} VALUES('{{0}}-ok','lob');".format(lob_table))
        case("P5: LOB template and argument in SQL",
             "SELECT JSON_UNQUOTE(JSON_EXTRACT(AI_PROMPT(t,a),'$.template')) FROM {};".format(lob_table),
             lambda out:out=="{0}-ok")
        case("P6: Unicode template",
             "SELECT JSON_UNQUOTE(JSON_EXTRACT(AI_PROMPT('你好 {0} {0}','世界'),'$.template'));",
             lambda out:out=="你好 {0} {0}")

        for key,kind,model in [
            ("complete","completion","qwen3-32b"),
            ("embed","dense_embedding","text-embedding-v1"),
            ("rerank","rerank","BAAI/bge-reranker-v2-m3"),
            ("ollama_complete","completion","qwen2.5"),
            ("ollama_embed","dense_embedding","nomic-embed-text"),
        ]:
            case("Register model "+key,create_model(key,kind,model))
        for key,path,provider,access in [
            ("complete","/compatible-mode/v1/chat/completions","openai","mock-key"),
            ("embed","/compatible-mode/v1/embeddings","openAI","mock-key"),
            ("rerank","/v1/rerank","siliconflow","mock-key"),
            ("ollama_complete","/api/generate","ollama",""),
            ("ollama_embed","/api/embed","OLLAMA",""),
        ]:
            case("Register endpoint "+key,post_endpoint(key,path,provider,access))

        case("C1: COMPLETE explicit SQL NULL config",
             "SELECT AI_COMPLETE('{}','hello',NULL);".format(model_names["complete"]),
             lambda out:out.startswith("OPENAI_OK:"))
        case("C2: COMPLETE nested AI_PROMPT + temperature and max_tokens",
             "SELECT AI_COMPLETE('{}', AI_PROMPT('describe {{0}}',JSON_OBJECT('text','你好')), '{{\"temperature\":0.2,\"max_tokens\":16}}');".format(model_names["complete"]),
             lambda out:out.startswith("OPENAI_OK:"))
        chat_requests=[r for r in server.requests if r["path"].endswith("/v1/chat/completions")]
        latest=chat_requests[-1]["request"] if chat_requests else {}
        case("C3: HTTP config forwarded to remote model",
             "SELECT 1;",lambda out:out=="1" and
                    latest.get("temperature")==0.2 and latest.get("max_tokens")==16)

        case("E1: default embedding dimensions 4",
             "SELECT JSON_LENGTH(AI_EMBED('{}','single-row'));".format(model_names["embed"]),
             lambda out:out=="4")
        case("E2: explicit embedding dimension 768",
             "SELECT JSON_LENGTH(AI_EMBED('{}','dimension-row',768));".format(model_names["embed"]),
             lambda out:out=="768")
        case("E3: create real SQL table",
             "CREATE TABLE {}(id INT PRIMARY KEY,content TEXT);".format(table))
        case("E4: insert two values with NULL middle",
             "INSERT INTO {} VALUES(1,'first'),(2,NULL),(3,'third');".format(table))
        before=len(server.requests)
        case("E5: real-table nested SQL embedding row map",
             "SELECT id,JSON_LENGTH(AI_EMBED('{}',content)) FROM {} ORDER BY id;".format(
                 model_names["embed"],table),
             lambda out:row_map(out)=={"1":"4","2":"NULL","3":"4"})
        sizes=http_counts(before,"/v1/embeddings")
        report["nested_batch_sizes"]=sizes
        case("E6: HTTP request has 2 inputs (not [1,1])",
             "SELECT 1;",lambda out:out=="1" and 2 in sizes)
        before=len(server.requests)
        case("E7: direct real-table embedding",
             "SELECT id,AI_EMBED('{}',content,4) FROM {} ORDER BY id;".format(
                 model_names["embed"],table),
             lambda out:len(out.splitlines())==3 and "\tNULL" in out)
        direct_sizes=http_counts(before,"/v1/embeddings")
        report["direct_batch_sizes"]=direct_sizes
        case("E8: direct call also grouped as one HTTP request",
             "SELECT 1;",lambda out:out=="1" and 2 in direct_sizes)
        before=len(server.requests)
        case("E9: CASE applies row skip information",
             "SELECT id,CASE WHEN id=1 THEN JSON_LENGTH(AI_EMBED('{}',content,4)) ELSE 0 END FROM {} ORDER BY id;".format(
                 model_names["embed"],table),
             lambda out:row_map(out)=={"1":"4","2":"0","3":"0"})
        case_sizes=http_counts(before,"/v1/embeddings")
        report["case_batch_sizes"]=case_sizes
        case("E10: CASE doesn't call skipped rows",
             "SELECT 1;",lambda out:out=="1" and sum(case_sizes)==1)

        case("R1: RERANK string list and SQL NULL text key",
             """SELECT JSON_LENGTH(AI_RERANK('{}','apple',
                         '["apple","banana","fruit","vegetable"]',NULL));""".format(
                 model_names["rerank"]),lambda out:out=="4")
        case("R2: RERANK object list ID mapping and sorting",
             """SELECT JSON_UNQUOTE(JSON_EXTRACT(AI_RERANK('{}','apple',
                       '[{{"id":"a","text":"apple"}},{{"id":"b","text":"banana"}}]',
                       'text'),'$[0].id'));""".format(model_names["rerank"]),
             lambda out:out=="a")

        case("O1: Ollama empty key endpoint provider display",
             """SELECT provider FROM oceanbase.DBA_OB_AI_MODEL_ENDPOINTS
                WHERE endpoint_name='{}';""".format(endpoint_names["ollama_complete"]),
             lambda out:out.lower()=="ollama")
        case("O2: ALTER Ollama provider casing",
             """CALL DBMS_AI_SERVICE.ALTER_AI_MODEL_ENDPOINT('{}',
                    '{{"provider":"Ollama"}}');""".format(endpoint_names["ollama_complete"]))
        case("O3: Ollama /api/generate nonstreaming completion",
             "SELECT AI_COMPLETE('{}','hello');".format(model_names["ollama_complete"]),
             lambda out:out.startswith("OLLAMA_OK:hello"))
        case("O4: Ollama /api/embed keyless embedding",
             "SELECT JSON_VALID(AI_EMBED('{}','hello'));".format(model_names["ollama_embed"]),
             lambda out:out=="1")

        succeeded=True
        report["status"]="PUBLIC_SQL_SMOKE_PASS"
        print("\nAI_PUBLIC_SQL_SMOKE_PASS: {} checks".format(passed))
        print("HTTP_BATCH_SIZES:",{"nested":sizes,"direct":direct_sizes,"case":case_sizes})
    except Exception as exc:
        report["status"]="PUBLIC_SQL_SMOKE_FAIL"
        report["failure"]=str(exc)[:1700]
        print("\nAI_PUBLIC_SQL_SMOKE_FAIL:",str(exc)[:1000])
    finally:
        # Only clean up these run-specific names in 2884, never external
        # user data or StepFun on 2881.
        for item in (table,lob_table):
            try:
                execute("DROP TABLE IF EXISTS "+item+";")
            except Exception:
                pass
        for endpoint in endpoint_names.values():
            try:
                execute("CALL DBMS_AI_SERVICE.DROP_AI_MODEL_ENDPOINT('{}');".format(endpoint))
            except Exception:
                pass
        for model in model_names.values():
            try:
                execute("CALL DBMS_AI_SERVICE.DROP_AI_MODEL('{}');".format(model))
            except Exception:
                pass
        report["mock_requests"]=[{
            "path":r["path"],"count":r["count"],"model":r.get("model"),
            "dimensions":r.get("dimensions")
        } for r in server.requests]
        OUT.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding="utf-8")
        print("REPORT:",OUT)
        server.shutdown()
        server.server_close()
    return 0 if succeeded else 1

if __name__=="__main__":
    sys.exit(main())
