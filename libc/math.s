global __frexpl
global scalblnl
global logbl
global __ilogbl
global rintl
global lrintf
global lrint
global lrintl
global llrintf
global llrint
global llrintl
global roundl
global lroundf
global lround
global lroundl
global llroundf
global llround
global llroundl
global truncl
global floorl
global ceill
global __exp2l
global expl
global __expm1l
global log2l
global logl
global log10l
global log1pl
global __powl
global sqrtl
global atan2l

extern exp2l

FPCW_RC equ 0x0C00
FPCW_RC_TONEAREST equ 0x0000
FPCW_RC_DOWNWARD equ 0x0400
FPCW_RC_UPWARD equ 0x0800
FPCW_RC_TOWARDZERO equ 0x0C00

MXCSR_RC equ 0x6000

; Does not handle zero/inf/nan correctly
__frexpl:
  ; Load number
  fld tword [rsp + 8]
  ; Split into exponent in st0 and mantissa in st1
  fxtract
  fxch
  ; Store and normalize exponent
  fistp dword [rdi]
  add dword [rdi], 1
  ; Normalize mantissa
  push qword 2
  fidiv dword [rsp]
  add rsp, 8
  ret

scalblnl:
  ; Load arguments
  push qword rdi
  fild qword [rsp]
  add rsp, 8
  fld tword [rsp + 8]
  ; Scale
  fscale
  fstp st1
  ret

logbl:
  ; Load number
  fld tword [rsp + 8]
  ; Get exponent
  fxtract
  fstp st0
  ret

; Does not handle zero/inf/nan correctly
__ilogbl:
  ; Load number
  fld tword [rsp + 8]
  ; Get exponent
  fxtract
  fstp st0
  ; Store exponent
  fistp dword [rsp - 8]
  mov eax, [rsp - 8]
  ret

rintl:
  ; Load number
  fld tword [rsp + 8]
  ; Round
  frndint
  ret

lrintf:
llrintf:
  cvtss2si rax, xmm0
  ret

lrint:
llrint:
  cvtsd2si rax, xmm0
  ret

lrintl:
llrintl:
  ; Load number
  fld tword [rsp + 8]
  ; Round and store result
  fistp qword [rsp - 8]
  mov rax, [rsp - 8]
  ret

lroundf:
llroundf:
  ; Set rounding mode to nearest
  stmxcsr [rsp - 8]
  mov dx, [rsp - 8]
  and word [rsp - 8], ~MXCSR_RC
  ldmxcsr [rsp - 8]
  ; Round
  cvtss2si rax, xmm0
  ; Restore rounding mode
  mov [rsp - 8], dx
  ldmxcsr [rsp - 8]
  ret

lround:
llround:
  ; Set rounding mode to nearest
  stmxcsr [rsp - 8]
  mov dx, [rsp - 8]
  and word [rsp - 8], ~MXCSR_RC
  ldmxcsr [rsp - 8]
  ; Round
  cvtsd2si rax, xmm0
  ; Restore rounding mode
  mov [rsp - 8], dx
  ldmxcsr [rsp - 8]
  ret

lroundl:
llroundl:
  ; Load number
  fld tword [rsp + 8]
  ; Set rounding mode to nearest
  fnstcw [rsp - 8]
  mov dx, [rsp - 8]
  and word [rsp - 8], ~FPCW_RC
  fldcw [rsp - 8]
  ; Round and store result
  fistp qword [rsp - 8]
  mov rax, [rsp - 8]
  ; Restore rounding mode
  mov [rsp - 8], dx
  fldcw [rsp - 8]
  ret

; long double rintl_mode(long double f, uint16_t rounding_mode)
; Round a long double with the given rounding mode
rintl_mode:
  ; Load number
  fld tword [rsp + 8]
  ; Set rounding mode to nearest
  fnstcw [rsp - 8]
  mov dx, [rsp - 8]
  and word [rsp - 8], ~FPCW_RC
  or word [rsp - 8], di
  fldcw [rsp - 8]
  ; Round
  frndint
  ; Restore rounding mode
  mov [rsp - 8], dx
  fldcw [rsp - 8]
  ret

roundl:
  mov di, FPCW_RC_TONEAREST
  jmp rintl_mode

truncl:
  mov di, FPCW_RC_TOWARDZERO
  jmp rintl_mode

floorl:
  mov di, FPCW_RC_DOWNWARD
  jmp rintl_mode

ceill:
  mov di, FPCW_RC_UPWARD
  jmp rintl_mode

; Does not handle infinity correctly
__exp2l:
  ; Load number
  fld tword [rsp + 8]
.loaded:
  ; Split into integral part in st1 and fractional part in st0
  fld st0
  frndint
  fsub st1, st0
  fxch
  ; Use F2XM1 to calculate 2^(fractional part)
  f2xm1
  fld1
  fadd st1, st0
  fstp st0
  ; Use FSCALE to multiply by 2^(integral part)
  fscale
  fstp st1
  ret

expl:
  ; Compute as 2 ^ (log_2(e) * x)
  fld tword [rsp + 8]
  fldl2e
  fmulp
  fstp tword [rsp + 8]
  jmp exp2l

; Does not handle infinity correctly
__expm1l:
  xchg bx, bx
  ; Load number
  fld tword [rsp + 8]
  ; Multiply by log_2(e)
  fldl2e
  fmulp
  ; Compare absolute value of product to one
  fld1
  fld st1
  fabs
  fcomip st0, st1
  fstp st0
  ja .large
  ; If the product is small, use F2XM1 to calculate e^x - 1
  f2xm1
  fstp st1
  ret
.large:
  ; If the product is too large for F2XM1, do normal exponentiation and subtract one
  call __exp2l.loaded
  fld1
  fsubp
  ret

log2l:
  fld1
  fld tword [rsp + 8]
  fyl2x
  ret

logl:
  fldln2
  fld tword [rsp + 8]
  fyl2x
  ret

log10l:
  fldlg2
  fld tword [rsp + 8]
  fyl2x
  ret

section .rodata

fyl2xp1_bound: dt 0x9.5f619980c4336f6p-5

section .text

log1pl:
  ; Load arguments to FYL2X / FYL2XP1
  fldln2
  fld tword [rsp + 8]
  ; Compare against 1 - sqrt(1/2)
  fld tword [fyl2xp1_bound]
  fld st1
  fabs
  fcomip st0, st1
  fstp st0
  ja .large
  ; If the number is small, use FYL2XP1 to calculate log_e(x)
  fyl2xp1
  ret
.large:
  ; If the number is too large for FYL2XP1, add one and use FYL2X
  fld1
  faddp
  fyl2x
  ret

; Only works correctly if both numbers are finite and x is positive
__powl:
  ; Compute as 2 ^ (log_2(x) * y)
  fld tword [rsp + 24]
  fld tword [rsp + 8]
  fyl2x
  fstp tword [rsp + 8]
  jmp __exp2l

sqrtl:
  fld tword [rsp + 8]
  fsqrt
  ret

atan2l:
  fld tword [rsp + 8]
  fld tword [rsp + 24]
  fpatan
  ret
