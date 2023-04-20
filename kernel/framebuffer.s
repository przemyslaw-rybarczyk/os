global framebuffer_fast_copy_32_bit

extern fb_pitch
extern fb_width
extern fb_height
extern fb_fast_copy_shuf_mask

; Copy the data from the source buffer using a RGB888 format to the framebuffer using a 32-bit format
; The shuffle mask specifies the exact framebuffer format.
; Argmuments: rdi = framebuffer, rsi = source
framebuffer_fast_copy_32_bit:
  ; Load variables into registers
  movzx r9d, word [fb_width]
  lea r8d, [r9d - 4]
  movzx r10d, word [fb_height]
  movzx r11d, word [fb_pitch]
  movdqa xmm1, [fb_fast_copy_shuf_mask]
  ; Loop over each row of pixels
  xor edx, edx
.y_loop:
  cmp edx, r10d
  jae .y_loop_end
  ; Loop over each group of four pixels in the row
  xor ecx, ecx
.x_vector_loop:
  cmp ecx, r8d
  ja .x_vector_loop_end
  ; Copy the next four pixels
  movdqu xmm0, [rsi]
  pshufb xmm0, xmm1
  movdqu [rdi + rcx * 4], xmm0
  add ecx, 4
  add rsi, 12
  jmp .x_vector_loop
.x_vector_loop_end:
  ; If there are pixels remaining, process the last four pixels
  cmp ecx, r9d
  je .x_line_end
  lea eax, [3 * ecx]
  sub rsi, rax
  lea eax, [3 * r8d]
  add rsi, rax
  movdqu xmm0, [rsi]
  pshufb xmm0, xmm1
  movdqu [rdi + r8 * 4], xmm0
  add rsi, 12
.x_line_end:
  add edx, 1
  add rdi, r11
  jmp .y_loop
.y_loop_end:
  ret
