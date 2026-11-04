# A small authored operations corpus. Dense vectors are genuine MiniLM outputs;
# this fixture validates SQL composition, not retrieval quality or throughput.
CREATE TABLE hr_entities(tenant BIGINT,id BIGINT,PRIMARY KEY(tenant,id));
CREATE TABLE hr_facts(tenant BIGINT,id BIGINT,src BIGINT,dst BIGINT,
 kind VARCHAR(30),valid_from BIGINT,valid_to BIGINT,PRIMARY KEY(tenant,id));
CREATE TABLE hr_documents(tenant BIGINT,id BIGINT,version BIGINT,
 source_uri VARCHAR(80),deleted BOOL,PRIMARY KEY(tenant,id,version));
CREATE TABLE hr_chunks(tenant BIGINT,id BIGINT,entity_id BIGINT,
 document_id BIGINT,source_version BIGINT,content VARCHAR(200),embedding VECTOR(384),
 PRIMARY KEY(tenant,id),FULLTEXT INDEX hr_terms(content) WITH PARSER space);
INSERT INTO hr_entities VALUES(1,1),(1,2),(1,3),(1,4),(1,5),(1,6),(2,1),(2,2);
INSERT INTO hr_facts VALUES
 (1,10,1,2,'depends_on',2020,NULL),(1,11,1,2,'depends_on',2020,NULL),
 (1,12,2,3,'depends_on',2020,NULL),(1,13,1,5,'mentions',2020,NULL),
 (1,14,1,6,'depends_on',2020,2021),(1,15,1,6,'depends_on',2025,NULL),
 (1,16,2,1,'depends_on',2020,NULL),(1,17,4,3,'depends_on',2020,NULL),
 (1,18,4,99,'depends_on',2020,NULL),(2,10,1,2,'depends_on',2020,NULL);
INSERT INTO hr_documents VALUES
 (1,1000,7,'doc://backup-policy',0),(1,1000,6,'doc://old-policy',0),
 (1,1001,3,'doc://vault',0),(1,1001,1,'doc://deleted-vault',1),
 (1,1002,9,'doc://rotation',0),(1,1003,2,'doc://audit',0),
 (1,1004,1,'doc://legacy',1),(1,1005,4,'doc://review',0),
 (2,1000,7,'doc://other-tenant',0);
--source mysql_test/test_suite/graph/hybrid_retrieval_vectors.inc
INSERT INTO hr_chunks SELECT 2,100,1,1000,7,content,embedding FROM hr_chunks WHERE tenant=1 AND id=100;
INSERT INTO hr_chunks SELECT 1,107,2,1001,1,content,embedding FROM hr_chunks WHERE tenant=1 AND id=101;
CREATE PROPERTY GRAPH hr_graph
 VERTEX TABLES(hr_entities KEY(tenant,id) LABEL entity PROPERTIES(tenant,id))
 EDGE TABLES(hr_facts KEY(tenant,id) SOURCE KEY(tenant,src) REFERENCES hr_entities(tenant,id)
  DESTINATION KEY(tenant,dst) REFERENCES hr_entities(tenant,id)
  LABEL fact PROPERTIES(tenant,id,kind,valid_from,valid_to));
SET @hr_tenant=1,@hr_asof=2024;
