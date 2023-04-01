struc PerCPU
  .current_process: resq 1
  .tss: resq 1
  .user_rsp: resq 1
  .idle_stack: resq 1
  .interrupt_disable: resq 1
  .preempt_disable: resq 1
  .preempt_delayed: resq 1
endstruc
