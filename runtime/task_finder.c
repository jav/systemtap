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
#include "task_finder_vma.c"

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

typedef int (*stap_task_finder_vm_callback)(struct task_struct *tsk,
					    int map_p, char *vm_path,
					    unsigned long vm_start,
					    unsigned long vm_end,
					    unsigned long vm_pgoff);

#ifdef DEBUG_TASK_FINDER_VMA
int __stp_tf_vm_cb(struct task_struct *tsk,
		   int map_p, char *vm_path,
		   unsigned long vm_start,
		   unsigned long vm_end,
		   unsigned long vm_pgoff)
{
	_stp_dbug(__FUNCTION__, __LINE__,
		  "vm_cb: tsk %d:%d path %s, start 0x%08lx, end 0x%08lx, offset 0x%lx\n",
		  tsk->pid, map_p, vm_path, vm_start, vm_end, vm_pgoff);
	if (map_p) {
		// FIXME: What should we do with vm_path?  We can't save
		// the vm_path pointer itself, but we don't have any
		// storage space allocated to save it in...
		stap_add_vma_map_info(tsk, vm_start, vm_end, vm_pgoff);
	}
	else {
		stap_remove_vma_map_info(tsk, vm_start, vm_end, vm_pgoff);
	}
	return 0;
}
#endif

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
	stap_task_finder_vm_callback vm_callback;
};

static u32
__stp_utrace_task_finder_target_death(struct utrace_attached_engine *engine,
				      struct task_struct *tsk);

static u32
__stp_utrace_task_finder_target_quiesce(struct utrace_attached_engine *engine,
					struct task_struct *tsk);

static u32
__stp_utrace_task_finder_target_syscall_entry(struct utrace_attached_engine *engine,
					      struct task_struct *tsk,
					      struct pt_regs *regs);

static u32
__stp_utrace_task_finder_target_syscall_exit(struct utrace_attached_engine *engine,
					     struct task_struct *tsk,
					     struct pt_regs *regs);

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

	engine = utrace_attach(tsk, UTRACE_ATTACH_MATCH_OPS, ops, 0);
	if (IS_ERR(engine)) {
		rc = -PTR_ERR(engine);
		if (rc != ENOENT) {
			_stp_error("utrace_attach returned error %d on pid %d",
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
		rc = utrace_detach(tsk, engine);
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
		rc = ERR_PTR(-ENOENT);
	}
	up_read(&mm->mmap_sem);
	return rc;
}

#define __STP_TASK_FINDER_EVENTS (UTRACE_EVENT(CLONE)		\
				  | UTRACE_EVENT(EXEC)		\
				  | UTRACE_EVENT(DEATH))

#define __STP_ATTACHED_TASK_BASE_EVENTS (UTRACE_EVENT(DEATH))

#define __STP_ATTACHED_TASK_VM_BASE_EVENTS (__STP_ATTACHED_TASK_BASE_EVENTS \
					    | UTRACE_EVENT(SYSCALL_ENTRY) \
					    | UTRACE_EVENT(SYSCALL_EXIT))

#define __STP_ATTACHED_TASK_VM_EVENTS (__STP_ATTACHED_TASK_VM_BASE_EVENTS \
				       | UTRACE_ACTION_QUIESCE	\
				       | UTRACE_EVENT(QUIESCE))

#define __STP_ATTACHED_TASK_EVENTS(tgt) \
	((((tgt)->vm_callback) == NULL) ? __STP_ATTACHED_TASK_BASE_EVENTS \
	 : __STP_ATTACHED_TASK_VM_EVENTS)

static int
stap_utrace_attach(struct task_struct *tsk,
		   const struct utrace_engine_ops *ops, void *data,
		   unsigned long event_flags)
{
	struct utrace_attached_engine *engine;
	struct mm_struct *mm;
	int rc = 0;

	// Ignore init
	if (tsk == NULL || tsk->pid <= 1)
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
		rc = utrace_set_flags(tsk, engine, event_flags);
		if (rc == 0)
			debug_task_finder_attach();
		else
			_stp_error("utrace_set_flags returned error %d on pid %d",
				   rc, (int)tsk->pid);
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

	filelen = strlen(filename);
	list_for_each(tgt_node, &__stp_task_finder_list) {
		struct list_head *cb_node;

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

			// Set up events we need for attached tasks.
			if (register_p) {
				rc = stap_utrace_attach(tsk, &cb_tgt->ops,
							cb_tgt,
							__STP_ATTACHED_TASK_EVENTS(cb_tgt));
				if (rc != 0 && rc != EPERM)
					break;
				cb_tgt->engine_attached = 1;
			}
			else {
				rc = stap_utrace_detach(tsk, &cb_tgt->ops);
				if (rc != 0)
					break;
				cb_tgt->engine_attached = 0;
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
				__STP_TASK_FINDER_EVENTS);
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
				   (tsk->signal == NULL) || (atomic_read(&tsk->signal->live) == 0),
				   tgt);
		if (rc != 0) {
			_stp_error("death callback for %d failed: %d",
				   (int)tsk->pid, rc);
		}
	}
	debug_task_finder_detach();
	return UTRACE_ACTION_DETACH;
}

static u32
__stp_utrace_task_finder_target_quiesce(struct utrace_attached_engine *engine,
					struct task_struct *tsk)
{
	struct stap_task_finder_target *tgt = engine->data;

	// Turn off quiesce handling.
	utrace_set_flags(tsk, engine, __STP_ATTACHED_TASK_VM_BASE_EVENTS);

	if (atomic_read(&__stp_task_finder_state) != __STP_TF_RUNNING) {
		debug_task_finder_detach();
		return UTRACE_ACTION_DETACH;
	}

	if (tgt != NULL && tgt->vm_callback != NULL) {
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
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,25)
				mmpath = d_path(vma->vm_file->f_dentry,
						vma->vm_file->f_vfsmnt,
						mmpath_buf, PATH_MAX);
#else
				mmpath = d_path(&(vma->vm_file->f_path),
						mmpath_buf, PATH_MAX);
#endif
				if (mmpath) {
					// Call the callback
					rc = tgt->vm_callback(tsk, 1, mmpath,
							      vma->vm_start,
							      vma->vm_end,
							      (vma->vm_pgoff
							       << PAGE_SHIFT));
					if (rc != 0) {
					    _stp_error("vm callback for %d failed: %d",
						       (int)tsk->pid, rc);
					}

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
	return (UTRACE_ACTION_NEWSTATE | UTRACE_ACTION_RESUME);
}


struct vm_area_struct *
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

static u32
__stp_utrace_task_finder_target_syscall_entry(struct utrace_attached_engine *engine,
					      struct task_struct *tsk,
					      struct pt_regs *regs)
{
	struct stap_task_finder_target *tgt = engine->data;
	unsigned long syscall_no;
	struct mm_struct *mm;
	struct vm_area_struct *vma;
	unsigned long *arg0_addr, arg0;
	int rc;

	if (atomic_read(&__stp_task_finder_state) != __STP_TF_RUNNING) {
		debug_task_finder_detach();
		return UTRACE_ACTION_DETACH;
	}

	if (tgt == NULL || tgt->vm_callback == NULL)
		return UTRACE_ACTION_RESUME;

	// See if syscall is one we're interested in.
	//
	// FIXME: do we need to handle mremap()?
	syscall_no = __stp_user_syscall_nr(regs);
	if (syscall_no != MMAP_SYSCALL_NO(tsk)
	    && syscall_no != MPROTECT_SYSCALL_NO(tsk)
	    && syscall_no != MUNMAP_SYSCALL_NO(tsk))
		return UTRACE_ACTION_RESUME;


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
	return UTRACE_ACTION_RESUME;
}

static void
__stp_target_call_vm_callback(struct stap_task_finder_target *tgt,
			      struct task_struct *tsk,
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
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,25)
	mmpath = d_path(vma->vm_file->f_dentry, vma->vm_file->f_vfsmnt,
			mmpath_buf, PATH_MAX);
#else
	mmpath = d_path(&(vma->vm_file->f_path), mmpath_buf, PATH_MAX);
#endif
	if (mmpath == NULL || IS_ERR(mmpath)) {
		rc = -PTR_ERR(mmpath);
		_stp_error("Unable to get path (error %d) for pid %d",
			   rc, (int)tsk->pid);
	}
	else {
		rc = tgt->vm_callback(tsk, 1, mmpath, vma->vm_start,
				      vma->vm_end,
				      (vma->vm_pgoff << PAGE_SHIFT));
		if (rc != 0) {
			_stp_error("vm callback for %d failed: %d",
				   (int)tsk->pid, rc);
		}
	}
	_stp_kfree(mmpath_buf);
}

static u32
__stp_utrace_task_finder_target_syscall_exit(struct utrace_attached_engine *engine,
					     struct task_struct *tsk,
					     struct pt_regs *regs)
{
	struct stap_task_finder_target *tgt = engine->data;
	unsigned long syscall_no;
	unsigned long *rv_addr, rv;
	unsigned long *arg0_addr, arg0;
	int rc;
	struct mm_struct *mm;
	struct vm_area_struct *vma;
	struct __stp_tf_vma_entry *entry = NULL;

	if (atomic_read(&__stp_task_finder_state) != __STP_TF_RUNNING) {
		debug_task_finder_detach();
		return UTRACE_ACTION_DETACH;
	}

	if (tgt == NULL || tgt->vm_callback == NULL)
		return UTRACE_ACTION_RESUME;

	// See if syscall is one we're interested in.
	//
	// FIXME: do we need to handle mremap()?
	syscall_no = __stp_user_syscall_nr(regs);
	if (syscall_no != MMAP_SYSCALL_NO(tsk)
	    && syscall_no != MPROTECT_SYSCALL_NO(tsk)
	    && syscall_no != MUNMAP_SYSCALL_NO(tsk))
		return UTRACE_ACTION_RESUME;

	// Get return value
	rv_addr = __stp_user_syscall_return_value(tsk, regs);
	if ((rc = __stp_get_user(rv, rv_addr)) != 0) {
		_stp_error("couldn't read syscall return value for pid %d: %d",
			   tsk->pid, rc);
		return UTRACE_ACTION_RESUME;
	}

	// We need the first syscall argument to see what address we
	// were operating on.
	arg0_addr = __stp_user_syscall_arg(tsk, regs, 0);
	if ((rc = __stp_get_user(arg0, arg0_addr)) != 0) {
		_stp_error("couldn't read syscall arg 0 for pid %d: %d",
			   tsk->pid, rc);
		return UTRACE_ACTION_RESUME;
	}

#ifdef DEBUG_TASK_FINDER_VMA
	_stp_dbug(__FUNCTION__, __LINE__,
		  "tsk %d found %s(0x%lx), returned 0x%lx\n",
		  tsk->pid,
		  ((syscall_no == MMAP_SYSCALL_NO(tsk)) ? "mmap"
		   : ((syscall_no == MPROTECT_SYSCALL_NO(tsk)) ? "mprotect"
		      : ((syscall_no == MUNMAP_SYSCALL_NO(tsk)) ? "munmap"
			 : "UNKNOWN"))),
		  arg0, rv);
#endif

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
				__stp_target_call_vm_callback(tgt, tsk,
							      vma);
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
				rc = tgt->vm_callback(tsk, 0, NULL,
						      entry->vm_start,
						      entry->vm_end,
						      (entry->vm_pgoff
						       << PAGE_SHIFT));
				if (rc != 0) {
					_stp_error("vm callback for %d failed: %d",
						   (int)tsk->pid, rc);
				}
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
				rc = tgt->vm_callback(tsk, 0, NULL,
						      entry->vm_start,
						      entry->vm_end,
						      (entry->vm_pgoff
						       << PAGE_SHIFT));
				if (rc != 0) {
					_stp_error("vm callback for %d failed: %d",
						   (int)tsk->pid, rc);
				}

				// Now find all the new vma's that
				// made up the original vma's address
				// space and call the callback on each
				// new vma.
				tmp = entry->vm_start;
				while (((vma = __stp_find_file_based_vma(mm,
									 tmp))
					!= NULL)
				       && vma->vm_end <= entry->vm_end) {
					__stp_target_call_vm_callback(tgt, tsk,
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
	return UTRACE_ACTION_RESUME;
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

		rc = stap_utrace_attach(tsk, &__stp_utrace_task_finder_ops, 0,
					__STP_TASK_FINDER_EVENTS);
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
			/* Notice that "pid == 0" (which means to
			 * probe all threads) falls through. */

			list_for_each(cb_node, &tgt->callback_list_head) {
				struct stap_task_finder_target *cb_tgt;
				cb_tgt = list_entry(cb_node,
						    struct stap_task_finder_target,
						    callback_list);
				if (cb_tgt == NULL)
					continue;
					
				// Call the callback.  Assume that if
				// the thread is a thread group
				// leader, it is a process.
				if (cb_tgt->callback != NULL) {
					rc = cb_tgt->callback(tsk, 1,
							      (tsk->pid == tsk->tgid),
							      cb_tgt);
					if (rc != 0) {
						_stp_error("attach callback for %d failed: %d",
							   (int)tsk->pid, rc);
						goto stf_err;
					}
				}

				// Set up events we need for attached tasks.
				rc = stap_utrace_attach(tsk, &cb_tgt->ops,
							cb_tgt,
							__STP_ATTACHED_TASK_EVENTS(cb_tgt));
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


#endif /* TASK_FINDER_C */
