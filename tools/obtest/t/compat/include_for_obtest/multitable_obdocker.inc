connection tentsql_conn;
--disable_all_result_log
--error 0,1007
create database mutitable;
use mutitable;
eval CREATE TABLE tbl2_1(id INT,name VARCHAR(20));
let $cnt=0;
while($cnt <= 50)
{
 eval INSERT INTO tbl2_1 (id, name) VALUES ($cnt, 'Alice');
 inc $cnt;
}
eval CREATE INDEX tbl2_idx1_1 ON tbl2_1(id);
let $cnt=100;
while($cnt <= 150)
{
 eval INSERT INTO tbl2_1 (id, name) VALUES ($cnt, 'Alice');
 inc $cnt;
}
eval CREATE TABLE tbl3_1(id INT,name VARCHAR(20));
eval ALTER TABLE tbl3_1 ADD INDEX tbl3_idx1_1(id);
eval CREATE TABLE sale_1(date_col date, sales_col int, name_col varchar(10));
let $cnt=0;
while($cnt <= 50)
{
 eval INSERT INTO sale_1 (date_col, sales_col, name_col) VALUES (NOW(), $cnt, '$cnt');
 inc $cnt;
}
eval CREATE INDEX idx1_1 ON sale_1((month(date_col)));
let $cnt=100;
while($cnt <= 150)
{
 eval INSERT INTO sale_1 (date_col, sales_col, name_col) VALUES (NOW(), $cnt, '$cnt');
 inc $cnt;
}
eval CREATE TABLE tbl1_1(id INT,name VARCHAR(18),date DATE,PRIMARY KEY (id),INDEX tbl1_idx1_1 (date));
let $cnt=0;
while($cnt <= 50)
{
 eval INSERT INTO tbl1_1 (id, name, date) VALUES ($cnt, '$cnt', NOW());
 inc $cnt;
}
eval CREATE TABLE tbl1_l_1 (col1 BIGINT PRIMARY KEY,col2 VARCHAR(50)) PARTITION BY LIST(col1) (PARTITION p0 VALUES IN (1, 2, 3), PARTITION p1 VALUES IN (5, 6), PARTITION p2 VALUES IN (DEFAULT) );
let $cnt=0;
while($cnt <= 50)
{
 eval INSERT INTO tbl1_l_1(col1,col2) values ($cnt,'test_$cnt');
 inc $cnt;
}
eval CREATE TABLE tbl1_lc_1 (id INT,partition_id VARCHAR(2)) PARTITION BY LIST COLUMNS(partition_id) (PARTITION p0 VALUES IN ('00','01'), PARTITION p1 VALUES IN ('02','03'), PARTITION p2 VALUES IN (DEFAULT) );
let $cnt=0;
while($cnt <= 50)
{
 eval INSERT INTO tbl1_lc_1(id,partition_id) values (1,'00');
 eval INSERT INTO tbl1_lc_1(id,partition_id) values (2,'01');
 eval INSERT INTO tbl1_lc_1(id,partition_id) values (3,'02');
 eval INSERT INTO tbl1_lc_1(id,partition_id) values (4,'03');
 eval INSERT INTO tbl1_lc_1(id,partition_id) values (5,'04');
 eval INSERT INTO tbl1_lc_1(id,partition_id) values (6,'05');
 eval INSERT INTO tbl1_lc_1(id,partition_id) values (7,'06');
 eval INSERT INTO tbl1_lc_1(id,partition_id) values (8,'07');
 eval INSERT INTO tbl1_lc_1(id,partition_id) values (9,'08');
 inc $cnt;
}
eval CREATE TABLE tbl1_h_1(col1 INT,col2 VARCHAR(50)) PARTITION BY HASH(col1) PARTITIONS 60;
let $cnt=0;
while($cnt <= 600)
{
 eval INSERT INTO tbl1_h_1 (col1,col2) VALUES ($cnt, '$cnt');
 inc $cnt;
}
eval CREATE TABLE tbl1_k_1(id INT,gmt_create DATETIME,info VARCHAR(20)) PARTITION BY KEY(id,gmt_create) PARTITIONS 10;
let $cnt=0;
while($cnt <= 50)
{
 eval INSERT INTO tbl1_k_1(id, gmt_create, info) VALUES ($cnt,NOW(),'$cnt');
 inc $cnt;
}
eval CREATE TABLE tbl_rr_1(col1 INT,col2 INT,col3 INT) PARTITION BY RANGE(col1) SUBPARTITION BY RANGE(col2) SUBPARTITION TEMPLATE (SUBPARTITION mp1 VALUES LESS THAN(100), SUBPARTITION mp2 VALUES LESS THAN(200), SUBPARTITION mp3 VALUES LESS THAN(300) ) (PARTITION p0 VALUES LESS THAN(100), PARTITION p1 VALUES LESS THAN(200), PARTITION p2 VALUES LESS THAN(300) );
let $cnt=0;
while($cnt < 300)
{
 eval INSERT INTO tbl_rr_1(col1,col2,col3)  VALUES ($cnt,$cnt,$cnt);
 inc $cnt;
}
eval CREATE TABLE t2_m_rcr_1 (col1 INT NOT NULL,col2 varchar(50),col3 INT NOT NULL) PARTITION BY RANGE COLUMNS(col1) SUBPARTITION BY RANGE(col3) SUBPARTITION TEMPLATE (SUBPARTITION mp0 VALUES LESS THAN(1000), SUBPARTITION mp1 VALUES LESS THAN(2000), SUBPARTITION mp2 VALUES LESS THAN(3000) ) (PARTITION p0 VALUES LESS THAN(100), PARTITION p1 VALUES LESS THAN(200), PARTITION p2 VALUES LESS THAN(300) );
let $cnt=0;
while($cnt < 300)
{
 eval INSERT INTO t2_m_rcr_1 (col1,col2,col3)  VALUES ($cnt,$cnt,$cnt);
 inc $cnt;
}
eval CREATE TABLE t_m_rclc_1(col1 INT,col2 INT) PARTITION BY RANGE COLUMNS(col1) SUBPARTITION BY LIST COLUMNS(col2) SUBPARTITION TEMPLATE (SUBPARTITION mp0 VALUES IN(1,3), SUBPARTITION mp1 VALUES IN(4,6), SUBPARTITION mp2 VALUES IN(7) ) (PARTITION p0 VALUES LESS THAN(100), PARTITION p1 VALUES LESS THAN(200), PARTITION p2 VALUES LESS THAN(300) );
let $cnt=0;
while($cnt < 300)
{
 eval INSERT INTO t_m_rclc_1(col1,col2) VALUES ($cnt,1);
 eval INSERT INTO t_m_rclc_1(col1,col2) VALUES ($cnt,3);
 eval INSERT INTO t_m_rclc_1(col1,col2) VALUES ($cnt,4);
 eval INSERT INTO t_m_rclc_1(col1,col2) VALUES ($cnt,6);
 eval INSERT INTO t_m_rclc_1(col1,col2) VALUES ($cnt,7);
 inc $cnt;
}
eval CREATE TABLE t2_m_rch_1(col1 INT,col2 INT) PARTITION BY RANGE COLUMNS(col1) SUBPARTITION BY HASH(col2) SUBPARTITIONS 5 (PARTITION p0 VALUES LESS THAN(100), PARTITION p1 VALUES LESS THAN(200), PARTITION p2 VALUES LESS THAN(300) );
let $cnt=0;
while($cnt < 300)
{
 eval INSERT INTO t2_m_rch_1(col1,col2) VALUES ($cnt,$cnt);
 inc $cnt;
}
eval CREATE TABLE tbl2_m_klc_1(col1 INT,col2 INT,col3 INT) PARTITION BY KEY(col1) SUBPARTITION BY LIST COLUMNS(col2) SUBPARTITION TEMPLATE (SUBPARTITION p0 VALUES IN(1,3), SUBPARTITION p1 VALUES IN(4,6), SUBPARTITION p2 VALUES IN(7,9) ) PARTITIONS 5;
let $cnt=1;
while($cnt <= 500)
{
 eval INSERT INTO tbl2_m_klc_1(col1,col2,col3)  VALUES ($cnt,1,$cnt);
 eval INSERT INTO tbl2_m_klc_1(col1,col2,col3)  VALUES ($cnt,3,$cnt);
 eval INSERT INTO tbl2_m_klc_1(col1,col2,col3)  VALUES ($cnt,4,$cnt); 
 eval INSERT INTO tbl2_m_klc_1(col1,col2,col3)  VALUES ($cnt,6,$cnt);
 eval INSERT INTO tbl2_m_klc_1(col1,col2,col3)  VALUES ($cnt,7,$cnt);
 eval INSERT INTO tbl2_m_klc_1(col1,col2,col3)  VALUES ($cnt,9,$cnt);
 inc $cnt;
}
eval CREATE TABLE t2_f_rr_1(col1 INT,col2 TIMESTAMP) PARTITION BY RANGE(col1) SUBPARTITION BY RANGE(UNIX_TIMESTAMP(col2)) (PARTITION p0 VALUES LESS THAN(100) (SUBPARTITION sp0 VALUES LESS THAN(UNIX_TIMESTAMP('2021/04/01')), SUBPARTITION sp1 VALUES LESS THAN(UNIX_TIMESTAMP('2021/07/01')), SUBPARTITION sp2 VALUES LESS THAN(UNIX_TIMESTAMP('2021/10/01')), SUBPARTITION sp3 VALUES LESS THAN(UNIX_TIMESTAMP('2022/01/01')) ), PARTITION p1 VALUES LESS THAN(200) (SUBPARTITION sp4 VALUES LESS THAN(UNIX_TIMESTAMP('2021/04/01')), SUBPARTITION sp5 VALUES LESS THAN(UNIX_TIMESTAMP('2021/07/01')), SUBPARTITION sp6 VALUES LESS THAN(UNIX_TIMESTAMP('2021/10/01')), SUBPARTITION sp7 VALUES LESS THAN(UNIX_TIMESTAMP('2022/01/01')) ) );
let $cnt=0;
while($cnt <= 50)
{
 eval INSERT INTO t2_f_rr_1 (col1, col2) VALUES ($cnt, '2021-02-15 10:30:45'),($cnt, '2021-05-20 14:23:45'),($cnt, '2021-08-10 08:15:30'),($cnt, '2021-11-05 16:45:00'),($cnt, '2021-12-25 20:00:00');
 inc $cnt;
}
eval CREATE TABLE t2_f_hk_1 (col1 INT,col2 INT) PARTITION BY HASH(col1) SUBPARTITION BY KEY(col2) (PARTITION p1 (SUBPARTITION sp0 ,SUBPARTITION sp1 ,SUBPARTITION sp2 ,SUBPARTITION sp3 ), PARTITION p2 (SUBPARTITION sp4 ,SUBPARTITION sp5 ,SUBPARTITION sp6 ,SUBPARTITION sp7 ) );
let $cnt=0;
while($cnt <= 50)
{
 eval INSERT INTO t2_f_hk_1(col1,col2)  VALUES ($cnt,$cnt);
 inc $cnt;
}
eval CREATE TABLE t_1(c1 TEXT(30), c2 TEXT(300), c3 TEXT(30000), c4 TEXT(10000000));
let $cnt=0;
while($cnt <= 50)
{
 eval INSERT INTO t_1 (c1, c2, c3, c4) VALUES ('short', 'medium length text', REPEAT('a', 30000), REPEAT('b', 10000000)),('example', 'another medium text', REPEAT('c', 30000), REPEAT('d', 10000000)),('last', 'final medium text', REPEAT('e', 30000), REPEAT('f', 10000000));
 inc $cnt;
}
eval CREATE TABLE tbl1_g_1(g GEOMETRY NOT NULL SRID 0);
let $cnt=0;
while($cnt <= 50)
{
 eval INSERT INTO tbl1_g_1 (g) VALUES (ST_GeomFromText('POINT(10 20)', 0));
 eval INSERT INTO tbl1_g_1 (g) VALUES (ST_GeomFromText('LINESTRING(10 10, 20 20, 30 40)', 0));
 eval INSERT INTO tbl1_g_1 (g) VALUES (ST_GeomFromText('POLYGON((0 0, 0 10, 10 10, 10 0, 0 0))', 0));
 eval INSERT INTO tbl1_g_1 (g) VALUES (ST_GeomFromText('MULTIPOINT((10 20), (30 40), (50 60))', 0));
 inc $cnt;
}
eval CREATE TABLE tbl1txt_1 (id INT PRIMARY KEY, docs JSON NOT NULL, docs1 JSON);
let $cnt=0;
while($cnt <= 50)
{
 eval INSERT INTO tbl1txt_1 (id, docs, docs1) VALUES  ($cnt, JSON_OBJECT('name', 'name_$cnt', 'age', 30), JSON_ARRAY('red', 'green', 'blue'));
 inc $cnt;
}
eval CREATE TABLE mutidata1_1 (c1 DECIMAL(8,2),c2 SMALLINT,c3 DECIMAL,c4 BIT(4),c5 MEDIUMINT(20),c6 DATETIME(6),c7 TIMESTAMP(5),col1 INT PRIMARY KEY);
let $cnt=10;
while($cnt <= 29)
{
 eval INSERT INTO mutidata1_1 (c1, c2, c3, c4, c5, c6, c7, col1) VALUES  (1234.$cnt, 32767, 9876543210, b'1010', 8388607, '2023-10-01 12:34:56.123456', '2023-10-01 12:34:56.12345', $cnt);
 inc $cnt;
}
eval CREATE TABLE mutidata2_1 (c1 YEAR(4),c2 VARBINARY(5),c3 TINYBLOB,c4 BLOB(5),c5 MEDIUMBLOB,c6 TINYTEXT,c7 TEXT ) LOB_INROW_THRESHOLD = 8192;
let $cnt=10;
while($cnt <= 99)
{
 eval INSERT INTO mutidata2_1 (c1, c2, c3, c4, c5, c6, c7) VALUES  (2023, UNHEX('4A6F686E'), 'Hello', 'World', 'This is a medium blob example.', 'Short text', 'This is a longer text example.'), (2024, UNHEX('4D617279'), 'Hi', 'Blob', 'Another medium blob example.', 'Tiny text', 'This is another longer text example.');
 inc $cnt;
}
eval CREATE TABLE mutidata3_1 (c1 ENUM('例子1','例子2','例子3') CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci);
let $cnt=0;
while($cnt <= 99)
{
 eval INSERT INTO mutidata3_1 (c1) VALUES  ('例子1'),  ('例子2'),  ('例子3');
 inc $cnt;
}
eval CREATE TABLE mutidata4_1 (id INT AUTO_INCREMENT PRIMARY KEY, name VARCHAR(100) NOT NULL,hobbies SET('足球', '篮球', '阅读', '旅行', '音乐') CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci);
let $cnt=0;
while($cnt <= 99)
{
 eval INSERT INTO mutidata4_1 (name, hobbies) VALUES  ('张三', '足球,篮球'),  ('李四', '阅读,旅行'), ('王五', '音乐'),  ('赵六', '足球,音乐,旅行'),  ('孙七', '');
 inc $cnt;
}
eval CREATE TABLE mutidata5_1 (c1 TINYBLOB,c2 BLOB,c3 MEDIUMBLOB,c4 LONGBLOB,c5 TINYTEXT,c6 TEXT(65535),c7 MEDIUMTEXT,c8 LONGTEXT) LOB_INROW_THRESHOLD = 8192;
let $cnt=0;
while($cnt <= 99)
{
  eval INSERT INTO mutidata5_1 (c1, c2, c3, c4, c5, c6, c7, c8) VALUES  ('TINYBLOB data', 'BLOB data with more content.', 'MEDIUMBLOB data with even more content to test larger size.', 'LONGBLOB data for testing very large binary objects.', 'Short text', 'This is a longer text example for TEXT field.', 'This is a MEDIUMTEXT example with much more content to test larger text fields.', 'This is a LONGTEXT example for testing extremely large text fields.');
 eval INSERT INTO mutidata5_1 (c1, c2, c3, c4, c5, c6, c7, c8) VALUES  ('TinyBlob2', 'Blob2 with more data.', 'MediumBlob2 with extended content.', 'LongBlob2 for testing large objects.', 'TinyText2', 'Text2 with extended length.', 'MediumText2 with even more content.', 'LongText2 for testing extreme length.');
 eval INSERT INTO mutidata5_1 (c1, c2, c3, c4, c5, c6, c7, c8) VALUES  ('TinyBlob3', 'Blob3 with additional data.', 'MediumBlob3 with more content.', 'LongBlob3 for testing large objects.', 'TinyText3', 'Text3 with extended length.', 'MediumText3 with even more content.', 'LongText3 for testing extreme length.');
 eval INSERT INTO mutidata5_1 (c1, c2, c3, c4, c5, c6, c7, c8) VALUES  ('TinyBlob4', 'Blob4 with extra data.', 'MediumBlob4 with extended content.', 'LongBlob4 for testing large objects.', 'TinyText4', 'Text4 with extended length.', 'MediumText4 with even more content.', 'LongText4 for testing extreme length.');
 eval INSERT INTO mutidata5_1 (c1, c2, c3, c4, c5, c6, c7, c8) VALUES  ('TinyBlob5', 'Blob5 with more data.', 'MediumBlob5 with extended content.', 'LongBlob5 for testing large objects.', 'TinyText5', 'Text5 with extended length.', 'MediumText5 with even more content.', 'LongText5 for testing extreme length.');
 inc $cnt;
}
eval CREATE TABLE mutidata6_1 (id INT PRIMARY KEY, docs JSON NOT NULL, docs1 JSON);
let $cnt=1;
while($cnt <= 199)
{
  eval INSERT INTO mutidata6_1 (id, docs, docs1) VALUES ($cnt,JSON_OBJECT('name', 'Alice', 'age', $cnt, 'city', 'New York'),JSON_OBJECT('hobbies', JSON_ARRAY('reading', 'traveling')) );
  inc $cnt;
}
eval CREATE TABLE mutidata7_1 (id INT UNSIGNED PRIMARY KEY AUTO_INCREMENT COMMENT '主键', json_info JSON COMMENT 'JSON 数据', json_id INT GENERATED ALWAYS AS (json_info -> '\$.id') COMMENT 'JSON 数据的虚拟字段', json_name VARCHAR(50) GENERATED ALWAYS AS (json_info -> '\$.NAME'), index json_info_id_idx (json_id) ) COMMENT 'json 示例表';
let $cnt=1;
while($cnt <= 199)
{
  eval INSERT INTO mutidata7_1 (json_info) VALUES (JSON_OBJECT('id', 101, 'NAME', 'Alice', 'age', 25) );
  inc $cnt;
}
--error 1064,0
eval CREATE TABLE mutidata8_1 (c1 ARRAY(INT),c2 ARRAY(VARCHAR(256)),c3 ARRAY(BIGINT),c4 ARRAY(TINYINT),c5 ARRAY(FLOAT),c6 ARRAY(DOUBLE),c7 ARRAY(SMALLINT));
let $cnt=0;
while($cnt <= 199)
{
  --error 1146,0
  INSERT INTO mutidata8_1 (c1, c2, c3, c4, c5, c6, c7) VALUES ('[1, 2, 3]','["Alice", "Bob", "Charlie"]','[1000000000, 2000000000]','[1, 0, 1]','[1.1, 2.2, 3.3]','[1.11, 2.22, 3.33]','[100, 200, 300]');
  inc $cnt;
}
--error 1064,0
eval CREATE TABLE mutidata9_1 (c1 ARRAY(ARRAY(ARRAY(ARRAY(ARRAY(ARRAY(INT)))))));
let $cnt=1;
while($cnt <= 199)
{
  --error 1146,0,1064
  eval INSERT INTO mutidata9_1 (c1) VALUES ([[[[[[1, 2], [3, 4]], [[5, 6], [7, 8]]], [[[9, 10], [11, 12]], [[13, 14], [15, 16]]]]]]);
  inc $cnt;
}
eval CREATE TABLE mutidata10_1 (c1 int,c2 int);
let $cnt=0;
while($cnt <= 500)
{
 eval  INSERT INTO mutidata10_1 (c1, c2) VALUES ($cnt,$cnt);
 inc $cnt;
}
eval CREATE TABLE mutidata10_copy_1 AS SELECT * FROM mutidata10_1;
--error 1235,0
eval CREATE TABLE mutidata11_1 (col1 INT PRIMARY KEY, col2 VARCHAR(50));
--error 1064,0
eval CREATE TABLE mutidata12_1 (col1 INT PRIMARY KEY, col2 VARCHAR(50));
--error 1064,0
eval CREATE TABLE mutidata13_1 (col1 INT PRIMARY KEY, col2 VARCHAR(50));
eval CREATE TABLE mutidata14_1 (col1 INT SKIP_INDEX(MIN_MAX, SUM),col2 FLOAT SKIP_INDEX(MIN_MAX),col3 VARCHAR(1024) SKIP_INDEX(MIN_MAX),col4 CHAR(10));
let $cnt=0;
while($cnt <= 199)
{
  eval INSERT INTO mutidata14_1 (col1, col2, col3, col4)  VALUES (1, 3.14,'这是一个测试字符串','固定长度');
  inc $cnt;
}
eval CREATE TABLE mutidata15_1 (c1 int not null auto_increment);
let $cnt=0;
while($cnt <= 199)
{
  eval INSERT INTO mutidata15_1 VALUES ();
  eval INSERT INTO mutidata15_1 (c1) VALUES (NULL);
  eval INSERT INTO mutidata15_1 (c1) VALUES ($cnt);
  inc $cnt;
}
eval CREATE TABLE mutidata16_1 (col1 INT CHECK(col1>0),col2 INT);
let $cnt=0;
while($cnt <= 199)
{
  eval INSERT INTO mutidata16_1 (col1, col2) VALUES (10, 20);
  --error 3819,0
  eval INSERT INTO mutidata16_1 (col1, col2) VALUES (-5, 20);
  inc $cnt;
}
eval CREATE TABLE mutidata17_1 (col1 INT,col2 INT,FOREIGN KEY(col2) REFERENCES mutidata1_1(col1) ON UPDATE CASCADE ON DELETE RESTRICT);
let $cnt=0;
INSERT INTO mutidata1_1 (col1) VALUES (1), (2), (3);
while($cnt <= 199)
{
  eval INSERT INTO mutidata17_1 (col1, col2) VALUES (10, 1);
  eval INSERT INTO mutidata17_1 (col1, col2) VALUES (20, 2);
  eval INSERT INTO mutidata17_1 (col1, col2) VALUES (30, 3);
  --error 1452,0
  eval INSERT INTO mutidata17_1 (col1, col2) VALUES (40, 4);
  inc $cnt;
}
--enable_all_result_log
