CREATE TABLE IF NOT EXISTS base_table_1 (
     tinyintType           TINYINT,
     intType               INT NOT NULL primary key COMMENT 'commet with \'',
     boolType              BOOL,
     smalleintType         SMALLINT,
     mediumintType         MEDIUMINT,
     bigintType            BIGINT,
     floatType             FLOAT ZEROFILL,
     doubleType            DOUBLE UNSIGNED,
     decimalType           DECIMAL,
     numericType           NUMERIC,
     KEY idx_bigint (intType, bigintType),
     KEY idx_bool  (boolType, mediumintType),
     UNIQUE KEY uk_test (doubleType, smalleintType)
) COMMENT='This table check all possible numeric data type and comment with \'';
insert into base_table_1 values(1,2,true,24,23,5,6,4.5,8,9);
insert into base_table_1 values(1,6,false,32,55,32,6,7.6,5.6,3);
insert into base_table_1 values(1,7,true,2,32,8,6,8.4,6.3,2);
insert into base_table_1 values(1,8,false,5,234,10,6,9.5,7,11);
insert into base_table_1 values(-128,-2147483648,4,-32768,-8388608,-9223372036854775808,4.7,5.6,2.5,7);
insert into base_table_1 values(127,2147483647,1,32767,-8388607,9223372036854775807,4.7,5.6,-2.5,7);
DROP INDEX idx_bigint on base_table_1;
CREATE INDEX idx_float_int on base_table_1 (floatType ASC, intType ASC);
CREATE UNIQUE INDEX uk_newindex on base_table_1 (bigintType, floatType);


CREATE TABLE IF NOT EXISTS base_table_2(
     charType          CHAR(100) primary key ,
     varcharType       VARCHAR(500),
     binaryType        BINARY(9),
     varbinaryType     VARBINARY(9),
     KEY idx_1 (charType, varcharType) STORING(varbinaryType),
     KEY idx_2 (binaryType, varbinaryType)
) COMMENT='This table check all character-based type';
insert into base_table_2 values ('value1','test1test2test3','nonsense0','nonsense1');
insert into base_table_2 values ('value2','see','nonsense2','nonsense3');
ALTER TABLE base_table_2 DROP INDEX idx_1;


CREATE TABLE IF NOT EXISTS base_table_3 (
     dateType              DATE,
     datetimeType	   DATETIME(4),
     timestampType         TIMESTAMP(5),
     timeType              TIME,
     yearType              YEAR primary key,
     KEY idx_year (yearType,dateType),
     KEY idx_timestamp (timestampType, timeType)
) COMMENT='This table checks all date and time based type';
insert into base_table_3 values('1997-05-01','1996-09-11 08:35:59.4517','2012-01-03 07:00:00.00000','453:54:53.00320','1999');
insert into base_table_3 values('1937-11-21','1995-08-10 04:11:45.2537','2013-01-04 06:00:00.00000','353:54:53.00320','2013');
insert into base_table_3 values('2010-07-03','1994-07-13 05:10:12.7867','2010-02-07 05:00:00.00000','253:54:53.00320','2012');
insert into base_table_3 values('1991-04-19','1993-06-12 08:11:53.9967','2001-03-04 08:00:00.00000','323:54:53.00320','2011');
INSERT INTO base_table_3(yearType) values ('2000'),('2001'),('2002'),('2004'),('2003');
REPLACE INTO base_table_3(yearType,dateType) values ('2003','2010-07-03'),('2012','2010-07-08');
UPDATE base_table_3 set timeType ='253:54:53.00320' where yearType = '2013';
DELETE FROM base_table_3 where yearType = '2011';


CREATE TABLE IF NOT EXISTS base_table_empty(
     c1 int
) COMMENT='Empty table to test extra configuration' PCTFREE=6 COMPRESSION='zstd_1.3.8';
alter table base_table_empty SET READ ONLY;


CREATE TABLE IF NOT EXISTS base_table_ghost(
     col1 int,
     col2 VARCHAR(30),
     key i1(col2)
) COMMENT='This table has been wiped out. Should not exist by any chance.';
insert into base_table_ghost values(1,'test1');
insert into base_table_ghost values(2,'test2');
insert into base_table_ghost values(3,'test3');
drop table base_table_ghost;

create table if not exists base_table_virtual_1(
  c1 varchar(10) primary key,
  c2 varchar(8) generated always as (substring(c1, 2)) virtual,
  c3 int)
partition by key(c2) partitions 5;
insert into base_table_virtual_1 (c1, c3) values('abcde', 10);


create table if not exists base_table_virtual_2(
  c1 varchar(10) primary key,
  c2 varchar(8) generated always as (substring(c1, 2)) virtual,
  c3 int, unique key t31(c3, c1) local)
partition by key(c2) partitions 5;
insert into base_table_virtual_2 (c1, c3) values('bcde', 1);
insert into base_table_virtual_2 (c1, c3) values('baaaa', 1);


CREATE TABLE IF NOT EXISTS base_table_partition_1 (
    id int ,
    name varchar(30),
    salary float
) COMMENT='This chunk tests table partition as well as basic operations on it'
PARTITION BY RANGE (id) (
    PARTITION p0 VALUES LESS THAN (6),
    PARTITION p1 VALUES LESS THAN (11),
    PARTITION p2 VALUES LESS THAN (16),
    PARTITION p3 VALUES LESS THAN (21),
    PARTITION p4 VALUES LESS THAN (29)
);
insert into base_table_partition_1 values (1,'Tom',1.0);
insert into base_table_partition_1 values (3,'Jerry',1.2);
insert into base_table_partition_1 values (5,'Nick',1.3);
insert into base_table_partition_1 values (8,'Mclarent',1.4);
insert into base_table_partition_1 values (10,'Joe',1.6);
insert into base_table_partition_1 values (13,'Haffey',2.0);
insert into base_table_partition_1 values (16,'Nan',2.1);
insert into base_table_partition_1 values (19,'Hoa',2.3);
insert into base_table_partition_1 values (20,'Shu',4.0);
insert into base_table_partition_1 values (25,'Tian',5.0);
insert into base_table_partition_1 values (27,'Sle',6.0);
ALTER TABLE base_table_partition_1 ADD PARTITION (PARTITION p5 VALUES LESS THAN (35));
insert into base_table_partition_1 values (32,'test',7.5);
insert into base_table_partition_1 values (34,'t5',6.5);
insert into base_table_partition_1 values (28,'t7',8.5);
ALTER TABLE base_table_partition_1 DROP PARTITION (p5);
ALTER TABLE base_table_partition_1 DROP PARTITION (p2);


CREATE TABLE IF NOT EXISTS base_table_partition_2(
    id2 int,
    name2 varchar(40),
    salary2 float,
    KEY idx_hash_split_id_salary (id2, salary2) LOCAL BLOCK_SIZE = 32768
) COMMENT = 'test hash-split table as well as index on partitions'
PARTITION BY hash(id2) partitions 4;
insert into base_table_partition_2 values (14,'Hicky',1.5);
insert into base_table_partition_2 values (21,'Flunky',4.5);
insert into base_table_partition_2 values (34,'Jack',9.5);


CREATE TABLE IF NOT EXISTS base_table_partition_3(
    id3 int,
    name3 varchar(40),
    salary3 float,
    KEY idx_key_split_id_salary (id3, salary3) LOCAL
) COMMENT = 'test key-split table as well as index on partitions'
PARTITION BY key(id3) partitions 4;
insert into base_table_partition_3 values (15,'Key-Split',1.5);
insert into base_table_partition_3 values (26,'JUNKY',4.5);
insert into base_table_partition_3 values (37,'GAMKE',9.5);
insert into base_table_partition_3(salary3) values(1.5);

CREATE TABLE IF NOT EXISTS base_table_partition_4 (
    id4 int ,
    name4 varchar(30),
    salary4 float,
    type4 int
) COMMENT='This tests subpartition'
PARTITION BY RANGE (id4)
SUBPARTITION BY HASH(type4) SUBPARTITIONS 3
(
    PARTITION p0 VALUES LESS THAN (7),
    PARTITION p1 VALUES LESS THAN (12),
    PARTITION p2 VALUES LESS THAN (17),
    PARTITION p3 VALUES LESS THAN (26),
    PARTITION p4 VALUES LESS THAN (30)
);
insert into base_table_partition_4 values (2,'Mick',1.0,5);
insert into base_table_partition_4 values (4,'Euhar',1.2,3);
insert into base_table_partition_4 values (6,'Dan',1.3,8);
insert into base_table_partition_4 values (9,'Cham',1.4,6);
insert into base_table_partition_4 values (11,'Li',1.6,7);
insert into base_table_partition_4 values (14,'dj',2.0,9);
insert into base_table_partition_4 values (17,'SH',2.1,15);
insert into base_table_partition_4 values (21,'Bark',4.0,3);
insert into base_table_partition_4 values (26,'BKl',5.0,15);
insert into base_table_partition_4(salary4) values (2.1);
alter table base_table_partition_4 add partition (partition p5 values less than (40));
insert into base_table_partition_4(id4) values (37), (38);
delete from base_table_partition_4 where id4=17;
alter table base_table_partition_4 DROP PARTITION (p1);
alter table base_table_partition_4 DROP PARTITION (p0);


CREATE TABLE IF NOT EXISTS base_table_like_1 LIKE base_table_1;
CREATE TABLE IF NOT EXISTS base_table_like_2 LIKE base_table_partition_1;

#
#

CREATE TABLE IF NOT EXISTS base_table_alteration_1(
    col1 int primary key,
    col2 int NOT NULL,
    col3 float
) COMMENT = 'DLL operation - Add new column';
INSERT INTO base_table_alteration_1 values(1,2,1.0);
INSERT INTO base_table_alteration_1 values(2,5,3.0);
INSERT INTO base_table_alteration_1 values(3,4,2.0);
ALTER TABLE base_table_alteration_1 add column newColumn int NOT NULL COMMENT 'new column';
INSERT INTO base_table_alteration_1 values (4,5,6,3);


CREATE TABLE IF NOT EXISTS base_table_alteration_2(
    col1 int primary key,
    col2 int NOT NULL,
    col3 float
) COMMENT = 'DDL operation - Delete existing column';
INSERT INTO base_table_alteration_2 values(1,2,1.0);
INSERT INTO base_table_alteration_2 values(2,5,3.0);
INSERT INTO base_table_alteration_2 values(3,4,2.0);
ALTER TABLE base_table_alteration_2 DROP column col2;
INSERT INTO base_table_alteration_2 values(5,8);


CREATE TABLE IF NOT EXISTS base_table_alteration_mix(
    col1 int primary key,
    col2 int NOT NULL,
    col3 float
) COMMENT = 'DLL operation - Miscellaneous operation';
INSERT INTO base_table_alteration_mix values(1,2,1.0);
INSERT INTO base_table_alteration_mix values(2,5,3.0);
INSERT INTO base_table_alteration_mix values(3,4,2.0);
ALTER TABLE base_table_alteration_mix DROP column col2;
INSERT INTO base_table_alteration_mix values(5,8);
ALTER TABLE base_table_alteration_mix add column newColumn char(30) COMMENT 'new column';
ALTER TABLE base_table_alteration_mix RENAME to base_table_alteration_3;
INSERT INTO base_table_alteration_3 values (7,14,'hoho');
DELETE FROM base_table_alteration_3 where col1=2;


CREATE TABLE IF NOT EXISTS base_table_alteration_4(
  col1 int primary key,
  col2 varchar (20),
  col3 float,
  key i1(col3)
) COMMENT = 'Truncate table is considered as DDL operations';
INSERT INTO base_table_alteration_4 values (1,'Number 1',1.7);
INSERT INTO base_table_alteration_4 values (2,'Number 2',2.7);
INSERT INTO base_table_alteration_4 values (3,'Number 3',3.7);
INSERT INTO base_table_alteration_4 values (4,'Number 4',3.7);
TRUNCATE TABLE base_table_alteration_4;
rename table base_table_alteration_4 to base_table_truncated;

CREATE TABLE IF NOT EXISTS base_table_default_value_with_escape_character(
  c1 varchar(10) default '\'INIT\'',
  c2 char(10) default '\'INIT\'',
  c3 varbinary(10) default '\'INIT\'',
  c4 binary(10) default '\'INIT\''
);

CREATE TABLE IF NOT EXISTS base_table_increment_1(
    id INT NOT NULL AUTO_INCREMENT,
    name varchar(30)
)  AUTO_INCREMENT=10;
insert into base_table_increment_1(name) values ('a'),('b'),('c');
alter table base_table_increment_1 auto_increment=50;
insert into base_table_increment_1(name) values ('c'),('d'),('e'),('f');
delete from base_table_increment_1 where id =51;
insert into base_table_increment_1(name) values ('f'),('g'),('h');
insert into base_table_increment_1(id,name) values (24,'cutdown');
insert into base_table_increment_1(name) values('i'),('j');

CREATE TABLE IF NOT EXISTS base_table_with_primary_zone(
    id INT NOT NULL AUTO_INCREMENT
) PRIMARY_ZONE = 'z1';

create user base_user_for_table_rename IDENTIFIED BY PASSWORD '*1975d095ac033caf4e1bf94f7202a9bbfeeb66f1';
CREATE TABLE IF NOT EXISTS base_table_before_rename(
    id INT NOT NULL AUTO_INCREMENT
) PRIMARY_ZONE = 'z1';
eval GRANT ALTER ON `$dataDB`.`base_table_before_rename` TO 'base_user_for_table_rename';
RENAME TABLE base_table_before_rename to base_table_after_rename;

CREATE TABLE IF NOT EXISTS base_table_with_index_1(grp_id bigint, row_id bigint, row_hex varchar(2048), trx_grp bigint,
v1 varchar(65536), v1_check bigint, v2 varchar(65536), v2_check bigint, r1 int, r2 int, glike varchar(4096),
c1 bigint, c2 bigint, c3 bigint, c4 bigint, c5 bigint, c6 tinyint, c7 bigint, c8 bigint, c9 bigint, c10 bigint, v3 varchar(1024),
ts timestamp(6) DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6), t1 timestamp(6) null, t2 datetime(6),
dec1 decimal(16,4), dec2 decimal(16,4), dec3 decimal(16,6),
b1 boolean, d1 bigint default 0, ac1 bigint auto_increment, gmt_create timestamp(6) default now(6), gmt_modified timestamp(6) default now(6),
primary key(grp_id, row_id, row_hex),
index base_table_with_index_idx_1(glike,row_id, v1_check, ac1, t1) LOCAL,
unique index base_table_with_index_uniq_1(grp_id,row_id, v1_check, t1, ac1) LOCAL )
PARTITION by RANGE (row_id)
SUBPARTITION BY HASH(grp_id)
SUBPARTITIONS 2 (
PARTITION p0 VALUES LESS THAN (1000000),
PARTITION p1 VALUES LESS THAN (10000000),
PARTITION p2 VALUES LESS THAN MAXVALUE
);

CREATE TABLE IF NOT EXISTS base_table_with_index_2(grp_id bigint, row_id bigint, row_hex varchar(2048), trx_grp bigint,
v1 varchar(65536), v1_check bigint, v2 varchar(65536), v2_check bigint, r1 int, r2 int, glike varchar(4096),
c1 bigint, c2 bigint, c3 bigint, c4 bigint, c5 bigint, c6 tinyint, c7 bigint, c8 bigint, c9 bigint, c10 bigint, v3 varchar(1024),
ts timestamp(6) DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6), t1 timestamp(6) null, t2 datetime(6),
dec1 decimal(16,4), dec2 decimal(16,4), dec3 decimal(16,6),
b1 boolean, d1 bigint default 0, ac1 bigint auto_increment, gmt_create timestamp(6) default now(6), gmt_modified timestamp(6) default now(6),
primary key(grp_id, row_id, row_hex),
index base_table_with_index_idx_2(glike,row_id, v1_check, ac1, t1) LOCAL,
unique index base_table_with_index_uniq_2(grp_id,row_id, v1_check, t1, ac1) LOCAL )
PARTITION by RANGE (row_id) (
PARTITION p0 VALUES LESS THAN (1000000),
PARTITION p1 VALUES LESS THAN (10000000),
PARTITION p2 VALUES LESS THAN MAXVALUE
);

CREATE TABLE IF NOT EXISTS base_table_with_constraint(
payment_id varchar(32) primary key,
apply_id varchar(32),
comment varchar(32),
partition_id varchar(2) GENERATED ALWAYS AS (substr(payment_id,14,2)),
constraint cst check (partition_id=substr(apply_id,14, 2)),
unique index i1 (apply_id) local)
partition by list columns (partition_id)
(
partition p0 values in ('00'),
partition p1 values in ('01'),
partition p2 values in ('02')
);

CREATE TABLE IF NOT EXISTS base_table_with_prefix_idx(
payment_id int primary key,
my_comment_1 varchar(32),
my_comment_2 varchar(32),
index idx(my_comment_1(10)));
CREATE INDEX prefix_idx on base_table_with_prefix_idx(my_comment_2(10));

CREATE TABLE IF NOT EXISTS base_table_with_fulltext_idx(
pk int primary key,
a int,
b varchar(100),
c varchar(100),
fulltext index full_text1(b,c) ctxcat(b));
CREATE fulltext INDEX full_text2 on base_table_with_fulltext_idx(b,c);

CREATE TABLE IF NOT EXISTS base_table_with_global_idx(
pk int primary key,
a int
)
partition by hash(pk) partitions 8
;
CREATE INDEX glb_idx_hash on base_table_with_global_idx(a) global partition by HASH(a) partitions 4;
