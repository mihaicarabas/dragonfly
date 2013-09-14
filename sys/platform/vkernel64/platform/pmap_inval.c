/*
 * Copyright (c) 2003,2004 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
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
 *
 * $DragonFly: src/sys/platform/vkernel/platform/pmap_inval.c,v 1.4 2007/07/02 02:22:58 dillon Exp $
 */

/*
 * pmap invalidation support code.  Certain hardware requirements must
 * be dealt with when manipulating page table entries and page directory
 * entries within a pmap.  In particular, we cannot safely manipulate
 * page tables which are in active use by another cpu (even if it is
 * running in userland) for two reasons: First, TLB writebacks will
 * race against our own modifications and tests.  Second, even if we
 * were to use bus-locked instruction we can still screw up the
 * target cpu's instruction pipeline due to Intel cpu errata.
 *
 * For our virtual page tables, the real kernel will handle SMP interactions
 * with pmaps that may be active on other cpus.  Even so, we have to be
 * careful about bit setting races particularly when we are trying to clean
 * a page and test the modified bit to avoid races where the modified bit
 * might get set after our poll but before we clear the field.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/vmmeter.h>
#include <sys/thread2.h>

#include <sys/mman.h>
#include <sys/vmspace.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_object.h>

#include <machine/cputypes.h>
#include <machine/md_var.h>
#include <machine/specialreg.h>
#include <machine/smp.h>
#include <machine/globaldata.h>
#include <machine/pmap.h>
#include <machine/pmap_inval.h>

#include <unistd.h>

extern int vmm_enabled;

static __inline
void
vmm_cpu_invltlb(void)
{
	uint64_t rax = -1;
	__asm __volatile("syscall;"
			:
			: "a" (rax)
			:);
}

/*
 * Invalidate va in the TLB on the current cpu
 */
static __inline
void
pmap_inval_cpu(struct pmap *pmap, vm_offset_t va, size_t bytes)
{
	if (vmm_enabled) {
		vmm_cpu_invltlb(); /* For VMM mode forces vmmexit/resume */
	} else if (pmap == &kernel_pmap) {
		madvise((void *)va, bytes, MADV_INVAL);
	} else {
		vmspace_mcontrol(pmap, (void *)va, bytes, MADV_INVAL, 0);
	}
}

/*
 * Invalidate a pte in a pmap and synchronize with target cpus
 * as required.  Throw away the modified and access bits.  Use
 * pmap_clean_pte() to do the same thing but also get an interlocked
 * modified/access status.
 *
 * Clearing the field first (basically clearing VPTE_V) prevents any
 * new races from occuring while we invalidate the TLB (i.e. the pmap
 * on the real cpu), then clear it again to clean out any race that
 * might have occured before the invalidation completed.
 */
void
pmap_inval_pte(volatile vpte_t *ptep, struct pmap *pmap, vm_offset_t va)
{
	if (vmm_enabled) {
		pmap_inval_info info;
		pmap_inval_init(&info);
		pmap_inval_interlock(&info, pmap, va);
		*ptep = 0;
		pmap_inval_deinterlock(&info, pmap);
		pmap_inval_done(&info);
	} else {
		*ptep = 0;
		pmap_inval_cpu(pmap, va, PAGE_SIZE);
	}
}

/*
 * Same as pmap_inval_pte() but only synchronize with the current
 * cpu.  For the moment its the same as the non-quick version.
 */
void
pmap_inval_pte_quick(volatile vpte_t *ptep, struct pmap *pmap, vm_offset_t va)
{
	*ptep = 0;
	pmap_inval_cpu(pmap, va, PAGE_SIZE);
}

/*
 * Invalidating page directory entries requires some additional
 * sophistication.  The cachemask must be cleared so the kernel
 * resynchronizes its temporary page table mappings cache.
 */
void
pmap_inval_pde(volatile vpte_t *ptep, struct pmap *pmap, vm_offset_t va)
{
	if (vmm_enabled) {
		pmap_inval_info info;
		pmap_inval_init(&info);
		pmap_inval_interlock(&info, pmap, va);
		*ptep = 0;
		pmap_inval_deinterlock(&info, pmap);
		pmap_inval_done(&info);
	} else {
		*ptep = 0;
		pmap_inval_cpu(pmap, va, SEG_SIZE);
	}
}

void
pmap_inval_pde_quick(volatile vpte_t *ptep, struct pmap *pmap, vm_offset_t va)
{
	pmap_inval_pde(ptep, pmap, va);
}

/*
 * These carefully handle interactions with other cpus and return
 * the original vpte.  Clearing VPTE_RW prevents us from racing the
 * setting of VPTE_M, allowing us to invalidate the tlb (the real cpu's
 * pmap) and get good status for VPTE_M.
 *
 * When messing with page directory entries we have to clear the cpu
 * mask to force a reload of the kernel's page table mapping cache.
 *
 * clean: clear VPTE_M and VPTE_RW
 * setro: clear VPTE_RW
 * load&clear: clear entire field
 */
#include<stdio.h>
vpte_t
pmap_clean_pte(volatile vpte_t *ptep, struct pmap *pmap, vm_offset_t va)
{
	vpte_t pte;
	pte = *ptep;
	if (pte & VPTE_V) {
		atomic_clear_long(ptep, VPTE_RW);
		if (vmm_enabled) {
			pmap_inval_info info;
			pmap_inval_init(&info);
			pmap_inval_interlock(&info, pmap, va);
			pte = *ptep;
			pmap_inval_deinterlock(&info, pmap);
			pmap_inval_done(&info);

		} else {
			pmap_inval_cpu(pmap, va, PAGE_SIZE);
			pte = *ptep;
		}
		atomic_clear_long(ptep, VPTE_RW|VPTE_M);
	}
	return(pte);
}

vpte_t
pmap_clean_pde(volatile vpte_t *ptep, struct pmap *pmap, vm_offset_t va)
{
	vpte_t pte;

	pte = *ptep;
	if (pte & VPTE_V) {
		atomic_clear_long(ptep, VPTE_RW);
		if (vmm_enabled) {
			pmap_inval_info info;
			pmap_inval_init(&info);
			pmap_inval_interlock(&info, pmap, va);
			pte = *ptep;
			pmap_inval_deinterlock(&info, pmap);
			pmap_inval_done(&info);
		} else {

			pmap_inval_cpu(pmap, va, SEG_SIZE);
			pte = *ptep;
		}
			atomic_clear_long(ptep, VPTE_RW|VPTE_M);
	}
	return(pte);
}

/*
 * This is an odd case and I'm not sure whether it even occurs in normal
 * operation.  Turn off write access to the page, clean out the tlb
 * (the real cpu's pmap), and deal with any VPTE_M race that may have
 * occured.  VPTE_M is not cleared.
 */
vpte_t
pmap_setro_pte(volatile vpte_t *ptep, struct pmap *pmap, vm_offset_t va)
{
	vpte_t pte;

	pte = *ptep;
	if (pte & VPTE_V) {
		pte = *ptep;
		atomic_clear_long(ptep, VPTE_RW);
		if (vmm_enabled) {
			pmap_inval_info info;
			pmap_inval_init(&info);
			pmap_inval_interlock(&info, pmap, va);
			pte |= *ptep & VPTE_M;
			pmap_inval_deinterlock(&info, pmap);
			pmap_inval_done(&info);
		} else {
			pmap_inval_cpu(pmap, va, PAGE_SIZE);
			pte |= *ptep & VPTE_M;
		}
	}
	return(pte);
}

/*
 * This is a combination of pmap_inval_pte() and pmap_clean_pte().
 * Firts prevent races with the 'A' and 'M' bits, then clean out
 * the tlb (the real cpu's pmap), then incorporate any races that
 * may have occured in the mean time, and finally zero out the pte.
 */
vpte_t
pmap_inval_loadandclear(volatile vpte_t *ptep, struct pmap *pmap,
			vm_offset_t va)
{
	vpte_t pte;

	pte = *ptep;
	if (pte & VPTE_V) {
		pte = *ptep;
		atomic_clear_long(ptep, VPTE_RW);
		if (vmm_enabled) {
			pmap_inval_info info;
			pmap_inval_init(&info);
			pmap_inval_interlock(&info, pmap, va);
			pte |= *ptep & (VPTE_A | VPTE_M);
			pmap_inval_deinterlock(&info, pmap);
			pmap_inval_done(&info);
		} else {
			pmap_inval_cpu(pmap, va, PAGE_SIZE);
			pte |= *ptep & (VPTE_A | VPTE_M);
		}
	}
	*ptep = 0;
	return(pte);
}

/* VMM used stuff */
static void pmap_inval_callback(void *arg);

/*
 * Initialize for add or flush
 *
 * The critical section is required to prevent preemption, allowing us to
 * set CPUMASK_LOCK on the pmap.  The critical section is also assumed
 * when lwkt_process_ipiq() is called.
 */
void
pmap_inval_init(pmap_inval_info_t info)
{
    info->pir_flags = 0;
    crit_enter_id("inval");
}

/*
 * Add a (pmap, va) pair to the invalidation list and protect access
 * as appropriate.
 *
 * CPUMASK_LOCK is used to interlock thread switchins, otherwise another
 * cpu can switch in a pmap that we are unaware of and interfere with our
 * pte operation.
 */
void
pmap_inval_interlock(pmap_inval_info_t info, pmap_t pmap, vm_offset_t va)
{
    cpumask_t oactive;
    cpumask_t nactive;

    DEBUG_PUSH_INFO("pmap_inval_interlock");
    for (;;) {
	oactive = pmap->pm_active;
	cpu_ccfence();
	nactive = oactive | CPUMASK_LOCK;
	if ((oactive & CPUMASK_LOCK) == 0 &&
	    atomic_cmpset_cpumask(&pmap->pm_active, oactive, nactive)) {
		break;
	}
	lwkt_process_ipiq();
	cpu_pause();
    }
    DEBUG_POP_INFO();
    KKASSERT((info->pir_flags & PIRF_CPUSYNC) == 0);
    info->pir_va = va;
    info->pir_flags = PIRF_CPUSYNC;
    lwkt_cpusync_init(&info->pir_cpusync, oactive, pmap_inval_callback, info);
    lwkt_cpusync_interlock(&info->pir_cpusync);
}

void
pmap_inval_invltlb(pmap_inval_info_t info)
{
	info->pir_va = (vm_offset_t)-1;
}

void
pmap_inval_deinterlock(pmap_inval_info_t info, pmap_t pmap)
{
	KKASSERT(info->pir_flags & PIRF_CPUSYNC);
	atomic_clear_cpumask(&pmap->pm_active, CPUMASK_LOCK);
	lwkt_cpusync_deinterlock(&info->pir_cpusync);
	info->pir_flags = 0;
}

static void
pmap_inval_callback(void *arg)
{
	pmap_inval_info_t info = arg;

	if (info->pir_va == (vm_offset_t)-1)
		cpu_invltlb();
	else
		cpu_invlpg((void *)info->pir_va);
}

void
pmap_inval_done(pmap_inval_info_t info)
{
    KKASSERT((info->pir_flags & PIRF_CPUSYNC) == 0);
    crit_exit_id("inval");
}

/*
 * Synchronize a kvm mapping originally made for the private use on
 * some other cpu so it can be used on all cpus.
 *
 * XXX add MADV_RESYNC to improve performance.
 *
 * We don't need to do anything because our pmap_inval_pte_quick()
 * synchronizes it immediately.
 */
void
pmap_kenter_sync(vm_offset_t va __unused)
{
}

void
cpu_invlpg(void *addr)
{
	if (vmm_enabled)
		vmm_cpu_invltlb(); /* For VMM mode forces vmmexit/resume */
	else
		madvise(addr, PAGE_SIZE, MADV_INVAL);
}

void
cpu_invltlb(void)
{
	if (vmm_enabled)
		vmm_cpu_invltlb(); /* For VMM mode forces vmmexit/resume */
	else
		madvise((void *)KvaStart, KvaEnd - KvaStart, MADV_INVAL);
}

void
smp_invltlb(void)
{
	/* XXX must invalidate the tlb on all cpus */
	/* at the moment pmap_inval_pte_quick */
	/* do nothing */
}
