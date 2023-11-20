%include "kernel/percpu.inc"

global preempt_disable
global preempt_enable
global spinlock_acquire
global spinlock_release

extern delayed_timer_interrupt_handle
extern interrupt_disable
extern send_input_events
extern send_input_delayed

preempt_disable:
  add qword gs:[PerCPU.preempt_disable], 1
  ret

preempt_enable:
  ; Check if preemption is only disabled once and interrupts are enabled
  cmp qword gs:[PerCPU.preempt_disable], 1
  jne .no_preempt
  cmp qword gs:[PerCPU.interrupt_disable], 0
  jne .no_preempt
  ; If there are pending messages in the input buffer, send them
  cmp byte [send_input_delayed], 0
  jz .no_input
  mov byte [send_input_delayed], 0
  call send_input_events
.no_input:
  ; If there's a pending preemption, preempt the current thread
  cmp byte gs:[PerCPU.timer_interrupt_delayed], 0
  je .no_preempt
  sub qword gs:[PerCPU.preempt_disable], 1
  mov byte gs:[PerCPU.timer_interrupt_delayed], 0
  call delayed_timer_interrupt_handle
  ret
  ; Otherwise just decrement the preempt disable counter
.no_preempt:
  sub qword gs:[PerCPU.preempt_disable], 1
  ret

; A spinlock has only two valid values - 0 (free) and 1 (used)

; Acquire a spinlock
spinlock_acquire:
  ; Disable preemption
  call preempt_disable
  mov edx, 1
.try_lock:
  ; Try to atomically acquire the lock with a LOCK CMPXCHG and return if the operation succeeds
  xor eax, eax
  xacquire lock cmpxchg [rdi], edx
  jne .wait
  ret
.wait:
  ; If the lock is not free, read from it until it is
  ; This is done instead of retrying the acquisition to avoid performace issues caused by locking memory repeatedly.
  cmp dword [rdi], 0
  pause
  jne .wait
  ; After the lock becomes free, try to acquire it again
  jmp .try_lock

; Release a spinlock
spinlock_release:
  ; Mark the lock as free
  xrelease mov dword [rdi], 0
  jmp preempt_enable
