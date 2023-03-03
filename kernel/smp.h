#pragma once

#include "types.h"

void apic_init(void);
void smp_init(void);
void smp_init_sync_1(void);
void smp_init_sync_2(void);
void apic_eoi(void);

extern u8 cpus[];
extern size_t cpu_num;
extern void *lapic;
