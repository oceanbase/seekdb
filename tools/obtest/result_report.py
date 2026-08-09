# -*- coding: utf-8 -*-
import urllib2
import json
import sys
import os
import re
import subprocess
"""
This module demonstrates documentation as how we set off alarm message through DingDing.
maintainer: zww378891
Usage:
python result_report.py %GID ./mytestlog_storage_ha $alarm_url $CONCINDEX

"""
case_author = {}
job_name = "" 
job_info=""
def send_dingding_msg(url,msg):
    """
    Send message to a dingding robot.

    Args:
        url: By checking the robot setting, you should be able to get the url
        msg: The message you are sending to dingding group

    Returns:
        None

    """
    headers = {'Content-Type': 'application/json'}
    data = {'msgtype':'text', 'text': {'content': msg} }
    json_str = json.dumps(data)
    request = urllib2.Request(url=url, headers=headers, data=json_str.encode('utf_8', 'strict'))
    response = urllib2.urlopen(request)
    response_msg = str(response.read())
    print("response_msg=" + response_msg)


def extract_case_names(input):
    """
    Extraction of case names

    Args:
        input: the general input should look like '^[[31m[ FAIL LST ]^[[0m backup_restore.1002_111_rs_switch_when_backup,backup_restore.1003_3zones_to_1zone,backup_restore.1001_111_basic_function,backup_restore.1004_1zone_to_3zones,backup_restore.1005_111_rs_switch_when_restore'

    Returns:
        backup_restore.1002_111_rs_switch_when_backup,backup_restore.1003_3zones_to_1zone,backup_restore.1001_111_basic_function,backup_restore.1004_1zone_to_3zones,backup_restore.1005_111_rs_switch_when_restore

    """
    lst_start= input.rfind("[")+4
    return input[lst_start:].strip()


def format_warn_msg(total_num,failed_cases):
    """
    formation of warn message
    """
    #build_url=os.path.dirname(build_url)
    #buildnum=os.path.basename(build_url)
    #jobid=os.path.basename(os.path.dirname(build_url))
    result=""
    if failed_cases != "":
       failed_count=failed_cases.count("@")
       if state.job_name !="":
          result="Failled!\nJob name: %s\nObserver Commit: %s\nFarm Gid: %s\nFarm Machine index: %s\nFailed number: %d\nTotal number: %d\nFailed list:\n%s" % (state.job_name,state.job_info,GID,INDEX,failed_count,total_num,failed_cases)
       else:
          result="Failled!\nFarm Gid: %s\nFarm Machine index: %s\nFailed number: %d\nTotal number: %d\nFailed list:\n%s" % (GID,INDEX,failed_count,total_num,failed_cases)
    elif total_num == 0:
       result="Failled!\nNo test case has run. \n"
    else:
       result="Success!\nTotal number: %d\n" % (total_num)
    return result


def report_on_build(log_path,token):
    """
    We are parsing logs to get Fail list as well as other information.

    Args:
        log_path: where the log file is
        build_url: pass down from hudson
        token: token to specify which dingding robot we are sending result to. 
    """
    failed_cases = ""
    total_cases = 0
    if  os.path.exists(log_path) :
       with open(log_path) as log_file:
          for line in log_file:
               if "FAIL LST" in line:
                    failed_cases = ""
                    cases=extract_case_names(line)
                    for case_name in cases.split(","):
                        a = case_name.replace(".test","")
                        failed_cases+=a
                        failed_cases+="   @"+case_author[a]+"\n"
               elif "Fatal error!!!" in line:
                        a = line.split("=>")[2]
                        a = a.split("(maybe)")[0]
                        a = a.split(".test")[0].strip()
                        failed_cases += a
                        failed_cases+= " @"+case_author[a]+"\n"

               if "RUN" in line:
                    total_cases = total_cases+1
    message = format_warn_msg(total_cases,failed_cases)
    print message
    send_dingding_msg(token,message)
    if failed_cases!="":
        sys.exit(2)
    sys.exit()

def case_to_author(case_path):
    for fpathe,dirs,fs in os.walk(case_path):
        for f in fs:
            if os.path.splitext(f)[1]  == ".test":
                with open(os.path.join(fpathe,f)) as log_file:
                    has_owner=0
                    for line in log_file:
                        if "# owner: " in line:
                            case_author[f.replace(".test","")]=line.replace("# owner: ","").replace("\n","")
                            has_owner=1
                            break
                    if has_owner==0:
                        case_author[f.replace(".test","")]="zww378891"
    print case_author               

def get_job_name(file_path):
    if os.path.exists(file_path) and os.path.isfile(file_path):
       # Execute bin/server -V command and get output
       output = subprocess.check_output([file_path, '-V'], stderr=subprocess.STDOUT, universal_newlines=True)
       # Use regular expression to extract BUILD_BRANCH and REVISION
       build_branch_match = re.search(r'BUILD_BRANCH: (.+)', output)
       revision_match = re.search(r'REVISION: 1-(.+)', output)
       if build_branch_match and revision_match:
          state.job_name = build_branch_match.group(1)
          state.job_info = revision_match.group(1)
          state.job_name += "_nfs"
    else:
       print("observer binary解析异常!")
    print state.job_name,state.job_info

def case_to_author_tablelistfile(caseListFile):
    if os.path.exists(caseListFile):
        with open(caseListFile) as caseUrl_file:
           for line in caseUrl_file:
               testcases = line.split(",")
               #print testcases
               for testcase in testcases:
                  testcase = testcase.strip()
                  if os.path.splitext(testcase)[1]  == ".test":
                    with open(testcase) as case_file:
                        has_owner=0
                        for f in case_file:
                            if "# owner: " in f or "# owner:" in f:
                                case_author[testcase.replace(".test","")]=f.replace("# owner: ","").replace("\n","")
                                has_owner=1
                                break
                            if "# owner : " in f:
                                case_author[testcase.replace(".test","")]=f.replace("# owner : ","").replace("\n","")
                                has_owner=1
                                break
                        if has_owner==0:
                                case_author[testcase.replace(".test","")]="zww378891"
    print case_author

class GlobalState:
    def __init_(self):
        self.job_name = ""
        self.job_info = ""

state = GlobalState()

if __name__ == '__main__':
    
    case_to_author_tablelistfile("farm_test.list")
    GID = sys.argv[1]
    log_path = sys.argv[2]
    token = sys.argv[3]
    INDEX = sys.argv[4]
    get_job_name('bin/observer')
    report_on_build(log_path,token)
