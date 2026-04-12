#include "drivers/drivers.h"
#include "drivers/plic.h"
#include "drivers/rtc.h"
#include "kernel/console.h"

void drivers_init(void) {
    // initialize the hardware interrupt controller
    plic_init();
    // Console backends — registered before console_init() activates one
    console_uart_register();
    // Real-time clock
    rtc_init();

    // Future drivers get registered here:
    // virtio_init();
    // disk_init();
    // etc.
}
