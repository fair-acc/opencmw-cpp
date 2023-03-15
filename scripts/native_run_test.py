#!/usr/bin/env python3

import sys
import os
import time

import argparse
import subprocess

parser = argparse.ArgumentParser(
                    prog='build_all',
                    description='Runs the specified test')

parser.add_argument('--server-build-dir')
parser.add_argument('--test-name')
args = parser.parse_args()

os.environ["DISABLE_REST_HTTPS"] = "1"

rest_command = "exec " + args.server_build_dir + "/concepts/majordomo/MajordomoRest_example " + os.getcwd() + "/"
print("Starting server " + rest_command)
rest_server = subprocess.Popen(rest_command, stdout=subprocess.PIPE, shell=True)

time.sleep(2)

error_present = False

try:
    test_command = "exec ./build_native/" + args.test_name
    print("Running test command " + test_command)
    test_process = subprocess.Popen(test_command)
    if test_process.wait() != 0:
        error_present = True

except:
    print("Exception")

rest_server.kill()

if error_present:
    print("Tests failed")
    sys.exit(1)

else:
    print("All tests passed")
