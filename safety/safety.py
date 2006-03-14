#!/usr/bin/env python
# -*- coding: utf-8 -*-
# vim: noet sw=4 ts=4 enc=utf-8
"A static safety-checker for SystemTap modules."

# Copyright (C) 2006 Intel Corporation.
#
# This file is part of systemtap, and is free software.  You can
# redistribute it and/or modify it under the terms of the GNU General
# Public License (GPL); either version 2, or (at your option) any
# later version.


# in python 2.4, set & frozenset are builtins
# in python 2.3, the equivalents live in the 'sets' module
from sys import hexversion as __hexversion
if __hexversion < 0x020400f0:
	from sets import Set as set, ImmutableSet as frozenset


def main(argv):
	"""
		CLI to the SystemTap static safety-checker.

		Provides a command-line interface for running the SystemTap module
		safety checker.  Use '-h' or '--help' for a description of the
		command-line options.

		Returns the number of modules that failed the check.
	"""
	bad = 0
	(options, args) = __parse_args(argv[1:])
	safe = StaticSafety(options.arch, options.release, options.datapath)
	for m in args:
		if not safe.check_module(m):
			bad += 1
	return bad


def __parse_args(argv):
	from optparse import OptionParser
	parser = OptionParser(usage="usage: %prog [options] [module]...",
			description=__doc__)
	parser.add_option('--data-path', dest='datapath', metavar='PATH',
			help='specify the whitelist data files [default: <script-dir>/data]')
	parser.add_option('-m', '--machine', '--architecture', dest='arch',
			help='specify the machine architecture of the target')
	parser.add_option('-r', '--kernel-release', dest='release',
			help='specify the kernel release running on the target')
	return parser.parse_args(argv)


class StaticSafety:
	"Manage a safety-checking session."

	def __init__(self, arch=None, release=None, datapath=None):
		from os import uname
		self.__arch = arch or uname()[4]
		self.__release = release or uname()[2]
		self.__build_data_path(datapath)
		self.__build_search_suffixes()
		self.__load_allowed_references()
		self.__load_allowed_opcodes()

	def __build_data_path(self, datapath):
		"Determine where the data directory resides."
		from sys import argv
		from os.path import dirname, isdir, realpath
		if datapath is None:
			local = dirname(realpath(argv[0]))
			self.__data_path = local + '/data'
		else:
			self.__data_path = datapath

		if not isdir(self.__data_path):
			raise StandardError(
					"Can't find the data directory! (looking in %s)"
					% self.__data_path)

	def __build_search_suffixes(self):
		"Construct arch & kernel-versioning search suffixes."
		ss = set()

		# add empty string
		ss.add('')

		# add architecture search path
		archsfx = '-%s' % self.__arch
		ss.add(archsfx)

		# add full kernel-version-release (2.6.NN-FOOBAR) + arch
		relsfx = '-%s' % self.__release
		ss.add(relsfx)
		ss.add(relsfx + archsfx)

		# add kernel version (2.6.NN) + arch
		dash_i = relsfx.find('-')
		if dash_i > 0:
			ss.add(relsfx[:dash_i])
			ss.add(relsfx[:dash_i] + archsfx)

		# start dropping decimals
		dot_i = relsfx.rfind('.', 0, dash_i)
		while dot_i > 0:
			ss.add(relsfx[:dot_i])
			ss.add(relsfx[:dot_i] + archsfx)
			dot_i = relsfx.rfind('.', 0, dot_i)

		self.__search_suffixes = frozenset(ss)
	
	def __load_allowed_references(self):
		"Build the list of allowed external references from the data files."
		wr = set()
		for sfx in self.__search_suffixes:
			try:
				refs = open(self.__data_path + '/references' + sfx)
				for line in refs:
					wr.add(line.rstrip())
				refs.close()
			except IOError:
				pass
		if not len(wr):
			raise StandardError("No whitelisted references found!")
		self.__white_references = frozenset(wr)

	def __load_allowed_opcodes(self):
		"Build the regular expression matcher for allowed opcodes from the data files."
		from re import compile
		wo = []
		for sfx in self.__search_suffixes:
			try:
				opcs = open(self.__data_path + '/opcodes' + sfx)
				for line in opcs:
					wo.append(line.rstrip())
				opcs.close()
			except IOError:
				pass
		if not len(wo):
			raise StandardError("No whitelisted opcodes found!")
		self.__white_opcodes_re = compile(r'^(?:' + r'|'.join(wo) + r')$')

	def __check_references(self, module):
		"Check that all unresolved references in the module are allowed."
		from os import popen
		from re import compile

		sym_re = compile(r'^([\w@.]+) [Uw]\s+$')
		def check(line):
			m = sym_re.match(line)
			if m:
				ref = m.group(1)
				if ref not in self.__white_references:
					print 'ERROR: Invalid reference to %s' % ref
					return False
				return True
			print 'WARNING: Unmatched line:\n  %s' % `line`
			return True

		command = 'nm --format=posix --no-sort --undefined-only ' + `module`
		ok = True
		nm = popen(command)
		for line in nm:
			ok &= check(line)
		if nm.close():
			ok = False
		return ok

	def __check_opcodes(self, module):
		"Check that all disassembled opcodes in the module are allowed."
		from os import popen
		from re import compile

		skip_ud2a = [0]

		ignore_re = compile(r'^$|^\s+\.{3}$|^.*Disassembly of section|^.*file format')
		if self.__arch == 'ia64':
			opc = r'(?:\[[IBFLMX]{3}\]\s+)?(?:\(p\d\d\)\s+)?([\w.]+)\b'
		elif self.__arch == 'x86_64' or self.__arch == 'i686':
			opc = r'(?:lock\s+)?|(?:repn?[ze]?\s+)?|(?:rex\w+\s+)?(\w+)\b'
		else:
			opc = r'(\w+)\b'
		opc_re = compile(r'^[A-Fa-f\d]+\s+<([^>]+)>\s+%s' % opc)
		def check(line):
			m = ignore_re.match(line)
			if m:
				return True
			m = opc_re.match(line)
			if m:
				loc, opc = m.groups()
				if opc == 'ud2a':
					# The kernel abuses ud2a for BUG checks by following it
					# directly with __LINE__ and __FILE__.  Objdump doesn't
					# know this though, so it tries to interpret the data as
					# real instructions.  Because x86(-64) instructions are
					# variable-length, it's hard to tell when objdump is synced
					# up again.  We'll fast-forward to the next function
					# boundary and hope things are better there.
					for skip in objdump:
						mskip = opc_re.match(skip)
						if mskip:
							locskip = mskip.group(1)
							# a loc without an offset marks a new function
							if '+' not in locskip:
								return check(skip)
							skip_ud2a[0] += 1
					return True
				elif not self.__white_opcodes_re.match(opc):
					print "ERROR: Invalid opcode '%s' at <%s>" % (opc, loc)
					return False
				return True
			print 'WARNING: Unmatched line:\n  %s' % `line`
			return True

		command = 'objdump --disassemble --prefix-addresses ' + `module`
		ok = True
		objdump = popen(command)
		for line in objdump:
			ok &= check(line)
		if objdump.close():
			ok = False

		if skip_ud2a[0]:
			#print 'WARNING: Skipped %d lines due to ud2a corruption' % skip_ud2a[0]
			pass

		return ok

	def check_module(self, module):
		"Check a module for exclusively safe opcodes and external references."
		from os.path import isfile
		if not isfile(module):
			print 'ERROR: %s is not a file!' % `module`
			return False
		res = self.__check_references(module) and self.__check_opcodes(module)
		if res:
			print 'PASS: %s' % module
		else:
			print 'FAIL: %s' % module
		return res


if __name__ == '__main__':
	from sys import exit, argv
	exit(main(argv))

