#!/usr/bin/env python3
'''
Wrapper script for clang-format

Copyright (c) 2015 MarcoFalke
Copyright (c) 2015 The Bitcoin Core developers
Copyright (c) 2015-2017 The Bitcoin Unlimited developers
Distributed under the MIT software license, see the accompanying
file COPYING or http://www.opensource.org/licenses/mit-license.php.
'''

import os
import sys
import subprocess
import difflib
from io import StringIO
import pdb
import tempfile

# A set of versions known to produce the same output
tested_versions = ['3.8.0',
                   '3.8.1',  # 3.8.1-12ubuntu1 on yakkety works
                   '3.9.0',
                   '3.9.1',
                  ]
accepted_file_extensions = ('.h', '.cpp') # Files to format
trailing_comment_exe = "trailing-comment.py"
max_col_len = 120

def check_clang_format_version(clang_format_exe):
    try:
        output = subprocess.check_output([clang_format_exe, '-version'])
        for ver in tested_versions:
            if ver in output.decode('utf8'):
                return
        raise RuntimeError("Untested version: " + output)
    except Exception as e:
        print('Could not verify version of ' + clang_format_exe + '.')
        raise e

def check_command_line_args(argv):
    required_args = ['{operation}','{clang-format-exe}', '{files}']
    example_args = ['clang-format', 'format', 'src/main.cpp', 'src/wallet/*']

    if(len(argv) < len(required_args) + 1):
        for word in (['Usage:', argv[0]] + required_args):
            print(word, end =' ')
        print('')
        for word in (['E.g:', argv[0]] + example_args):
            print(word, end = ' ')
        print('')
        sys.exit(1)

def run_clang_check(clang_format_exe, files):
    changed = set()
    nonexistent = []
    for target in files:
        if os.path.isdir(target):
            for path, dirs, files in os.walk(target):
                run_clang_check(clang_format_exe, (os.path.join(path, f) for f in files))
        elif not os.path.exists(target):
            print("File %s does not exist" % target)
            nonexistent.append(target)
        elif target.endswith(accepted_file_extensions):
            formattedFile = tempfile.TemporaryFile()
            trailingCommentFile = tempfile.TemporaryFile()
            subprocess.check_call([trailing_comment_exe, str(max_col_len)], stdin=open(target,"rb"), stdout=trailingCommentFile, stderr=subprocess.STDOUT)
            trailingCommentFile.seek(0)
            subprocess.check_call([clang_format_exe,'-style=file','-assume-filename=%s' % target], stdin=trailingCommentFile, stdout=formattedFile, stderr=subprocess.STDOUT)
            with open(target,"rb") as f: inputContents = [ x.decode('utf8') for x in f.readlines() ]
            formattedFile.seek(0)
            formattedContents = [ x.decode('utf8') for x in formattedFile.readlines() ]
            for l in difflib.unified_diff(inputContents,formattedContents, target, target+"_formatted"):
                sys.stdout.write(l)
                changed.add(target)
        else:
            print("Skip " + target)
    if nonexistent:
        print("\nNonexistent files: " + ",".join(nonexistent))
    if changed:
        print("\nImproper formatting found in: " + ",".join(list(changed)))
        print("To properly format these files run: " + sys.argv[0] + " format " + clang_format_exe + " " + " ".join(list(changed)))
        return 1
    else:
        print("All existing files are properly formatted")

    if nonexistent:
        return 2
    return 0

def run_clang_format(clang_format_exe, files, stdout=False):
    for target in files:
        if os.path.isdir(target):
            for path, dirs, files in os.walk(target):
                run_clang_format(clang_format_exe, (os.path.join(path, f) for f in files))
        elif target.endswith(accepted_file_extensions):
            if  stdout:
                tce_pipe = subprocess.Popen([trailing_comment_exe, str(max_col_len)],
                                            stdin=open(target, "rb"),
                                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)

                cfe_pipe = subprocess.Popen([clang_format_exe, '-assume-filename='+target, '-style=file'],
                                            stdin =tce_pipe.stdout,
                                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)

                sout, serr = cfe_pipe.communicate()
                sys.stdout.write(sout.decode())
                sys.stderr.write(serr.decode())
            else:
                print("Format " + target)
                subprocess.check_call([trailing_comment_exe, str(max_col_len), target])
                subprocess.check_call([clang_format_exe, '-i', '-style=file', target],
                                      stdout=open(os.devnull, 'wb'), stderr=subprocess.STDOUT)

        else:
            print("Skip " + target)

def main(argv):
    global trailing_comment_exe
    check_command_line_args(argv)
    mypath = os.path.realpath(__file__)
    trailing_comment_exe = os.path.join(os.path.dirname(mypath), trailing_comment_exe)
    operation = argv[1]
    clang_format_exe = argv[2]
    files = argv[3:]
    check_clang_format_version(clang_format_exe)
    if operation == "check":
        return run_clang_check(clang_format_exe, files)
    elif operation == "format":
        run_clang_format(clang_format_exe, files)
    elif operation == "format-stdout":
        run_clang_format(clang_format_exe, files, stdout=True)
    elif operation == "format-stdout-if-wanted":
        formatted_files_fn = os.path.join(
            os.path.dirname(mypath), "..", "..", "src", ".formatted-files")
        formatted_files = [
            os.path.join("src/", x.replace("\n", "")) for x in open(formatted_files_fn).readlines()]

        for fn in files:
            if fn in formatted_files:
                run_clang_format(clang_format_exe, [fn], stdout=True)
            else:
                sys.stdout.write(open(fn).read())
    else:
        raise RuntimeError("Invalid operation '%s'." % operation)

if __name__ == "__main__":
    if len(sys.argv) == 1:
        print("clang-format.py: helper wrapper for clang-format")
        check_command_line_args(sys.argv)

    result = main(sys.argv)
    sys.exit(result)
