CREATE TABLE IF NOT EXISTS inc_table_1 (
  tinyintType   TINYINT,
  smallIntType  SMALLINT,
  mediumIntType MEDIUMINT,
  intType       INT NOT NULL PRIMARY KEY,
  bigIntType    BIGINT,
  boolType      BOOL,
  floatType     FLOAT,
  doubleType    DOUBLE UNSIGNED,
  decimalType   DECIMAL ZEROFILL,
  numericType   NUMERIC,
  KEY idx_bigint (bigIntType),
  UNIQUE KEY uk_test (doubleType, smallIntType)
)
  COMMENT = 'This table check all possible numeric data type';
INSERT INTO inc_table_1 VALUES (-3, -32768, 12345, 50, 512341234123412, TRUE, 6.123, 4564.1231235, 123345, 93453);
INSERT INTO inc_table_1 VALUES (-2, -16384, 12345, 60, -1234645636, FALSE, 4123.12, 697891.566732, 34564576, -1230);
INSERT INTO inc_table_1 VALUES (-1, -8192, 12345, -1, 0, TRUE, 2341.657324, 212.345215, 678677, 1233);
INSERT INTO inc_table_1 VALUES (0, -4096, 12345, 0, -1, TRUE, 980.345, 12123, 8123, 0);
INSERT INTO inc_table_1
VALUES (1, -2048, 12345, 2, 90123445667887587, TRUE, 12.21, 3456.0000, 812310, 3425);
INSERT INTO inc_table_1 VALUES (2, -1024, 12345, -100, -9233372036854775, TRUE, -12392, 0.54567850000, 0, 75842);
INSERT INTO inc_table_1 VALUES (3, 0, 12345, 1234, 12356743256, TRUE, 0, 23456, 123412, 103098234);
CREATE INDEX idx_decimal_numeric on inc_table_1 (decimalType, numericType);

CREATE TABLE IF NOT EXISTS inc_table_2 (
  yearType      YEAR,
  dateType      DATE,
  timeType      TIME,
  timestampType TIMESTAMP,
  datetimeType  DATETIME,
  UNIQUE KEY idx_date (dateType)
)
  COMMENT = 'This table check all possible time and time data type';

INSERT INTO inc_table_2 VALUES ('2018', '2018-02-08', '11:50:38', '2018-02-08 11:50:38', '2018-02-08 11:50:38.123456');
DROP INDEX idx_bigint on inc_table_1;

UPDATE inc_table_1 SET mediumIntType = 10000 WHERE intType = 100;
UPDATE inc_table_1 SET mediumIntType = 10000 WHERE intType = 60;
INSERT INTO inc_table_1
VALUES (-128, -32768, -8388608, -2147483648, -9223372036854775808, TRUE, -3.402823466E+38, 0, 123345, 9345);
INSERT INTO inc_table_2 VALUES ('1970', '1970-01-01', '00:00:00', '1970-01-01 00:00:00', '1970-01-01 00:00:00.000000');
CREATE INDEX idx_float_int on inc_table_1 (floatType ASC, intType ASC);
INSERT INTO inc_table_2 VALUES ('1971', '1970-01-02', '00:00:01', '1970-01-01 00:00:01', '1970-01-01 00:00:00.000001');
INSERT INTO inc_table_2 VALUES ('2071', '1999-01-02', '00:00:01', '2000-01-01 00:00:01', '1970-01-01 00:00:01.100001');
INSERT INTO inc_table_1 VALUES
  (127, 32767, 8388607, 2147483647, 9223372036854775807, FALSE, 3.402823466E+38, 2.2250738585072014E-308, 123345,
   934534523);

CREATE TABLE IF NOT EXISTS inc_table_3 (
  charType    CHAR(20),
  varcharType VARCHAR(100),
  UNIQUE KEY idx_char (charType)
)
  COMMENT = 'This table check all possible string data type, (supported by current ob version 1.4.6)';

INSERT INTO inc_table_3 values ('ads','a123123');
INSERT INTO inc_table_3 values ('asdfasdf','yu567aer');
INSERT INTO inc_table_3 values ('azcvqwetqweds','45634634a');
INSERT INTO inc_table_3 values ('fgfgxcvb3434','vnbqwwqeadfghdfh');
INSERT INTO inc_table_3 values ('dsd1213','xcbtya');
INSERT INTO inc_table_3 values ('zbxcxb','sasdfa1234');
INSERT INTO inc_table_3 values ('zcxxbcv','asdfxcv');
INSERT INTO inc_table_3 values ('lol78;/X"X"X',null);
INSERT INTO inc_table_3 values (null,null);


CREATE TABLE IF NOT EXISTS inc_table_4 (
  col1 VARCHAR(100),
  col2 VARCHAR(100)
)
  COMMENT = 'This table check no primary key and unique key column';

INSERT into inc_table_4 VALUES ('12345678','87654321');
INSERT into inc_table_4 VALUES ('12345678','87654321');
INSERT into inc_table_4 VALUES ('12345678','87654321');
INSERT into inc_table_4 VALUES ('12345678','87654321');
INSERT into inc_table_4 VALUES ('12345678','87654321');
INSERT into inc_table_4 VALUES ('12345678','87654321');
INSERT into inc_table_4 VALUES ('12345678','87654321');
INSERT into inc_table_4 VALUES ('12345678','87654321');
INSERT into inc_table_4 VALUES ('12345678','87654321');
INSERT into inc_table_4 VALUES ('12345','00000');
INSERT into inc_table_4 VALUES ('12345','00000');
INSERT into inc_table_4 VALUES ('12345','00000');
INSERT into inc_table_4 VALUES ('12345','00000');
INSERT into inc_table_4 VALUES ('12345','00000');
INSERT into inc_table_4 VALUES ('12345','00000');
DELETE from inc_table_4 where col2 = '87654321' LIMIT 1;
DELETE from inc_table_4 where col2 = '00000' LIMIT 1;

CREATE TABLE inc_table_5 (
    col1 int not null,
    col2 int not null,
    generate_column int GENERATED ALWAYS AS (col1 * col2) ,
    unique key generate_index1(`generate_column`),
    key generate_index2(`generate_column`)
);

INSERT INTO `backup_data_db`.`inc_table_5` (`col1`,`col2`) VALUES (1,5);
INSERT INTO `backup_data_db`.`inc_table_5` (`col1`,`col2`) VALUES (2,6);
INSERT INTO `backup_data_db`.`inc_table_5` (`col1`,`col2`) VALUES (3,7);

UPDATE `inc_table_5` SET `col2` = 10 WHERE `generate_column` = 5;
