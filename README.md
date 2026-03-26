# Linux 7.0.0-rc1 — IPC Classes (Intel Thread Director)

Porte dos patches de Ricardo Neri com suporte a **IPC Classes** (classificação de threads por tipo de carga via Intel Thread Director - ITD) para o kernel 7.0.0-rc1.

Esta feature não tem entrada no Kconfig — precisa ser ativada manualmente no `.config`.

---

## Requisitos de hardware

- Intel Alder Lake (12ª gen) ou Raptor Lake (13ª gen) ou mais novo
- Suporte a Intel HFI (Hardware Feedback Interface) no firmware

---

## Configs necessárias

Edite o `.config` (ou use `scripts/config`) antes de compilar:

```
CONFIG_IPC_CLASSES=y          # NÃO tem entrada no menuconfig — adicionar manualmente
CONFIG_INTEL_HFI_THERMAL=y    # necessário para leitura dos MSRs ITD
CONFIG_X86_INTEL_PSTATE=y     # necessário para ITMT e governor de frequência
```

> **ITMT** (Intel Turbo Boost Max 3.0) é ativado automaticamente via bypass no
> `intel_hfi.c` usando os valores de `perf_cap` do HFI, mesmo quando o firmware
> não ativa via CPPC (caso comum em laptops).

---

## Aviso: `make olddefconfig` remove `CONFIG_IPC_CLASSES`

Como não há entrada no Kconfig, o `olddefconfig` remove silenciosamente a config:

```bash
make olddefconfig   # remove CONFIG_IPC_CLASSES=y sem aviso
```

Sempre verifique depois:

```bash
grep CONFIG_IPC_CLASSES .config
# Se não aparecer, adicione:
scripts/config --enable CONFIG_IPC_CLASSES
```

---

## Como verificar se está ativo após bootar

**1. Verificar suporte ITD no CPU:**
```bash
grep itd /proc/cpuinfo
```

**2. Ver classificação de uma tarefa em tempo real:**
```bash
taskset -c 0 stress-ng --matrix 1 --timeout 30 &
PID=$!
sleep 5
cat /proc/$PID/sched | grep ipcc
kill $PID
```

Saída esperada (P-core com carga):
```
ipcc           : 1   # classificado (1=scalar, 2=vector, 3=VNNI, 4=spin-wait)
ipcc_candidate : 1
ipcc_count     : 4   # 4 leituras consecutivas iguais (ITD_CLASS_STABILITY_TICKS)
```

**3. E-cores NUNCA classificam** — sempre retornam `ipcc=0`. É limitação de hardware.

**4. Verificar ITMT:**
```bash
cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_driver   # deve mostrar intel_pstate
```

---

## Mapeamento de classes

| ipcc | Significado | classid hardware |
|------|-------------|-----------------|
| 0 | unclassified (não classificado ou E-core) | — |
| 1 | scalar (uso geral, inteiros) | 0 |
| 2 | vector (SIMD/AVX) | 1 |
| 3 | VNNI (inferência de IA, AVX-512 VNNI) | 2 |
| 4 | spin-wait (locks, busy-wait) | 3 |

> No Raptor Lake na prática só aparecem `ipcc=1` (scalar) e eventualmente `ipcc=2`. E-cores e tasks background ficam em `ipcc=0`.

---

## Arquivos modificados vs mainline

| Arquivo | O que foi alterado |
|---------|-------------------|
| `drivers/thermal/intel/intel_hfi.c` | classificação ITD, bypass ITMT via HFI |
| `drivers/thermal/intel/intel_hfi.h` | defines de classes e MSRs |
| `kernel/sched/fair.c` | tiebreaker IPCC no load balancer |
| `kernel/sched/core.c` | `sched_tick` chama `arch_update_ipcc()` |
| `kernel/sched/sched.h` | `nr_ipcc[]` por runqueue |
| `kernel/sched/debug.c` | exporta `ipcc` em `/proc/<pid>/sched` |
| `include/linux/sched.h` | campos `ipcc` na `task_struct` |
