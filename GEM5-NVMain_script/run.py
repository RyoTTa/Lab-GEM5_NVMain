#!/bin/python3
# -*- coding: utf-8 -*-
import os
import json
import time
import argparse
import configparser
import datetime
import shutil
import re
import subprocess
import signal

#signal.signal(signal.SIGTTOU, signal.SIG_IGN)

################### argparser #######################
#buildfile = GEM 5의 빌드파일
#configfile = GEM 5의 구성파일
#optionfile = GEM 5실행시 옵션값파일

parser = argparse.ArgumentParser(description='GEM5 script')
parser.add_argument('-f', '--optionfile', action='store', dest='optionfile', help='all option file')
parser.add_argument('-d', '--desc', action='store', dest='desc', help='GEM5 Description')
parser.add_argument('-i', '--gem5id', action='store', dest='gem5id', help='specify a simulation id for re-run')
parser.add_argument('-w', '--waittime', action='store', dest='waittime', help='specify a wiat time before running simulation')
parser.add_argument('-a', '--addfile', action='store', dest='addfile', help='additional files to be saved')
parser.add_argument('-n', '--foldername', action='store', dest='folder_name', help='result folder name')

args = parser.parse_args()


config = configparser.ConfigParser()
config.optionxform = str
config.read(args.optionfile)
#########################################################

date=datetime.datetime.now().strftime("%m-%d-%y")
time_h_m=datetime.datetime.now().strftime("%H:%M")

gem5_id=""
folder_name = ""

if args.gem5id == None:
    gem5_id=str(int(time.time()*1000))
else:
    gem5_id=str(args.gem5id)

wait_time=args.waittime

########Setup simulation parameters ######################
PWD = os.getcwd()
OUTPUT_DIR=PWD+"/out/"+date
MAXMEM = 4*1024

########### Create Output directory #######################
if os.path.exists(OUTPUT_DIR)==False:
    print("Create " + OUTPUT_DIR)
    os.mkdir(OUTPUT_DIR)
    os.system("chmod g+w " + OUTPUT_DIR)

################벤치마크 금회차 설정########################

history_file=PWD+"/history.json"
description=""
if args.gem5id == None:
    print("\nBatch simulation")
    print("Date: " +date)
    print("Time: " +time_h_m)
    print("Simulaiton ID: "+str(gem5_id))
    print("Simulation History File: "+history_file)

    if args.folder_name == None:
        folder_name=input("Folder Name?: ")
    else:
        folder_name=args.folder_name

    if args.desc == None:
        description=input("Description?: ")
    else:
        description=args.desc
    #print(description)

    print("Output directory: "+OUTPUT_DIR)
else:
    print("rerun gem5id: "+str(gem5_id))

########### Crate Detail Ouotput Directory ####################

OUTPUT_DIR=PWD+"/out/"+date+"/"+folder_name

if os.path.exists(OUTPUT_DIR)==False:
    print("Create " + OUTPUT_DIR)
    os.mkdir(OUTPUT_DIR)
    os.system("chmod g+w " + OUTPUT_DIR)

if '/' in str(args.optionfile) :
    shutil.copy2(args.optionfile, OUTPUT_DIR+"/"+str(gem5_id)+"-"+str(args.optionfile).split('/')[-1])
else :
    shutil.copy2(args.optionfile, OUTPUT_DIR+"/"+str(gem5_id)+"-"+str(args.optionfile))

if args.addfile != None :
    print(args.addfile)
    shutil.copy2(args.addfile, OUTPUT_DIR+"/"+str(gem5_id)+"-addfile-"+str(args.addfile).split('/')[-1])

f = open(OUTPUT_DIR+"/"+str(gem5_id)+"-"+"description.txt","w")
f.write(description)
f.close()
################빌드 및 구성 파일 옵션 설정 ####################

def AddOption(tag) :
    parm=""
    options=config.options(tag)
    for option_name in options:
        option_value = config.get(tag,option_name)
        if option_value == "" :
            parm = parm + " "+ option_name
        else :
            parm = parm + " "+ option_name + "=" + option_value
    return parm

gem5_build_option = ""
gem5_config_option = ""
output_post_fix = ""

gem5_config_option = AddOption("CONFIGOPT")
checkpoint_make_check = "checkpoint-at-end" in gem5_config_option
checkpoint_use_check = "checkpoint-restore" in gem5_config_option

output_post_fix = gem5_id + '.'



GEM5_BUILD_FILE=config.get("TRACE","BUILDFILE").split('\n')
GEM5_CONFIG_FILE=config.get("TRACE","CONFIGFILE").split('\n')
BENCH_BUILD_DIR=config.get("TRACE","BENCHDIR").split('\n')

#######################체크포인트 설정#######################
if checkpoint_make_check :
    mix_count = 1
    bench_pwd=config.options("BENCHMARK")
    for i in range(len(bench_pwd)) :
        if ';' in bench_pwd[i] :
            if 'mix' in bench_pwd[i] :
                bench_name = 'mix' + str(mix_count)
                bench_pwd[i] = bench_pwd[i].replace('mix;','')
                mix_count +=1
                check_mix = True

            else :
                bench_name = str(bench_pwd[i]).split(';')[0]
                bench_name=bench_name.replace("'","")
        else :
            bench_name = bench_pwd[i]
        os.mkdir(OUTPUT_DIR+"/"+bench_name.split('/')[-1])
if checkpoint_use_check :
    CHECKPOINT_DIR = config.get("TRACE","CHECKPOINTDIR").split('\n')

########################## 실행 #############################

def splitBenchOption(bench_option) :
    oinput = ''
    ooption = ''
    temp = bench_option.split(';')
    print(temp)
    for entry in temp :
        if '<' in entry :
            oinput = oinput + entry.replace('<','') + ';'
            ooption = ooption + ';'
        else :
            ooption = ooption + entry + ';'
            oinput = oinput + ';'
    oinput = oinput[0:-1]
    ooption = ooption[0:-1]

    return oinput, ooption



def RunBench(tag) :
    mix_count = 1
    bench_pwd=config.options(tag)
    for i in range(len(bench_pwd)) :
        cmd=""
        gem5_build_option = ""
        check_mix = False

        bench_input,bench_option = splitBenchOption(config.get(tag,bench_pwd[i]))

        outdir = "--outdir=" + OUTPUT_DIR
        if ';' in bench_pwd[i] :
            if 'mix' in bench_pwd[i] :
                bench_name = 'mix' + str(mix_count)
                bench_pwd[i] = bench_pwd[i].replace('mix;','')
                mix_count +=1
                check_mix = True

            else :
                bench_name = str(bench_pwd[i]).split(';')[0]
                bench_name=bench_name.replace("'","")
        else :
            bench_name = bench_pwd[i]

        gem5_build_option = outdir + " --stats-file="+ gem5_id +"-"+bench_name.split('/')[-1] + ".result"
        stdout_file = gem5_id + "-" + bench_name.split('/')[-1] + ".out"
        stderr_file = gem5_id + "-" + bench_name.split('/')[-1] + ".err"

        if checkpoint_make_check :
            gem5_config_option_temp = gem5_config_option + ' --checkpoint-dir=' + OUTPUT_DIR + "/" + bench_name.split('/')[-1]
        elif checkpoint_use_check :
            gem5_config_option_temp = gem5_config_option + ' --checkpoint-dir=' + CHECKPOINT_DIR[0] + bench_name.split('/')[-1]
        else :
            gem5_config_option_temp = gem5_config_option

        if bench_option == None :
            cmd = GEM5_BUILD_FILE[0] + " " \
            + gem5_build_option + " " \
            + GEM5_CONFIG_FILE[0] + " " \
            + gem5_config_option_temp + " " \
            + "--cmd='" + bench_pwd[i] + "' "\
            + " > " +OUTPUT_DIR +"/"+ stdout_file \
            + " 2> " +OUTPUT_DIR + "/" + stderr_file
        else :
            cmd = GEM5_BUILD_FILE[0] + " " \
            + gem5_build_option + " " \
            + GEM5_CONFIG_FILE[0] + " " \
            + gem5_config_option_temp + " " \
            + "--cmd='" + bench_pwd[i] + "' "\
            + "--input='" + bench_input +"' " \
            + "--options=" + "'" + bench_option + "'" \
            + " > " +OUTPUT_DIR +"/" + stdout_file \
            + " 2> " +OUTPUT_DIR + "/" + stderr_file

        print(cmd)
        wd = os.getcwd()
        if check_mix == True :
            os.chdir(BENCH_BUILD_DIR[0] + bench_name.split('/')[-1][0:3])
        else :
            os.chdir(BENCH_BUILD_DIR[0] + bench_name.split('/')[-1])
        #subprocess.Popen("srun -x compasslab4 "+ cmd,shell=True)
        subprocess.Popen("srun -J "+ bench_name.split('/')[-1] +" " + cmd,shell=True)
        #fd = os.open("/dev/tty", os.O_RDWR)
        #os.tcsetpgrp(fd, os.getpgrp())
        os.chdir(wd)
        time.sleep(1)



RunBench("BENCHMARK")
#time.sleep(100)
###############history 추가##################

data= {}
if os.path.exists(history_file)==True:
    with open(history_file,'r') as json_file:
         data=json.load(json_file)
else:
    data['log']=[]

data['log'].append({
    'id': gem5_id,
    'date': date,
    'time': time_h_m,
    #'benchmarks':all_benchmark,
    #'options':all_option,
    'foldername' :folder_name,
    'description':description
    })
with open(history_file,"w") as json_file:
    json.dump(data, json_file, indent="\t")
