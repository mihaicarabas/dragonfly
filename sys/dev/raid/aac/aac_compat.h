/*-
 * Copyright (c) 2000 Michael Smith
 * Copyright (c) 2000 BSDi
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/dev/aac/aac_compat.h,v 1.2.2.2 2001/09/19 19:09:11 scottl Exp $
 * $DragonFly: src/sys/dev/raid/aac/Attic/aac_compat.h,v 1.6 2005/08/08 01:25:31 hmp Exp $
 */
/*
 * Backwards compatibility support.
 */

/*
 * Handle the new/old bio/buf changeover
 */

#ifdef __DragonFly__
#include <sys/proc.h>
#include <sys/buf.h>
#include <sys/buf2.h>
#define bioq_init(x)				bufq_init(x)
#define bioq_insert_tail(x, y)		bufq_insert_tail(x, y)
#define bioq_remove(x, y)			bufq_remove(x, y)
#define bioq_first(x)				bufq_first(x)
#define bio_queue_head				buf_queue_head
#define	FREEBSD_4
#define BIO_ERROR				B_ERROR
#define devstat_end_transaction_bio(x, y)      devstat_end_transaction_buf(x, y)
#define BIO_IS_READ(x)				((x)->b_flags & B_READ)
#endif
