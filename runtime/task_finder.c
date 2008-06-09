#include <linux/list.h>
#include <linux/binfmts.h>

static LIST_HEAD(__stp_task_finder_list);

struct stap_task_finder_target;

#define __STP_TF_STARTING	0
#define __STP_TF_RUNNING	1
#define __STP_TF_STOPPING	2
#define __STP_TF_STOPPED	3
atomic_t __stp_task_finder_state = ATOMIC_INIT(__STP_TF_STARTING);

#ifdef DEBUG_TASK_FINDER
atomic_t __stp_attach_count = ATOMIC_INIT (0);

#define debug_task_finder_attach() (atomic_inc(&__stp_attach_count))
#define debug_task_finder_detach() (atomic_dec(&__stp_attach_count))
#define debug_task_finder_report() (_stp_dbug(__FUNCTION__, __LINE__, \
	"attach count: %d\n", atomic_read(&__stp_attach_count)))
#else
#define debug_task_finder_attach()	/* empty */
#define debug_task_finder_detach()	/* empty */
#define debug_task_finder_report()	/* empty */
#endif

typedef int (*stap_task_finder_callback)(struct task_struct *tsk,
					 int register_p,
					 int process_p,
					 struct stap_task_finder_target *tgt);

struct stap_task_finder_target {
/* private: */
	struct list_head list;		/* __stp_task_finder_list linkage */
	struct list_head callback_list_head;
	struct list_head callback_list;
	struct utrace_engine_ops ops;
	int engine_attached;
	size_t pathlen;

/* public: */
    	const char *pathname;
	pid_t pid;
	stap_task_finder_callback callback;
};

static u32
__stp_utrace_task_finder_target_death(struct utrace_attached_engine *engine,
				      struct task_struct *tsk);

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
	memset(&new_tgt->ops, 0, sizeof(new_tgt->ops));
	new_tgt->ops.report_death = &__stp_utrace_task_finder_target_death;

	// Search the list for an existing entry for pathname/pid.
	list_for_each(node, &__stp_task_finder_list) {
		tgt = list_entry(node, struct stap_task_finder_target, list);
		if (tgt != NULL
		    /* pathname-based target */
		    && ((new_tgt->pathlen > 0
			 && tgt->pathlen == new_tgt->pathlen
			 && strcmp(tgt->pathname, new_tgt->pathname) == 0)
			/* pid-based target */
			|| (new_tgt->pid != 0 && tgt->pid == new_tgt->pid))) {
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
	return 0;
}

static void
stap_utrace_detach_ops(struct utrace_engine_ops *ops)
{
	struct task_struct *grp, *tsk;
	struct utrace_attached_engine *engine;
	long error = 0;
	pid_t pid = 0;

	rcu_read_lock();
	do_each_thread(grp, tsk) {
		struct mm_struct *mm;

		if (tsk->pid <= 1)
			continue;

		mm = get_task_mm(tsk);
		if (mm) {
			mmput(mm);
			engine = utrace_attach(tsk, UTRACE_ATTACH_MATCH_OPS,
					       ops, 0);
			if (IS_ERR(engine)) {
				error = -PTR_ERR(engine);
				if (error != ENOENT) {
					pid = tsk->pid;
					goto udo_err;
				}
				error = 0;
			}
			else if (engine != NULL) {
				utrace_detach(tsk, engine);
				debug_task_finder_detach();
			}
		}
	} while_each_thread(grp, tsk);
udo_err:
	rcu_read_unlock();

	if (error != 0) {
		_stp_error("utrace_attach returned error %d on pid %d",
			   error, pid);
	}
	debug_task_finder_report();
}

static void
__stp_task_finder_cleanup(void)
{
	struct list_head *tgt_node, *tgt_next;
	struct list_head *cb_node, *cb_next;
	struct stap_task_finder_target *tgt;

	// Walk the main list, cleaning up as we go.
	list_for_each_safe(tgt_node, tgt_next, &__stp_task_finder_list) {
		tgt = list_entry(tgt_node, struct stap_task_finder_target,
				 list);
		if (tgt == NULL)
			continue;

		list_for_each_safe(cb_node, cb_next,
				   &tgt->callback_list_head) {
			struct stap_task_finder_target *cb_tgt;
			cb_tgt = list_entry(cb_node,
					    struct stap_task_finder_target,
					    callback_list);
			if (cb_tgt == NULL)
				continue;

			if (cb_tgt->engine_attached) {
				stap_utrace_detach_ops(&cb_tgt->ops);
				cb_tgt->engine_attached = 0;
			}

			list_del(&cb_tgt->callback_list);
		}
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
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,25)
		rc = d_path(vma->vm_file->f_dentry, vma->vm_file->f_vfsmnt,
			    buf, buflen);
#else
		rc = d_path(&(vma->vm_file->f_path), buf, buflen);
#endif
	}
	else {
		*buf = '\0';
		rc = ERR_PTR(ENOENT);
	}
	up_read(&mm->mmap_sem);
	return rc;
}

#define __STP_UTRACE_TASK_FINDER_EVENTS (UTRACE_EVENT(CLONE)	\
					 | UTRACE_EVENT(EXEC)	\
					 | UTRACE_EVENT(DEATH))

#define __STP_UTRACE_ATTACHED_TASK_EVENTS (UTRACE_EVENT(DEATH))

static int
stap_utrace_attach(struct task_struct *tsk,
		   const struct utrace_engine_ops *ops, void *data,
		   unsigned long event_flags)
{
	struct utrace_attached_engine *engine;
	struct mm_struct *mm;
	int rc = 0;

	// Ignore init
	if (tsk->pid <= 1)
		return EPERM;

	// Ignore threads with no mm (which are kernel threads).
	mm = get_task_mm(tsk);
	if (! mm)
		return EPERM;
	mmput(mm);

	engine = utrace_attach(tsk, UTRACE_ATTACH_CREATE, ops, data);
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
		utrace_set_flags(tsk, engine, event_flags);
		debug_task_finder_attach();
	}
	return rc;
}

static inline void
__stp_utrace_attach_match_filename(struct task_struct *tsk,
				   const char * const filename,
				   int register_p, int process_p)
{
	size_t filelen;
	struct list_head *tgt_node;
	struct stap_task_finder_target *tgt;
	int found_node = 0;

	filelen = strlen(filename);
	list_for_each(tgt_node, &__stp_task_finder_list) {
		tgt = list_entry(tgt_node, struct stap_task_finder_target,
				 list);
		// Note that we don't bother with looking for pids
		// here, since they are handled at startup.
		if (tgt != NULL && tgt->pathlen > 0
		    && tgt->pathlen == filelen
		    && strcmp(tgt->pathname, filename) == 0) {
			found_node = 1;
			break;
		}
	}
	if (found_node) {
		struct list_head *cb_node;
		list_for_each(cb_node, &tgt->callback_list_head) {
			struct stap_task_finder_target *cb_tgt;
			int rc;

			cb_tgt = list_entry(cb_node,
					    struct stap_task_finder_target,
					    callback_list);
			if (cb_tgt == NULL)
				continue;

			if (cb_tgt->callback != NULL) {
				int rc = cb_tgt->callback(tsk, register_p,
							  process_p, cb_tgt);
				if (rc != 0) {
					_stp_error("callback for %d failed: %d",
						   (int)tsk->pid, rc);
					break;
				}
			}

			// Set up thread death notification.
			if (register_p) {
				rc = stap_utrace_attach(tsk, &cb_tgt->ops,
							cb_tgt,
							__STP_UTRACE_ATTACHED_TASK_EVENTS);
				if (rc != 0 && rc != EPERM)
					break;
				cb_tgt->engine_attached = 1;
			}
			else {
				struct utrace_attached_engine *engine;
				engine = utrace_attach(tsk,
						       UTRACE_ATTACH_MATCH_OPS,
						       &cb_tgt->ops, 0);
				if (! IS_ERR(engine) && engine != NULL) {
					utrace_detach(tsk, engine);
					debug_task_finder_detach();
				}
			}
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

	if (path_tsk->pid <= 1 || match_tsk->pid <= 1)
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

static u32
__stp_utrace_task_finder_report_clone(struct utrace_attached_engine *engine,
				      struct task_struct *parent,
				      unsigned long clone_flags,
				      struct task_struct *child)
{
	int rc;
	struct mm_struct *mm;
	char *mmpath_buf;
	char *mmpath;

	if (atomic_read(&__stp_task_finder_state) != __STP_TF_RUNNING)
		return UTRACE_ACTION_RESUME;

	// On clone, attach to the child.
	rc = stap_utrace_attach(child, engine->ops, 0,
				__STP_UTRACE_TASK_FINDER_EVENTS);
	if (rc != 0 && rc != EPERM)
		return UTRACE_ACTION_RESUME;

	__stp_utrace_attach_match_tsk(parent, child, 1,
				      (clone_flags & CLONE_THREAD) == 0);
	return UTRACE_ACTION_RESUME;
}

static u32
__stp_utrace_task_finder_report_exec(struct utrace_attached_engine *engine,
				     struct task_struct *tsk,
				     const struct linux_binprm *bprm,
				     struct pt_regs *regs)
{
	size_t filelen;
	struct list_head *tgt_node;
	struct stap_task_finder_target *tgt;
	int found_node = 0;

	if (atomic_read(&__stp_task_finder_state) != __STP_TF_RUNNING)
		return UTRACE_ACTION_RESUME;

	// When exec'ing, we need to let callers detach from the
	// parent thread (if necessary).  For instance, assume
	// '/bin/bash' clones and then execs '/bin/ls'.  If the user
	// was probing '/bin/bash', the cloned thread is still
	// '/bin/bash' up until the exec.
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,25)
#define real_parent parent
#endif
	if (tsk != NULL && tsk->real_parent != NULL
	    && tsk->real_parent->pid > 1) {
		// We'll hardcode this as a process end, but a thread
		// *could* call exec (although they aren't supposed to).
		__stp_utrace_attach_match_tsk(tsk->real_parent, tsk, 0, 1);
	}

	// On exec, check bprm
	if (bprm->filename == NULL)
		return UTRACE_ACTION_RESUME;

	// We assume that all exec's are exec'ing a new process
	__stp_utrace_attach_match_filename(tsk, bprm->filename, 1, 1);

	return UTRACE_ACTION_RESUME;
}

static u32
stap_utrace_task_finder_report_death(struct utrace_attached_engine *engine,
				     struct task_struct *tsk)
{
	debug_task_finder_detach();
	return UTRACE_ACTION_DETACH;
}

static u32
__stp_utrace_task_finder_target_death(struct utrace_attached_engine *engine,
				      struct task_struct *tsk)
{
	struct stap_task_finder_target *tgt = engine->data;

	if (atomic_read(&__stp_task_finder_state) != __STP_TF_RUNNING) {
		debug_task_finder_detach();
		return UTRACE_ACTION_DETACH;
	}

	// The first implementation of this added a
	// UTRACE_EVENT(DEATH) handler to
	// __stp_utrace_task_finder_ops.  However, dead threads don't
	// have a mm_struct, so we can't find the exe's path.  So, we
	// don't know which callback(s) to call.
	//
	// So, now when an "interesting" thread is found, we add a
	// separate UTRACE_EVENT(DEATH) handler for every probe.

	if (tgt != NULL && tgt->callback != NULL) {
		int rc;

		// Call the callback
		rc = tgt->callback(tsk, 0,
				   (atomic_read(&tsk->signal->live) == 0),
				   tgt);
		if (rc != 0) {
			_stp_error("death callback for %d failed: %d",
				   (int)tsk->pid, rc);
		}
	}
	debug_task_finder_detach();
	return UTRACE_ACTION_DETACH;
}

struct utrace_engine_ops __stp_utrace_task_finder_ops = {
	.report_clone = __stp_utrace_task_finder_report_clone,
	.report_exec = __stp_utrace_task_finder_report_exec,
	.report_death = stap_utrace_task_finder_report_death,
};

int
stap_start_task_finder(void)
{
	int rc = 0;
	struct task_struct *grp, *tsk;
	char *mmpath_buf;

	mmpath_buf = _stp_kmalloc(PATH_MAX);
	if (mmpath_buf == NULL) {
		_stp_error("Unable to allocate space for path");
		return ENOMEM;
	}

	atomic_set(&__stp_task_finder_state, __STP_TF_RUNNING);

	rcu_read_lock();
	do_each_thread(grp, tsk) {
		struct mm_struct *mm;
		char *mmpath;
		size_t mmpathlen;
		struct list_head *tgt_node;

		rc = stap_utrace_attach(tsk, &__stp_utrace_task_finder_ops, 0,
					__STP_UTRACE_TASK_FINDER_EVENTS);
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
			_stp_error("Unable to get path (error %d) for pid %d",
				   rc, (int)tsk->pid);
			goto stf_err;
		}

		/* Check the thread's exe's path/pid against our list. */
		mmpathlen = strlen(mmpath);
		list_for_each(tgt_node, &__stp_task_finder_list) {
			struct stap_task_finder_target *tgt;
			struct list_head *cb_node;

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

			list_for_each(cb_node, &tgt->callback_list_head) {
				struct stap_task_finder_target *cb_tgt;
				cb_tgt = list_entry(cb_node,
						    struct stap_task_finder_target,
						    callback_list);
				if (cb_tgt == NULL || cb_tgt->callback == NULL)
					continue;
					
				// Call the callback.  Assume that if
				// the thread is a thread group
				// leader, it is a process.
				rc = cb_tgt->callback(tsk, 1,
						      (tsk->pid == tsk->tgid),
						      cb_tgt);
				if (rc != 0) {
					_stp_error("attach callback for %d failed: %d",
						   (int)tsk->pid, rc);
					goto stf_err;
				}

				// Set up thread death notification.
				rc = stap_utrace_attach(tsk, &cb_tgt->ops,
							cb_tgt,
							__STP_UTRACE_ATTACHED_TASK_EVENTS);
				if (rc != 0 && rc != EPERM)
					goto stf_err;
				cb_tgt->engine_attached = 1;
			}
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
	atomic_set(&__stp_task_finder_state, __STP_TF_STOPPING);
	debug_task_finder_report();
	stap_utrace_detach_ops(&__stp_utrace_task_finder_ops);
	__stp_task_finder_cleanup();
	debug_task_finder_report();
	atomic_set(&__stp_task_finder_state, __STP_TF_STOPPED);
}
