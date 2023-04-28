#pragma once

#include "types.h"

#include "channel.h"
#include "process.h"

extern Process *keyboard_kernel_thread;
extern Channel *keyboard_channel;

_Noreturn void keyboard_kernel_thread_main(void);
