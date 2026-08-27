CREATE TABLE IF NOT EXISTS inc_table_1 (
     tinyintType             TINYINT,
     intType                 INT NOT NULL primary key,
     boolType                BOOL,
     smalleintType           SMALLINT,
     mediumintType           MEDIUMINT,
     bigintType              BIGINT,
     floatType               FLOAT,
     doubleType              DOUBLE UNSIGNED,
     decimalType             DECIMAL ZEROFILL,
     numericType             NUMERIC,
     KEY idx_bigint (intType, bigintType),
     KEY idx_bool  (boolType, mediumintType),
     UNIQUE KEY uk_test (doubleType, smalleintType)
) COMMENT='This table check all possible numeric data type';
insert into inc_table_1 values(1,2,true,24,23,5,6,4.5,8,9);
insert into inc_table_1 values(1,6,false,32,55,32,6,7.6,5.6,3);
insert into inc_table_1 values(1,7,true,2,32,8,6,8.4,6.3,2);
insert into inc_table_1 values(1,8,false,5,234,10,6,9.5,7,11);
insert into inc_table_1 values(-128,-2147483648,4,-32768,-8388608,-9223372036854775808,4.7,5.6,2.5,7);
insert into inc_table_1 values(127,2147483647,1,32767,-8388607,9223372036854775807,4.7,5.6,2.5,7);
DROP INDEX idx_bigint on inc_table_1;
CREATE INDEX idx_float_int on inc_table_1 (floatType ASC, intType ASC);
CREATE UNIQUE INDEX uk_newindex on inc_table_1 (bigintType, floatType);

CREATE TABLE IF NOT EXISTS inc_table_2 (
   d1 int primary key,
   d2 double,
   d3 decimal,
   d4 char(30),
   KEY idx_1 (d2,d3),
   KEY idx_2 (d3,d4),
   UNIQUE KEY uk_test(d3,d2)
);

CREATE TABLE IF NOT EXISTS inc_table_3 (
   c1 int primary key,
   c2 varchar(20),
   c3 int,
   c4 int
);
INSERT INTO inc_table_3(c1,c2) values (1,'a'),(2,'b'),(3,'c'),(4,'d'),(5,'e'),(6,'f'),(7,'g');
REPLACE INTO inc_table_3(c1,c2) values (2,'replaced'),(3,'replaced');
UPDATE inc_table_3 set c2 ='Updated',c3=3,c4=4 where c1 =6;
UPDATE inc_table_3 set c2 ='Updated',c4=3 where c1 =7;
DELETE FROM inc_table_3 where c1 = 4;


CREATE TABLE IF NOT EXISTS inc_table_4 (
   t1 year,
   t2 datetime,
   t3 timestamp,
   t4 time,
   t5 date
);
CREATE UNIQUE INDEX uk_year ON inc_table_4(t5);
insert into inc_table_4(t1,t4) values(2014,'10:16:38');
insert into inc_table_4(t1,t3) values(2017,'2017-11-17 10:16:38');
insert into inc_table_4 values('2018','2017-11-20 10:16:38','2017-11-20 10:16:38','10:16:38','2017-11-20');
insert into inc_table_4(t1) values('2019');
update inc_table_4 set t5='2017-11-21',t2='2017-11-15 11:16:38' where t1=2019;
DROP INDEX uk_year ON inc_table_4;


CREATE TABLE IF NOT EXISTS inc_table_empty(
     c1 int
) COMMENT='Empty table to test extra configuration' PCTFREE=6 COMPRESSION='zstd_1.3.8';
ALTER TABLE inc_table_empty set PCTFREE = 8;
ALTER TABLE inc_table_empty SET READ ONLY;

CREATE TABLE IF NOT EXISTS inc_table_ghost (
   ghostId int primary key,
   ghostName varchar(30),
   key i1(ghostName)
);
DROP TABLE inc_table_ghost;


#create table if not exists inc_table_virtual_1(
#  c1 varchar(10) primary key,
#  c2 varchar(8) generated always as (substring(c1, 2)) virtual,
#  c3 int)
#partition by key(c2) partitions 5;
#insert into inc_table_virtual_1 (c1, c3) values('abcde', 10);
#
#
#create table if not exists inc_table_virtual_2(
#  c1 varchar(10) primary key,
#  c2 varchar(8) generated always as (substring(c1, 2)) virtual,
#  c3 int, unique key t31(c3, c1) local)
#partition by key(c2) partitions 5;
#insert into inc_table_virtual_2 (c1, c3) values('bcde', 1);
#insert into inc_table_virtual_2 (c1, c3) values('baaaa', 1);


CREATE TABLE IF NOT EXISTS inc_table_partition_1 (
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
insert into inc_table_partition_1 values (1,'Tom',1.0);
insert into inc_table_partition_1 values (3,'Jerry',1.2);
insert into inc_table_partition_1 values (5,'Nick',1.3);
insert into inc_table_partition_1 values (8,'Mclarent',1.4);
insert into inc_table_partition_1 values (10,'Joe',1.6);
insert into inc_table_partition_1 values (13,'Haffey',2.0);
insert into inc_table_partition_1 values (16,'Nan',2.1);
insert into inc_table_partition_1 values (19,'Hoa',2.3);
insert into inc_table_partition_1 values (20,'Shu',4.0);
insert into inc_table_partition_1 values (25,'Tian',5.0);
insert into inc_table_partition_1 values (27,'Sle',6.0);
ALTER TABLE inc_table_partition_1 ADD PARTITION (PARTITION p5 VALUES LESS THAN (35));
insert into inc_table_partition_1 values (32,'test',7.5);
insert into inc_table_partition_1 values (34,'t5',6.5);
insert into inc_table_partition_1 values (28,'t7',8.5);
ALTER TABLE inc_table_partition_1 DROP PARTITION (p5);
ALTER TABLE inc_table_partition_1 DROP PARTITION (p2);


CREATE TABLE IF NOT EXISTS inc_table_partition_2(
    id2 int,
    name2 varchar(40),
    salary2 float,
    KEY idx_hash_split_id_salary (id2, salary2) LOCAL BLOCK_SIZE = 32768
) COMMENT = 'test hash-split table as well as index on partitions'
PARTITION BY hash(id2) partitions 4;
insert into inc_table_partition_2 values (14,'Hicky',1.5);
insert into inc_table_partition_2 values (21,'Flunky',4.5);
insert into inc_table_partition_2 values (34,'Jack',9.5);


CREATE TABLE IF NOT EXISTS inc_table_partition_3(
    id3 int,
    name3 varchar(40),
    salary3 float,
    KEY idx_key_split_id_salary (id3, salary3) LOCAL
) COMMENT = 'test key-split table as well as index on partitions'
PARTITION BY key(id3) partitions 4;
insert into inc_table_partition_3 values (15,'Key-Split',1.5);
insert into inc_table_partition_3 values (26,'JUNKY',4.5);
insert into inc_table_partition_3 values (37,'GAMKE',9.5);


CREATE TABLE IF NOT EXISTS inc_table_partition_4 (
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
    PARTITION p4 VALUES LESS THAN MAXVALUE
);
insert into inc_table_partition_4 values (2,'Mick',1.0,5);
insert into inc_table_partition_4 values (4,'Euhar',1.2,3);
insert into inc_table_partition_4 values (6,'Dan',1.3,8);
insert into inc_table_partition_4 values (9,'Cham',1.4,6);
insert into inc_table_partition_4 values (11,'Li',1.6,7);
insert into inc_table_partition_4 values (14,'dj',2.0,9);
insert into inc_table_partition_4 values (17,'SH',2.1,15);
insert into inc_table_partition_4 values (20,'djk',2.3,2);
insert into inc_table_partition_4 values (21,'Bark',4.0,3);
insert into inc_table_partition_4 values (26,'BKl',5.0,15);


CREATE TABLE IF NOT EXISTS inc_table_like_1 LIKE inc_table_1;
CREATE TABLE IF NOT EXISTS inc_table_like_2 LIKE inc_table_partition_1;

#
#
#


CREATE TABLE IF NOT EXISTS inc_table_alteration_1(
    col1 int primary key,
    col2 int NOT NULL,
    col3 float

) COMMENT = 'Test adding column';
INSERT INTO inc_table_alteration_1 values(1,2,1.0);
INSERT INTO inc_table_alteration_1 values(2,5,3.0);
INSERT INTO inc_table_alteration_1 values(3,4,2.0);
ALTER TABLE inc_table_alteration_1 add column newColumn int NOT NULL COMMENT 'new column';
INSERT INTO inc_table_alteration_1 values (4,5,6,3);


CREATE TABLE IF NOT EXISTS inc_table_alteration_2(
    col1 int primary key,
    col2 int NOT NULL,
    col3 float

) COMMENT = 'Test dropping column';
INSERT INTO inc_table_alteration_2 values(1,2,1.0);
INSERT INTO inc_table_alteration_2 values(2,5,3.0);
INSERT INTO inc_table_alteration_2 values(3,4,2.0);
ALTER TABLE inc_table_alteration_2 DROP column col2;
INSERT INTO inc_table_alteration_2 values(5,8);


CREATE TABLE IF NOT EXISTS inc_table_alteration_mix(
    col1 int primary key,
    col2 int NOT NULL,
    col3 float

) COMMENT = 'Test miscellaneous operations';
INSERT INTO inc_table_alteration_mix values(1,2,1.0);
INSERT INTO inc_table_alteration_mix values(2,5,3.0);
INSERT INTO inc_table_alteration_mix values(3,4,2.0);
ALTER TABLE inc_table_alteration_mix DROP column col2;
INSERT INTO inc_table_alteration_mix values(5,8);
ALTER TABLE inc_table_alteration_mix add column newColumn char(30) COMMENT 'new column';
ALTER TABLE inc_table_alteration_mix RENAME to inc_table_alteration_3;
INSERT INTO inc_table_alteration_3 values (7,14,'hoho');


CREATE TABLE IF NOT EXISTS inc_table_alteration_4(
  col1 int primary key,
  col2 varchar (20),
  col3 float,
  key i1(col3)
) COMMENT = 'Truncate table is considered as DDL operations';
INSERT INTO inc_table_alteration_4 values (1,'Number 1',1.7);
INSERT INTO inc_table_alteration_4 values (2,'Number 2',2.7);
INSERT INTO inc_table_alteration_4 values (3,'Number 3',3.7);
INSERT INTO inc_table_alteration_4 values (4,'Number 4',3.7);
TRUNCATE TABLE inc_table_alteration_4;
RENAME TABLE inc_table_alteration_4 to inc_table_truncated;


CREATE TABLE IF NOT EXISTS inc_table_increment_1(
    id INT NOT NULL AUTO_INCREMENT,
    name varchar(30)
)  AUTO_INCREMENT=10;
insert into inc_table_increment_1(name) values ('a'),('b'),('c');
alter table inc_table_increment_1 auto_increment=50;
insert into inc_table_increment_1(name) values ('c'),('d'),('e'),('f');
delete from inc_table_increment_1 where id =51;
insert into inc_table_increment_1(name) values ('f'),('g'),('h');
insert into inc_table_increment_1(id,name) values (24,'cutdown');
insert into inc_table_increment_1(name) values('i'),('j');

CREATE TABLE IF NOT EXISTS `!@#$%^&*()_+-=~[]\;',./{}|:"<>?`(
    `,!@#$%^&*()_+-=~[]\;',./{}|:"<>?` INT(11) NOT NULL,
    `,,!@#$%^&*()_+-=~[]\;',./{}|:"<>?` INT(11) NOT NULL,
    `,,,!@#$%^&*()_+-=~[]\;',./{}|:"<>?` INT(11) NOT NULL,
    primary key (`,!@#$%^&*()_+-=~[]\;',./{}|:"<>?`,`,,!@#$%^&*()_+-=~[]\;',./{}|:"<>?`,`,,,!@#$%^&*()_+-=~[]\;',./{}|:"<>?`)
);
insert into `!@#$%^&*()_+-=~[]\;',./{}|:"<>?` values (1,2,3);
insert into `!@#$%^&*()_+-=~[]\;',./{}|:"<>?` values (1,3,4);
insert into `!@#$%^&*()_+-=~[]\;',./{}|:"<>?` values (1,4,5);
delete from `!@#$%^&*()_+-=~[]\;',./{}|:"<>?` where `,,!@#$%^&*()_+-=~[]\;',./{}|:"<>?` = 2;
update `!@#$%^&*()_+-=~[]\;',./{}|:"<>?` set `,,!@#$%^&*()_+-=~[]\;',./{}|:"<>?` = 100 where `,,!@#$%^&*()_+-=~[]\;',./{}|:"<>?` = 3;

CREATE INDEX idx_special_column on `!@#$%^&*()_+-=~[]\;',./{}|:"<>?` (`,,!@#$%^&*()_+-=~[]\;',./{}|:"<>?`, `,,,!@#$%^&*()_+-=~[]\;',./{}|:"<>?`);
