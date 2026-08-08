create user base_user_1 IDENTIFIED BY PASSWORD '*4acfe3202a5ff5cf467898fc58aab1d615029441';
create user base_user_2 IDENTIFIED BY PASSWORD '*e9c2bcdc178a99b7b08dd25db58ded2ee5bff050';
create user base_user_3  IDENTIFIED BY PASSWORD '*1975d095ac033caf4e1bf94f7202a9bbfeeb66f1';
create user base_user_4 IDENTIFIED BY PASSWORD '*4acfe3202a5ff5cf467898fc58aab1d615029441';
create user base_user_5 IDENTIFIED BY PASSWORD '*e9c2bcdc178a99b7b08dd25db58ded2ee5bff050';
create user base_user_6 IDENTIFIED BY PASSWORD '*1975d095ac033caf4e1bf94f7202a9bbfeeb66f1';
create user base_user_7 IDENTIFIED BY PASSWORD '*4acfe3202a5ff5cf467898fc58aab1d615029441';
create user base_user_8 IDENTIFIED BY PASSWORD '*e9c2bcdc178a99b7b08dd25db58ded2ee5bff050';
create user base_user_9 IDENTIFIED BY PASSWORD '*1975d095ac033caf4e1bf94f7202a9bbfeeb66f1';
create user base_user_10 IDENTIFIED BY '';
drop user base_user_9;
#alter user root identified by 'abc';

GRANT ALL PRIVILEGES ON `mysql`.* TO 'root';
GRANT DROP ON `test`.* TO 'base_user_2';
GRANT ALTER ON `mysql`.* TO 'base_user_8';
GRANT CREATE ON `mysql`.* TO 'base_user_5';
GRANT UPDATE ON `mysql`.* TO 'base_user_3';
GRANT DELETE ON `test`.* TO 'base_user_6';
GRANT INSERT ON `mysql`.* TO 'base_user_7';
GRANT SELECT ON `test`.* TO 'root';
GRANT ALL PRIVILEGES ON `test`.* TO 'root';
GRANT ALL PRIVILEGES ON *.* TO 'base_user_1' WITH GRANT OPTION;
GRANT SELECT ON `oceanbase`.* TO 'base_user_5';
GRANT INDEX ON `mysql`.* TO 'base_user_4';
GRANT CREATE VIEW ON `mysql`.* TO 'base_user_7';
GRANT SHOW VIEW ON `test`.* TO 'base_user_6';
GRANT select ON `oceanbase`.* TO 'base_user_4';

REVOKE ALL PRIVILEGES, GRANT OPTION FROM 'base_user_1';
REVOKE UPDATE ON `mysql`.* FROM 'base_user_3';
REVOKE SELECT ON `oceanbase`.* FROM 'base_user_4';	

