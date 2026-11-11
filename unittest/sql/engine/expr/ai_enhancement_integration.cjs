// Copyright (c) 2026 OceanBase.
// SPDX-License-Identifier: Apache-2.0
// Run against a freshly built seekdb. Requires Node >=18 and a mysql CLI.
// MYSQL_ARGS is a JSON array; use MYSQL_PWD rather than a command-line password.
const http = require('node:http');
const { spawn } = require('node:child_process');
const path = require('node:path');
const assert = require('node:assert/strict');
const port = Number(process.env.AI_MOCK_PORT || 8000);
const mysql = process.env.MYSQL_PYTHON || process.env.MYSQL_BIN || 'mysql';
const clientPrefix = process.env.MYSQL_PYTHON ? [path.join(__dirname, 'ai_mysql_client.py')] : [];
const mysqlArgs = JSON.parse(process.env.MYSQL_ARGS || '["-h127.0.0.1","-P2881","-uroot","-Dtest"]');
const requests = [];
const prefix = `ai_reg_${process.pid}`;
const models = [];
function quote(value) { return "'" + value.replace(/\\/g, '\\\\').replace(/'/g, "''") + "'"; }
function sql(text, fail = false) {
  return new Promise((resolve, reject) => {
    const child = spawn(mysql, [...clientPrefix, ...mysqlArgs, '-N', '-B', '--raw', '-e', text]);
    let out = '', err = '';
    child.stdout.on('data', x => { out += x; });
    child.stderr.on('data', x => { err += x; });
    child.on('error', reject);
    child.on('close', code => {
      if (fail ? code === 0 : code !== 0) reject(new Error(`SQL exit ${code}: ${err}\n${text}`));
      else resolve(out.trim());
    });
  });
}
function vector(text, dim = 3) {
  const seed = Array.from(text).reduce((n, c) => n + c.codePointAt(0), 0);
  return Array.from({ length: dim }, (_, i) => (seed + i) % 101);
}
const server = http.createServer(async (req, res) => {
  try {
    let raw = '';
    for await (const part of req) raw += part;
    const body = JSON.parse(raw);
    const url = new URL(req.url, `http://localhost:${port}`);
    requests.push({ path: url.pathname, body });
    const mode = url.searchParams.get('mode');
    res.setHeader('Content-Type', 'application/json');
    if (mode === 'root-array') return res.end('[]');
    if (mode === 'invalid-json') return res.end('{');
    if (mode === 'http-error') { res.statusCode = 400; return res.end('{"error":"test"}'); }
    let result;
    if (url.pathname === '/complete') {
      const content = body.messages.at(-1).content;
      result = { choices: [{ message: { content: mode === 'empty' ? '' : content } }] };
    } else if (url.pathname === '/generate') {
      assert.equal(body.stream, false);
      result = { response: body.prompt };
    } else if (url.pathname === '/rerank') {
      // Return unordered results with ties spanning more than one request.
      result = { results: body.documents.map((text, index) => ({ index, relevance_score: text.length % 3 })).reverse() };
      if (mode === 'duplicate') result.results[0].index = result.results[1].index;
      if (mode === 'bad-score') result.results[0].relevance_score = 'oops';
    } else {
      assert.ok(Array.isArray(body.input));
      const vectors = body.input.map(text => vector(text, body.dimensions || 3));
      if (url.pathname === '/ollama-embed') {
        result = { embeddings: vectors };
      } else {
        result = { data: vectors.map((embedding, index) => ({ index, embedding })).reverse() };
        if (mode === 'duplicate' && result.data.length > 1) result.data[0].index = result.data[1].index;
        if (mode === 'out-of-range') result.data[0].index = vectors.length;
        if (mode === 'bad-element') result.data[0].embedding[0] = 'oops';
        if (mode === 'bad-dimension') result.data[0].embedding.push(0);
        if (mode === 'missing') result.data.pop();
        if (mode === 'bad-data') result.data = {};
      }
    }
    res.end(JSON.stringify(result));
  } catch (error) {
    res.statusCode = 400;
    res.end(JSON.stringify({ error: error.message }));
  }
});
async function endpoint(name, path) {
  await sql(`CALL DBMS_AI_SERVICE.ALTER_AI_MODEL_ENDPOINT(${quote(name + '_ep')},${quote(JSON.stringify({url:`http://127.0.0.1:${port}${path}`}))})`);
}
async function register(suffix, type, provider, path) {
  const name = `${prefix}_${suffix}`;
  await sql(`CALL DBMS_AI_SERVICE.CREATE_AI_MODEL(${quote(name)},${quote(JSON.stringify({type,model_name:name}))})`);
  models.push(name);
  await sql(`CALL DBMS_AI_SERVICE.CREATE_AI_MODEL_ENDPOINT(${quote(name+'_ep')},${quote(JSON.stringify({ai_model_name:name,scope:'all',url:`http://127.0.0.1:${port}${path}`,access_key:provider.toLowerCase()==='ollama'?'':'mock-key',provider}))})`);
  return name;
}
async function main() {
  await new Promise(resolve => server.listen(port, '127.0.0.1', resolve));
  const embed = await register('embed','dense_embedding','openai','/embed');
  const complete = await register('complete','completion','openai','/complete');
  const ollama = await register('ollama','completion','oLlAmA','/generate');
  const oe = await register('oe','dense_embedding','OLLAMA','/ollama-embed');
  const rerank = await register('rank','rerank','siliconflow','/rerank');
  assert.deepEqual(JSON.parse(await sql(`SELECT AI_EMBED('${embed}','你好',5)`)), vector('你好',5));
  assert.deepEqual(JSON.parse(await sql(`SELECT AI_EMBED('${embed}','default')`)), vector('default'));
  assert.deepEqual(JSON.parse(await sql(`SELECT AI_EMBED('${oe}','native')`)), vector('native'));
  assert.equal(await sql(`SELECT AI_EMBED('${embed}',NULL)`), 'NULL');
  for (const dim of ['0','-1','NULL','1.5']) await sql(`SELECT AI_EMBED('${embed}','x',${dim})`, true);
  // JSON structure must survive AI_PROMPT; nested prompts are rendered by completion.
  const prompt = JSON.parse(await sql(`SELECT AI_PROMPT('{0}/{0}/{1}',JSON_ARRAY(1,true),JSON_OBJECT('k',2))`));
  assert.deepEqual(prompt.args, [[1,true],{k:2}]);
  await sql(`SELECT AI_COMPLETE('${complete}',AI_PROMPT('{0}/{0}/{0}',AI_PROMPT('inner {0}','世界')),NULL)`);
  assert.equal(requests.at(-1).body.messages.at(-1).content, 'inner 世界/inner 世界/inner 世界');
  await sql(`SELECT AI_COMPLETE('${complete}','config',JSON_OBJECT('temperature',0.2,'max_tokens',16))`);
  assert.equal(requests.at(-1).body.temperature, 0.2);
  assert.equal(requests.at(-1).body.max_tokens, 16);
  for (const config of ["'[]'", "'true'", "'bad-json'"]) {
    await sql(`SELECT AI_COMPLETE('${complete}','x',${config})`,true);
  }
  await sql(`SELECT AI_COMPLETE('${ollama}','hello')`);
  assert.equal(requests.at(-1).body.stream, false);
  await sql(`SELECT AI_COMPLETE('${complete}',AI_PROMPT('{99999999999999999999999}','x'))`,true);
  const table = `${prefix}_rows`;
  await sql(`CREATE TABLE ${table}(id INT PRIMARY KEY, content LONGTEXT)`);
  try {
    await sql(`INSERT INTO ${table} VALUES(-1,REPEAT('a',100000))`);
    await sql(`SELECT LENGTH(AI_COMPLETE('${complete}',AI_PROMPT('{0}/{0}',content))) FROM ${table} WHERE id=-1`);
    assert.equal(requests.at(-1).body.messages.at(-1).content, 'a'.repeat(100000)+'/'+'a'.repeat(100000));
    await sql(`DELETE FROM ${table} WHERE id=-1`);
    const values = Array.from({length:137},(_,i)=>`(${i},${i%11===0?'NULL':quote('row-'+i)})`).join(',');
    await sql(`INSERT INTO ${table} VALUES ${values}`);
    const before = requests.length;
    const lines = (await sql(`SELECT id,AI_EMBED('${embed}',content) FROM ${table} ORDER BY id`)).split('\n');
    for (const line of lines) {
      const [id, value] = line.split('\t');
      if (Number(id)%11===0) assert.equal(value,'NULL');
      else assert.deepEqual(JSON.parse(value),vector('row-'+id));
    }
    assert.equal(lines.length,137);
    assert.ok(requests.slice(before).some(r=>r.body.input?.length>1),'SQL batch callback must combine inputs');
    const beforeNulls = requests.length;
    const nulls = (await sql(`SELECT AI_EMBED('${embed}',content) FROM ${table} WHERE content IS NULL`)).split('\n');
    assert.equal(nulls.length,13);
    assert.ok(nulls.every(value=>value==='NULL'));
    assert.equal(requests.length,beforeNulls,'NULL-only batches must not make HTTP requests');
    // A batch may contain different endpoints and requested dimensions.
    const mixed = (await sql(`SELECT id,AI_EMBED(CASE WHEN MOD(id,3)=0 THEN '${oe}' ELSE '${embed}' END,content,CASE WHEN MOD(id,2)=0 THEN 4 ELSE 6 END) FROM ${table} WHERE id BETWEEN 1 AND 12 ORDER BY id`)).split('\n');
    assert.equal(mixed.length,12);
    for (const line of mixed) {
      const [id,value]=line.split('\t');
      if (Number(id)%11===0) assert.equal(value,'NULL');
      else assert.deepEqual(JSON.parse(value),vector('row-'+id,Number(id)%2===0?4:6));
    }
    const filtered = (await sql(`SELECT id,AI_EMBED('${embed}',content) FROM ${table} WHERE id>17 AND id<90 ORDER BY id LIMIT 9`)).split('\n');
    assert.equal(filtered.length,9);
    for (const line of filtered) {
      const [id,value]=line.split('\t');
      assert.equal(Number(id)%11===0?value:JSON.stringify(JSON.parse(value)),Number(id)%11===0?'NULL':JSON.stringify(vector('row-'+id)));
    }
    for (const mode of ['duplicate','out-of-range','bad-element','missing','bad-data','root-array','invalid-json','http-error']) {
      await endpoint(embed,`/embed?mode=${mode}`);
      await sql(`SELECT AI_EMBED('${embed}',content) FROM ${table} WHERE id BETWEEN 1 AND 4`,true);
    }
    await endpoint(embed,'/embed?mode=bad-dimension');
    await sql(`SELECT AI_EMBED('${embed}','x',3)`,true);
    await endpoint(embed,'/embed');
    assert.deepEqual(JSON.parse(await sql(`SELECT AI_EMBED('${embed}','after-error')`)),vector('after-error'));
  } finally { await sql(`DROP TABLE ${table}`); }
  const docs = Array.from({length:47},(_,i)=>({id:`original-${i}`,text:'x'.repeat(i%7+1)}));
  const expected = docs.map((doc,i)=>({doc,i,score:doc.text.length%3})).sort((a,b)=>b.score-a.score||a.i-b.i);
  const ranked = JSON.parse(await sql(`SELECT AI_RERANK('${rerank}','q',${quote(JSON.stringify(docs))},'text')`));
  assert.deepEqual(ranked.map(x=>x.id),expected.map(x=>x.doc.id));
  const strings = JSON.parse(await sql(`SELECT AI_RERANK('${rerank}','q',${quote(JSON.stringify(docs.map(x=>x.text)))},NULL)`));
  assert.deepEqual(strings.map(x=>x.index),expected.map(x=>x.i));
  for (const mode of ['duplicate','bad-score']) {
    await endpoint(rerank,`/rerank?mode=${mode}`);
    await sql(`SELECT AI_RERANK('${rerank}','q','["one","two"]',NULL)`,true);
  }
  await endpoint(complete,'/complete?mode=empty');
  await sql(`SELECT AI_COMPLETE('${complete}','x')`,true);
  console.log('PASS: SQL semantics, request batching, response mapping, native Ollama, rerank, and error recovery');
}
main().catch(error=>{console.error(error);process.exitCode=1;}).finally(async()=>{
  for (const name of models.reverse()) {
    try { await sql(`CALL DBMS_AI_SERVICE.DROP_AI_MODEL_ENDPOINT(${quote(name+'_ep')})`); } catch {}
    try { await sql(`CALL DBMS_AI_SERVICE.DROP_AI_MODEL(${quote(name)})`); } catch {}
  }
  server.close();
});
