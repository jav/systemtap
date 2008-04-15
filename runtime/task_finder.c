#include <linux/list.h>

static LIST_HEAD(__stp_task_finder_list);

struct stap_task_finder_target;

typedef int (*stap_task_finder_callback)(struct task_struct *tsk,
					 int register_p,
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

static int
stap_register_task_finder_target(struct stap_task_finder_target *new_tgt)
{
	// Since this __stp_task_finder_list is (currently) only
        // written to in one big setup operation before the task
        // finder process is started, we don't need to lock it.
	struct list_head *node;
	struct stap_task_finder_target *tgt = NULL;
	int found_node = 0;

	if (new_tgt->pathname != NULL)
		new_tgt->pathlen = strlen(new_tgt->pathname);
	else
		new_tgt->pathlen = 0;

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
	new_tgt->engine_attached = 0;
	list_add_tail(&new_tgt->callback_list, &tgt->callback_list_head);
	return 0;
}

static void
stap_utrace_detach_ops(struct utrace_engine_ops *ops)
{
	struct task_struct *tsk;
	struct utrace_attached_engine *engine;
	long error = 0;
	pid_t pid = 0;

	rcu_read_lock();
	for_each_process(tsk) {
		struct mm_struct *mm;
		mm = get_task_mm(tsk);
		if (mm) {
			mmput(mm);
			engine = utrace_attach(tsk, UTRACE_ATTACH_MATCH_OPS,
					       ops, 0);
			if (IS_ERR(engine)) {
				error = -PTR_ERR(engine);
				if (error != ENOENT) {
					pid = tsk->pid;
					break;
				}
				error = 0;
			}
			else if (engine != NULL) {
				utrace_detach(tsk, engine);
			}
		}
	}
	rcu_read_unlock();

	if (error != 0) {
		_stp_error("utrace_attach returned error %d on pid %d",
			   error, pid);
	}
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
		struct vfsmount *mnt = mntget(vma->vm_file->f_path.mnt);
		struct dentry *dentry = dget(vma->vm_file->f_path.dentry);
		rc = d_path(dentry, mnt, buf, buflen);
		dput(dentry);
		mntput(mnt);
	}
	else {
		*buf = '\0';
		rc = ERR_PTR(ENOENT);
	}
	up_read(&mm->mmap_sem);
	return rc;
}

#define __STP_UTRACE_TASK_FINDER_EVENTS (UTRACE_EVENT(CLONE)	\
					 | UTRACE_EVENT(EXEC))

#define __STP_UTRACE_ATTACHED_TASK_EVENTS (UTRACE_EVENT(DEATH))

static u32
__stp_utrace_task_finder_clone(struct utrace_attached_engine *engine,
			       struct task_struct *parent,
			       unsigned long clone_flags,
			       struct task_struct *child)
{
	struct utrace_attached_engine *child_engine;
	struct mm_struct *mm;

	// On clone, attach to the child.  Ignore threads with no mm
	// (which are kernel threads).
	mm = get_task_mm(child);
	if (mm) {
		mmput(mm);
		child_engine = utrace_attach(child, UTRACE_ATTACH_CREATE,
					     engine->ops, 0);
		if (IS_ERR(child_engine))
			_stp_error("attach to clone child %d failed: %ld",
				   (int)child->pid, PTR_ERR(child_engine));
		else {
			utrace_set_flags(child, child_engine,
					 __STP_UTRACE_TASK_FINDER_EVENTS);
		}
	}
	return UTRACE_ACTION_RESUME;
}

static u32
__stp_utrace_task_finder_death(struct utrace_attached_engine *engine,
			       struct task_struct *tsk)
{
	struct stap_task_finder_target *tgt = engine->data;

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
		rc = tgt->callback(tsk, 0, tgt);
		if (rc != 0) {
			_stp_error("death callback for %d failed: %d",
				   (int)tsk->pid, rc);
		}
	}
	return UTRACE_ACTION_RESUME;
}

static u32
__stp_utrace_task_finder_exec(struct utrace_attached_engine *engine,
			      struct task_struct *tsk,
			      const struct linux_binprm *bprm,
			      struct pt_regs *regs)
{
	size_t filelen;
	struct list_head *tgt_node;
	struct stap_task_finder_target *tgt;
	int found_node = 0;

	// On exec, check bprm
	if (bprm->filename == NULL)
		return UTRACE_ACTION_RESUME;

	filelen = strlen(bprm->filename);
	list_for_each(tgt_node, &__stp_task_finder_list) {
		tgt = list_entry(tgt_node, struct stap_task_finder_target,
				 list);
		// Note that we don't bother with looking for pids
		// here, since they are handled at startup.
		if (tgt != NULL && tgt->pathlen > 0
		    && tgt->pathlen == filelen
		    && strcmp(tgt->pathname, bprm->filename) == 0) {
			found_node = 1;
			break;
		}
	}
	if (found_node) {
		struct list_head *cb_node;
		list_for_each(cb_node, &tgt->callback_list_head) {
			struct stap_task_finder_target *cb_tgt;
			cb_tgt = list_entry(cb_node,
					    struct stap_task_finder_target,
					    callback_list);
			if (cb_tgt == NULL)
				continue;

			if (cb_tgt->callback != NULL) {
				int rc = cb_tgt->callback(tsk, 1, cb_tgt);
				if (rc != 0) {
					_stp_error("exec callback for %d failed: %d",
						   (int)tsk->pid, rc);
					break;
				}
			}

			// Set up thread death notification.
			memset(&cb_tgt->ops, 0, sizeof(cb_tgt->ops));
			cb_tgt->ops.report_death
				= &__stp_utrace_task_finder_death;

			engine = utrace_attach(tsk,
					       UTRACE_ATTACH_CREATE,
					       &cb_tgt->ops, cb_tgt);
			if (IS_ERR(engine)) {
				_stp_error("attach to exec'ed %d failed: %ld",
					   (int)tsk->pid,
					   PTR_ERR(engine));
			}
			else {
				utrace_set_flags(tsk, engine,
						 __STP_UTRACE_ATTACHED_TASK_EVENTS);
				cb_tgt->engine_attached = 1;
			}
		}
	}
	return UTRACE_ACTION_RESUME;
}

struct utrace_engine_ops __stp_utrace_task_finder_ops = {
	.report_clone = __stp_utrace_task_finder_clone,
	.report_exec = __stp_utrace_task_finder_exec,
};

int
stap_start_task_finder(void)
{
	int rc = 0;
	struct task_struct *tsk;
	char *mmpath_buf;

	mmpath_buf = _stp_kmalloc(PATH_MAX);
	if (mmpath_buf == NULL) {
		_stp_error("Unable to allocate space for path");
		return ENOMEM;
	}

	rcu_read_lock();
	for_each_process(tsk) {
		struct utrace_attached_engine *engine;
		struct mm_struct *mm;
		char *mmpath;
		size_t mmpathlen;
		struct list_head *tgt_node;

		mm = get_task_mm(tsk);
		if (! mm) {
		    /* If the thread doesn't have a mm_struct, it is
		     * a kernel thread which we need to skip. */
		    continue;
		}

		/* Attach to the thread */
		engine = utrace_attach(tsk, UTRACE_ATTACH_CREATE,
				       &__stp_utrace_task_finder_ops, 0);
		if (IS_ERR(engine)) {
			int error = -PTR_ERR(engine);
			if (error != ENOENT) {
				mmput(mm);
				_stp_error("utrace_attach returned error %d on pid %d",
					   error, (int)tsk->pid);
				rc = error;
				break;
			}
		}
		else if (unlikely(engine == NULL)) {
			mmput(mm);
			_stp_error("utrace_attach returned NULL on pid %d",
				   (int)tsk->pid);
			rc = EFAULT;
			break;
		}
		utrace_set_flags(tsk, engine, __STP_UTRACE_TASK_FINDER_EVENTS);

		/* Check the thread's exe's path/pid against our list. */
		mmpath = __stp_get_mm_path(mm, mmpath_buf, PATH_MAX);
		mmput(mm);		/* We're done with mm */
		if (IS_ERR(mmpath)) {
			rc = -PTR_ERR(mmpath);
			_stp_error("Unable to get path (error %d) for pid %d",
				   rc, (int)tsk->pid);
			break;
		}

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
					
				// Call the callback.
				rc = cb_tgt->callback(tsk, 1, cb_tgt);
				if (rc != 0) {
					_stp_error("attach callback for %d failed: %d",
						   (int)tsk->pid, rc);
					break;
				}

				// Set up thread death notification.
				memset(&cb_tgt->ops, 0, sizeof(cb_tgt->ops));
				cb_tgt->ops.report_death
					= &__stp_utrace_task_finder_death;

				engine = utrace_attach(tsk,
						       UTRACE_ATTACH_CREATE,
						       &cb_tgt->ops, cb_tgt);
				if (IS_ERR(engine)) {
					_stp_error("attach to %d failed: %ld",
						   (int)tsk->pid,
						   PTR_ERR(engine));
				}
				else {
					utrace_set_flags(tsk, engine,
							 __STP_UTRACE_ATTACHED_TASK_EVENTS);
					cb_tgt->engine_attached = 1;
				}
			}
		}
	}
	rcu_read_unlock();
	_stp_kfree(mmpath_buf);
	return rc;
}

static void
stap_stop_task_finder(void)
{
	stap_utrace_detach_ops(&__stp_utrace_task_finder_ops);
	__stp_task_finder_cleanup();
}
