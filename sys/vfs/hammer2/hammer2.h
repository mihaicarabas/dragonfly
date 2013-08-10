/*
 * Copyright (c) 2011-2013 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@dragonflybsd.org>
 * by Venkatesh Srinivas <vsrinivas@dragonflybsd.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * This header file contains structures used internally by the HAMMER2
 * implementation.  See hammer2_disk.h for on-disk structures.
 */

#ifndef _VFS_HAMMER2_HAMMER2_H_
#define _VFS_HAMMER2_HAMMER2_H_

#include <sys/param.h>
#include <sys/types.h>
#include <sys/kernel.h>
#include <sys/conf.h>
#include <sys/systm.h>
#include <sys/tree.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/proc.h>
#include <sys/mountctl.h>
#include <sys/priv.h>
#include <sys/stat.h>
#include <sys/thread.h>
#include <sys/globaldata.h>
#include <sys/lockf.h>
#include <sys/buf.h>
#include <sys/queue.h>
#include <sys/limits.h>
#include <sys/buf2.h>
#include <sys/signal2.h>
#include <sys/dmsg.h>

#include "hammer2_disk.h"
#include "hammer2_mount.h"
#include "hammer2_ioctl.h"
#include "hammer2_ccms.h"

struct hammer2_chain;
struct hammer2_inode;
struct hammer2_mount;
struct hammer2_pfsmount;
struct hammer2_span;
struct hammer2_state;
struct hammer2_msg;

/*
 * The chain structure tracks blockref recursions all the way to the root
 * volume.  These consist of indirect blocks, inodes, and eventually the
 * volume header itself.
 *
 * In situations where a duplicate is needed to represent different snapshots
 * or flush points a new chain will be allocated but associated with the
 * same shared chain_core.  The RBTREE is contained in the shared chain_core
 * and entries in the RBTREE are versioned.
 *
 * Duplication can occur whenever a chain must be modified.  Note that
 * a deletion is not considered a modification.
 *
 *	(a) General modifications at data leafs
 *	(b) When a chain is resized
 *	(c) When a chain's blockref array is updated
 *	(d) When a chain is renamed
 *	(e) When a chain is moved (when an indirect block is split)
 *
 * Advantages:
 *
 *	(1) Fully coherent snapshots can be taken without requiring
 *	    a pre-flush, resulting in extremely fast (sub-millisecond)
 *	    snapshots.
 *
 *	(2) Multiple synchronization points can be in-flight at the same
 *	    time, representing multiple snapshots or flushes.
 *
 *	(3) The algorithms needed to keep track of everything are actually
 *	    not that complex.
 *
 * Special Considerations:
 *
 *	A chain is ref-counted on a per-chain basis, but the chain's lock
 *	is associated with the shared chain_core and is not per-chain.
 *
 *	Each chain is representative of a filesystem topology.  Even
 *	though the shared chain_core's are effectively multi-homed, the
 *	chain structure is not.
 *
 *	chain->parent is a stable pointer and can be iterated without locking
 *	as long as either the chain or *any* deep child under the chain
 *	is held.
 */
RB_HEAD(hammer2_chain_tree, hammer2_chain);
TAILQ_HEAD(flush_deferral_list, hammer2_chain);

struct hammer2_chain_core {
	struct ccms_cst	cst;
	struct hammer2_chain_tree rbtree;
	struct hammer2_chain	*first_parent;
	u_int		sharecnt;
	u_int		flags;
};

typedef struct hammer2_chain_core hammer2_chain_core_t;

#define HAMMER2_CORE_INDIRECT		0x0001

struct hammer2_chain {
	RB_ENTRY(hammer2_chain) rbnode;
	hammer2_blockref_t	bref;
	hammer2_chain_core_t	*core;
	hammer2_chain_core_t	*above;
	struct hammer2_chain	*next_parent;
	struct hammer2_state	*state;		/* if active cache msg */
	struct hammer2_mount	*hmp;

	hammer2_tid_t	modify_tid;		/* snapshot/flush filter */
	hammer2_tid_t	delete_tid;
	hammer2_key_t   data_count;		/* delta's to apply */
	hammer2_key_t   inode_count;		/* delta's to apply */
	struct buf	*bp;			/* physical data buffer */
	u_int		bytes;			/* physical data size */
	int		index;			/* blockref index in parent */
	u_int		flags;
	u_int		refs;
	u_int		lockcnt;
	hammer2_media_data_t *data;		/* data pointer shortcut */
	TAILQ_ENTRY(hammer2_chain) flush_node;	/* flush deferral list */
};

typedef struct hammer2_chain hammer2_chain_t;

int hammer2_chain_cmp(hammer2_chain_t *chain1, hammer2_chain_t *chain2);
RB_PROTOTYPE(hammer2_chain_tree, hammer2_chain, rbnode, hammer2_chain_cmp);

/*
 * Special notes on flags:
 *
 * INITIAL - This flag allows a chain to be created and for storage to
 *	     be allocated without having to immediately instantiate the
 *	     related buffer.  The data is assumed to be all-zeros.  It
 *	     is primarily used for indirect blocks.
 *
 * MOVED   - A modified chain becomes MOVED after it flushes.  A chain
 *	     can also become MOVED if it is moved within the topology
 *	     (even if not modified).
 */
#define HAMMER2_CHAIN_MODIFIED		0x00000001	/* dirty chain data */
#define HAMMER2_CHAIN_ALLOCATED		0x00000002	/* kmalloc'd chain */
#define HAMMER2_CHAIN_DIRTYBP		0x00000004	/* dirty on unlock */
#define HAMMER2_CHAIN_SUBMODIFIED	0x00000008	/* recursive flush */
#define HAMMER2_CHAIN_DELETED		0x00000010	/* deleted chain */
#define HAMMER2_CHAIN_INITIAL		0x00000020	/* initial create */
#define HAMMER2_CHAIN_FLUSHED		0x00000040	/* flush on unlock */
#define HAMMER2_CHAIN_MOVED		0x00000080	/* bref changed */
#define HAMMER2_CHAIN_IOFLUSH		0x00000100	/* bawrite on put */
#define HAMMER2_CHAIN_DEFERRED		0x00000200	/* on a deferral list */
#define HAMMER2_CHAIN_DESTROYED		0x00000400	/* destroying inode */
#define HAMMER2_CHAIN_VOLUMESYNC	0x00000800	/* needs volume sync */
#define HAMMER2_CHAIN_RECYCLE		0x00001000	/* force recycle */
#define HAMMER2_CHAIN_MOUNTED		0x00002000	/* PFS is mounted */
#define HAMMER2_CHAIN_ONRBTREE		0x00004000	/* on parent RB tree */
#define HAMMER2_CHAIN_SNAPSHOT		0x00008000	/* snapshot special */
#define HAMMER2_CHAIN_EMBEDDED		0x00010000	/* embedded data */

/*
 * Flags passed to hammer2_chain_lookup() and hammer2_chain_next()
 *
 * NOTE: MATCHIND allows an indirect block / freemap node to be returned
 *	 when the passed key range matches the radix.  Remember that key_end
 *	 is inclusive (e.g. {0x000,0xFFF}, not {0x000,0x1000}).
 */
#define HAMMER2_LOOKUP_NOLOCK		0x00000001	/* ref only */
#define HAMMER2_LOOKUP_NODATA		0x00000002	/* data left NULL */
#define HAMMER2_LOOKUP_SHARED		0x00000100
#define HAMMER2_LOOKUP_MATCHIND		0x00000200
#define HAMMER2_LOOKUP_FREEMAP		0x00000400	/* freemap base */
#define HAMMER2_LOOKUP_ALWAYS		0x00000800	/* resolve data */

/*
 * Flags passed to hammer2_chain_modify() and hammer2_chain_resize()
 *
 * NOTE: OPTDATA allows us to avoid instantiating buffers for INDIRECT
 *	 blocks in the INITIAL-create state.
 */
#define HAMMER2_MODIFY_OPTDATA		0x00000002	/* data can be NULL */
#define HAMMER2_MODIFY_NO_MODIFY_TID	0x00000004
#define HAMMER2_MODIFY_ASSERTNOCOPY	0x00000008
#define HAMMER2_MODIFY_NOREALLOC	0x00000010

/*
 * Flags passed to hammer2_chain_lock()
 */
#define HAMMER2_RESOLVE_NEVER		1
#define HAMMER2_RESOLVE_MAYBE		2
#define HAMMER2_RESOLVE_ALWAYS		3
#define HAMMER2_RESOLVE_MASK		0x0F

#define HAMMER2_RESOLVE_SHARED		0x10	/* request shared lock */
#define HAMMER2_RESOLVE_NOREF		0x20	/* already ref'd on lock */

/*
 * Flags passed to hammer2_chain_delete()
 */
#define HAMMER2_DELETE_WILLDUP		0x0001	/* no blk free, will be dup */

/*
 * Flags passed to hammer2_chain_delete_duplicate()
 */
#define HAMMER2_DELDUP_RECORE		0x0001

/*
 * Cluster different types of storage together for allocations
 */
#define HAMMER2_FREECACHE_INODE		0
#define HAMMER2_FREECACHE_INDIR		1
#define HAMMER2_FREECACHE_DATA		2
#define HAMMER2_FREECACHE_UNUSED3	3
#define HAMMER2_FREECACHE_TYPES		4

/*
 * hammer2_freemap_alloc() block preference
 */
#define HAMMER2_OFF_NOPREF		((hammer2_off_t)-1)

/*
 * BMAP read-ahead maximum parameters
 */
#define HAMMER2_BMAP_COUNT		16	/* max bmap read-ahead */
#define HAMMER2_BMAP_BYTES		(HAMMER2_PBUFSIZE * HAMMER2_BMAP_COUNT)

/*
 * Misc
 */
#define HAMMER2_FLUSH_DEPTH_LIMIT	40	/* stack recursion limit */

/*
 * HAMMER2 IN-MEMORY CACHE OF MEDIA STRUCTURES
 *
 * There is an in-memory representation of all on-media data structure.
 *
 * When accessed read-only the data will be mapped to the related buffer
 * cache buffer.
 *
 * When accessed read-write (marked modified) a kmalloc()'d copy of the
 * is created which can then be modified.  The copy is destroyed when a
 * filesystem block is allocated to replace it.
 *
 * Active inodes (those with vnodes attached) will maintain the kmalloc()'d
 * copy for both the read-only and the read-write case.  The combination of
 * (bp) and (data) determines whether (data) was allocated or not.
 *
 * The in-memory representation may remain cached (for example in order to
 * placemark clustering locks) even after the related data has been
 * detached.
 */

RB_HEAD(hammer2_inode_tree, hammer2_inode);

/*
 * A hammer2 inode.
 *
 * NOTE: The inode's attribute CST which is also used to lock the inode
 *	 is embedded in the chain (chain.cst) and aliased w/ attr_cst.
 */
struct hammer2_inode {
	RB_ENTRY(hammer2_inode) rbnode;		/* inumber lookup (HL) */
	ccms_cst_t		topo_cst;	/* directory topology cst */
	struct hammer2_pfsmount	*pmp;		/* PFS mount */
	struct hammer2_inode	*pip;		/* parent inode */
	struct vnode		*vp;
	hammer2_chain_t		*chain;		/* NOTE: rehomed on rename */
	struct lockf		advlock;
	hammer2_tid_t		inum;
	u_int			flags;
	u_int			refs;		/* +vpref, +flushref */
};

typedef struct hammer2_inode hammer2_inode_t;

#define HAMMER2_INODE_MODIFIED		0x0001
#define HAMMER2_INODE_SROOT		0x0002	/* kmalloc special case */
#define HAMMER2_INODE_RENAME_INPROG	0x0004
#define HAMMER2_INODE_ONRBTREE		0x0008

int hammer2_inode_cmp(hammer2_inode_t *ip1, hammer2_inode_t *ip2);
RB_PROTOTYPE2(hammer2_inode_tree, hammer2_inode, rbnode, hammer2_inode_cmp,
		hammer2_tid_t);

/*
 * A hammer2 transaction and flush sequencing structure.
 *
 * This global structure is tied into hammer2_mount and is used
 * to sequence modifying operations and flushes.
 *
 * (a) Any modifying operations with sync_tid >= flush_tid will stall until
 *     all modifying operating with sync_tid < flush_tid complete.
 *
 *     The flush related to flush_tid stalls until all modifying operations
 *     with sync_tid < flush_tid complete.
 *
 * (b) Once unstalled, modifying operations with sync_tid > flush_tid are
 *     allowed to run.  All modifications cause modify/duplicate operations
 *     to occur on the related chains.  Note that most INDIRECT blocks will
 *     be unaffected because the modifications just overload the RBTREE
 *     structurally instead of actually modifying the indirect blocks.
 *
 * (c) The actual flush unstalls and RUNS CONCURRENTLY with (b), but only
 *     utilizes the chain structures with sync_tid <= flush_tid.  The
 *     flush will modify related indirect blocks and inodes in-place
 *     (rather than duplicate) since the adjustments are compatible with
 *     (b)'s RBTREE overloading
 *
 *     SPECIAL NOTE:  Inode modifications have to also propagate along any
 *		      modify/duplicate chains.  File writes detect the flush
 *		      and force out the conflicting buffer cache buffer(s)
 *		      before reusing them.
 *
 * (d) Snapshots can be made instantly but must be flushed and disconnected
 *     from their duplicative source before they can be mounted.  This is
 *     because while H2's on-media structure supports forks, its in-memory
 *     structure only supports very simple forking for background flushing
 *     purposes.
 *
 * TODO: Flush merging.  When fsync() is called on multiple discrete files
 *	 concurrently there is no reason to stall the second fsync.
 *	 The final flush that reaches to root can cover both fsync()s.
 *
 *     The chains typically terminate as they fly onto the disk.  The flush
 *     ultimately reaches the volume header.
 */
struct hammer2_trans {
	TAILQ_ENTRY(hammer2_trans) entry;
	struct hammer2_pfsmount *pmp;
	hammer2_tid_t		sync_tid;
	thread_t		td;		/* pointer */
	int			flags;
	int			blocked;
	uint8_t			inodes_created;
	uint8_t			dummy[7];
};

typedef struct hammer2_trans hammer2_trans_t;

#define HAMMER2_TRANS_ISFLUSH		0x0001
#define HAMMER2_TRANS_RESTRICTED	0x0002	/* snapshot flush restrict */

#define HAMMER2_FREEMAP_HEUR_NRADIX	4	/* pwr 2 PBUFRADIX-MINIORADIX */
#define HAMMER2_FREEMAP_HEUR_TYPES	8
#define HAMMER2_FREEMAP_HEUR		(HAMMER2_FREEMAP_HEUR_NRADIX * \
					 HAMMER2_FREEMAP_HEUR_TYPES)

/*
 * Global (per device) mount structure for device (aka vp->v_mount->hmp)
 */
TAILQ_HEAD(hammer2_trans_queue, hammer2_trans);

struct hammer2_mount {
	struct vnode	*devvp;		/* device vnode */
	int		ronly;		/* read-only mount */
	int		pmp_count;	/* PFS mounts backed by us */
	TAILQ_ENTRY(hammer2_mount) mntentry; /* hammer2_mntlist */

	struct malloc_type *mchain;
	int		nipstacks;
	int		maxipstacks;
	hammer2_chain_t vchain;		/* anchor chain */
	hammer2_chain_t fchain;		/* freemap chain special */
	hammer2_chain_t *schain;	/* super-root */
	hammer2_inode_t	*sroot;		/* super-root inode */
	struct lock	alloclk;	/* lockmgr lock */
	struct lock	voldatalk;	/* lockmgr lock */
	struct hammer2_trans_queue transq; /* all in-progress transactions */
	hammer2_trans_t	*curflush;	/* current flush in progress */
	hammer2_tid_t	topo_flush_tid;	/* currently synchronizing flush pt */
	hammer2_tid_t	free_flush_tid;	/* currently synchronizing flush pt */
	hammer2_off_t	heur_freemap[HAMMER2_FREEMAP_HEUR];
	int		flushcnt;	/* #of flush trans on the list */

	int		volhdrno;	/* last volhdrno written */
	hammer2_volume_data_t voldata;
	hammer2_volume_data_t volsync;	/* synchronized voldata */
};

typedef struct hammer2_mount hammer2_mount_t;

/*
 * HAMMER2 cluster - a device/root associated with a PFS.
 *
 * A PFS may have several hammer2_cluster's associated with it.
 */
struct hammer2_cluster {
	struct hammer2_mount	*hmp;		/* device global mount */
	hammer2_chain_t 	*rchain;	/* PFS root chain */
};

typedef struct hammer2_cluster hammer2_cluster_t;

/*
 * HAMMER2 PFS mount point structure (aka vp->v_mount->mnt_data).
 *
 * This structure represents a cluster mount and not necessarily a
 * PFS under a specific device mount (HMP).  The distinction is important
 * because the elements backing a cluster mount can change on the fly.
 */
struct hammer2_pfsmount {
	struct mount		*mp;		/* kernel mount */
	hammer2_cluster_t	*mount_cluster;
	hammer2_cluster_t	*cluster;
	hammer2_inode_t		*iroot;		/* PFS root inode */
	hammer2_off_t		inode_count;	/* copy of inode_count */
	ccms_domain_t		ccms_dom;
	struct netexport	export;		/* nfs export */
	int			ronly;		/* read-only mount */
	struct malloc_type	*minode;
	struct malloc_type	*mmsg;
	kdmsg_iocom_t		iocom;
	struct spinlock		inum_spin;	/* inumber lookup */
	struct hammer2_inode_tree inum_tree;
};

typedef struct hammer2_pfsmount hammer2_pfsmount_t;

struct hammer2_cbinfo {
	hammer2_chain_t	*chain;
	void (*func)(hammer2_chain_t *, struct buf *, char *, void *);
	void *arg;
	size_t boff;
};

typedef struct hammer2_cbinfo hammer2_cbinfo_t;

#if defined(_KERNEL)

MALLOC_DECLARE(M_HAMMER2);

#define VTOI(vp)	((hammer2_inode_t *)(vp)->v_data)
#define ITOV(ip)	((ip)->vp)

/*
 * Currently locked chains retain the locked buffer cache buffer for
 * indirect blocks, and indirect blocks can be one of two sizes.  The
 * device buffer has to match the case to avoid deadlocking recursive
 * chains that might otherwise try to access different offsets within
 * the same device buffer.
 */
static __inline
int
hammer2_devblkradix(int radix)
{
#if 1
	if (radix <= HAMMER2_LBUFRADIX) {
		return (HAMMER2_LBUFRADIX);
	} else {
		return (HAMMER2_PBUFRADIX);
	}
#else
	return (HAMMER2_PBUFRADIX);
#endif
}

static __inline
size_t
hammer2_devblksize(size_t bytes)
{
#if 1
	if (bytes <= HAMMER2_LBUFSIZE) {
		return(HAMMER2_LBUFSIZE);
	} else {
		KKASSERT(bytes <= HAMMER2_PBUFSIZE &&
			 (bytes ^ (bytes - 1)) == ((bytes << 1) - 1));
		return (HAMMER2_PBUFSIZE);
	}
#else
	KKASSERT(bytes <= HAMMER2_PBUFSIZE &&
		 (bytes ^ (bytes - 1)) == ((bytes << 1) - 1));
	return(HAMMER2_PBUFSIZE);
#endif
}


static __inline
hammer2_pfsmount_t *
MPTOPMP(struct mount *mp)
{
	return ((hammer2_pfsmount_t *)mp->mnt_data);
}

static __inline
hammer2_mount_t *
MPTOHMP(struct mount *mp)
{
	return (((hammer2_pfsmount_t *)mp->mnt_data)->cluster->hmp);
}

static __inline
int
hammer2_chain_refactor_test(hammer2_chain_t *chain, int traverse_hlink)
{
	if ((chain->flags & HAMMER2_CHAIN_DELETED) &&
	    chain->next_parent &&
	    (chain->next_parent->flags & HAMMER2_CHAIN_SNAPSHOT) == 0) {
		return (1);
	}
	if (traverse_hlink &&
	    chain->bref.type == HAMMER2_BREF_TYPE_INODE &&
	    chain->data->ipdata.type == HAMMER2_OBJTYPE_HARDLINK &&
	    chain->next_parent &&
	    (chain->next_parent->flags & HAMMER2_CHAIN_SNAPSHOT) == 0) {
		return(1);
	}

	return (0);
}

extern struct vop_ops hammer2_vnode_vops;
extern struct vop_ops hammer2_spec_vops;
extern struct vop_ops hammer2_fifo_vops;

extern int hammer2_debug;
extern int hammer2_cluster_enable;
extern int hammer2_hardlink_enable;
extern long hammer2_iod_file_read;
extern long hammer2_iod_meta_read;
extern long hammer2_iod_indr_read;
extern long hammer2_iod_fmap_read;
extern long hammer2_iod_volu_read;
extern long hammer2_iod_file_write;
extern long hammer2_iod_meta_write;
extern long hammer2_iod_indr_write;
extern long hammer2_iod_fmap_write;
extern long hammer2_iod_volu_write;
extern long hammer2_ioa_file_read;
extern long hammer2_ioa_meta_read;
extern long hammer2_ioa_indr_read;
extern long hammer2_ioa_fmap_read;
extern long hammer2_ioa_volu_read;
extern long hammer2_ioa_file_write;
extern long hammer2_ioa_meta_write;
extern long hammer2_ioa_indr_write;
extern long hammer2_ioa_fmap_write;
extern long hammer2_ioa_volu_write;

/*
 * hammer2_subr.c
 */
#define hammer2_icrc32(buf, size)	iscsi_crc32((buf), (size))
#define hammer2_icrc32c(buf, size, crc)	iscsi_crc32_ext((buf), (size), (crc))

hammer2_chain_t *hammer2_inode_lock_ex(hammer2_inode_t *ip);
hammer2_chain_t *hammer2_inode_lock_sh(hammer2_inode_t *ip);
void hammer2_inode_unlock_ex(hammer2_inode_t *ip, hammer2_chain_t *chain);
void hammer2_inode_unlock_sh(hammer2_inode_t *ip, hammer2_chain_t *chain);
void hammer2_voldata_lock(hammer2_mount_t *hmp);
void hammer2_voldata_unlock(hammer2_mount_t *hmp, int modify);
ccms_state_t hammer2_inode_lock_temp_release(hammer2_inode_t *ip);
void hammer2_inode_lock_temp_restore(hammer2_inode_t *ip, ccms_state_t ostate);
ccms_state_t hammer2_inode_lock_upgrade(hammer2_inode_t *ip);
void hammer2_inode_lock_downgrade(hammer2_inode_t *ip, ccms_state_t ostate);

void hammer2_mount_exlock(hammer2_mount_t *hmp);
void hammer2_mount_shlock(hammer2_mount_t *hmp);
void hammer2_mount_unlock(hammer2_mount_t *hmp);

int hammer2_get_dtype(hammer2_chain_t *chain);
int hammer2_get_vtype(hammer2_chain_t *chain);
u_int8_t hammer2_get_obj_type(enum vtype vtype);
void hammer2_time_to_timespec(u_int64_t xtime, struct timespec *ts);
u_int64_t hammer2_timespec_to_time(struct timespec *ts);
u_int32_t hammer2_to_unix_xid(uuid_t *uuid);
void hammer2_guid_to_uuid(uuid_t *uuid, u_int32_t guid);

hammer2_key_t hammer2_dirhash(const unsigned char *name, size_t len);
int hammer2_getradix(size_t bytes);

int hammer2_calc_logical(hammer2_inode_t *ip, hammer2_off_t uoff,
			 hammer2_key_t *lbasep, hammer2_key_t *leofp);
void hammer2_update_time(uint64_t *timep);

/*
 * hammer2_inode.c
 */
struct vnode *hammer2_igetv(hammer2_inode_t *ip, int *errorp);

void hammer2_inode_lock_nlinks(hammer2_inode_t *ip);
void hammer2_inode_unlock_nlinks(hammer2_inode_t *ip);
hammer2_inode_t *hammer2_inode_lookup(hammer2_pfsmount_t *pmp,
			hammer2_tid_t inum);
hammer2_inode_t *hammer2_inode_get(hammer2_pfsmount_t *pmp,
			hammer2_inode_t *dip, hammer2_chain_t *chain);
void hammer2_inode_free(hammer2_inode_t *ip);
void hammer2_inode_ref(hammer2_inode_t *ip);
void hammer2_inode_drop(hammer2_inode_t *ip);
void hammer2_inode_repoint(hammer2_inode_t *ip, hammer2_inode_t *pip,
			hammer2_chain_t *chain);

hammer2_inode_t *hammer2_inode_create(hammer2_trans_t *trans,
			hammer2_inode_t *dip,
			struct vattr *vap, struct ucred *cred,
			const uint8_t *name, size_t name_len,
			hammer2_chain_t **chainp, int *errorp);
int hammer2_inode_connect(hammer2_trans_t *trans, int hlink,
			hammer2_inode_t *dip, hammer2_chain_t **chainp,
			const uint8_t *name, size_t name_len);
hammer2_inode_t *hammer2_inode_common_parent(hammer2_inode_t *fdip,
			hammer2_inode_t *tdip);

int hammer2_unlink_file(hammer2_trans_t *trans, hammer2_inode_t *dip,
			const uint8_t *name, size_t name_len, int isdir,
			int *hlinkp);
int hammer2_hardlink_consolidate(hammer2_trans_t *trans, hammer2_inode_t *ip,
			hammer2_chain_t **chainp,
			hammer2_inode_t *tdip, int linkcnt);
int hammer2_hardlink_deconsolidate(hammer2_trans_t *trans, hammer2_inode_t *dip,
			hammer2_chain_t **chainp, hammer2_chain_t **ochainp);
int hammer2_hardlink_find(hammer2_inode_t *dip,
			hammer2_chain_t **chainp, hammer2_chain_t **ochainp);

/*
 * hammer2_chain.c
 */
void hammer2_modify_volume(hammer2_mount_t *hmp);
hammer2_chain_t *hammer2_chain_alloc(hammer2_mount_t *hmp,
				hammer2_trans_t *trans,
				hammer2_blockref_t *bref);
void hammer2_chain_core_alloc(hammer2_chain_t *chain,
				hammer2_chain_core_t *core);
void hammer2_chain_ref(hammer2_chain_t *chain);
void hammer2_chain_drop(hammer2_chain_t *chain);
int hammer2_chain_lock(hammer2_chain_t *chain, int how);
void hammer2_chain_load_async(hammer2_chain_t *chain,
				void (*func)(hammer2_chain_t *, struct buf *,
					     char *, void *),
				void *arg);
void hammer2_chain_moved(hammer2_chain_t *chain);
void hammer2_chain_modify(hammer2_trans_t *trans,
				hammer2_chain_t **chainp, int flags);
hammer2_inode_data_t *hammer2_chain_modify_ip(hammer2_trans_t *trans,
				hammer2_inode_t *ip, hammer2_chain_t **chainp,
				int flags);
void hammer2_chain_resize(hammer2_trans_t *trans, hammer2_inode_t *ip,
				struct buf *bp,
				hammer2_chain_t *parent,
				hammer2_chain_t **chainp,
				int nradix, int flags);
void hammer2_chain_unlock(hammer2_chain_t *chain);
void hammer2_chain_wait(hammer2_chain_t *chain);
hammer2_chain_t *hammer2_chain_find(hammer2_chain_t *parent, int index);
hammer2_chain_t *hammer2_chain_get(hammer2_chain_t *parent, int index,
				int flags);
hammer2_chain_t *hammer2_chain_lookup_init(hammer2_chain_t *parent, int flags);
void hammer2_chain_lookup_done(hammer2_chain_t *parent);
hammer2_chain_t *hammer2_chain_lookup(hammer2_chain_t **parentp,
				hammer2_key_t key_beg, hammer2_key_t key_end,
				int flags);
hammer2_chain_t *hammer2_chain_next(hammer2_chain_t **parentp,
				hammer2_chain_t *chain,
				hammer2_key_t key_beg, hammer2_key_t key_end,
				int flags);
int hammer2_chain_iterate(hammer2_chain_t *parent,
			  int (*callback)(hammer2_chain_t *parent,
					  hammer2_chain_t **chainp,
					  void *arg),
			  void *arg, int flags);

int hammer2_chain_create(hammer2_trans_t *trans,
				hammer2_chain_t **parentp,
				hammer2_chain_t **chainp,
				hammer2_key_t key, int keybits,
				int type, size_t bytes);
void hammer2_chain_duplicate(hammer2_trans_t *trans, hammer2_chain_t *parent,
				int i,
				hammer2_chain_t **chainp,
				hammer2_blockref_t *bref);
int hammer2_chain_snapshot(hammer2_trans_t *trans, hammer2_inode_t *ip,
				hammer2_ioc_pfs_t *pfs);
void hammer2_chain_delete(hammer2_trans_t *trans, hammer2_chain_t *chain,
				int flags);
void hammer2_chain_delete_duplicate(hammer2_trans_t *trans,
				hammer2_chain_t **chainp, int flags);
void hammer2_chain_flush(hammer2_trans_t *trans, hammer2_chain_t *chain);
void hammer2_chain_commit(hammer2_trans_t *trans, hammer2_chain_t *chain);
void hammer2_chain_setsubmod(hammer2_trans_t *trans, hammer2_chain_t *chain);

/*
 * hammer2_trans.c
 */
void hammer2_trans_init(hammer2_trans_t *trans,
			hammer2_pfsmount_t *pmp, int flags);
void hammer2_trans_done(hammer2_trans_t *trans);

/*
 * hammer2_ioctl.c
 */
int hammer2_ioctl(hammer2_inode_t *ip, u_long com, void *data,
				int fflag, struct ucred *cred);

/*
 * hammer2_msgops.c
 */
int hammer2_msg_dbg_rcvmsg(kdmsg_msg_t *msg);
int hammer2_msg_adhoc_input(kdmsg_msg_t *msg);

/*
 * hammer2_vfsops.c
 */
void hammer2_clusterctl_wakeup(kdmsg_iocom_t *iocom);
void hammer2_volconf_update(hammer2_pfsmount_t *pmp, int index);
void hammer2_cluster_reconnect(hammer2_pfsmount_t *pmp, struct file *fp);
void hammer2_dump_chain(hammer2_chain_t *chain, int tab, int *countp);

/*
 * hammer2_freemap.c
 */
int hammer2_freemap_alloc(hammer2_trans_t *trans, hammer2_mount_t *hmp,
				hammer2_blockref_t *bref, size_t bytes);
void hammer2_freemap_free(hammer2_trans_t *trans, hammer2_mount_t *hmp,
				hammer2_blockref_t *bref, int how);


#endif /* !_KERNEL */
#endif /* !_VFS_HAMMER2_HAMMER2_H_ */
