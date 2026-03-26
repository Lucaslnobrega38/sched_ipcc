# Contexto: Kernel Linux 7.0.0-rc1 com IPC Classes (Intel Thread Director)

## Hardware
- Intel Raptor Lake i5-13400U (mobile)
- 2 P-cores (4 HT threads: CPUs 0-3), 8 E-cores (CPUs 4-11)
- SO: Arch Linux (EndeavourOS), systemd-boot, dracut

## Projeto
Porte dos patches de Ricardo Neri (IPC Classes / Intel Thread Director) para o kernel 7.0.0-rc1 mainline. O objetivo é usar classificação de hardware (ITD) para informar o scheduler CFS sobre o tipo de workload de cada task, melhorando placement em arquiteturas híbridas P-core/E-core.

## Limitações de hardware descobertas
1. **E-cores NUNCA classificam** — ITD retorna ipcc=0 (unclassified) sempre em E-cores
2. **SMT siblings interferem** — quando ambos HT de um P-core estão ocupados, classificação de classes 0-1 é considerada não-confiável pelo datasheet Intel
3. **Apenas 2 classes práticas** — stress-ng --matrix retorna scalar (ipcc=1); workloads mistos alternam; background raramente classificado
4. **ITMT não ativa via CPPC** neste laptop — bypass implementado via HFI perf_cap em `update_capabilities()`

## Arquitetura do código

### Arquivos modificados (vs kernel 7.0.0-rc1 vanilla)

**drivers/thermal/intel/intel_hfi.c** — arquivo central:
- `arch_update_ipcc()` (~linha 812): lê MSR_IA32_HW_FEEDBACK_CHAR a cada sched_tick, aplica:
  - Filtro SMT: rejeita classes 0-1 se sibling busy (configurável por variante)
  - Filtro de estabilidade: requer ITD_CLASS_STABILITY_TICKS=4 leituras consecutivas iguais
  - Cooldown adaptativo (variante ipcc_adaptive): após classificar, pula ticks exponencialmente (2→4→8, max 8)
  - Mapeia classid hardware 0-3 → ipcc 1-4 (ipcc=0 = unclassified)
- `arch_get_ipcc_score()` (~linha 858): retorna score por classe+CPU (perf_cap base, 1.5x vector, 2x VNNI)
- `intel_hfi_online()`: ativa ITD per-CPU via MSR_IA32_HW_FEEDBACK_THREAD_CONFIG
- `update_capabilities()`: bypass ITMT via HFI perf_cap quando intel_pstate não ativa

**drivers/thermal/intel/intel_hfi.h**:
- ITD class IDs: SCALAR=0, VECTOR=1, VNNI=2, SPIN=3
- NR_HFI_ITD_CLASSES=4, ITD_CLASS_STABILITY_TICKS=4
- MSR defines: 0x17D4 (thread config), 0x17D2 (feedback char)

**kernel/sched/fair.c**:
- `enqueue_task_fair/dequeue_task_fair`: mantém `rq->nr_ipcc[p->ipcc]` (contadores por runqueue)
- `update_sg_lb_ipcc_stats()` (~linha 10462): coleta bitmask de classes IPCC por sched_group
- `ipcc_mask_pcore_prio()` (~linha 10445): converte bitmask → prioridade P-core (3>2>1>0)
- `update_sd_pick_busiest()` (~linha 10659): tiebreaker IPCC no group_asym_packing
- `update_sg_lb_stats()` (~linha 10579): chama update_sg_lb_ipcc_stats quando SD_ASYM_PACKING

**kernel/sched/core.c**:
- `__sched_fork()`: inicializa ipcc=0, ipcc_candidate=0, ipcc_count=0, ipcc_cooldown=0
- `sched_tick()`: chama `arch_update_ipcc(rq->curr)` em user ticks quando sched_ipcc_enabled()

**kernel/sched/sched.h**:
- NR_IPC_CLASSES=5 (0=unclassified + classids 0-3 → ipcc 1-4)
- `rq->nr_ipcc[NR_IPC_CLASSES]` contadores por runqueue

**include/linux/sched.h**:
- task_struct: `ipcc`, `ipcc_candidate`, `ipcc_count`, `ipcc_cooldown` (unsigned short, sob CONFIG_IPC_CLASSES)

**kernel/sched/debug.c**:
- Exporta ipcc/ipcc_candidate/ipcc_count/ipcc_cooldown em /proc/<pid>/sched

## Variantes de kernel testadas (benchmarks com 30 runs cada)

| Variante | ITMT | IPC Classes | Filtro SMT | Cooldown | LOCALVERSION |
|----------|------|-------------|------------|----------|--------------|
| vanilla | off | off | — | — | (nenhum) |
| asym_packing | on | off | — | — | -itmt-only |
| ipcc | on | on | strict | nenhum | -ipcc |
| ipcc_unsecure | on | on | relaxado* | nenhum | -ipcc_unsecure |
| ipcc_adaptive | on | on | strict | exponencial (2→4→8) | -ipcc_adaptive |

*relaxado = aceita leitura não-confiável para tasks ainda não classificadas (ipcc==0)

## Resultados de benchmark (resumo)

### asym_packing vs vanilla:
- Compute placement: +8.9% (P-cores priorizados)
- Tail latency p99.9: -7.9% (melhora)
- Energia: +4.4% pior (P-cores consomem mais)
- Hackbench: +5.6% pior (contenção nos P-cores)

### ipcc vs asym_packing:
- Compute placement: -35.9% REGRESSÃO (filtro SMT impede classificação em P-cores busy)
- Mixed placement: +117.5% (tiebreaker funciona para workloads mistos)
- Energia: -1.1% melhor que vanilla (distribui tasks pelos E-cores)
- Context switches: -23.3%

### ipcc_unsecure vs vanilla:
- Compute placement: recuperado (filtro relaxado permite classificação)
- Migrações: reduzidas (tasks classificadas ficam estáveis)

### ipcc_adaptive vs vanilla:
- Compute placement: +0.2% (recuperado pelo cooldown)
- Tail latency p99.9: -12.0% (melhor de todos)
- Context switches: -64.5% (redução massiva)
- Energia: +5.3% pior (tasks ficam nos P-cores durante cooldown)
- Mixed placement: +9.7% (vs +117.5% do ipcc — perde reatividade)
- Rand throughput: +2.1% ns (vs +4.1% do ipcc — cooldown não reage a oscilações)

## Problema arquitetural identificado (questão aberta)

O tiebreaker IPCC atual é um **desempate no pull** de tasks entre grupos E-core. Mas E-cores nunca classificam (ipcc=0 sempre), então o tiebreaker nunca distingue nada entre grupos E-core.

A proposta mais promissora é **inverter a lógica**: quando P-core está overloaded, ele deve EMPURRAR a task de menor prioridade IPCC para E-cores, em vez de esperar que E-cores puxem. O mecanismo já existe no kernel:

- `detach_one_task()` (fair.c ~9594): seleciona UMA task para push via active balance — atualmente pega a primeira migrável. Modificar para preferir menor ipcc.
- `detach_tasks()` (fair.c ~9625): bulk migration — quando migration_type==migrate_task, poderia selecionar por menor ipcc primeiro.

## Padrões de referência no kernel para adaptive monitoring
- `balance_interval *= 2` em sched_balance_rq() — exponential backoff no load balancer
- `iowait_boost <<= 1` / `>>= 1` em cpufreq_schedutil — doubling/halving
- PELT: half-life fixo de 32ms
- util_est: skip update se dentro de 1% de margem
- Ricardo Neri upstream: SEM cooldown, lê MSR a cada tick incondicionalmente

## Configs necessárias (.config)
- CONFIG_IPC_CLASSES=y (não tem Kconfig entry — precisa adicionar manualmente, olddefconfig remove)
- CONFIG_SCHED_ITMT=y
- CONFIG_INTEL_HFI_THERMAL=y
- CONFIG_X86_INTEL_PSTATE=y

## Scripts de build
- `instalar.sh`: compila, instala módulos, copia bzImage, gera initrd com dracut, cria entry systemd-boot
- ESP: `/efi/<machine-id>/<version>/linux` e `/efi/<machine-id>/<version>/initrd`
- MACHINE_ID: 7c6b088b521f449bbab32c3b40d117eb
- Root UUID: 94a96fe0-4ff2-4d7a-8a06-de97b249506a
