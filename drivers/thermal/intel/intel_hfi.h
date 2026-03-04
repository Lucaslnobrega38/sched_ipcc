/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _INTEL_HFI_H
#define _INTEL_HFI_H

#include <linux/sched.h>

#define HFI_ITD_CLASS_DEFAULT   0
#define HFI_ITD_CLASS_INT       1   /* compute intensivo */
#define HFI_ITD_CLASS_VEC       2   /* vetorial/SIMD */
#define HFI_ITD_CLASS_LIGHT     3   /* background/leve */

#define NR_HFI_ITD_CLASSES      4

/* MSR do ITD Thread Director — não está nos headers upstream */
#define MSR_IA32_HW_FEEDBACK_THREAD_CONFIG  0x17D4

/* Máscara para extrair a classe ITD (bits 7:0) */
#define ITD_CLASS_MASK  0xFF

#if defined(CONFIG_INTEL_HFI_THERMAL)
void __init intel_hfi_init(void);
void intel_hfi_online(unsigned int cpu);
void intel_hfi_offline(unsigned int cpu);
void intel_hfi_process_event(__u64 pkg_therm_status_msr_val);
void intel_hfi_update_ipcc(struct task_struct *curr);
int  intel_hfi_get_ipcc_score(unsigned short ipcc, int cpu);

    #ifdef CONFIG_IPC_CLASSES
    bool arch_has_ipcc_classes(void);
    void arch_update_ipcc(struct task_struct *p);
    int  arch_get_ipcc_score(unsigned short ipcc, int cpu);
    #endif

#else
static inline void intel_hfi_init(void) { }
static inline void intel_hfi_online(unsigned int cpu) { }
static inline void intel_hfi_offline(unsigned int cpu) { }
static inline void intel_hfi_process_event(__u64 pkg_therm_status_msr_val) { }
static inline void intel_hfi_update_ipcc(struct task_struct *curr) { }
static inline int  intel_hfi_get_ipcc_score(unsigned short ipcc, int cpu) { return 0; }
#endif /* CONFIG_INTEL_HFI_THERMAL */

#endif /* _INTEL_HFI_H */