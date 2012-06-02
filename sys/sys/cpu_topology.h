#ifndef _CPU_TOPOLOGY_H_
#define _CPU_TOPOLOGY_H_

#ifdef _KERNEL

/* CPU TOPOLOGY DATA AND FUNCTIONS */
struct cpu_node {
	struct cpu_node * parent_node;
	struct cpu_node * child_node;
	uint32_t child_no;
	cpumask_t members;
	uint8_t type;
};
typedef struct cpu_node cpu_node_t;

cpu_node_t * build_cpu_topology	(void);
cpumask_t get_cpumask_from_level(cpu_node_t * root,
			int cpuid,
			uint8_t level_type);
void init_cpu_topology(void);

#define LEVEL_NO 4

/* Level type for CPU siblings */
#define	PACKAGE_LEVEL	1
#define	CHIP_LEVEL	2
#define	CORE_LEVEL	3
#define	THREAD_LEVEL	4

#define	CPU_ISSET(n, p)	((CPUMASK(n) & p) != 0)

#define CPUSET_FOREACH(cpu, mask)			\
	for ((cpu) = 0; (cpu) < ncpus; (cpu)++)	\
		if (CPU_ISSET(cpu, mask))


#endif /* _KERNEL */
#endif /* _CPU_TOPOLOGY_H_ */
