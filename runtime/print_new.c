/* -*- linux-c -*- 
 * Print Flush Function
 * Copyright (C) 2007-2008 Red Hat Inc.
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

static DEFINE_SPINLOCK(_stp_print_lock);

void EXPORT_FN(stp_print_flush)(_stp_pbuf *pb)
{
	size_t len = pb->len;
	struct _stp_entry *entry = NULL;

	/* check to see if there is anything in the buffer */
	dbug_trans(1, "len = %zu\n", len);
	if (likely(len == 0))
		return;

	pb->len = 0;

//DRS FIXME: this digs down too deep in internals
//	if (unlikely(!_stp_utt || _stp_utt->trace_state != Utt_trace_running))
//		return;

#ifdef STP_BULKMODE
#ifdef NO_PERCPU_HEADERS
	{
		char *bufp = pb->buf;

		while (len > 0) {
			size_t bytes_reserved;

			bytes_reserved = _stp_data_write_reserve(len, &entry);
			if (likely(entry && bytes_reserved > 0)) {
				memcpy(entry->buf, bufp, bytes_reserved);
				_stp_data_write_commit(entry);
				bufp += bytes_reserved;
				len -= bytes_reserved;
			}
			else {
				atomic_inc(&_stp_transport_failures);
				break;
			}
		}
	}

#else  /* !NO_PERCPU_HEADERS */

	{
		char *bufp = pb->buf;
		struct _stp_trace t = {	.sequence = _stp_seq_inc(),
					.pdu_len = len};
		size_t bytes_reserved;

		bytes_reserved = _stp_data_write_reserve(sizeof(struct _stp_trace), &entry);
		if (likely(entry && bytes_reserved > 0)) {
			/* prevent unaligned access by using memcpy() */
			memcpy(entry->buf, &t, sizeof(t));
			_stp_data_write_commit(entry);
		}
		else {
			atomic_inc(&_stp_transport_failures);
			return;
		}

		while (len > 0) {
			bytes_reserved = _stp_data_write_reserve(len, &entry);
			if (likely(entry && bytes_reserved > 0)) {
				memcpy(entry->buf, bufp, bytes_reserved);
				_stp_data_write_commit(entry);
				bufp += bytes_reserved;
				len -= bytes_reserved;
			}
			else {
				atomic_inc(&_stp_transport_failures);
				break;
			}
		}
	}
#endif /* !NO_PERCPU_HEADERS */
#else  /* !STP_BULKMODE */
	{
		unsigned long flags;
		char *bufp = pb->buf;

		dbug_trans(1, "calling _stp_data_write...\n");
		spin_lock_irqsave(&_stp_print_lock, flags);
		while (len > 0) {
			size_t bytes_reserved;

			bytes_reserved = _stp_data_write_reserve(len, &entry);
			if (likely(entry && bytes_reserved > 0)) {
				memcpy(entry->buf, bufp, bytes_reserved);
				_stp_data_write_commit(entry);
				bufp += bytes_reserved;
				len -= bytes_reserved;
			}
			else {
			    atomic_inc(&_stp_transport_failures);
			    break;
			}
		}
		spin_unlock_irqrestore(&_stp_print_lock, flags);
	}
#endif /* !STP_BULKMODE */
}
