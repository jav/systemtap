// -*- C++ -*-
// Copyright (C) 2009 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#ifndef TASK_FINDER_H
#define TASK_FINDER_H

// Declare that task_finder is needed in this session
void enable_task_finder(systemtap_session& s);

// Declare that vma tracker is needed in this session,
// implies that the task_finder is needed.
void enable_vma_tracker(systemtap_session& s);

// Whether the vma tracker is needed in this session.
bool vma_tracker_enabled(systemtap_session& s);

#endif // TASK_FINDER_H

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
