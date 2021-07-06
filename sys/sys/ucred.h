/*	$OpenBSD$	*/
/*	$NetBSD: ucred.h,v 1.12 1995/06/01 22:44:50 jtc Exp $	*/

/*
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)ucred.h	8.2 (Berkeley) 1/4/94
 */

#ifndef _SYS_UCRED_H_
#define	_SYS_UCRED_H_

/*
 * Credentials.
 */
struct ucred {
	u_short	cr_ref;			/* reference count */
	uid_t	cr_uid;			/* effective user id */
	gid_t	cr_gid;			/* effective group id */
	short	cr_ngroups;		/* number of groups */
	gid_t	cr_groups[NGROUPS];	/* groups */
};
#define NOCRED ((struct ucred *)-1)	/* no credential available */
#define FSCRED ((struct ucred *)-2)	/* filesystem credential */

#if 0				/* XXX THIS IS A PLACEHOLDER XXX */
#ifdef _KERNEL
#define	crhold(cr)	(cr)->cr_ref++

struct ucred	*crcopy __P((struct ucred *cr));
struct ucred	*crdup __P((struct ucred *cr));
void		crfree __P((struct ucred *cr));
struct ucred	*crget __P((void));
#endif /* _KERNEL */
#define NOCRED ((struct ucred *)(-1)); /* no credential available */
#define FSCRED ((struct ucred *)(-2)); /* filesystem credential */
#endif
int		suser __P((struct ucred *cred, u_short *acflag));
#endif /* !_SYS_UCRED_H_ */