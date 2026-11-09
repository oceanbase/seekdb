CREATE TABLE graph_person (id BIGINT PRIMARY KEY, name VARCHAR(40), age BIGINT);
CREATE TABLE graph_company (id BIGINT PRIMARY KEY, name VARCHAR(40));
CREATE TABLE graph_knows (id BIGINT PRIMARY KEY, src BIGINT, dst BIGINT, since BIGINT);
CREATE TABLE graph_works (id BIGINT PRIMARY KEY, src BIGINT, dst BIGINT);
INSERT INTO graph_person VALUES (1,'Alice',30),(2,'Bob',NULL),(3,'Carol',25),(4,NULL,40),(5,'Isolated',50);
INSERT INTO graph_company VALUES (1,'Acme');
INSERT INTO graph_knows VALUES (10,1,2,2020),(11,1,2,2021),(12,2,3,2022),(13,3,1,2023),(14,4,4,2024),(15,1,99,2025),(16,NULL,2,2026),(17,98,2,2027);
INSERT INTO graph_works VALUES (20,1,1);
CREATE PROPERTY GRAPH graph_social
  VERTEX TABLES (
    graph_person KEY (id) LABEL person PROPERTIES (id,name,age),
    graph_company KEY (id) LABEL company PROPERTIES (id,name)
  )
  EDGE TABLES (
    graph_knows KEY (id) SOURCE KEY (src) REFERENCES graph_person (id)
      DESTINATION KEY (dst) REFERENCES graph_person (id)
      LABEL knows PROPERTIES (id,since),
    graph_works KEY (id) SOURCE KEY (src) REFERENCES graph_person (id)
      DESTINATION KEY (dst) REFERENCES graph_company (id)
      LABEL works PROPERTIES (id)
  );
