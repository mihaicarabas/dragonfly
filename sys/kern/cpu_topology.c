/*
 * Copyright (c) 2012 The DragonFly Project.  All rights reserved.
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
 */

#include "opt_cpu.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/malloc.h>
#include <sys/memrange.h>
#include <sys/cons.h>	/* cngetc() */
#include <sys/machintr.h>

#include <sys/mplock2.h>

#include <sys/lock.h>
#include <sys/user.h>
#ifdef GPROF 
#include <sys/gmon.h>
#endif

#include <machine/smp.h>
#include <machine_base/apic/apicreg.h>
#include <machine/atomic.h>
#include <machine/cpufunc.h>
#include <machine/cputypes.h>
#include <machine_base/apic/lapic.h>
#include <machine_base/apic/ioapic.h>
#include <machine/psl.h>
#include <machine/segments.h>
#include <machine/tss.h>
#include <machine/specialreg.h>
#include <machine/globaldata.h>
#include <machine/pmap_inval.h>

#include <sys/sbuf.h>
#include <sys/cpu_topology.h>

#define INDENT_BUF_SIZE LEVEL_NO*3

static cpu_node_t cpu_topology_nodes[MAXCPU];
static cpu_node_t *cpu_root_node;

static int print_cpu_topology_tree_sysctl(SYSCTL_HANDLER_ARGS);

/************************************/
/* CPU TOPOLOGY BUILDING  FUNCTIONS */
/************************************/


/* Generic topology tree.
 * @param children_no_per_level : the number of children on each level
 * @param level_types : the type of the level (THREAD, CORE, CHIP, etc)
 * @param cur_level : the current level of the tree
 * @param node : the current node
 * @param last_free_node : the last free node in the global array.
 * @param cpuid : basicly this are the ids of the leafs
 */ 
static void
build_topology_tree(int *children_no_per_level,
		uint8_t *level_types,
		int cur_level,
		cpu_node_t *node,
		cpu_node_t **last_free_node,
		int *cpuid)
{
	int i;

	node->child_no = children_no_per_level[cur_level];
	node->type = level_types[cur_level];
	node->members = 0;

	if (node->child_no == 0) {
		node->child_node = NULL;
		node->members = CPUMASK(*cpuid);
		(*cpuid)++;
		return;
	}

	node->child_node = *last_free_node;
	(*last_free_node) += node->child_no;
	
	for (i = 0; i< node->child_no; i++) {

		node->child_node[i].parent_node = node;

		build_topology_tree(children_no_per_level,
				level_types,
				cur_level + 1,
				&(node->child_node[i]),
				last_free_node,
				cpuid);

		node->members |= node->child_node[i].members;
	}
}

/* Build CPU topology. The detection is made by comparing the
 * chip,core and logical IDs of each CPU with the IDs of the 
 * BSP. When we found a match, at that level the CPUs are siblings.
 */
cpu_node_t *
build_cpu_topology(void)
{
	detect_cpu_topology();
	int i;
	int BSPID = 0;
	int threads_per_core = 0;
	int cores_per_chip = 0;
	int chips_per_package = 0;
	int children_no_per_level[LEVEL_NO];
	uint8_t level_types[LEVEL_NO];
	int cpuid = 0;

	cpu_node_t *root = &cpu_topology_nodes[0];
	cpu_node_t *last_free_node = root + 1;

	/* Assume that the topology is uniform.
	 * Find the number of siblings within chip
	 * and witin core to build up the topology
	 */
	for (i = 0; i < ncpus; i++) {
		
		if (get_chip_ID(BSPID) == get_chip_ID(i)) {
			cores_per_chip++;
		} else {
			continue;
		}

		if (get_core_number_within_chip(BSPID)
			== get_core_number_within_chip(i)) {
			threads_per_core++;
		}
		
	}
	cores_per_chip /= threads_per_core;
	chips_per_package = ncpus / (cores_per_chip * threads_per_core);

	/* Init topo info.
	 * For now we assume that we have a four level topology
	 */
	children_no_per_level[0] = chips_per_package;
	children_no_per_level[1] = cores_per_chip;
	children_no_per_level[2] = threads_per_core;
	children_no_per_level[3] = 0;

	level_types[0] = PACKAGE_LEVEL;
	level_types[1] = CHIP_LEVEL;
	level_types[2] = CORE_LEVEL;
	level_types[3] = THREAD_LEVEL;

	build_topology_tree(children_no_per_level,
				level_types,
				0,
				root,
				&last_free_node,
				&cpuid);
		
	return root;
}

/* Find a cpu_node_t by a mask */
static cpu_node_t *
get_cpu_node_by_cpumask(cpu_node_t * node,
			cpumask_t mask) {

	cpu_node_t * found = NULL;
	int i;

	if (node->members == mask) {
		return node;
	}

	for (i = 0; i < node->child_no; i++) {
		found = get_cpu_node_by_cpumask(&(node->child_node[i]), mask);
		if (found != NULL) {
			return found;
		}
	}
	return NULL;
}

/* Get the mask of siblings for level_type of a cpuid */
cpumask_t
get_cpumask_from_level(cpu_node_t * root,
			int cpuid,
			uint8_t level_type)
{
	cpu_node_t * node;
	cpumask_t mask = CPUMASK(cpuid);
	node = get_cpu_node_by_cpumask(root, mask);
	if (node == NULL) {
		return 0;
	}
	while (node != NULL) {
		if (node->type == level_type) {
			return node->members;
		}
		node = node->parent_node;
	}
	return 0;
}
static struct sysctl_ctx_list cpu_topology_sysctl_ctx;
static struct sysctl_oid *cpu_topology_sysctl_tree;

void
init_cpu_topology(void)
{
	cpu_root_node = build_cpu_topology();

	sysctl_ctx_init(&cpu_topology_sysctl_ctx); 

	cpu_topology_sysctl_tree = SYSCTL_ADD_NODE(&cpu_topology_sysctl_ctx,
					SYSCTL_STATIC_CHILDREN(_hw),
					OID_AUTO,
					"cpu_topology",
					CTLFLAG_RD, 0, "");

	SYSCTL_ADD_PROC(&cpu_topology_sysctl_ctx, SYSCTL_CHILDREN(cpu_topology_sysctl_tree),
			OID_AUTO, "tree", CTLTYPE_STRING | CTLFLAG_RD,
			NULL, 0, print_cpu_topology_tree_sysctl, "A", "Tree print of CPU topology");

}

static void
print_cpu_topology_tree_sysctl_helper(cpu_node_t *node, struct sbuf *sb, char * buf, int buf_len, int last)
{
	int i;
	int bsr_member;

	sbuf_bcat(sb, buf, buf_len);
	if (last) {
		sbuf_printf(sb, "\\-");
		buf[buf_len] = ' ';buf_len++;
		buf[buf_len] = ' ';buf_len++;
	} else {
		sbuf_printf(sb, "\\-");
		buf[buf_len] = '|';buf_len++;
		buf[buf_len] = ' ';buf_len++;
	}
	
	bsr_member = BSRCPUMASK(node->members);

	if (node->type == PACKAGE_LEVEL) {
		sbuf_printf(sb,"PACKAGE MEMBERS: ");
	} else if (node->type == CHIP_LEVEL) {
		sbuf_printf(sb,"CHIP ID %d: ",
			get_chip_ID(bsr_member));
	} else if (node->type == CORE_LEVEL) {
		sbuf_printf(sb,"CORE ID %d: ",
			get_core_number_within_chip(bsr_member));
	} else if (node->type == THREAD_LEVEL) {
		sbuf_printf(sb,"THREAD ID %d: ",
		get_logical_CPU_number_within_core(bsr_member));
	} else {
		sbuf_printf(sb,"UNKNOWN: ");
	}
	CPUSET_FOREACH(i, node->members) {
		sbuf_printf(sb,"cpu%d ", i);
	}	
	
	sbuf_printf(sb,"\n");

	for (i = 0; i < node->child_no; i++) {
		print_cpu_topology_tree_sysctl_helper(&(node->child_node[i]), sb, buf, buf_len, i == (node->child_no -1));
	}
}
static int
print_cpu_topology_tree_sysctl(SYSCTL_HANDLER_ARGS)
{
	struct sbuf *sb;
	int ret;
	char buf[INDENT_BUF_SIZE];

	KASSERT(cpu_root_node != NULL, ("cpu_root_node isn't initialized"));

	sb = sbuf_new(NULL, NULL, 500, SBUF_AUTOEXTEND);
	if (sb == NULL) {
		return (ENOMEM);
	}
	print_cpu_topology_tree_sysctl_helper(cpu_root_node, sb, buf, 0, 0);

	sbuf_finish(sb);

	ret = SYSCTL_OUT(req, sbuf_data(sb), sbuf_len(sb));

	sbuf_delete(sb);

	return ret;	
}
