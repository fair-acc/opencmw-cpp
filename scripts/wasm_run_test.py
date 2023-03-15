#!/usr/bin/env python3

import sys
import os
import time

from selenium import webdriver
from selenium.webdriver.common.desired_capabilities import DesiredCapabilities
from selenium.webdriver.support import expected_conditions as EC
from selenium.webdriver.support.ui import WebDriverWait

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

try:
    options = webdriver.ChromeOptions()
    options.add_argument('--headless')
    options.add_argument('--ignore-ssl-errors=yes')
    options.add_argument('--ignore-certificate-errors')

    capabilities = DesiredCapabilities.CHROME
    capabilities['loggingPrefs'] = { 'browser':'ALL' }
    capabilities['goog:loggingPrefs'] = { 'browser':'ALL' }
    driver = webdriver.Chrome(desired_capabilities=capabilities, options=options)

    uri = "http://localhost:8080/" + args.test_name + ".html"
    print("Loading " + uri)
    driver.get(uri)

    wait = WebDriverWait(driver, 20)
    wait.until(EC.title_is("DONE"))

except:
    print("Exception")

error_present = False

for log_entry in driver.get_log('browser'):
    if log_entry['source'] == 'console-api':
        print("{}: {}".format(log_entry['level'], log_entry['message']));
        if log_entry['level'] != 'INFO':
            error_present = True

driver.close()

rest_server.kill()

if error_present:
    print("Tests failed")
    sys.exit(1)

else:
    print("All tests passed")
