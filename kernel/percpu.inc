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
  .preempt_delayed: resb 1
  .idle: resb 1
    resb 2
  .next_cpu: resq 1
endstruc
