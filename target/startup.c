/*
 * Minimal Cortex-M startup for running the unit tests on QEMU (mps2-an386, Cortex-M4).
 *
 * Output and exit go through ARM semihosting (newlib librdimon), so printf()
 * in the tests appears on the QEMU console and the test result becomes the
 * exit status of QEMU.
 */
#include <stdint.h>

extern int main(void);
extern void exit(int status);
extern void initialise_monitor_handles(void);

/* Symbols from the linker script */
extern uint32_t _estack;
extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss;

void Reset_Handler(void);
void Fault_Handler(void);

__attribute__((section(".isr_vector"), used))
static void (*const vector_table[])(void) = {
    (void (*)(void))(&_estack), /* initial stack pointer */
    Reset_Handler,              /* reset */
    Fault_Handler,              /* NMI */
    Fault_Handler,              /* HardFault */
    Fault_Handler,              /* MemManage */
    Fault_Handler,              /* BusFault */
    Fault_Handler,              /* UsageFault */
};

void Reset_Handler(void)
{
    uint32_t *src = &_sidata;
    uint32_t *dst = &_sdata;

    while (dst < &_edata) {     /* copy initialised data from flash to RAM */
        *dst++ = *src++;
    }
    for (dst = &_sbss; dst < &_ebss; ) {
        *dst++ = 0u;            /* zero .bss */
    }

    initialise_monitor_handles();
    exit(main());
}

void Fault_Handler(void)
{
    exit(99);                   /* any fault fails the run instead of hanging it */
}
