#pragma once

#include "types.h"

void apic_init(bool bsp);
void smp_init(void);
void smp_init_sync(void);
void apic_eoi(void);
void send_wakeup_ipi(u32);
void send_halt_ipi(void);

extern u8 cpus[];
extern size_t cpu_num;
extern void *lapic;
