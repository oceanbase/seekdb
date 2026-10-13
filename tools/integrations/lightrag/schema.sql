-- Provision once, in a dedicated database, before initializing the adapter.
-- No database is created or dropped by the adapter.
CREATE TABLE lr_seekdb_scopes (
  id BIGINT PRIMARY KEY AUTO_INCREMENT,
  workspace VARBINARY(512) NOT NULL,
  namespace VARBINARY(512) NOT NULL,
  UNIQUE KEY scope_name(workspace,namespace)
);
CREATE TABLE lr_seekdb_nodes (
  id BIGINT PRIMARY KEY AUTO_INCREMENT,
  scope_id BIGINT NOT NULL,
  node_key VARBINARY(2048) NOT NULL,
  attributes JSON NOT NULL,
  UNIQUE KEY node_name(scope_id,node_key)
);
CREATE TABLE lr_seekdb_edges (
  scope_id BIGINT NOT NULL,
  src BIGINT NOT NULL,
  dst BIGINT NOT NULL,
  attributes JSON NOT NULL,
  PRIMARY KEY(scope_id,src,dst),
  KEY incoming(scope_id,dst,src),
  KEY outgoing(src,scope_id,dst)
);
CREATE PROPERTY GRAPH lr_seekdb_graph
  VERTEX TABLES (lr_seekdb_nodes KEY(id)
    LABEL entity PROPERTIES(scope_id,node_key,attributes))
  EDGE TABLES (lr_seekdb_edges KEY(scope_id,src,dst)
    SOURCE KEY(src) REFERENCES lr_seekdb_nodes(id)
    DESTINATION KEY(dst) REFERENCES lr_seekdb_nodes(id)
    LABEL relation PROPERTIES(attributes));
