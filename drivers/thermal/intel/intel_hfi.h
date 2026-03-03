/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _INTEL_HFI_H
#define _INTEL_HFI_H
#define HFI_ITD_CLASS_DEFAULT   0  
#define HFI_ITD_CLASS_INT       1   //compute intensivo
#define HFI_ITD_CLASS_VEC       2   //vetorial/SIMD         
#define HFI_ITD_CLASS_LIGHT     3   // background/leve       

#define NR_HFI_ITD_CLASSES      4  

#if defined(CONFIG_INTEL_HFI_THERMAL)
void __init intel_hfi_init(void);
void intel_hfi_online(unsigned int cpu);
void intel_hfi_offline(unsigned int cpu);
void intel_hfi_process_event(__u64 pkg_therm_status_msr_val);
#else
static inline void intel_hfi_init(void) { }
static inline void intel_hfi_online(unsigned int cpu) { }
static inline void intel_hfi_offline(unsigned int cpu) { }
static inline void intel_hfi_process_event(__u64 pkg_therm_status_msr_val) { }
#endif /* CONFIG_INTEL_HFI_THERMAL */

#endif /* _INTEL_HFI_H */
