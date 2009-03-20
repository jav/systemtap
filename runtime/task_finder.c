#ifndef TASK_FINDER_C
#define TASK_FINDER_C

#if ! defined(CONFIG_UTRACE)
#error "Need CONFIG_UTRACE!"
#endif

#include <linux/utrace.h>
#include <linux/list.h>
#include <linux/binfmts.h>
#include <linux/mount.h>

#include "syscall.h"
#include "utrace_compatibility.h"
#include "task_finder_vma.c"

static LIST_HEAD(__stp_task_finder_list);

struct stap_task_finder_target;

#define __STP_TF_STARTING	0
#define __STP_TF_RUNNING	1
#define __STP_TF_STOPPING	2
#define __STP_TF_STOPPED	3
static atomic_t __stp_task_finder_state = ATOMIC_INIT(__STP_TF_STARTING);
static atomic_t __stp_inuse_count = ATOMIC_INIT (0);

#define __stp_tf_handler_start() (atomic_inc(&__stp_inuse_count))
#define __stp_tf_handler_end() (atomic_dec(&__stp_inuse_count))

#ifdef DEBUG_TASK_FINDER
static atomic_t __stp_attach_count = ATOMIC_INIT (0);

#define debug_task_finder_attach() (atomic_inc(&__stp_attach_count))
#define debug_task_finder_detach() (atomic_dec(&__stp_attach_count))
#define debug_task_finder_report() (_stp_dbug(__FUNCTION__, __LINE__, \
					      "attach count: %d, inuse count: %d\n", \
					      atomic_read(&__stp_attach_count), \
					      atomic_read(&__stp_inuse_count)))
#else
#define debug_task_finder_attach()	/* empty */
#define debug_task_finder_detach()	/* empty */
#define debug_task_finder_report()	/* empty */
#endif

typedef int (*stap_task_finder_callback)(struct stap_task_finder_target *tgt,
					 struct task_struct *tsk,
					 int register_p,
					 int process_p);

typedef int (*stap_task_finder_vm_callback)(struct stap_task_finder_target *tgt,
					    struct task_struct *tsk,
					    int map_p, char *vm_path,
					    unsigned long vm_start,
					    unsigned long vm_end,
					    unsigned long vm_pgoff);

struct stap_task_finder_target {
/* private: */
	struct list_head list;		/* __stp_task_finder_list linkage */
	struct list_head callback_list_head;
	struct list_head callback_list;
	struct utrace_engine_ops ops;
	unsigned engine_attached:1;
	unsigned vm_events:1;
	size_t pathlen;

/* public: */
	const char *pathname;
	pid_t pid;
	stap_task_finder_callback callback;
	stap_task_finder_vm_callback vm_callback;
};

#ifdef UTRACE_ORIG_VERSION
static u32
__stp_utrace_task_finder_target_death(struct utrace_attached_engine *engine,
				      struct task_struct *tsk);
#else
static u32
__stp_utrace_task_finder_target_death(struct utrace_attached_engine *engine,
				      struct task_struct *tsk,
				      bool group_dead, int signal);
#endif

#ifdef UTRACE_ORIG_VERSION
static u32
__stp_utrace_task_finder_target_quiesce(struct utrace_attached_engine *engine,
					struct task_struct *tsk);
#else
static u32
__stp_utrace_task_finder_target_quiesce(enum utrace_resume_action action,
					struct utrace_attached_engine *engine,
					struct task_struct *tsk,
					unsigned long event);
#endif

#ifdef UTRACE_ORIG_VERSION
static u32
__stp_utrace_task_finder_target_syscall_entry(struct utrace_attached_engine *engine,
					      struct task_struct *tsk,
					      struct pt_regs *regs);
#else
static u32
__stp_utrace_task_finder_target_syscall_entry(enum utrace_resume_action action,
					      struct utrace_attached_engine *engine,
					      struct task_struct *tsk,
					      struct pt_regs *regs);
#endif

#ifdef UTRACE_ORIG_VERSION
static u32
__stp_utrace_task_finder_target_syscall_exit(struct utrace_attached_engine *engine,
					     struct task_struct *tsk,
					     struct pt_regs *regs);
#else
static u32
__stp_utrace_task_finder_target_syscall_exit(enum utrace_resume_action action,
					     struct utrace_attached_engine *engine,
					     struct task_struct *tsk,
					     struct pt_regs *regs);
#endif

static int
stap_register_task_finder_target(struct stap_task_finder_target *new_tgt)
{
	// Since this __stp_task_finder_list is (currently) only
	// written to in one big setup operation before the task
	// finder process is started, we don't need to lock it.
	struct list_head *node;
	struct stap_task_finder_target *tgt = NULL;
	int found_node = 0;

	if (new_tgt == NULL)
		return EFAULT;

	if (new_tgt->pathname != NULL)
		new_tgt->pathlen = strlen(new_tgt->pathname);
	else
		new_tgt->pathlen = 0;

	// Make sure everything is initialized properly.
	new_tgt->engine_attached = 0;
	new_tgt->vm_events = 0;
	memset(&new_tgt->ops, 0, sizeof(new_tgt->ops));
	new_tgt->ops.report_death = &__stp_utrace_task_finder_target_death;
	new_tgt->ops.report_quiesce = &__stp_utrace_task_finder_target_quiesce;
	new_tgt->ops.report_syscall_entry = \
		&__stp_utrace_task_finder_target_syscall_entry;
	new_tgt->ops.report_syscall_exit = \
		&__stp_utrace_task_finder_target_syscall_exit;

	// Search the list for an existing entry for pathname/pid.
	list_for_each(node, &__stp_task_finder_list) {
		tgt = list_entry(node, struct stap_task_finder_target, list);
		if (tgt != NULL
		    /* pathname-based target */
		    && ((new_tgt->pathlen > 0
			 && tgt->pathlen == new_tgt->pathlen
			 && strcmp(tgt->pathname, new_tgt->pathname) == 0)
			/* pid-based target (a specific pid or all
			 * pids) */
			|| (new_tgt->pathlen == 0
			    && tgt->pid == new_tgt->pid))) {
			found_node = 1;
			break;
		}
	}

	// If we didn't find a matching existing entry, add the new
	// target to the task list.
	if (! found_node) {
		INIT_LIST_HEAD(&new_tgt->callback_list_head);
		list_add(&new_tgt->list, &__stp_task_finder_list);
		tgt = new_tgt;
	}

	// Add this target to the callback list for this task.
	list_add_tail(&new_tgt->callback_list, &tgt->callback_list_head);

	// If the new target has a vm_callback, remember this.
	if (new_tgt->vm_callback != NULL)
		tgt->vm_events = 1;
	return 0;
}

static int
stap_utrace_detach(struct task_struct *tsk,
		   const struct utrace_engine_ops *ops)
{
	struct utrace_attached_engine *engine;
	struct mm_struct *mm;
	int rc = 0;

	// Ignore init
	if (tsk == NULL || tsk->pid <= 1)
		return 0;

#ifdef PF_KTHREAD
	// Ignore kernel threads.  On systems without PF_KTHREAD,
	// we're ok, since kernel threads won't be matched by the
	// utrace_attach_task() call below.
	if (tsk->flags & PF_KTHREAD)
		return 0;
#endif

	// Notice we're not calling get_task_mm() here.  Normally we
	// avoid tasks with no mm, because those are kernel threads.
	// So, why is this function different?  When a thread is in
	// the process of dying, its mm gets freed.  Then, later the
	// thread gets in the dying state and the thread's DEATH event
	// handler gets called (if any).
	//
	// If a thread is in this "mortally wounded" state - no mm
	// but not dead - and at that moment this function is called,
	// we'd miss detaching from it if we were checking to see if
	// it had an mm.

	engine = utrace_attach_task(tsk, UTRACE_ATTACH_MATCH_OPS, ops, 0);
	if (IS_ERR(engine)) {
		rc = -PTR_ERR(engine);
		if (rc != ENOENT) {
			_stp_error("utrace_attach_task returned error %d on pid %d",
				   rc, tsk->pid);
		}
		else {
			rc = 0;
		}
	}
	else if (unlikely(engine == NULL)) {
		_stp_error("utrace_attach returned NULL on pid %d",
			   (int)tsk->pid);
		rc = EFAULT;
	}
	else {
		rc = utrace_control(tsk, engine, UTRACE_DETACH);
		switch (rc) {
		case 0:			/* success */
			debug_task_finder_detach();
			break;
		case -ESRCH:	    /* REAP callback already begun */
		case -EALREADY:	    /* DEATH callback already begun */
			rc = 0;	    /* ignore these errors*/
			break;
		default:
			rc = -rc;
			_stp_error("utrace_detach returned error %d on pid %d",
				   rc, tsk->pid);
			break;
		}
		utrace_engine_put(engine);
	}
	return rc;
}

static void
stap_utrace_detach_ops(struct utrace_engine_ops *ops)
{
	struct task_struct *grp, *tsk;
	struct utrace_attached_engine *engine;
	int rc = 0;
	pid_t pid = 0;

	// Notice we're not calling get_task_mm() in this loop. In
	// every other instance when calling do_each_thread, we avoid
	// tasks with no mm, because those are kernel threads.  So,
	// why is this function different?  When a thread is in the
	// process of dying, its mm gets freed.  Then, later the
	// thread gets in the dying state and the thread's
	// UTRACE_EVENT(DEATH) event handler gets called (if any).
	//
	// If a thread is in this "mortally wounded" state - no mm
	// but not dead - and at that moment this function is called,
	// we'd miss detaching from it if we were checking to see if
	// it had an mm.

	rcu_read_lock();
	do_each_thread(grp, tsk) {
#ifdef PF_KTHREAD
		// Ignore kernel threads.  On systems without
		// PF_KTHREAD, we're ok, since kernel threads won't be
		// matched by the stap_utrace_detach() call.
		if (tsk->flags & PF_KTHREAD)
			continue;
#endif

		rc = stap_utrace_detach(tsk, ops);
		if (rc != 0)
			goto udo_err;
	} while_each_thread(grp, tsk);
udo_err:
	rcu_read_unlock();
	debug_task_finder_report();
}

static void
__stp_task_finder_cleanup(void)
{
	struct list_head *tgt_node, *tgt_next;
	struct stap_task_finder_target *tgt;

	// Walk the main list, cleaning up as we go.
	list_for_each_safe(tgt_node, tgt_next, &__stp_task_finder_list) {
		tgt = list_entry(tgt_node, struct stap_task_finder_target,
				 list);
		if (tgt == NULL)
			continue;

		if (tgt->engine_attached) {
			stap_utrace_detach_ops(&tgt->ops);
			tgt->engine_attached = 0;
		}

		// Notice we're not walking the callback_list here.
		// There isn't anything to clean up and doing it would
		// mess up callbacks in progress.

		list_del(&tgt->list);
	}
}

static char *
__stp_get_mm_path(struct mm_struct *mm, char *buf, int buflen)
{
	struct vm_area_struct *vma;
	char *rc = NULL;

	down_read(&mm->mmap_sem);
	vma = mm->mmap;
	while (vma) {
		if ((vma->vm_flags & VM_EXECUTABLE) && vma->vm_file)
			break;
		vma = vma->vm_next;
	}
	if (vma) {
#ifdef STAPCONF_DPATH_PATH
		rc = d_path(&(vma->vm_file->f_path), buf, buflen);
#else
		rc = d_path(vma->vm_file->f_dentry, vma->vm_file->f_vfsmnt,
			    buf, buflen);
#endif
	}
	else {
		*buf = '\0';
		rc = ERR_PTR(-ENOENT);
	}
	up_read(&mm->mmap_sem);
	return rc;
}

/*
 * All user threads get an engine with __STP_TASK_FINDER_EVENTS events
 * attached to it so the task_finder layer can monitor new thread
 * creation/death.
 */
#define __STP_TASK_FINDER_EVENTS (UTRACE_EVENT(CLONE)		\
				  | UTRACE_EVENT(EXEC)		\
				  | UTRACE_EVENT(DEATH))

/*
 * __STP_TASK_BASE_EVENTS: base events for stap_task_finder_target's
 * without vm_callback's
 *
 * __STP_TASK_VM_BASE_EVENTS: base events for
 * stap_task_finder_target's with vm_callback's
 */
#define __STP_TASK_BASE_EVENTS	(UTRACE_EVENT(DEATH))

#define __STP_TASK_VM_BASE_EVENTS (__STP_TASK_BASE_EVENTS	\
				   | UTRACE_EVENT(SYSCALL_ENTRY)\
				   | UTRACE_EVENT(SYSCALL_EXIT))

/*
 * All "interesting" threads get an engine with
 * __STP_ATTACHED_TASK_EVENTS events attached to it.  After the thread
 * quiesces, we reset the events to __STP_ATTACHED_TASK_BASE_EVENTS
 * events.
 */
#define __STP_ATTACHED_TASK_EVENTS (__STP_TASK_BASE_EVENTS	\
				    | UTRACE_EVENT(QUIESCE))

#define __STP_ATTACHED_TASK_BASE_EVENTS(tgt) \
	((tgt)->vm_events ? __STP_TASK_VM_BASE_EVENTS : __STP_TASK_BASE_EVENTS)

static int
__stp_utrace_attach(struct task_struct *tsk,
		    const struct utrace_engine_ops *ops, void *data,
		    unsigned long event_flags,
		    enum utrace_resume_action action)
{
	struct utrace_attached_engine *engine;
	struct mm_struct *mm;
	int rc = 0;

	// Ignore init
	if (tsk == NULL || tsk->pid <= 1)
		return EPERM;

#ifdef PF_KTHREAD
	// Ignore kernel threads
	if (tsk->flags & PF_KTHREAD)
		return EPERM;
#endif

	// Ignore threads with no mm (which are either kernel threads
	// or "mortally wounded" threads).
	mm = get_task_mm(tsk);
	if (! mm)
		return EPERM;
	mmput(mm);

	engine = utrace_attach_task(tsk, UTRACE_ATTACH_CREATE, ops, data);
	if (IS_ERR(engine)) {
		int error = -PTR_ERR(engine);
		if (error != ENOENT) {
			_stp_error("utrace_attach returned error %d on pid %d",
				   error, (int)tsk->pid);
			rc = error;
		}
	}
	else if (unlikely(engine == NULL)) {
		_stp_error("utrace_attach returned NULL on pid %d",
			   (int)tsk->pid);
		rc = EFAULT;
	}
	else {
		rc = utrace_set_events(tsk, engine, event_flags);
		if (rc == -EINPROGRESS) {
			/*
			 * It's running our callback, so we have to
			 * synchronize.  We can't keep rcu_read_lock,
			 * so the task pointer might die.  But it's
			 * safe to call utrace_barrier() even with a
			 * stale task pointer, if we have an engine
			 * ref.
			 */
			rc = utrace_barrier(tsk, engine);
			if (rc != 0)
				_stp_error("utrace_barrier returned error %d on pid %d",
					   rc, (int)tsk->pid);
		}
		if (rc == 0) {
			debug_task_finder_attach();

			if (action != UTRACE_RESUME) {
				rc = utrace_control(tsk, engine, UTRACE_STOP);
				/* EINPROGRESS means we must wait for
				 * a callback, which is what we want. */
				if (rc != 0 && rc != -EINPROGRESS)
					_stp_error("utrace_control returned error %d on pid %d",
						   rc, (int)tsk->pid);
				else
					rc = 0;
			}

		}
		else
			_stp_error("utrace_set_events2 returned error %d on pid %d",
				   rc, (int)tsk->pid);
		utrace_engine_put(engine);
	}
	return rc;
}

static int
stap_utrace_attach(struct task_struct *tsk,
		   const struct utrace_engine_ops *ops, void *data,
		   unsigned long event_flags)
{
	return __stp_utrace_attach(tsk, ops, data, event_flags, UTRACE_RESUME);
}

static inline void
__stp_call_callbacks(struct stap_task_finder_target *tgt,
		     struct task_struct *tsk, int register_p, int process_p)
{
	struct list_head *cb_node;
	int rc;

	if (tgt == NULL || tsk == NULL)
		return;

	list_for_each(cb_node, &tgt->callback_list_head) {
		struct stap_task_finder_target *cb_tgt;

		cb_tgt = list_entry(cb_node, struct stap_task_finder_target,
				    callback_list);
		if (cb_tgt == NULL || cb_tgt->callback == NULL)
			continue;

		rc = cb_tgt->callback(cb_tgt, tsk, register_p, process_p);
		if (rc != 0) {
			_stp_error("callback for %d failed: %d",
				   (int)tsk->pid, rc);
		}
	}
}

static inline void
__stp_call_vm_callbacks(struct stap_task_finder_target *tgt,
			struct task_struct *tsk, int map_p, char *vm_path,
			unsigned long vm_start, unsigned long vm_end,
			unsigned long vm_pgoff)
{
	struct list_head *cb_node;
	int rc;

	if (tgt == NULL || tsk == NULL)
		return;

	list_for_each(cb_node, &tgt->callback_list_head) {
		struct stap_task_finder_target *cb_tgt;

		cb_tgt = list_entry(cb_node, struct stap_task_finder_target,
				    callback_list);
		if (cb_tgt == NULL || cb_tgt->vm_callback == NULL)
			continue;

		rc = cb_tgt->vm_callback(cb_tgt, tsk, map_p, vm_path,
					 vm_start, vm_end, vm_pgoff);
		if (rc != 0) {
			_stp_error("vm callback for %d failed: %d",
				   (int)tsk->pid, rc);
		}
	}
}

static inline void
__stp_utrace_attach_match_filename(struct task_struct *tsk,
				   const char * const filename,
				   int register_p, int process_p)
{
	size_t filelen;
	struct list_head *tgt_node;
	struct stap_task_finder_target *tgt;

	filelen = strlen(filename);
	list_for_each(tgt_node, &__stp_task_finder_list) {
		int rc;

		tgt = list_entry(tgt_node, struct stap_task_finder_target,
				 list);
		// If we've got a matching pathname or we're probing
		// all threads, we've got a match.  We've got to keep
		// matching since a single thread could match a
		// pathname and match an "all thread" probe.
		if (tgt == NULL)
			continue;
		else if (tgt->pathlen > 0
			 && (tgt->pathlen != filelen
			     || strcmp(tgt->pathname, filename) != 0))
			continue;
		/* Ignore pid-based target, they were handled at startup. */
		else if (tgt->pid != 0)
			continue;
		/* Notice that "pid == 0" (which means to probe all
		 * threads) falls through. */

		// Set up events we need for attached tasks. When
		// register_p is set, we won't actually call the
		// callbacks here - we'll call it when the thread gets
		// quiesced.  When register_p isn't set, we can go
		// ahead and call the callbacks.
		if (register_p) {
			rc = __stp_utrace_attach(tsk, &tgt->ops,
						 tgt,
						 __STP_ATTACHED_TASK_EVENTS,
						 UTRACE_STOP);
			if (rc != 0 && rc != EPERM)
				break;
			tgt->engine_attached = 1;
		}
		else {
			// Call the callbacks, then detach.
			__stp_call_callbacks(tgt, tsk, register_p, process_p);
			rc = stap_utrace_detach(tsk, &tgt->ops);
			if (rc != 0)
				break;

			// Note that we don't want to set
			// engine_attached to 0 here - only
			// when *all* threads using this
			// engine have been detached.
		}
	}
}

// This function handles the details of getting a task's associated
// pathname, and calling __stp_utrace_attach_match_filename() to
// attach to it if we find the pathname "interesting".  So, what's the
// difference between path_tsk and match_tsk?  Normally they are the
// same, except in one case.  In an UTRACE_EVENT(EXEC), we need to
// detach engines from the newly exec'ed process (since its path has
// changed).  In this case, we have to match the path of the parent
// (path_tsk) against the child (match_tsk).

static void
__stp_utrace_attach_match_tsk(struct task_struct *path_tsk,
			      struct task_struct *match_tsk, int register_p,
			      int process_p)
{
	struct mm_struct *mm;
	char *mmpath_buf;
	char *mmpath;

	if (path_tsk == NULL || path_tsk->pid <= 1
	    || match_tsk == NULL || match_tsk->pid <= 1)
		return;

	/* Grab the path associated with the path_tsk. */
	mm = get_task_mm(path_tsk);
	if (! mm) {
		/* If the thread doesn't have a mm_struct, it is
		 * a kernel thread which we need to skip. */
		return;
	}

	// Allocate space for a path
	mmpath_buf = _stp_kmalloc(PATH_MAX);
	if (mmpath_buf == NULL) {
		mmput(mm);
		_stp_error("Unable to allocate space for path");
		return;
	}

	// Grab the path associated with the new task
	mmpath = __stp_get_mm_path(mm, mmpath_buf, PATH_MAX);
	mmput(mm);			/* We're done with mm */
	if (mmpath == NULL || IS_ERR(mmpath)) {
		int rc = -PTR_ERR(mmpath);
		if (rc != ENOENT)
			_stp_error("Unable to get path (error %d) for pid %d",
				   rc, (int)path_tsk->pid);
	}
	else {
		__stp_utrace_attach_match_filename(match_tsk, mmpath,
						   register_p, process_p);
	}

	_stp_kfree(mmpath_buf);
	return;
}

#ifdef UTRACE_ORIG_VERSION
static u32
__stp_utrace_task_finder_report_clone(struct utrace_attached_engine *engine,
				      struct task_struct *parent,
				      unsigned long clone_flags,
				      struct task_struct *child)
#else
static u32
__stp_utrace_task_finder_report_clone(enum utrace_resume_action action,
				      struct utrace_attached_engine *engine,
				      struct task_struct *parent,
				      unsigned long clone_flags,
				      struct task_struct *child)
#endif
{
	int rc;
	struct mm_struct *mm;
	char *mmpath_buf;
	char *mmpath;

	if (atomic_read(&__stp_task_finder_state) != __STP_TF_RUNNING) {
		debug_task_finder_detach();
		return UTRACE_DETACH;
	}

	__stp_tf_handler_start();

	// On clone, attach to the child.
	rc = __stp_utrace_attach(child, engine->ops, 0,
				 __STP_TASK_FINDER_EVENTS, UTRACE_RESUME);
	if (rc != 0 && rc != EPERM) {
		__stp_tf_handler_end();
		return UTRACE_RESUME;
	}

	__stp_utrace_attach_match_tsk(parent, child, 1,
				      (clone_flags & CLONE_THREAD) == 0);
	__stp_tf_handler_end();
	return UTRACE_RESUME;
}

#ifdef UTRACE_ORIG_VERSION
static u32
__stp_utrace_task_finder_report_exec(struct utrace_attached_engine *engine,
				     struct task_struct *tsk,
				     const struct linux_binprm *bprm,
				     struct pt_regs *regs)
#else
static u32
__stp_utrace_task_finder_report_exec(enum utrace_resume_action action,
				     struct utrace_attached_engine *engine,
				     struct task_struct *tsk,
				     const struct linux_binfmt *fmt,
				     const struct linux_binprm *bprm,
				     struct pt_regs *regs)
#endif
{
	size_t filelen;
	struct list_head *tgt_node;
	struct stap_task_finder_target *tgt;
	int found_node = 0;

	if (atomic_read(&__stp_task_finder_state) != __STP_TF_RUNNING) {
		debug_task_finder_detach();
		return UTRACE_DETACH;
	}

	__stp_tf_handler_start();

	// When exec'ing, we need to let callers detach from the
	// parent thread (if necessary).  For instance, assume
	// '/bin/bash' clones and then execs '/bin/ls'.  If the user
	// was probing '/bin/bash', the cloned thread is still
	// '/bin/bash' up until the exec.
#if ! defined(STAPCONF_REAL_PARENT)
#define real_parent parent
#endif
	if (tsk != NULL && tsk->real_parent != NULL
	    && tsk->real_parent->pid > 1) {
		// We'll hardcode this as a process end, but a thread
		// *could* call exec (although they aren't supposed to).
		__stp_utrace_attach_match_tsk(tsk->real_parent, tsk, 0, 1);
	}
#undef real_parent

	// We assume that all exec's are exec'ing a new process.  Note
	// that we don't use bprm->filename, since that path can be
	// relative.
	__stp_utrace_attach_match_tsk(tsk, tsk, 1, 1);

	__stp_tf_handler_end();
	return UTRACE_RESUME;
}

#ifdef UTRACE_ORIG_VERSION
static u32
stap_utrace_task_finder_report_death(struct utrace_attached_engine *engine,
				     struct task_struct *tsk)
#else
static u32
stap_utrace_task_finder_report_death(struct utrace_attached_engine *engine,
				     struct task_struct *tsk,
				     bool group_dead, int signal)
#endif
{
	debug_task_finder_detach();
	return UTRACE_DETACH;
}

#ifdef UTRACE_ORIG_VERSION
static u32
__stp_utrace_task_finder_target_death(struct utrace_attached_engine *engine,
				      struct task_struct *tsk)
#else
static u32
__stp_utrace_task_finder_target_death(struct utrace_attached_engine *engine,
				      struct task_struct *tsk,
				      bool group_dead, int signal)
#endif
{
	struct stap_task_finder_target *tgt = engine->data;

	if (atomic_read(&__stp_task_finder_state) != __STP_TF_RUNNING) {
		debug_task_finder_detach();
		return UTRACE_DETACH;
	}

	__stp_tf_handler_start();
	// The first implementation of this added a
	// UTRACE_EVENT(DEATH) handler to
	// __stp_utrace_task_finder_ops.  However, dead threads don't
	// have a mm_struct, so we can't find the exe's path.  So, we
	// don't know which callback(s) to call.
	//
	// So, now when an "interesting" thread is found, we add a
	// separate UTRACE_EVENT(DEATH) handler for each attached
	// handler.
	if (tgt != NULL && tsk != NULL) {
		__stp_call_callbacks(tgt, tsk, 0,
				     ((tsk->signal == NULL)
				      || (atomic_read(&tsk->signal->live) == 0)));
	}

	__stp_tf_handler_end();
	debug_task_finder_detach();
	return UTRACE_DETACH;
}

#ifdef UTRACE_ORIG_VERSION
static u32
__stp_utrace_task_finder_target_quiesce(struct utrace_attached_engine *engine,
					struct task_struct *tsk)
#else
static u32
__stp_utrace_task_finder_target_quiesce(enum utrace_resume_action action,
					struct utrace_attached_engine *engine,
					struct task_struct *tsk,
					unsigned long event)
#endif
{
	struct stap_task_finder_target *tgt = engine->data;
	int rc;

	if (atomic_read(&__stp_task_finder_state) != __STP_TF_RUNNING) {
		debug_task_finder_detach();
		return UTRACE_DETACH;
	}

	if (tgt == NULL || tsk == NULL) {
		debug_task_finder_detach();
		return UTRACE_DETACH;
	}

	__stp_tf_handler_start();

	// Turn off quiesce handling
	rc = utrace_set_events(tsk, engine,
			       __STP_ATTACHED_TASK_BASE_EVENTS(tgt));

	if (rc == -EINPROGRESS) {
		/*
		 * It's running our callback, so we have to
		 * synchronize.  We can't keep rcu_read_lock,
		 * so the task pointer might die.  But it's
		 * safe to call utrace_barrier() even with
		 * a stale task pointer, if we have an engine ref.
		 */
		rc = utrace_barrier(tsk, engine);
		if (rc != 0)
			_stp_error("utrace_barrier returned error %d on pid %d",
				   rc, (int)tsk->pid);
		rc = utrace_set_events(tsk, engine,
				       __STP_ATTACHED_TASK_BASE_EVENTS(tgt));
	}
	if (rc != 0)
		_stp_error("utrace_set_events returned error %d on pid %d",
			   rc, (int)tsk->pid);


	/* Call the callbacks.  Assume that if the thread is a
	 * thread group leader, it is a process. */
	__stp_call_callbacks(tgt, tsk, 1, (tsk->pid == tsk->tgid));
 
	/* If this is just a thread other than the thread group leader,
           don't bother inform vm_callback clients about its memory map,
           since they will simply duplicate each other. */
	if (tgt->vm_events == 1 && tsk->tgid == tsk->pid) {
		struct mm_struct *mm;
		char *mmpath_buf;
		char *mmpath;
		struct vm_area_struct *vma;
		int rc;

		/* Call the vm_callback for every vma associated with
		 * a file. */
		mm = get_task_mm(tsk);
		if (! mm)
			goto utftq_out;

		// Allocate space for a path
		mmpath_buf = _stp_kmalloc(PATH_MAX);
		if (mmpath_buf == NULL) {
			mmput(mm);
			_stp_error("Unable to allocate space for path");
			goto utftq_out;
		}

		down_read(&mm->mmap_sem);
		vma = mm->mmap;
		while (vma) {
			if (vma->vm_file) {
#ifdef STAPCONF_DPATH_PATH
				mmpath = d_path(&(vma->vm_file->f_path),
						mmpath_buf, PATH_MAX);
#else
				mmpath = d_path(vma->vm_file->f_dentry,
						vma->vm_file->f_vfsmnt,
						mmpath_buf, PATH_MAX);
#endif
				if (mmpath) {
					__stp_call_vm_callbacks(tgt, tsk, 1, 
								mmpath,
								vma->vm_start,
								vma->vm_end,
								(vma->vm_pgoff
								 << PAGE_SHIFT));
				}
				else {
					_stp_dbug(__FUNCTION__, __LINE__,
						  "no mmpath?\n");
				}
			}
			vma = vma->vm_next;
		}
		up_read(&mm->mmap_sem);
		mmput(mm);		/* We're done with mm */
		_stp_kfree(mmpath_buf);
	}

utftq_out:
	__stp_tf_handler_end();
	return UTRACE_RESUME;
}


static struct vm_area_struct *
__stp_find_file_based_vma(struct mm_struct *mm, unsigned long addr)
{
	struct vm_area_struct *vma = find_vma(mm, addr);

	// I'm not positive why the checking for vm_start > addr is
	// necessary, but it seems to be (sometimes find_vma() returns
	// a vma that addr doesn't belong to).
	if (vma && (vma->vm_file == NULL || vma->vm_start > addr))
		vma = NULL;
	return vma;
}

#ifdef UTRACE_ORIG_VERSION
static u32
__stp_utrace_task_finder_target_syscall_entry(struct utrace_attached_engine *engine,
					      struct task_struct *tsk,
					      struct pt_regs *regs)
#else
static u32
__stp_utrace_task_finder_target_syscall_entry(enum utrace_resume_action action,
					      struct utrace_attached_engine *engine,
					      struct task_struct *tsk,
					      struct pt_regs *regs)
#endif
{
	struct stap_task_finder_target *tgt = engine->data;
	unsigned long syscall_no;
	struct mm_struct *mm;
	struct vm_area_struct *vma;
	unsigned long *arg0_addr, arg0;
	int rc;
#if defined(__ia64__)
	struct { unsigned long *unwaddr; } _c = {.unwaddr = NULL}, *c = &_c;
#endif

	if (atomic_read(&__stp_task_finder_state) != __STP_TF_RUNNING) {
		debug_task_finder_detach();
		return UTRACE_DETACH;
	}

	if (tgt == NULL || tgt->vm_events == 0)
		return UTRACE_RESUME;

	// See if syscall is one we're interested in.
	//
	// FIXME: do we need to handle mremap()?
	syscall_no = __stp_user_syscall_nr(regs);
	if (syscall_no != MMAP_SYSCALL_NO(tsk)
	    && syscall_no != MMAP2_SYSCALL_NO(tsk)
	    && syscall_no != MPROTECT_SYSCALL_NO(tsk)
	    && syscall_no != MUNMAP_SYSCALL_NO(tsk))
		return UTRACE_RESUME;

	__stp_tf_handler_start();

	// We need the first syscall argument to see what address
	// we're operating on.
	arg0_addr = __stp_user_syscall_arg(tsk, regs, 0);
	if ((rc = __stp_get_user(arg0, arg0_addr)) != 0) {
		_stp_error("couldn't read syscall arg 0 for pid %d: %d",
			   tsk->pid, rc);
	}
	else if (arg0 != (unsigned long)NULL) {
		mm = get_task_mm(tsk);
		if (mm) {
			down_read(&mm->mmap_sem);

			// If we can find a matching vma associated
			// with a file, save off its details.
			vma = __stp_find_file_based_vma(mm, arg0);
			if (vma != NULL) {
				__stp_tf_add_vma(tsk, arg0, vma);
			}

			up_read(&mm->mmap_sem);
			mmput(mm);
		}
	}
	__stp_tf_handler_end();
	return UTRACE_RESUME;
}

static void
__stp_call_vm_callbacks_with_vma(struct stap_task_finder_target *tgt,
				 struct task_struct *tsk,
				 int map_p,
				 struct vm_area_struct *vma)
{
	char *mmpath_buf;
	char *mmpath;
	int rc;

	// Allocate space for a path
	mmpath_buf = _stp_kmalloc(PATH_MAX);
	if (mmpath_buf == NULL) {
		_stp_error("Unable to allocate space for path");
		return;
	}

	// Grab the path associated with this vma.
#ifdef STAPCONF_DPATH_PATH
	mmpath = d_path(&(vma->vm_file->f_path), mmpath_buf, PATH_MAX);
#else
	mmpath = d_path(vma->vm_file->f_dentry, vma->vm_file->f_vfsmnt,
			mmpath_buf, PATH_MAX);
#endif
	if (mmpath == NULL || IS_ERR(mmpath)) {
		rc = -PTR_ERR(mmpath);
		_stp_error("Unable to get path (error %d) for pid %d",
			   rc, (int)tsk->pid);
	}
	else {
		__stp_call_vm_callbacks(tgt, tsk, map_p, mmpath,
					vma->vm_start, vma->vm_end,
					(vma->vm_pgoff << PAGE_SHIFT));
	}
	_stp_kfree(mmpath_buf);
}

#ifdef UTRACE_ORIG_VERSION
static u32
__stp_utrace_task_finder_target_syscall_exit(struct utrace_attached_engine *engine,
					     struct task_struct *tsk,
					     struct pt_regs *regs)
#else
static u32
__stp_utrace_task_finder_target_syscall_exit(enum utrace_resume_action action,
					     struct utrace_attached_engine *engine,
					     struct task_struct *tsk,
					     struct pt_regs *regs)
#endif
{
	struct stap_task_finder_target *tgt = engine->data;
	unsigned long syscall_no;
	unsigned long *rv_addr, rv;
	unsigned long *arg0_addr, arg0;
	int rc;
	struct mm_struct *mm;
	struct vm_area_struct *vma;
	struct __stp_tf_vma_entry *entry = NULL;
#if defined(__ia64__)
	struct { unsigned long *unwaddr; } _c = {.unwaddr = NULL}, *c = &_c;
#endif

	if (atomic_read(&__stp_task_finder_state) != __STP_TF_RUNNING) {
		debug_task_finder_detach();
		return UTRACE_DETACH;
	}

	if (tgt == NULL || tgt->vm_events == 0)
		return UTRACE_RESUME;

	// See if syscall is one we're interested in.
	//
	// FIXME: do we need to handle mremap()?
	syscall_no = __stp_user_syscall_nr(regs);
	if (syscall_no != MMAP_SYSCALL_NO(tsk)
	    && syscall_no != MMAP2_SYSCALL_NO(tsk)
	    && syscall_no != MPROTECT_SYSCALL_NO(tsk)
	    && syscall_no != MUNMAP_SYSCALL_NO(tsk))
		return UTRACE_RESUME;

	// Get return value
	rv_addr = __stp_user_syscall_return_value(tsk, regs);
	if ((rc = __stp_get_user(rv, rv_addr)) != 0) {
		_stp_error("couldn't read syscall return value for pid %d: %d",
			   tsk->pid, rc);
		return UTRACE_RESUME;
	}

	// We need the first syscall argument to see what address we
	// were operating on.
	arg0_addr = __stp_user_syscall_arg(tsk, regs, 0);
	if ((rc = __stp_get_user(arg0, arg0_addr)) != 0) {
		_stp_error("couldn't read syscall arg 0 for pid %d: %d",
			   tsk->pid, rc);
		return UTRACE_RESUME;
	}

#ifdef DEBUG_TASK_FINDER_VMA
	_stp_dbug(__FUNCTION__, __LINE__,
		  "tsk %d found %s(0x%lx), returned 0x%lx\n",
		  tsk->pid,
		  ((syscall_no == MMAP_SYSCALL_NO(tsk)) ? "mmap"
		   : ((syscall_no == MMAP2_SYSCALL_NO(tsk)) ? "mmap2"
		      : ((syscall_no == MPROTECT_SYSCALL_NO(tsk)) ? "mprotect"
			 : ((syscall_no == MUNMAP_SYSCALL_NO(tsk)) ? "munmap"
			    : "UNKNOWN")))),
		  arg0, rv);
#endif
	__stp_tf_handler_start();

	// Try to find the vma info we might have saved.
	if (arg0 != (unsigned long)NULL)
		entry = __stp_tf_get_vma_entry(tsk, arg0);

	// If entry is NULL, this means we didn't find a file based
	// vma to store in the syscall_entry routine. This could mean
	// we just created a new vma.
	if (entry == NULL) {
		mm = get_task_mm(tsk);
		if (mm) {
			down_read(&mm->mmap_sem);
			vma = __stp_find_file_based_vma(mm, rv);
			if (vma != NULL) {
				__stp_call_vm_callbacks_with_vma(tgt, tsk, 0, vma);
			}
			up_read(&mm->mmap_sem);
			mmput(mm);
		}
	}
	// If we found saved vma information, try to match it up with
	// what currently exists.
	else {
#ifdef DEBUG_TASK_FINDER_VMA
		_stp_dbug(__FUNCTION__, __LINE__,
			  "** found stored vma 0x%lx/0x%lx/0x%lx!\n",
			  entry->vm_start, entry->vm_end, entry->vm_pgoff);
#endif
		mm = get_task_mm(tsk);
		if (mm) {
			down_read(&mm->mmap_sem);
			vma = __stp_find_file_based_vma(mm, entry->vm_start);

			// We couldn't find the vma at all. The
			// original vma was deleted.
			if (vma == NULL) {
				// FIXME: We'll need to figure out to
				// retrieve the path of a deleted
				// vma.

				__stp_call_vm_callbacks(tgt, tsk, 0, NULL,
							entry->vm_start,
							entry->vm_end,
							(entry->vm_pgoff
							 << PAGE_SHIFT));
			}

			// If nothing has changed, there is no
			// need to call the callback.
			else if (vma->vm_start == entry->vm_start
				 && vma->vm_end == entry->vm_end
				 && vma->vm_pgoff == entry->vm_pgoff) {
				// do nothing
			}

			// The original vma has been changed. It is
			// possible that calling mprotect (e.g.) split
			// up an  existing vma into 2 or 3 new vma's
			// (assuming it protected a portion of the
			// original vma at the beginning, middle, or
			// end).  Try to determine what happened.
			else {
				unsigned long tmp;

				// First report that the original vma
				// is gone.
				//
				// FIXME: We'll need to figure out to
				// retrieve the path of a deleted
				// vma.
				__stp_call_vm_callbacks(tgt, tsk, 0, NULL,
							entry->vm_start,
							entry->vm_end,
							(entry->vm_pgoff
							 << PAGE_SHIFT));

				// Now find all the new vma's that
				// made up the original vma's address
				// space and call the callback on each
				// new vma.
				tmp = entry->vm_start;
				while (((vma = __stp_find_file_based_vma(mm,
									 tmp))
					!= NULL)
				       && vma->vm_end <= entry->vm_end) {
					__stp_call_vm_callbacks_with_vma(tgt,
									 tsk,
									 1,
									 vma);
					if (vma->vm_end >= entry->vm_end)
						break;
					tmp = vma->vm_end;
				}
			}
			up_read(&mm->mmap_sem);
			mmput(mm);
		}

		// Cleanup by deleting the saved vma info.
		__stp_tf_remove_vma_entry(entry);
	}
	__stp_tf_handler_end();
	return UTRACE_RESUME;
}

struct utrace_engine_ops __stp_utrace_task_finder_ops = {
	.report_clone = __stp_utrace_task_finder_report_clone,
	.report_exec = __stp_utrace_task_finder_report_exec,
	.report_death = stap_utrace_task_finder_report_death,
};

static int
stap_start_task_finder(void)
{
	int rc = 0;
	struct task_struct *grp, *tsk;
	char *mmpath_buf;

	debug_task_finder_report();
	mmpath_buf = _stp_kmalloc(PATH_MAX);
	if (mmpath_buf == NULL) {
		_stp_error("Unable to allocate space for path");
		return ENOMEM;
	}

	__stp_tf_vma_initialize();

	atomic_set(&__stp_task_finder_state, __STP_TF_RUNNING);

	rcu_read_lock();
	do_each_thread(grp, tsk) {
		struct mm_struct *mm;
		char *mmpath;
		size_t mmpathlen;
		struct list_head *tgt_node;

		/* Skip over processes other than that specified with
		 * stap -c or -x. */
		if (_stp_target && tsk->tgid != _stp_target)
			continue;

		rc = __stp_utrace_attach(tsk, &__stp_utrace_task_finder_ops, 0,
					 __STP_TASK_FINDER_EVENTS,
					 UTRACE_RESUME);
		if (rc == EPERM) {
			/* Ignore EPERM errors, which mean this wasn't
			 * a thread we can attach to. */
			rc = 0;
			continue;
		}
		else if (rc != 0) {
			/* If we get a real error, quit. */
			goto stf_err;
		}

		/* Grab the path associated with this task. */
		mm = get_task_mm(tsk);
		if (! mm) {
		    /* If the thread doesn't have a mm_struct, it is
		     * a kernel thread which we need to skip. */
		    continue;
		}
		mmpath = __stp_get_mm_path(mm, mmpath_buf, PATH_MAX);
		mmput(mm);		/* We're done with mm */
		if (mmpath == NULL || IS_ERR(mmpath)) {
			rc = -PTR_ERR(mmpath);
			if (rc == ENOENT) {
				continue;
			}
			else {
				_stp_error("Unable to get path (error %d) for pid %d",
					   rc, (int)tsk->pid);
				goto stf_err;
			}
		}

		/* Check the thread's exe's path/pid against our list. */
		mmpathlen = strlen(mmpath);
		list_for_each(tgt_node, &__stp_task_finder_list) {
			struct stap_task_finder_target *tgt;

			tgt = list_entry(tgt_node,
					 struct stap_task_finder_target, list);
			if (tgt == NULL)
				continue;
			/* pathname-based target */
			else if (tgt->pathlen > 0
				 && (tgt->pathlen != mmpathlen
				     || strcmp(tgt->pathname, mmpath) != 0))
				 continue;
			/* pid-based target */
			else if (tgt->pid != 0 && tgt->pid != tsk->pid)
				continue;
			/* Notice that "pid == 0" (which means to
			 * probe all threads) falls through. */

			// Set up events we need for attached tasks.
			rc = __stp_utrace_attach(tsk, &tgt->ops, tgt,
						 __STP_ATTACHED_TASK_EVENTS,
						 UTRACE_STOP);
			if (rc != 0 && rc != EPERM)
				goto stf_err;
			tgt->engine_attached = 1;
		}
	} while_each_thread(grp, tsk);
stf_err:
	rcu_read_unlock();

	_stp_kfree(mmpath_buf);
	return rc;
}

static void
stap_stop_task_finder(void)
{
#ifdef DEBUG_TASK_FINDER
	int i = 0;
#endif

	atomic_set(&__stp_task_finder_state, __STP_TF_STOPPING);
	debug_task_finder_report();
	stap_utrace_detach_ops(&__stp_utrace_task_finder_ops);
	__stp_task_finder_cleanup();
	debug_task_finder_report();
	atomic_set(&__stp_task_finder_state, __STP_TF_STOPPED);

	/* Now that all the engines are detached, make sure
	 * all the callbacks are finished.  If they aren't, we'll
	 * crash the kernel when the module is removed. */
	while (atomic_read(&__stp_inuse_count) != 0) {
		schedule();
#ifdef DEBUG_TASK_FINDER
		i++;
#endif
	}
#ifdef DEBUG_TASK_FINDER
	if (i > 0)
		printk(KERN_ERR "it took %d polling loops to quit.\n", i);
#endif
	debug_task_finder_report();
}


#endif /* TASK_FINDER_C */
