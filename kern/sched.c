#include <inc/assert.h>
#include <inc/x86.h>
#include <kern/spinlock.h>
#include <kern/env.h>
#include <kern/pmap.h>
#include <kern/monitor.h>

void sched_halt(void);
int process_runs[NENV];
int scheduler_runs;

// Choose a user environment to run and run it.
unsigned int
read_cpu_timestamp()
{
	unsigned int low, high;
	// Ensamblador en línea para leer el registro de contador de tiempo de CPU
	__asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
	return low;
}
unsigned int
get_random(unsigned int total)
{
	// Initialize seed using CPU timestamp
	static unsigned long seed = 0;
	if (seed == 0) {
		seed = read_cpu_timestamp();
	}

	// Constants for the LCG
	const unsigned long a = 1103515245;
	const unsigned long c = 12345;
	const unsigned long m = 1UL << 31;

	// Update the seed and generate the next random number
	seed = (a * seed + c) % m;
	return (unsigned int) (seed) % (total);
}
void
sched_yield(void)
{
	scheduler_runs++;
#ifdef SCHED_ROUND_ROBIN
	// Implement simple round-robin scheduling.
	//
	// Search through 'envs' for an ENV_RUNNABLE environment in
	// circular fashion starting just after the env this CPU was
	// last running. Switch to the first such environment found.
	//
	// If no envs are runnable, but the environment previously
	// running on this CPU is still ENV_RUNNING, it's okay to
	// choose that environment.
	//
	// Never choose an environment that's currently running on
	// another CPU (env_status == ENV_RUNNING). If there are
	// no runnable environments, simply drop through to the code
	// below to halt the cpu.

	int starting_idx = 0;
	if (curenv) {
		starting_idx = ENVX(curenv->env_id) + 1;
	}

	for (int i = 0; i < NENV; i++) {
		int idx = (starting_idx + i);
		struct Env *env = &envs[idx];
		if (env->env_status == ENV_RUNNABLE) {
			process_runs[idx]++;
			env_run(env);
		}
	}
	for (int j = 0; j < starting_idx; j++) {
		struct Env *env = &envs[j];
		if (env->env_status == ENV_RUNNABLE) {
			process_runs[j]++;
			env_run(env);
		}
	}
	if (curenv && curenv->env_status == ENV_RUNNING) {
		process_runs[ENVX(curenv->env_id)]++;
		env_run(curenv);
	}

#endif

#ifdef SCHED_PRIORITIES
	// Implement simple priorities scheduling.
	//
	// Environments now have a "priority" so it must be consider
	// when the selection is performed.
	//
	// Be careful to not fall in "starvation" such that only one
	// environment is selected and run every time.

	// Your code here - Priorities
	uint32_t all_tickets = 0;
	for (int i = 0; i < NENV; i++) {
		if (envs[i].env_status == ENV_RUNNABLE) {
			all_tickets += envs[i].tickets;
		}
	}

	struct Env *participantes[NENV];
	unsigned int pos_cand = 0;

	if (!all_tickets) {
		if (curenv && curenv->env_status == ENV_RUNNING) {
			if (curenv->tickets) {
				curenv->tickets--;
				process_runs[ENVX(curenv->env_id)]++;
				env_run(curenv);
			} else {
				sched_halt();
			}
		}
	}

	// Obtenemos el proceso a correr.
	int contador = 0;
	unsigned int pos_ganador = get_random(all_tickets + 1);

	for (int i = 0; i < NENV; i++) {
		contador += envs[i].tickets;
		if (envs[i].env_status != ENV_RUNNABLE) {
			continue;
		}

		if (contador > pos_ganador) {
			struct Env *candidato = &envs[i];
			participantes[pos_cand] = candidato;
			pos_cand++;
		}
	}


	if (pos_cand > 0) {
		pos_ganador = get_random(pos_cand);
		struct Env *ganador = participantes[pos_ganador];
		if (ganador->tickets > 0) {
			ganador->tickets--;
		}
		process_runs[pos_ganador]++;
		env_run(ganador);
	}

#endif

	// sched_halt never returns
	sched_halt();
}

// Halt this CPU when there is nothing to do. Wait until the
// timer interrupt wakes it up. This function never returns.
//
void
sched_halt(void)
{
	int i;

	// For debugging and testing purposes, if there are no runnable
	// environments in the system, then drop into the kernel monitor.
	for (i = 0; i < NENV; i++) {
		if ((envs[i].env_status == ENV_RUNNABLE ||
		     envs[i].env_status == ENV_RUNNING ||
		     envs[i].env_status == ENV_DYING))
			break;
	}
	if (i == NENV) {
		cprintf("No runnable environments in the system!\n");
		cprintf("STATS: There are %d processes\n", NENV);
		for (int j = 0; j < NENV; j++) {
			cprintf("Process %d has ran %d times\n",
			        j,
			        process_runs[j]);
		}
		cprintf("Scheduler has been called %d times\n", scheduler_runs);
		while (1)
			monitor(NULL);
	}

	// Mark that no environment is running on this CPU
	curenv = NULL;
	lcr3(PADDR(kern_pgdir));

	// Mark that this CPU is in the HALT state, so that when
	// timer interupts come in, we know we should re-acquire the
	// big kernel lock
	xchg(&thiscpu->cpu_status, CPU_HALTED);

	// Release the big kernel lock as if we were "leaving" the kernel
	unlock_kernel();

	// Once the scheduler has finishied it's work, print statistics on
	// performance. Your code here

	// Reset stack pointer, enable interrupts and then halt.
	asm volatile("movl $0, %%ebp\n"
	             "movl %0, %%esp\n"
	             "pushl $0\n"
	             "pushl $0\n"
	             "sti\n"
	             "1:\n"
	             "hlt\n"
	             "jmp 1b\n"
	             :
	             : "a"(thiscpu->cpu_ts.ts_esp0));
}
