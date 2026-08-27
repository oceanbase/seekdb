#############
# 参数设置：
# tablenum:       创建的table数量,默认是1
# offset:         第几号表开始创建
# partnum:        分区数；默认是1
############

if ($tablenum == null)
{
	let $tablenum = 1;
}

if($offset == null)
{
	let $offset = 0;
}

if($partnum == null)
{
	let $partnum = 1;
}

--disable_query_log
--disable_result_log
###########
# drop if exist
###########
let $tnum = $tablenum;
while($tnum > 0)
{
	let $id = math($offset + $tnum);
	eval drop table if exists obtest$id;
	dec $tnum;
}

####################
# create if not exist
##################
let $tnum = $tablenum;
while($tnum >0)
{
	let $id = math($offset + $tnum);
	let $tablename=obtest$id;

	eval create table if not exists $tablename (i1 int,i2 int, v3 varchar(80), v4 char(20),i5 float, d6 datetime,i7 decimal(5,3),i8 bool, primary key(i1,v3,d6)) PARTITION BY HASH(i1) PARTITIONS $partnum;

	eval replace into $tablename(i1,i2,v3,v4,i5,d6,i7,i8) values (0, 0, '0_obtest', '0_obtest', 0.001, USEC_TO_TIME(0), 0, 0);
	eval replace into $tablename(i1,i2,v3,v4,i5,d6,i7,i8) values (1, 1, '1_obtest', '1_obtest', 1.001,USEC_TO_TIME(1), 1, 0);
	eval replace into $tablename(i1,i2,v3,v4,i5,d6,i7,i8) values (2, 2, '2_obtest', '2_obtest', 2.001,USEC_TO_TIME(2), 2, 0);
	eval replace into $tablename(i1,i2,v3,v4,i5,d6,i7,i8) values (3, 3, '3_obtest', '3_obtest', 3.001,USEC_TO_TIME(3), 3, 0);
	eval replace into $tablename(i1,i2,v3,v4,i5,d6,i7,i8) values (4, 4, '4_obtest', '4_obtest', 4.001,USEC_TO_TIME(4), 4, 0);
	eval replace into $tablename(i1,i2,v3,v4,i5,d6,i7,i8) values (5, 5, '5_obtest', '5_obtest', 5.001,USEC_TO_TIME(5), 5, 0);
	eval replace into $tablename(i1,i2,v3,v4,i5,d6,i7,i8) values (6, 6, '6_obtest', '6_obtest', 6.001,USEC_TO_TIME(6), 6, 0);
	eval replace into $tablename(i1,i2,v3,v4,i5,d6,i7,i8) values (7, 7, '7_obtest', '7_obtest', 7.001,USEC_TO_TIME(7), 7, 0);
	eval replace into $tablename(i1,i2,v3,v4,i5,d6,i7,i8) values (8, 8, '8_obtest', '8_obtest', 8.001,USEC_TO_TIME(8), 8, 0);
	eval replace into $tablename(i1,i2,v3,v4,i5,d6,i7,i8) values (9, 9, '9_obtest', '9_obtest', 9.001,USEC_TO_TIME(9), 9, 0);
	eval replace into $tablename(i1,i2,v3,v4,i5,d6,i7,i8) values (10, 10, '10_obtest', '10_obtest', 10.001,USEC_TO_TIME(10), 10, 0);
	eval replace into $tablename(i1,i2,v3,v4,i5,d6,i7,i8) values (11, 11, '11_obtest', '11_obtest', 11.001,USEC_TO_TIME(11), 11, 0);
	eval replace into $tablename(i1,i2,v3,v4,i5,d6,i7,i8) values (12, 12, '12_obtest', '12_obtest', 12.001,USEC_TO_TIME(12), 12, 0);
	eval replace into $tablename(i1,i2,v3,v4,i5,d6,i7,i8) values (13, 13, '13_obtest', '13_obtest', 13.001,USEC_TO_TIME(13), 13, 0);
	eval replace into $tablename(i1,i2,v3,v4,i5,d6,i7,i8) values (14, 14, '14_obtest', '14_obtest', 14.001,USEC_TO_TIME(14), 14, 0);
	eval replace into $tablename(i1,i2,v3,v4,i5,d6,i7,i8) values (15, 15, '15_obtest', '15_obtest', 15.001,USEC_TO_TIME(15), 15, 0);
	eval replace into $tablename(i1,i2,v3,v4,i5,d6,i7,i8) values (16, 16, '16_obtest', '16_obtest', 16.001,USEC_TO_TIME(16), 16, 0);
	eval replace into $tablename(i1,i2,v3,v4,i5,d6,i7,i8) values (17, 17, '17_obtest', '17_obtest', 17.001,USEC_TO_TIME(17), 17, 0);
	eval replace into $tablename(i1,i2,v3,v4,i5,d6,i7,i8) values (18, 18, '18_obtest', '18_obtest', 18.001,USEC_TO_TIME(18), 18, 0);
	eval replace into $tablename(i1,i2,v3,v4,i5,d6,i7,i8) values (19, 19, '19_obtest', '19_obtest', 19.001,USEC_TO_TIME(19), 19, 0);
	eval replace into $tablename(i1,i2,v3,v4,i5,d6,i7,i8) values (20, 20, '20_obtest', '20_obtest', 20.001,USEC_TO_TIME(20), 20, 0);
	eval replace into $tablename(i1,i2,v3,v4,i5,d6,i7,i8) values (21, 21, '21_obtest', '21_obtest', 21.001,USEC_TO_TIME(21), 21, 0);
	eval replace into $tablename(i1,i2,v3,v4,i5,d6,i7,i8) values (22, 22, '22_obtest', '22_obtest', 22.001,USEC_TO_TIME(22), 22, 0);
	eval replace into $tablename(i1,i2,v3,v4,i5,d6,i7,i8) values (23, 23, '23_obtest', '23_obtest', 23.001,USEC_TO_TIME(23), 23, 0);
	eval replace into $tablename(i1,i2,v3,v4,i5,d6,i7,i8) values (24, 24, '24_obtest', '24_obtest', 24.001,USEC_TO_TIME(24), 24, 0);
	eval replace into $tablename(i1,i2,v3,v4,i5,d6,i7,i8) values (25, 25, '25_obtest', '25_obtest', 25.001,USEC_TO_TIME(25), 25, 0);
	eval replace into $tablename(i1,i2,v3,v4,i5,d6,i7,i8) values (26, 26, '26_obtest', '26_obtest', 26.001,USEC_TO_TIME(26), 26, 0);
	eval replace into $tablename(i1,i2,v3,v4,i5,d6,i7,i8) values (27, 27, '27_obtest', '27_obtest', 27.001,USEC_TO_TIME(27), 27, 0);
	eval replace into $tablename(i1,i2,v3,v4,i5,d6,i7,i8) values (28, 28, '28_obtest', '28_obtest', 28.001,USEC_TO_TIME(28), 28, 0);
	eval replace into $tablename(i1,i2,v3,v4,i5,d6,i7,i8) values (29, 29, '29_obtest', '29_obtest', 29.001,USEC_TO_TIME(29), 29, 0);
	eval replace into $tablename(i1,i2,v3,v4,i5,d6,i7,i8) values (30, 30, '30_obtest', '30_obtest', 30.001,USEC_TO_TIME(30), 30, 0);
	eval replace into $tablename(i1,i2,v3,v4,i5,d6,i7,i8) values (31, 31, '31_obtest', '31_obtest', 31.001,USEC_TO_TIME(31), 31, 0);
	eval replace into $tablename(i1,i2,v3,v4,i5,d6,i7,i8) values (32, 32, '32_obtest', '32_obtest', 32.001,USEC_TO_TIME(32), 32, 0);
	eval replace into $tablename(i1,i2,v3,v4,i5,d6,i7,i8) values (33, 33, '33_obtest', '33_obtest', 33.001,USEC_TO_TIME(33), 33, 0);
	eval replace into $tablename(i1,i2,v3,v4,i5,d6,i7,i8) values (34, 34, '34_obtest', '34_obtest', 34.001,USEC_TO_TIME(34), 34, 0);
	eval replace into $tablename(i1,i2,v3,v4,i5,d6,i7,i8) values (35, 35, '35_obtest', '35_obtest', 35.001,USEC_TO_TIME(35), 35, 0);
	eval replace into $tablename(i1,i2,v3,v4,i5,d6,i7,i8) values (36, 36, '36_obtest', '36_obtest', 36.001,USEC_TO_TIME(36), 36, 0);
	eval replace into $tablename(i1,i2,v3,v4,i5,d6,i7,i8) values (37, 37, '37_obtest', '37_obtest', 37.001,USEC_TO_TIME(37), 37, 0);
	eval replace into $tablename(i1,i2,v3,v4,i5,d6,i7,i8) values (38, 38, '38_obtest', '38_obtest', 38.001,USEC_TO_TIME(38), 38, 0);
	eval replace into $tablename(i1,i2,v3,v4,i5,d6,i7,i8) values (39, 39, '39_obtest', '39_obtest', 39.001,USEC_TO_TIME(39), 39, 0);
	eval replace into $tablename(i1,i2,v3,v4,i5,d6,i7,i8) values (40, 40, '40_obtest', '40_obtest', 40.001,USEC_TO_TIME(40), 40, 0);
	eval replace into $tablename(i1,i2,v3,v4,i5,d6,i7,i8) values (41, 41, '41_obtest', '41_obtest', 41.001,USEC_TO_TIME(41), 41, 0);
	eval replace into $tablename(i1,i2,v3,v4,i5,d6,i7,i8) values (42, 42, '42_obtest', '42_obtest', 42.001,USEC_TO_TIME(42), 42, 0);
	eval replace into $tablename(i1,i2,v3,v4,i5,d6,i7,i8) values (43, 43, '43_obtest', '43_obtest', 43.001,USEC_TO_TIME(43), 43, 0);
	eval replace into $tablename(i1,i2,v3,v4,i5,d6,i7,i8) values (44, 44, '44_obtest', '44_obtest', 44.001,USEC_TO_TIME(44), 44, 0);
	eval replace into $tablename(i1,i2,v3,v4,i5,d6,i7,i8) values (45, 45, '45_obtest', '45_obtest', 45.001,USEC_TO_TIME(45), 45, 0);
	eval replace into $tablename(i1,i2,v3,v4,i5,d6,i7,i8) values (46, 46, '46_obtest', '46_obtest', 46.001,USEC_TO_TIME(46), 46, 0);
	eval replace into $tablename(i1,i2,v3,v4,i5,d6,i7,i8) values (47, 47, '47_obtest', '47_obtest', 47.001,USEC_TO_TIME(47), 47, 0);
	eval replace into $tablename(i1,i2,v3,v4,i5,d6,i7,i8) values (48, 48, '48_obtest', '48_obtest', 48.001,USEC_TO_TIME(48), 48, 0);
	eval replace into $tablename(i1,i2,v3,v4,i5,d6,i7,i8) values (49, 49, '49_obtest', '49_obtest', 49.001,USEC_TO_TIME(49), 49, 0);
	eval replace into $tablename(i1,i2,v3,v4,i5,d6,i7,i8) values (50, 50, '50_obtest', '50_obtest', 50.001,USEC_TO_TIME(50), 50, 0);


	dec $tnum;
}

--enable_query_log
--enable_result_log
