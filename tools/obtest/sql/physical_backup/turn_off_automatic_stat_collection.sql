--disable_all_result_log
call dbms_scheduler.disable('MONDAY_WINDOW');
call dbms_scheduler.disable('TUESDAY_WINDOW');
call dbms_scheduler.disable('WEDNESDAY_WINDOW');
call dbms_scheduler.disable('THURSDAY_WINDOW');
call dbms_scheduler.disable('FRIDAY_WINDOW');
call dbms_scheduler.disable('SATURDAY_WINDOW');
call dbms_scheduler.disable('SUNDAY_WINDOW');
--enable_all_result_log