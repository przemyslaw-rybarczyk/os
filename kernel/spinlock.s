global spinlock_acquire
global spinlock_release

; A spinlock has only two valid values - 0 (free) and 1 (used)

; Acquire a spinlock
; RDI holds the lock address.
spinlock_acquire:
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
; RDI holds the lock address.
spinlock_release:
  xrelease mov dword [rdi], 0
  ret
