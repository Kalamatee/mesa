# Copyright (C) 2017 Intel Corporation.   All Rights Reserved.
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice (including the next
# paragraph) shall be included in all copies or substantial portions of the
# Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
# IN THE SOFTWARE.

# Python source
# Compatible with Python2.X and Python3.X

from __future__ import print_function
import itertools
import math
import argparse
import os
import sys
from mako.template import Template
from mako.exceptions import RichTraceback

def write_template_to_string(template_filename, **kwargs):
    try:
        template = Template(filename=os.path.abspath(template_filename))
        # Split + Join fixes line-endings for whatever platform you are using
        return '\n'.join(template.render(**kwargs).splitlines())
    except:
        traceback = RichTraceback()
        for (filename, lineno, function, line) in traceback.traceback:
            print("File %s, line %s, in %s" % (filename, lineno, function))
            print(line, "\n")
        print("%s: %s" % (str(traceback.error.__class__.__name__), traceback.error))

def write_template_to_file(template_filename, output_filename, **kwargs):
    output_dirname = os.path.dirname(output_filename)
    if not os.path.exists(output_dirname):
        os.makedirs(output_dirname)
    with open(output_filename, "w") as outfile:
        print(write_template_to_string(template_filename, **kwargs), file=outfile)


def main(args=sys.argv[1:]):
    thisDir = os.path.dirname(os.path.realpath(__file__))
    parser = argparse.ArgumentParser("Generate files and initialization functions for all permutuations of BackendPixelRate.")
    parser.add_argument('--dim', help="gBackendPixelRateTable array dimensions", nargs='+', type=int, required=True)
    parser.add_argument('--outdir', help="output directory", nargs='?', type=str, default=thisDir)
    parser.add_argument('--split', help="how many lines of initialization per file [0=no split]", nargs='?', type=int, default='512')
    parser.add_argument('--cpp', help="Generate cpp file(s)", action='store_true', default=False)
    parser.add_argument('--cmake', help="Generate cmake file", action='store_true', default=False)


    args = parser.parse_args(args);

    output_list = []
    for x in args.dim:
        output_list.append(list(range(x)))

    # generate all permutations possible for template paremeter inputs
    output_combinations = list(itertools.product(*output_list))
    output_list = []

    # for each permutation
    for x in range(len(output_combinations)):
        # separate each template peram into its own list member
        new_list = [output_combinations[x][i] for i in range(len(output_combinations[x]))]
        tempStr = 'gBackendPixelRateTable'
        #print each list member as an index in the multidimensional array
        for i in new_list:
            tempStr += '[' + str(i) + ']'
        #map each entry in the permuation as its own string member, store as the template instantiation string
        tempStr += " = BackendPixelRate<SwrBackendTraits<" + ','.join(map(str, output_combinations[x])) + '>>;'
        #append the line of c++ code in the list of output lines
        output_list.append(tempStr)

    # how many files should we split the global template initialization into?
    if (args.split == 0):
        numFiles = 1
    else:
        numFiles = (len(output_list) + args.split - 1) // args.split
    linesPerFile = (len(output_list) + numFiles - 1) // numFiles
    chunkedList = [output_list[x:x+linesPerFile] for x in range(0, len(output_list), linesPerFile)]

    # generate .cpp files
    if args.cpp:
        baseCppName = os.path.join(args.outdir, 'gen_BackendPixelRate%s.cpp')
        templateCpp = os.path.join(thisDir, 'templates', 'gen_backend.cpp')

        for fileNum in range(numFiles):
            filename = baseCppName % str(fileNum)
            #print('Generating', filename)
            write_template_to_file(
                templateCpp,
                baseCppName % str(fileNum),
                cmdline=sys.argv,
                fileNum=fileNum,
                funcList=chunkedList[fileNum])

    # generate gen_backend.cmake file
    if args.cmake:
        templateCmake = os.path.join(thisDir, 'templates', 'gen_backend.cmake')
        cmakeFile = os.path.join(args.outdir, 'gen_backends.cmake')
        #print('Generating', cmakeFile)
        write_template_to_file(
            templateCmake,
            cmakeFile,
            cmdline=sys.argv,
            numFiles=numFiles,
            baseCppName='${RASTY_GEN_SRC_DIR}/backends/' + os.path.basename(baseCppName))

    #print("Generated %d template instantiations in %d files" % (len(output_list), numFiles))

    return 0

if __name__ == '__main__':
    sys.exit(main())
