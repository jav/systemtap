// task finder for user tapsets
// Copyright (C) 2005-2009 Red Hat Inc.
// Copyright (C) 2005-2007 Intel Corporation.
// Copyright (C) 2008 James.Bottomley@HansenPartnership.com
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.


#include "session.h"
#include "tapsets.h"
#include "task_finder.h"
#include "translate.h"
#include "util.h"

#include <cstring>
#include <string>


using namespace std;
using namespace __gnu_cxx;


// ------------------------------------------------------------------------
// task_finder derived 'probes': These don't really exist.  The whole
// purpose of the task_finder_derived_probe_group is to make sure that
// stap_start_task_finder()/stap_stop_task_finder() get called only
// once and in the right place.
// ------------------------------------------------------------------------

struct task_finder_derived_probe: public derived_probe
{
  // Dummy constructor for gcc 3.4 compatibility
  task_finder_derived_probe (): derived_probe (0) { assert(0); }
};


struct task_finder_derived_probe_group: public generic_dpg<task_finder_derived_probe>
{
public:
  void emit_module_decls (systemtap_session& ) { }
  void emit_module_init (systemtap_session& s);
  void emit_module_exit (systemtap_session& s);
};


void
task_finder_derived_probe_group::emit_module_init (systemtap_session& s)
{
  s.op->newline();
  s.op->newline() << "/* ---- task finder ---- */";
  s.op->newline() << "rc = stap_start_task_finder();";

  s.op->newline() << "if (rc) {";
  s.op->newline(1) << "stap_stop_task_finder();";
  s.op->newline(-1) << "}";
}


void
task_finder_derived_probe_group::emit_module_exit (systemtap_session& s)
{
  s.op->newline();
  s.op->newline() << "/* ---- task finder ---- */";
  s.op->newline() << "stap_stop_task_finder();";
}


// Declare that task_finder is needed in this session
void
enable_task_finder(systemtap_session& s)
{
  if (! s.task_finder_derived_probes)
    s.task_finder_derived_probes = new task_finder_derived_probe_group();
}

// Helper function to emit vma tracker callbacks.
void
emit_vma_callback_probe_decl (systemtap_session& s,
			      string path,
			      int64_t pid)
{
  s.op->newline() << "{";
  if (pid == 0)
    {
      s.op->line() << " .pathname=\"" << path << "\",";
      s.op->line() << " .pid=0,";
    }
  else
    {
      s.op->line() << " .pathname=NULL,";
      s.op->line() << " .pid=" << pid << ",";
    }
  s.op->line() << " .callback=NULL,";
  s.op->line() << " .mmap_callback=&_stp_tf_mmap_cb,";
  s.op->line() << " .munmap_callback=&_stp_tf_munmap_cb,";
  s.op->line() << " .mprotect_callback=NULL,";
  s.op->line() << " },";
}

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
