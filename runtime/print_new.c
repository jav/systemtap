/* -*- linux-c -*- 
 * Print Flush Function
 * Copyright (C) 2007 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

/** Send the print buffer to the transport now.
 * Output accumulates in the print buffer until it
 * is filled, or this is called. This MUST be called before returning
 * from a probe or accumulated output in the print buffer will be lost.
 *
 * @note Preemption must be disabled to use this.
 */
spinlock_t _stp_print_lock = SPIN_LOCK_UNLOCKED;

void EXPORT_FN(stp_print_flush) (_stp_pbuf *pb)
{
	uint32_t len = pb->len;

	/* check to see if there is anything in the buffer */
	if (likely (len == 0))
		return;

	pb->len = 0;

	if (unlikely(!_stp_utt || _stp_utt->trace_state != Utt_trace_running))
		return;

#ifdef STP_BULKMODE
	{
#ifdef NO_PERCPU_HEADERS
		void *buf = relay_reserve(_stp_utt->rchan, len);		
		if (likely(buf))
			memcpy(buf, pb->buf, len);
		else
			atomic_inc (&_stp_transport_failures);
#else
		struct _stp_trace *t = relay_reserve(_stp_utt->rchan, sizeof(*t) + len);
		if (likely(t)) {
			t->sequence = _stp_seq_inc();
			t->pdu_len = len;
			memcpy((void *) t + sizeof(*t), pb->buf, len);
		} else 
			atomic_inc (&_stp_transport_failures);
#endif
	} 
#else
	{
		void *buf;
		unsigned long flags;
		spin_lock_irqsave(&_stp_print_lock, flags);
		buf = relay_reserve(_stp_utt->rchan, len);		
		if (likely(buf))
			memcpy(buf, pb->buf, len);
		else
			atomic_inc (&_stp_transport_failures);
		spin_unlock_irqrestore(&_stp_print_lock, flags);
	}
#endif /* STP_BULKMODE */
}
