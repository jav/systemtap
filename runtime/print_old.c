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

void EXPORT_FN(stp_print_flush) (_stp_pbuf *pb)
{
	uint32_t len = pb->len;

	/* check to see if there is anything in the buffer */
	if (likely (len == 0))
		return;

	pb->len = 0;

#ifdef STP_BULKMODE
	{
#ifdef NO_PERCPU_HEADERS
		void *buf = relay_reserve(_stp_utt->rchan, len);		
		if (likely(buf))
			memcpy(buf, pb->buf, len);
		else
			atomic_inc (&_stp_transport_failures);
#else
		void *buf = relay_reserve(_stp_utt->rchan,
					sizeof(struct _stp_trace) + len);
		if (likely(buf)) {
			struct _stp_trace t = {	.sequence = _stp_seq_inc(),
						.pdu_len = len};
			memcpy(buf, &t, sizeof(t)); // prevent unaligned access
			memcpy(buf + sizeof(t), pb->buf, len);
		} else 
			atomic_inc (&_stp_transport_failures);
#endif
	} 
#else
	{
		if (unlikely(_stp_ctl_write(STP_REALTIME_DATA, pb->buf, len) <= 0))
			atomic_inc (&_stp_transport_failures);
	}
#endif /* STP_BULKMODE */
}
