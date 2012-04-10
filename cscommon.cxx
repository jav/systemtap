/*
 Compile-server and client common functions
 Copyright (C) 2011 Red Hat Inc.

 This file is part of systemtap, and is free software.  You can
 redistribute it and/or modify it under the terms of the GNU General
 Public License (GPL); either version 2, or (at your option) any
 later version.
*/
#include "config.h"

// Disable the code in this file if NSS is not available
#if HAVE_NSS
#include "util.h"
#include "cscommon.h"

#include <fstream>
#include <string>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <iomanip>

extern "C"
{
#include <ssl.h>
}

using namespace std;

cs_protocol_version::~cs_protocol_version ()
{
  assert (this->v);
  free ((void*)this->v);
}

const cs_protocol_version &
cs_protocol_version::operator= (const char *v)
{
  if (this->v)
    free ((void *)this->v);
  this->v = strdup (v);
  return *this;
}

bool
cs_protocol_version::operator< (const cs_protocol_version &that) const
{
  // Compare the levels of each version in turn.
  vector<string> these_tokens;
  tokenize (this->v, these_tokens, ".");
  vector<string> those_tokens;
  tokenize (that.v, those_tokens, ".");

  unsigned this_limit = these_tokens.size ();
  unsigned that_limit = those_tokens.size ();
  unsigned i;
  for (i = 0; i < this_limit && i < that_limit; ++i)
    {
      char *e;
      unsigned long this_level = strtoul (these_tokens[i].c_str (), & e, 0);
      assert (! *e);
      unsigned long that_level = strtoul (those_tokens[i].c_str (), & e, 0);
      assert (! *e);
      if (this_level > that_level)
	return false;
      if (this_level < that_level)
	return true;
    }

  // If the other version has more components, then this one is less than that one.
  if (i < that_limit)
    {
      assert (i == this_limit);
      return true;
    }
  // This version is greater than or equal to that one.
  return false;
}

int
read_from_file (const string &fname, cs_protocol_version &data)
{
  // C++ streams may not set errno in the even of a failure. However if we
  // set it to 0 before each operation and it gets set during the operation,
  // then we can use its value in order to determine what happened.
  string dataStr;
  errno = 0;
  ifstream f (fname.c_str ());
  if (! f.good ())
    {
      clog << _F("Unable to open file '%s' for reading: ", fname.c_str());
      goto error;
    }

  // Read the data;
  errno = 0;
  f >> dataStr;
  if (f.fail ())
    {
      clog << _F("Unable to read from file '%s': ", fname.c_str());
      goto error;
    }

  data = dataStr.c_str ();

  // NB: not necessary to f.close ();
  return 0; // Success

 error:
  if (errno)
    clog << strerror (errno) << endl;
  else
    clog << _("unknown error") << endl;
  return 1; // Failure
}

string get_cert_serial_number (const CERTCertificate *cert)
{
  ostringstream serialNumber;
  serialNumber << hex << setfill('0') << right;
  for (unsigned i = 0; i < cert->serialNumber.len; ++i)
    {
      if (i > 0)
	serialNumber << ':';
      serialNumber << setw(2) << (unsigned)cert->serialNumber.data[i];
    }
  return serialNumber.str ();
}
#endif /* HAVE_NSS */
