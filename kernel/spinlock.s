%include "kernel/percpu.inc"

global spinlock_acquire
global spinlock_release
global semaphore_decrement
global semaphore_increment

; A spinlock has only two valid values - 0 (free) and 1 (used)

; Acquire a spinlock
spinlock_acquire:
  mov edx, 1
.try_lock:
  add qword gs:[PerCPU.preempt_disable], 1
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
  xrelease mov dword [rdi], 0
  sub qword gs:[PerCPU.preempt_disable], 1
  ret

; Decrement a semaphore
semaphore_decrement:
.check:
  ; Check if the semaphore is zero and jump to the wait loop if it is
  mov rax, [rdi]
  test rax, rax
  jz .wait
.try_decrement:
  ; At this point, RAX contains the expected value of the semaphore
  ; Try to atomically decrement the semaphore with a LOCK CMPXCHG instruction and return if the operation succeeds
  lea rdx, [rax - 1]
  lock cmpxchg [rdi], rdx
  jne .check
  ret
.wait:
  add qword gs:[PerCPU.preempt_disable], 1
  sti
  ; If the semaphore is zero, wait until it is incremented
  cmp qword [rdi], 0
  pause
  je .wait
  ; After the semaphore becomes nonzero, try to decrement it again
  cli
  sub qword gs:[PerCPU.preempt_disable], 1
  jmp .check

; Increment a semaphore
semaphore_increment:
  lock add qword [rdi], 1
  ret
