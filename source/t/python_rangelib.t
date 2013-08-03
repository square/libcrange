#!/usr/bin/python

import unittest
import sys
if sys.platform == "darwin":
    print "Skipping python tests on OSX, buggy cffi"
    exit(0);
sys.path.append('../python')
from rangelib import RangeLib

class TestRangeLib(unittest.TestCase):
    def setUp(self):
        self.range = RangeLib("/etc/range.conf")
    def test_expand(self):
        self.assertEqual(sorted(self.range.expand("foo100..2")), ["foo100", "foo101", "foo102"])
    def test_compress(self):
        self.assertEqual(self.range.compress(["foo100", "foo101", "foo102"]), "foo100..2")
    def test_eval(self):
        self.assertEqual(self.range.eval("foo100,foo101,foo102"), "foo100..2")
        
if __name__ == '__main__':
    unittest.main()
