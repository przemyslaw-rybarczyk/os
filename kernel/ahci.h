#include "types.h"
#include "error.h"

#include "channel.h"

err_t ahci_init(void);
_Noreturn void ahci_drive_receive_kernel_thread_main(void);
_Noreturn void ahci_drive_reply_kernel_thread_main(void);
void drive_process_irq(void);
_Noreturn void ahci_main_kernel_thread_main(void);

extern MessageQueue *ahci_main_mqueue;
extern Message *drive_info_msg;
extern Channel *drive_open_channel;
