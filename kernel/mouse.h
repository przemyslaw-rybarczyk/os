#pragma once

#include "types.h"

#include "channel.h"
#include "process.h"

extern Process *mouse_kernel_thread;
extern Channel *mouse_channel;

_Noreturn void mouse_kernel_thread_main(void);
