// tapset for timers
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
#include "translate.h"
#include "util.h"

#include <cstring>
#include <string>


using namespace std;
using namespace __gnu_cxx;


static string TOK_TIMER("timer");


// ------------------------------------------------------------------------
// timer derived probes
// ------------------------------------------------------------------------


struct timer_derived_probe: public derived_probe
{
  int64_t interval, randomize;
  bool time_is_msecs; // NB: hrtimers get ms-based probes on modern kernels instead
  timer_derived_probe (probe* p, probe_point* l,
                       int64_t i, int64_t r, bool ms=false);
  virtual void join_group (systemtap_session& s);
};


struct timer_derived_probe_group: public generic_dpg<timer_derived_probe>
{
  void emit_interval (translator_output* o);
public:
  void emit_module_decls (systemtap_session& s);
  void emit_module_init (systemtap_session& s);
  void emit_module_exit (systemtap_session& s);
};


timer_derived_probe::timer_derived_probe (probe* p, probe_point* l,
                                          int64_t i, int64_t r, bool ms):
  derived_probe (p, l), interval (i), randomize (r), time_is_msecs(ms)
{
  if (interval <= 0 || interval > 1000000) // make i and r fit into plain ints
    throw semantic_error ("invalid interval for jiffies timer");
  // randomize = 0 means no randomization
  if (randomize < 0 || randomize > interval)
    throw semantic_error ("invalid randomize for jiffies timer");

  if (locations.size() != 1)
    throw semantic_error ("expect single probe point");
  // so we don't have to loop over them in the other functions
}


void
timer_derived_probe::join_group (systemtap_session& s)
{
  if (! s.timer_derived_probes)
    s.timer_derived_probes = new timer_derived_probe_group ();
  s.timer_derived_probes->enroll (this);
}


void
timer_derived_probe_group::emit_interval (translator_output* o)
{
  o->line() << "({";
  o->newline(1) << "unsigned i = stp->intrv;";
  o->newline() << "if (stp->rnd != 0)";
  o->newline(1) << "i += _stp_random_pm(stp->rnd);";
  o->newline(-1) << "stp->ms ? msecs_to_jiffies(i) : i;";
  o->newline(-1) << "})";
}


void
timer_derived_probe_group::emit_module_decls (systemtap_session& s)
{
  if (probes.empty()) return;

  s.op->newline() << "/* ---- timer probes ---- */";

  s.op->newline() << "static struct stap_timer_probe {";
  s.op->newline(1) << "struct timer_list timer_list;";
  s.op->newline() << "const char *pp;";
  s.op->newline() << "void (*ph) (struct context*);";
  s.op->newline() << "unsigned intrv, ms, rnd;";
  s.op->newline(-1) << "} stap_timer_probes [" << probes.size() << "] = {";
  s.op->indent(1);
  for (unsigned i=0; i < probes.size(); i++)
    {
      s.op->newline () << "{";
      s.op->line() << " .pp="
                   << lex_cast_qstring (*probes[i]->sole_location()) << ",";
      s.op->line() << " .ph=&" << probes[i]->name << ",";
      s.op->line() << " .intrv=" << probes[i]->interval << ",";
      s.op->line() << " .ms=" << probes[i]->time_is_msecs << ",";
      s.op->line() << " .rnd=" << probes[i]->randomize;
      s.op->line() << " },";
    }
  s.op->newline(-1) << "};";
  s.op->newline();

  s.op->newline() << "static void enter_timer_probe (unsigned long val) {";
  s.op->newline(1) << "struct stap_timer_probe* stp = & stap_timer_probes [val];";
  s.op->newline() << "if ((atomic_read (&session_state) == STAP_SESSION_STARTING) ||";
  s.op->newline() << "    (atomic_read (&session_state) == STAP_SESSION_RUNNING))";
  s.op->newline(1) << "mod_timer (& stp->timer_list, jiffies + ";
  emit_interval (s.op);
  s.op->line() << ");";
  s.op->newline(-1) << "{";
  s.op->indent(1);
  common_probe_entryfn_prologue (s.op, "STAP_SESSION_RUNNING", "stp->pp");
  s.op->newline() << "(*stp->ph) (c);";
  common_probe_entryfn_epilogue (s.op);
  s.op->newline(-1) << "}";
  s.op->newline(-1) << "}";
}


void
timer_derived_probe_group::emit_module_init (systemtap_session& s)
{
  if (probes.empty()) return;

  s.op->newline() << "for (i=0; i<" << probes.size() << "; i++) {";
  s.op->newline(1) << "struct stap_timer_probe* stp = & stap_timer_probes [i];";
  s.op->newline() << "probe_point = stp->pp;";
  s.op->newline() << "init_timer (& stp->timer_list);";
  s.op->newline() << "stp->timer_list.function = & enter_timer_probe;";
  s.op->newline() << "stp->timer_list.data = i;"; // NB: important!
  // copy timer renew calculations from above :-(
  s.op->newline() << "stp->timer_list.expires = jiffies + ";
  emit_interval (s.op);
  s.op->line() << ";";
  s.op->newline() << "add_timer (& stp->timer_list);";
  // note: no partial failure rollback is needed: add_timer cannot fail.
  s.op->newline(-1) << "}"; // for loop
}


void
timer_derived_probe_group::emit_module_exit (systemtap_session& s)
{
  if (probes.empty()) return;

  s.op->newline() << "for (i=0; i<" << probes.size() << "; i++)";
  s.op->newline(1) << "del_timer_sync (& stap_timer_probes[i].timer_list);";
  s.op->indent(-1);
}



// ------------------------------------------------------------------------
// hrtimer derived probes
// ------------------------------------------------------------------------
// This is a new timer interface that provides more flexibility in specifying
// intervals, and uses the hrtimer APIs when available for greater precision.
// While hrtimers were added in 2.6.16, the API's weren't exported until
// 2.6.17, so we must check this kernel version before attempting to use
// hrtimers.
//
// * hrtimer_derived_probe: creates a probe point based on the hrtimer APIs.


struct hrtimer_derived_probe: public derived_probe
{
  // set a (generous) maximum of one day in ns
  static const int64_t max_ns_interval = 1000000000LL * 60LL * 60LL * 24LL;

  // 100us seems like a reasonable minimum
  static const int64_t min_ns_interval = 100000LL;

  int64_t interval, randomize;

  hrtimer_derived_probe (probe* p, probe_point* l, int64_t i, int64_t r,
                         int64_t scale):
    derived_probe (p, l), interval (i), randomize (r)
  {
    if ((i < min_ns_interval) || (i > max_ns_interval))
      throw semantic_error(string("interval value out of range (")
                           + lex_cast<string>(scale < min_ns_interval
                                              ? min_ns_interval/scale : 1)
                           + ","
                           + lex_cast<string>(max_ns_interval/scale) + ")");

    // randomize = 0 means no randomization
    if ((r < 0) || (r > i))
      throw semantic_error("randomization value out of range");
  }

  void join_group (systemtap_session& s);
};


struct hrtimer_derived_probe_group: public generic_dpg<hrtimer_derived_probe>
{
  void emit_interval (translator_output* o);
public:
  void emit_module_decls (systemtap_session& s);
  void emit_module_init (systemtap_session& s);
  void emit_module_exit (systemtap_session& s);
};


void
hrtimer_derived_probe::join_group (systemtap_session& s)
{
  if (! s.hrtimer_derived_probes)
    s.hrtimer_derived_probes = new hrtimer_derived_probe_group ();
  s.hrtimer_derived_probes->enroll (this);
}


void
hrtimer_derived_probe_group::emit_interval (translator_output* o)
{
  o->line() << "({";
  o->newline(1) << "unsigned long nsecs;";
  o->newline() << "int64_t i = stp->intrv;";
  o->newline() << "if (stp->rnd != 0) {";
  // XXX: why not use stp_random_pm instead of this?
  o->newline(1) << "int64_t r;";
  o->newline() << "get_random_bytes(&r, sizeof(r));";
  // ensure that r is positive
  o->newline() << "r &= ((uint64_t)1 << (8*sizeof(r) - 1)) - 1;";
  o->newline() << "r = _stp_mod64(NULL, r, (2*stp->rnd+1));";
  o->newline() << "r -= stp->rnd;";
  o->newline() << "i += r;";
  o->newline(-1) << "}";
  o->newline() << "if (unlikely(i < stap_hrtimer_resolution))";
  o->newline(1) << "i = stap_hrtimer_resolution;";
  o->indent(-1);
  o->newline() << "nsecs = do_div(i, NSEC_PER_SEC);";
  o->newline() << "ktime_set(i, nsecs);";
  o->newline(-1) << "})";
}


void
hrtimer_derived_probe_group::emit_module_decls (systemtap_session& s)
{
  if (probes.empty()) return;

  s.op->newline() << "/* ---- hrtimer probes ---- */";

  s.op->newline() << "static unsigned long stap_hrtimer_resolution;"; // init later
  s.op->newline() << "static struct stap_hrtimer_probe {";
  s.op->newline(1) << "struct hrtimer hrtimer;";
  s.op->newline() << "const char *pp;";
  s.op->newline() << "void (*ph) (struct context*);";
  s.op->newline() << "int64_t intrv, rnd;";
  s.op->newline(-1) << "} stap_hrtimer_probes [" << probes.size() << "] = {";
  s.op->indent(1);
  for (unsigned i=0; i < probes.size(); i++)
    {
      s.op->newline () << "{";
      s.op->line() << " .pp=" << lex_cast_qstring (*probes[i]->sole_location()) << ",";
      s.op->line() << " .ph=&" << probes[i]->name << ",";
      s.op->line() << " .intrv=" << probes[i]->interval << "LL,";
      s.op->line() << " .rnd=" << probes[i]->randomize << "LL";
      s.op->line() << " },";
    }
  s.op->newline(-1) << "};";
  s.op->newline();

  // autoconf: add get/set expires if missing (pre 2.6.28-rc1)
  s.op->newline() << "#ifndef STAPCONF_HRTIMER_GETSET_EXPIRES";
  s.op->newline() << "#define hrtimer_get_expires(timer) ((timer)->expires)";
  s.op->newline() << "#define hrtimer_set_expires(timer, time) (void)((timer)->expires = (time))";
  s.op->newline() << "#endif";

  // autoconf: adapt to HRTIMER_REL -> HRTIMER_MODE_REL renaming near 2.6.21
  s.op->newline() << "#ifdef STAPCONF_HRTIMER_REL";
  s.op->newline() << "#define HRTIMER_MODE_REL HRTIMER_REL";
  s.op->newline() << "#endif";

  // The function signature changed in 2.6.21.
  s.op->newline() << "#ifdef STAPCONF_HRTIMER_REL";
  s.op->newline() << "static int ";
  s.op->newline() << "#else";
  s.op->newline() << "static enum hrtimer_restart ";
  s.op->newline() << "#endif";
  s.op->newline() << "enter_hrtimer_probe (struct hrtimer *timer) {";

  s.op->newline(1) << "int rc = HRTIMER_NORESTART;";
  s.op->newline() << "struct stap_hrtimer_probe *stp = container_of(timer, struct stap_hrtimer_probe, hrtimer);";
  s.op->newline() << "if ((atomic_read (&session_state) == STAP_SESSION_STARTING) ||";
  s.op->newline() << "    (atomic_read (&session_state) == STAP_SESSION_RUNNING)) {";
  // Compute next trigger time
  s.op->newline(1) << "hrtimer_set_expires(timer, ktime_add (hrtimer_get_expires(timer),";
  emit_interval (s.op);
  s.op->line() << "));";
  s.op->newline() << "rc = HRTIMER_RESTART;";
  s.op->newline(-1) << "}";
  s.op->newline() << "{";
  s.op->indent(1);
  common_probe_entryfn_prologue (s.op, "STAP_SESSION_RUNNING", "stp->pp");
  s.op->newline() << "(*stp->ph) (c);";
  common_probe_entryfn_epilogue (s.op);
  s.op->newline(-1) << "}";
  s.op->newline() << "return rc;";
  s.op->newline(-1) << "}";
}


void
hrtimer_derived_probe_group::emit_module_init (systemtap_session& s)
{
  if (probes.empty()) return;

  s.op->newline() << "{";
  s.op->newline(1) << "struct timespec res;";
  s.op->newline() << "hrtimer_get_res (CLOCK_MONOTONIC, &res);";
  s.op->newline() << "stap_hrtimer_resolution = timespec_to_ns (&res);";
  s.op->newline(-1) << "}";

  s.op->newline() << "for (i=0; i<" << probes.size() << "; i++) {";
  s.op->newline(1) << "struct stap_hrtimer_probe* stp = & stap_hrtimer_probes [i];";
  s.op->newline() << "probe_point = stp->pp;";
  s.op->newline() << "hrtimer_init (& stp->hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);";
  s.op->newline() << "stp->hrtimer.function = & enter_hrtimer_probe;";
  // There is no hrtimer field to identify *this* (i-th) probe handler
  // callback.  So instead we'll deduce it at entry time.
  s.op->newline() << "(void) hrtimer_start (& stp->hrtimer, ";
  emit_interval (s.op);
  s.op->line() << ", HRTIMER_MODE_REL);";
  // Note: no partial failure rollback is needed: hrtimer_start only
  // "fails" if the timer was already active, which cannot be.
  s.op->newline(-1) << "}"; // for loop
}


void
hrtimer_derived_probe_group::emit_module_exit (systemtap_session& s)
{
  if (probes.empty()) return;

  s.op->newline() << "for (i=0; i<" << probes.size() << "; i++)";
  s.op->newline(1) << "hrtimer_cancel (& stap_hrtimer_probes[i].hrtimer);";
  s.op->indent(-1);
}



// ------------------------------------------------------------------------
// profile derived probes
// ------------------------------------------------------------------------
//   On kernels < 2.6.10, this uses the register_profile_notifier API to
//   generate the timed events for profiling; on kernels >= 2.6.10 this
//   uses the register_timer_hook API.  The latter doesn't currently allow
//   simultaneous users, so insertion will fail if the profiler is busy.
//   (Conflicting users may include OProfile, other SystemTap probes, etc.)


struct profile_derived_probe: public derived_probe
{
  profile_derived_probe (systemtap_session &s, probe* p, probe_point* l);
  void join_group (systemtap_session& s);
};


struct profile_derived_probe_group: public generic_dpg<profile_derived_probe>
{
public:
  void emit_module_decls (systemtap_session& s);
  void emit_module_init (systemtap_session& s);
  void emit_module_exit (systemtap_session& s);
};


profile_derived_probe::profile_derived_probe (systemtap_session &, probe* p, probe_point* l):
  derived_probe(p, l)
{
}


void
profile_derived_probe::join_group (systemtap_session& s)
{
  if (! s.profile_derived_probes)
    s.profile_derived_probes = new profile_derived_probe_group ();
  s.profile_derived_probes->enroll (this);
}


struct profile_builder: public derived_probe_builder
{
  profile_builder() {}
  virtual void build(systemtap_session & sess,
                     probe * base,
                     probe_point * location,
                     literal_map_t const &,
                     vector<derived_probe *> & finished_results)
  {
    sess.unwindsym_modules.insert ("kernel");
    finished_results.push_back(new profile_derived_probe(sess, base, location));
  }
};


// timer.profile probe handlers are hooked up in an entertaining way
// to the underlying kernel facility.  The fact that 2.6.11+ era
// "register_timer_hook" API allows only one consumer *system-wide*
// will give a hint.  We will have a single entry function (and thus
// trivial registration / unregistration), and it will call all probe
// handler functions in sequence.

void
profile_derived_probe_group::emit_module_decls (systemtap_session& s)
{
  if (probes.empty()) return;

  // kernels < 2.6.10: use register_profile_notifier API
  // kernels >= 2.6.10: use register_timer_hook API
  s.op->newline() << "/* ---- profile probes ---- */";

  // This function calls all the profiling probe handlers in sequence.
  // The only tricky thing is that the context will be reused amongst
  // them.  While a simple sequence of calls to the individual probe
  // handlers is unlikely to go terribly wrong (with c->last_error
  // being set causing an early return), but for extra assurance, we
  // open-code the same logic here.

  s.op->newline() << "static void enter_all_profile_probes (struct pt_regs *regs) {";
  s.op->indent(1);
  string pp = lex_cast_qstring("timer.profile"); // hard-coded for convenience
  common_probe_entryfn_prologue (s.op, "STAP_SESSION_RUNNING", pp);
  s.op->newline() << "c->regs = regs;";

  for (unsigned i=0; i<probes.size(); i++)
    {
      if (i > 0)
        {
          // Some lightweight inter-probe context resetting
          // XXX: not quite right: MAXERRORS not respected
          s.op->newline() << "c->actionremaining = MAXACTION;";
        }
      s.op->newline() << "if (c->last_error == NULL) " << probes[i]->name << " (c);";
    }
  common_probe_entryfn_epilogue (s.op);
  s.op->newline(-1) << "}";

  s.op->newline() << "#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,10)"; // == using_rpn of yore

  s.op->newline() << "static int enter_profile_probes (struct notifier_block *self,"
                  << " unsigned long val, void *data) {";
  s.op->newline(1) << "(void) self; (void) val;";
  s.op->newline() << "enter_all_profile_probes ((struct pt_regs *) data);";
  s.op->newline() << "return 0;";
  s.op->newline(-1) << "}";
  s.op->newline() << "struct notifier_block stap_profile_notifier = {"
                  << " .notifier_call = & enter_profile_probes };";

  s.op->newline() << "#else";

  s.op->newline() << "static int enter_profile_probes (struct pt_regs *regs) {";
  s.op->newline(1) << "enter_all_profile_probes (regs);";
  s.op->newline() << "return 0;";
  s.op->newline(-1) << "}";

  s.op->newline() << "#endif";
}


void
profile_derived_probe_group::emit_module_init (systemtap_session& s)
{
  if (probes.empty()) return;

  s.op->newline() << "probe_point = \"timer.profile\";"; // NB: hard-coded for convenience
  s.op->newline() << "#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,10)"; // == using_rpn of yore
  s.op->newline() << "rc = register_profile_notifier (& stap_profile_notifier);";
  s.op->newline() << "#else";
  s.op->newline() << "rc = register_timer_hook (& enter_profile_probes);";
  s.op->newline() << "#endif";
}


void
profile_derived_probe_group::emit_module_exit (systemtap_session& s)
{
  if (probes.empty()) return;

  s.op->newline() << "for (i=0; i<" << probes.size() << "; i++)";
  s.op->newline(1) << "#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,10)"; // == using_rpn of yore
  s.op->newline() << "unregister_profile_notifier (& stap_profile_notifier);";
  s.op->newline() << "#else";
  s.op->newline() << "unregister_timer_hook (& enter_profile_probes);";
  s.op->newline() << "#endif";
  s.op->indent(-1);
}



// ------------------------------------------------------------------------
// unified probe builder for timer probes
// ------------------------------------------------------------------------


struct timer_builder: public derived_probe_builder
{
    virtual void build(systemtap_session & sess,
                       probe * base, probe_point * location,
                       literal_map_t const & parameters,
                       vector<derived_probe *> & finished_results);

    static void register_patterns(systemtap_session& s);
};

void
timer_builder::build(systemtap_session & sess,
    probe * base,
    probe_point * location,
    literal_map_t const & parameters,
    vector<derived_probe *> & finished_results)
{
  int64_t scale=1, period, rand=0;

  if (!get_param(parameters, "randomize", rand))
    rand = 0;

  if (get_param(parameters, "jiffies", period))
    {
      // always use basic timers for jiffies
      finished_results.push_back
        (new timer_derived_probe(base, location, period, rand, false));
      return;
    }
  else if (get_param(parameters, "hz", period))
    {
      if (period <= 0)
        throw semantic_error ("frequency must be greater than 0");
      period = (1000000000 + period - 1)/period;
    }
  else if (get_param(parameters, "s", period) ||
           get_param(parameters, "sec", period))
    {
      scale = 1000000000;
      period *= scale;
      rand *= scale;
    }
  else if (get_param(parameters, "ms", period) ||
           get_param(parameters, "msec", period))
    {
      scale = 1000000;
      period *= scale;
      rand *= scale;
    }
  else if (get_param(parameters, "us", period) ||
           get_param(parameters, "usec", period))
    {
      scale = 1000;
      period *= scale;
      rand *= scale;
    }
  else if (get_param(parameters, "ns", period) ||
           get_param(parameters, "nsec", period))
    {
      // ok
    }
  else
    throw semantic_error ("unrecognized timer variant");

  // Redirect wallclock-time based probes to hrtimer code on recent
  // enough kernels.
  if (strverscmp(sess.kernel_base_release.c_str(), "2.6.17") < 0)
    {
      // hrtimers didn't exist, so use the old-school timers
      period = (period + 1000000 - 1)/1000000;
      rand = (rand + 1000000 - 1)/1000000;

      finished_results.push_back
        (new timer_derived_probe(base, location, period, rand, true));
    }
  else
    finished_results.push_back
      (new hrtimer_derived_probe(base, location, period, rand, scale));
}

void
register_tapset_timers(systemtap_session& s)
{
  match_node* root = s.pattern_root;
  derived_probe_builder *builder = new timer_builder();

  root = root->bind(TOK_TIMER);

  root->bind_num("s")->bind(builder);
  root->bind_num("s")->bind_num("randomize")->bind(builder);
  root->bind_num("sec")->bind(builder);
  root->bind_num("sec")->bind_num("randomize")->bind(builder);

  root->bind_num("ms")->bind(builder);
  root->bind_num("ms")->bind_num("randomize")->bind(builder);
  root->bind_num("msec")->bind(builder);
  root->bind_num("msec")->bind_num("randomize")->bind(builder);

  root->bind_num("us")->bind(builder);
  root->bind_num("us")->bind_num("randomize")->bind(builder);
  root->bind_num("usec")->bind(builder);
  root->bind_num("usec")->bind_num("randomize")->bind(builder);

  root->bind_num("ns")->bind(builder);
  root->bind_num("ns")->bind_num("randomize")->bind(builder);
  root->bind_num("nsec")->bind(builder);
  root->bind_num("nsec")->bind_num("randomize")->bind(builder);

  root->bind_num("jiffies")->bind(builder);
  root->bind_num("jiffies")->bind_num("randomize")->bind(builder);

  root->bind_num("hz")->bind(builder);

  root->bind("profile")->bind(new profile_builder());
}



/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
