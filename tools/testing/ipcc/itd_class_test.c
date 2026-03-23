// SPDX-License-Identifier: GPL-2.0
/*
 * itd_class_test.c - Test ITD classification on P-cores vs E-cores
 *
 * Runs a scalar or vector workload and prints the ipcc classification
 * from /proc/self/sched every second.
 *
 * Usage: ./itd_class_test <scalar|vector> <seconds>
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sched.h>
#include <unistd.h>
#include <signal.h>
#include <immintrin.h>
#include <pthread.h>

static volatile int running = 1;

static void sighandler(int sig)
{
	(void)sig;
	running = 0;
}

/* Pure integer scalar work — no vectorization */
static void __attribute__((noinline, optimize("O2,no-tree-vectorize")))
scalar_work(void)
{
	volatile unsigned long a = 1, b = 2, c = 3;

	while (running) {
		for (int i = 0; i < 10000000; i++) {
			a = a * 31 + b;
			b = b ^ (c << 3);
			c = c + a - b;
		}
	}
}

/* AVX2 vector work */
static void __attribute__((noinline))
vector_work(void)
{
	__m256i a = _mm256_set1_epi32(1);
	__m256i b = _mm256_set1_epi32(2);
	__m256i c = _mm256_set1_epi32(3);

	while (running) {
		for (int i = 0; i < 10000000 && running; i++) {
			a = _mm256_add_epi32(a, b);
			b = _mm256_mullo_epi32(b, c);
			c = _mm256_xor_si256(c, a);
		}
	}

	/* Prevent dead-code elimination */
	volatile int sink = _mm256_extract_epi32(a, 0);
	(void)sink;
}

static void read_ipcc(int cpu)
{
	FILE *f;
	char line[256];
	int ipcc = -1, candidate = -1, cooldown = -1;

	f = fopen("/proc/self/sched", "r");
	if (!f) {
		perror("fopen /proc/self/sched");
		return;
	}

	while (fgets(line, sizeof(line), f)) {
		if (strstr(line, "ipcc "))
			sscanf(line, "%*s %d", &ipcc);
		else if (strstr(line, "ipcc_candidate"))
			sscanf(line, "%*s %d", &candidate);
		else if (strstr(line, "ipcc_cooldown"))
			sscanf(line, "%*s %d", &cooldown);
	}
	fclose(f);

	const char *class_names[] = {
		"unclassified", "scalar", "vector", "vnni", "spin-wait"
	};
	const char *name = (ipcc >= 0 && ipcc <= 4) ? class_names[ipcc] : "?";

	printf("  cpu=%2d  ipcc=%d (%s)  candidate=%d  cooldown=%d\n",
	       cpu, ipcc, name, candidate, cooldown);
}

static void *monitor_thread(void *arg)
{
	int cpu = *(int *)arg;
	int seconds = *((int *)arg + 1);

	for (int i = 0; i < seconds && running; i++) {
		sleep(1);
		read_ipcc(cpu);
	}
	running = 0;
	return NULL;
}

int main(int argc, char **argv)
{
	if (argc < 3) {
		fprintf(stderr, "Usage: %s <scalar|vector> <seconds>\n", argv[0]);
		return 1;
	}

	int is_vector = (strcmp(argv[1], "vector") == 0);
	int seconds = atoi(argv[2]);
	int cpu = sched_getcpu();

	signal(SIGINT, sighandler);
	signal(SIGALRM, sighandler);

	printf("Workload: %s | pinned to CPU %d | %d seconds\n",
	       is_vector ? "VECTOR (AVX2)" : "SCALAR (integer)",
	       cpu, seconds);

	int args[2] = { cpu, seconds };
	pthread_t mon;
	pthread_create(&mon, NULL, monitor_thread, args);

	if (is_vector)
		vector_work();
	else
		scalar_work();

	pthread_join(mon, NULL);
	printf("Done.\n");
	return 0;
}
