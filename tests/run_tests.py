#!/usr/bin/env python
#################################################################################
#                                                                               #
#                                StagBL Tests                                   #
#                                                                               #
#################################################################################
# Usage:                                                                        #
#  ./run_tests.py [-h] [-other_sciath_options]
#                                                                               #
#################################################################################

import os
import sys
import traceback

thisDir = os.path.dirname(os.path.abspath(__file__))

STAGBL_DIR = os.getenv('STAGBL_DIR')
if not STAGBL_DIR :
    print('You must define STAGBL_DIR in your environment. Exiting.')
    sys.exit(1)

STAGBL_ARCH = os.getenv('STAGBL_ARCH')
if not STAGBL_ARCH :
    print('You must define STAGBL_ARCH in your environment. Exiting.')
    sys.exit(1)
if os.path.normpath(os.path.join(STAGBL_DIR,'tests')) != thisDir :
    print('STAGBL_DIR is not set correctly in your environment. Exiting.')
    sys.exit(1)

chkpath = os.path.join(STAGBL_DIR,STAGBL_ARCH,'bin')
if not os.path.exists(chkpath) :
    print(chkpath,' not found. Set STAGBL_DIR and STAGBL_ARCH properly. Exiting.')
    sys.exit(1)

sys.path.insert(0,os.path.join(STAGBL_DIR,'sciath'))
try :
  from sciath.harness import Harness
  from sciath.test import Test
except Exception as errorMessage :
  if not sys.exc_info()[-1].tb_next :     # Check that the traceback has depth 1
    traceback.print_exc()
    print('********************')
    print('The required python package SciATH was not found. Exiting.')
    print('You can obtain SciATH by executing the following:')
    print('  git submodule init && git submodule update')
    print('********************')
    sys.exit(1)
  raise

#  Interpret any child directory containing 'test.py' as defining as a test
#  with a name defined by the relative path to the containing directory.
allTests = []
for (root, dirs, files) in os.walk(thisDir) :
    if 'test.py' in files :
        relpath = os.path.relpath(root,thisDir)
        mod = relpath.replace(os.sep,'.') + '.test'
        exec('import ' + mod)
        exec('allTests.append('+mod+'.test())')

# Run tests
h = Harness(allTests)
h.execute()
h.verify()
