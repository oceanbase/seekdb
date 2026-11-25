CREATE TABLE tp_vertex(tenant BIGINT,id BIGINT,PRIMARY KEY(tenant,id));
CREATE TABLE tp_edge(tenant BIGINT,id BIGINT,src BIGINT,dst BIGINT,
 kind VARCHAR(20),valid_from BIGINT,valid_to BIGINT,PRIMARY KEY(tenant,id));
CREATE TABLE tp_document(tenant BIGINT,id BIGINT,version BIGINT,
 source_uri VARCHAR(64),deleted BOOL,PRIMARY KEY(tenant,id,version));
CREATE TABLE tp_chunk(tenant BIGINT,id BIGINT,entity_id BIGINT,doc_id BIGINT,
 version BIGINT,PRIMARY KEY(tenant,id));
CREATE TABLE tp_seed(binding BIGINT PRIMARY KEY,tenant BIGINT,entity_id BIGINT,score BIGINT);
INSERT INTO tp_vertex VALUES(1,1),(1,2),(1,3),(1,4),(2,1),(2,2);
INSERT INTO tp_edge VALUES
 (1,10,1,2,'depends',2020,NULL),(1,11,2,3,'depends',2020,NULL),
 (1,12,2,4,'depends',2025,NULL),(1,13,1,3,'mentions',2020,NULL),
 (1,14,1,4,'depends',2020,2021),(2,10,1,2,'depends',2020,NULL);
INSERT INTO tp_document VALUES
 (1,1000,1,'doc://seed/v1',0),(1,1001,1,'doc://vault/v1',0),
 (1,1001,0,'doc://vault/old',0),(1,1002,1,'doc://rotation/v1',0),
 (1,1003,1,'doc://audit/v1',0),(2,1000,1,'doc://other/seed',0),
 (2,1001,1,'doc://other/vault',0);
INSERT INTO tp_chunk VALUES
 (1,100,1,1000,1),(1,101,2,1001,1),(1,102,3,1002,1),(1,103,4,1003,1),
 (2,100,1,1000,1),(2,101,2,1001,1);
INSERT INTO tp_seed VALUES(1,1,1,10),(2,1,1,20);
CREATE PROPERTY GRAPH tp_graph
 VERTEX TABLES(tp_vertex KEY(tenant,id) LABEL entity PROPERTIES(tenant,id))
 EDGE TABLES(tp_edge KEY(tenant,id) SOURCE KEY(tenant,src) REFERENCES tp_vertex(tenant,id)
 DESTINATION KEY(tenant,dst) REFERENCES tp_vertex(tenant,id)
 LABEL fact PROPERTIES(tenant,id,kind,valid_from,valid_to));
SET @tp_asof=2024;
# Composite (tenant,id) endpoints carry tenant isolation through every hop.
# The literal cutoff permits native execution. The equivalent explicit outer
# tenant/user-variable predicate below also exercises relational fallback.
let $tp_graph=SELECT s.binding,s.tenant,s.score,g.endpoint,g.hops,CAST(g.edges AS CHAR(100)) AS edges
 FROM tp_seed s,LATERAL (
 SELECT * FROM GRAPH_TABLE(tp_graph MATCH
 (a IS entity WHERE a.tenant=s.tenant AND a.id=s.entity_id)
 -[e IS fact WHERE e.kind='depends'
 AND e.valid_from<=2024 AND (e.valid_to IS NULL OR e.valid_to>2024)]->{0,2}
 (b IS entity)
 COLUMNS(b.id AS endpoint,COUNT(e.id) AS hops,JSON_ARRAYAGG(e.id) AS edges)) p) g;
let $tp_fallback=SELECT s.binding,s.tenant,s.score,g.endpoint,g.hops,CAST(g.edges AS CHAR(100)) AS edges
 FROM tp_seed s,LATERAL (
 SELECT * FROM GRAPH_TABLE(tp_graph MATCH
 (a IS entity WHERE a.tenant=s.tenant AND a.id=s.entity_id)
 -[e IS fact WHERE e.tenant=s.tenant AND e.kind='depends'
 AND e.valid_from<=@tp_asof AND (e.valid_to IS NULL OR e.valid_to>@tp_asof)]->{0,2}
 (b IS entity)
 COLUMNS(b.id AS endpoint,COUNT(e.id) AS hops,JSON_ARRAYAGG(e.id) AS edges)) p) g;
# Enumerate all eligible zero/one/two-hop paths without GRAPH_TABLE.
let $tp_oracle=SELECT s.binding,s.tenant,s.score,a.id AS endpoint,0 AS hops,
 CAST(NULL AS CHAR(100)) AS edges FROM tp_seed s
 JOIN tp_vertex a ON a.tenant=s.tenant AND a.id=s.entity_id
 UNION ALL
 SELECT s.binding,s.tenant,s.score,b.id,1,CAST(JSON_ARRAY(e.id) AS CHAR(100))
 FROM tp_seed s JOIN tp_vertex a ON a.tenant=s.tenant AND a.id=s.entity_id
 JOIN tp_edge e ON e.tenant=a.tenant AND e.src=a.id AND e.kind='depends'
 AND e.valid_from<=@tp_asof AND (e.valid_to IS NULL OR e.valid_to>@tp_asof)
 JOIN tp_vertex b ON b.tenant=e.tenant AND b.id=e.dst
 UNION ALL
 SELECT s.binding,s.tenant,s.score,c.id,2,CAST(JSON_ARRAY(e.id,f.id) AS CHAR(100))
 FROM tp_seed s JOIN tp_vertex a ON a.tenant=s.tenant AND a.id=s.entity_id
 JOIN tp_edge e ON e.tenant=a.tenant AND e.src=a.id AND e.kind='depends'
 AND e.valid_from<=@tp_asof AND (e.valid_to IS NULL OR e.valid_to>@tp_asof)
 JOIN tp_vertex b ON b.tenant=e.tenant AND b.id=e.dst
 JOIN tp_edge f ON f.tenant=b.tenant AND f.src=b.id AND f.kind='depends'
 AND f.valid_from<=@tp_asof AND (f.valid_to IS NULL OR f.valid_to>@tp_asof)
 JOIN tp_vertex c ON c.tenant=f.tenant AND c.id=f.dst;
let $tp_evidence=SELECT p.*,c.id AS chunk_id,c.doc_id,c.version,d.source_uri
 FROM paths p JOIN tp_chunk c ON c.tenant=p.tenant AND c.entity_id=p.endpoint
 JOIN tp_document d ON d.tenant=c.tenant AND d.id=c.doc_id
 AND d.version=c.version AND d.deleted=0;
