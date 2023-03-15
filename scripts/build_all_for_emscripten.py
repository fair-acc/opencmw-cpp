#!/usr/bin/env python3

import os
import argparse
import subprocess

parser = argparse.ArgumentParser(
                    prog='build_all',
                    description='Builds for several target platforms at once')

parser.add_argument('--src-path')
parser.add_argument('--build-prefix')
parser.add_argument('--emscripten-sdk-prefix')
parser.add_argument('-j')

def mk_and_chdir(dir):
    try:
        os.mkdir(dir)
    except OSError as error:
        print(error)
    os.chdir(dir)

def run_or_exit(cmd):
    completed = subprocess.run(cmd)
    if completed.returncode != 0:
        raise OSError("Command failed " + str(cmd))

args = parser.parse_args()
if args.src_path is None or args.build_prefix is None or args.emscripten_sdk_prefix is None:
    raise RuntimeError("CLI parameters are not optional")

if args.j is None:
    args.j = "1"

print(args)
mk_and_chdir(args.build_prefix)

native_build_dir = "build_native"

mk_and_chdir(args.build_prefix + "/" + native_build_dir)
run_or_exit(["cmake", args.src_path])
run_or_exit(["cmake", "--build", "."])

os.chdir(args.build_prefix)
run_or_exit([args.emscripten_sdk_prefix + "/upstream/emscripten/emcmake", "cmake", args.src_path])
run_or_exit([args.emscripten_sdk_prefix + "/upstream/emscripten/emmake", "make", "-j" + args.j])
