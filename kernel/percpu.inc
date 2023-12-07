struc PerCPU
  .self: resq 1
  .current_process: resq 1
  .tss: resq 1
  .user_rsp: resq 1
  .idle_stack: resq 1
  .interrupt_disable: resq 1
  .preempt_disable: resq 1
  .timeslice_start: resq 1
  .lapic_id: resd 1
  .timer_interrupt_delayed: resb 1
  .idle: resb 1
  .timeslice_interrupt_enabled: resb 1
  .waiting_for_timeout: resb 1
  .timeslice_timeout: resq 1
  .waiting_process_lock: resd 1
    resd 1
  .waiting_process: resq 1
  .tsc_deadline: resq 1
  .tsc_offset: resq 1
  .next_cpu: resq 1
endstruc
