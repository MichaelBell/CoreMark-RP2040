/*
Copyright 2018 Embedded Microprocessor Benchmark Consortium (EEMBC)

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

Original Author: Shay Gal-on
*/

/**
 * @file      core_portme.c
 * @authors   Protik Banerji <protik09@gmail.com>
 * @copyright Protik Banerji, 2021
 *
 * @brief Coremark port for the RP2040
 */

#include "coremark.h"
#include "core_portme.h"
#include <time.h>
#include <pico/stdlib.h>
#include "hardware/adc.h"
#include "hardware/pio.h"
#include "hardware/pwm.h"

#include "pico/bootrom.h"
#include "hardware/vreg.h"
#include "hardware/clocks.h"
#include "hardware/watchdog.h"
#include "hardware/timer.h"
#include "hardware/irq.h"
#include "hardware/structs/rosc.h"

#include "counter.pio.h"

#if VALIDATION_RUN
volatile ee_s32 seed1_volatile = 0x3415;
volatile ee_s32 seed2_volatile = 0x3415;
volatile ee_s32 seed3_volatile = 0x66;
#endif
#if PERFORMANCE_RUN
volatile ee_s32 seed1_volatile = 0x0;
volatile ee_s32 seed2_volatile = 0x0;
volatile ee_s32 seed3_volatile = 0x66;
#endif
#if PROFILE_RUN
volatile ee_s32 seed1_volatile = 0x8;
volatile ee_s32 seed2_volatile = 0x8;
volatile ee_s32 seed3_volatile = 0x8;
#endif

#define ITERATIONS 0
volatile ee_s32 seed4_volatile = ITERATIONS;
volatile ee_s32 seed5_volatile = 0;
/* Porting : Timing functions
        How to capture time and convert to seconds must be ported to whatever is
   supported by the platform. e.g. Read value from on board RTC, read value from
   cpu clock cycles performance counter etc. Sample implementation for standard
   time.h and windows.h definitions included.
*/
CORETIMETYPE barebones_clock()
{
    #if 0
    #ifndef NDEBUG
    return (CORETIMETYPE)to_us_since_boot(get_absolute_time());
    #else
    CORETIMETYPE t = to_us_since_boot(get_absolute_time());
    return t;
    #endif
    #else
    return pio0->rxf_putget[0][0];
    #endif
}
/* Define : TIMER_RES_DIVIDER
        Divider to trade off timer resolution and total time that can be
   measured.

        Use lower values to increase resolution, but make sure that overflow
   does not occur. If there are issues with the return value overflowing,
   increase this value.
        */
#define BB_CLOCKS_PER_SEC 1000000.0
#define GETMYTIME(_t) (*_t = barebones_clock())
#define MYTIMEDIFF(fin, ini) ((fin) - (ini))
#define TIMER_RES_DIVIDER 1
#define SAMPLE_TIME_IMPLEMENTATION 1
#define EE_TICKS_PER_SEC (BB_CLOCKS_PER_SEC / TIMER_RES_DIVIDER)
/** Define Host specific (POSIX), or target specific global time variables. */
static CORETIMETYPE start_time_val, stop_time_val;

/* Function : start_time
        This function will be called right before starting the timed portion of
   the benchmark.

        Implementation may be capturing a system timer (as implemented in the
   example code) or zeroing some system parameters - e.g. setting the cpu clocks
   cycles to 0.
*/
void start_time(void)
{
    GETMYTIME(&start_time_val);
}
/* Function : stop_time
        This function will be called right after ending the timed portion of the
   benchmark.

        Implementation may be capturing a system timer (as implemented in the
   example code) or other system parameters - e.g. reading the current value of
   cpu cycles counter.
*/
void stop_time(void)
{
    GETMYTIME(&stop_time_val);
}
/* Function : get_time
        Return an abstract "ticks" number that signifies time on the system.

        Actual value returned may be cpu cycles, milliseconds or any other
   value, as long as it can be converted to seconds by <time_in_secs>. This
   methodology is taken to accomodate any hardware or simulated platform. The
   sample implementation returns millisecs by default, and the resolution is
   controlled by <TIMER_RES_DIVIDER>
*/
CORE_TICKS get_time(void)
{
    CORE_TICKS elapsed
        = (CORE_TICKS)(MYTIMEDIFF(stop_time_val, start_time_val));
    return elapsed;
}
/* Function : time_in_secs
        Convert the value returned by get_time to seconds.

        The <secs_ret> type is used to accomodate systems with no support for
   floating point. Default implementation implemented by the EE_TICKS_PER_SEC
   macro above.
*/
secs_ret time_in_secs(CORE_TICKS ticks)
{
    secs_ret retval = ((secs_ret)ticks) / (secs_ret)EE_TICKS_PER_SEC;
    return retval;
}

ee_u32 default_num_contexts = 1;

#define ALARM_NUM 0
#define ALARM_IRQ timer_hardware_alarm_get_irq_num(timer_hw, ALARM_NUM)

void alarm_irq(void);

static void alarm_in_us(uint32_t delay_us) {
    // Enable the interrupt for our alarm (the timer outputs 4 alarm irqs)
    hw_set_bits(&timer_hw->inte, 1u << ALARM_NUM);
    // Set irq handler for alarm irq
    irq_set_exclusive_handler(ALARM_IRQ, alarm_irq);
    // Enable the alarm irq
    irq_set_enabled(ALARM_IRQ, true);
    // Enable interrupt in block and at processor

    // Alarm is only 32 bits so if trying to delay more
    // than that need to be careful and keep track of the upper
    // bits
    uint64_t target = timer_hw->timerawl + delay_us;

    // Write the lower 32 bits of the target time to the alarm which
    // will arm it
    timer_hw->alarm[ALARM_NUM] = (uint32_t) target;
}

void alarm_irq(void) {
    // Clear the alarm irq
    hw_clear_bits(&timer_hw->intr, 1u << ALARM_NUM);

    // Toggle LED
    gpio_xor_mask(1 << PICO_DEFAULT_LED_PIN);

    // Reset alarm
    alarm_in_us(500*1000);
}

/* References for this implementation:
 * raspberry-pi-pico-c-sdk.pdf, Section '4.1.1. hardware_adc'
 * pico-examples/adc/adc_console/adc_console.c */
float read_onboard_temperature() {
    
    /* 12-bit conversion, assume max value == ADC_VREF == 3.3 V */
    const float conversionFactor = 3.3f / (1 << 12);

    float adc = (float)adc_read() * conversionFactor;
    float tempC = 27.0f - (adc - 0.706f) / 0.001721f;

    return tempC;
}

/* Function : portable_init
        Target specific initialization code
        Test for some common mistakes.
*/
void portable_init(core_portable *p, int *argc, char *argv[])
{
    // Specific Init Code for RP 2040 with a nice welcome message
    stdio_init_all();

    // Count time based on a 1MHz signal coming in on GPIO 2
    uint pio_count_prog_offset = pio_add_program(pio0, &cycle_count_program);
    pio_gpio_init(pio0, 2);
    pio_sm_set_consecutive_pindirs(pio0, 0, 2, 1, false);

    pio_sm_config c = cycle_count_program_get_default_config(pio_count_prog_offset);
    sm_config_set_in_pins(&c, 2);
    pio_sm_init(pio0, 0, pio_count_prog_offset, &c);
    pio_sm_set_enabled(pio0, 0, true);

    // Output divided system clock for measurement
	gpio_set_function(3, GPIO_FUNC_PWM);
	uint slice_num = pwm_gpio_to_slice_num(3);

	// Set period of 1000 cycles (0 to 999 inclusive)
    pwm_set_wrap(slice_num, 999);
    // Set channel A output high for 500 cycles before dropping
    pwm_set_chan_level(slice_num, PWM_CHAN_B, 500);
    // Set the PWM running
    pwm_set_enabled(slice_num, true);

#if LIB_PICO_STDIO_USB    
    while (!stdio_usb_connected());
#endif
    ee_printf("CoreMark Performance Benchmark\n\n");
    ee_printf("CoreMark measures how quickly your processor can manage linked\n");
    ee_printf("lists, compute matrix multiply, and execute state machine code.\n\n");
    ee_printf("Iterations/Sec is the main benchmark result, higher numbers are better.\n\n");

    ee_printf("Type S to begin\n");
    while (getchar() != 'S');

    ee_printf("Select voltage setting:\n");
    ee_printf("0: Disable\n");
    ee_printf("11: 1.1V\n");
    ee_printf("13: 1.2V\n");
    ee_printf("15: 1.3V\n");
    ee_printf("18: 1.5V\n");
    ee_printf("21: 1.7V\n");

    int volt_selection;
    scanf("%d", &volt_selection);

    if (volt_selection == 0) {
        hw_set_bits(&powman_hw->vreg_ctrl, POWMAN_PASSWORD_BITS | POWMAN_VREG_CTRL_UNLOCK_BITS);
        hw_set_bits(&powman_hw->vreg, POWMAN_PASSWORD_BITS | POWMAN_VREG_HIZ_BITS);
    }
    else {
        if (volt_selection & 0x10)
            vreg_disable_voltage_limit();
        
        vreg_set_voltage(volt_selection);
    }

    ee_printf("Set voltage to setting %d\n", volt_selection);

    ee_printf("Frequency setting, in MHz or 0 for ROSC:\n");

    int freq_mhz;
    scanf("%d", &freq_mhz);

    if (freq_mhz == 0) {
        // This will move the UART on to the USB clock
        set_sys_clock_khz(150 * 1000, true);

        // Enable ROSC at TOOHIGH
        rosc_hw->ctrl = 0xfabfa6;

        // Set drive strengths to 2.5
        rosc_hw->freqa = 0x96962323;
        rosc_hw->freqb = 0x96962323;

        // Set divider to 1
        rosc_hw->div = 0xaa01;

        // Now switch over to ROSC
        clock_configure_undivided(clk_sys,
                        CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLKSRC_CLK_SYS_AUX,
                        CLOCKS_CLK_SYS_CTRL_AUXSRC_VALUE_ROSC_CLKSRC,
                        300 * 1000);
    }
    else {
        set_sys_clock_khz(freq_mhz * 1000, true);
    }

    stdio_init_all();

    ee_printf("Set frequency to %dMHz\n", freq_mhz);

    //getchar(); // PAUSE FOR HUMAN INPUT
    ee_printf("Running.... (usually requires 12 to 20 seconds)\n\n");

    #ifndef PICO_DEFAULT_LED_PIN
    #warning This program requires a board with a regular LED. Replace this line with your own stuff.

    #else
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    gpio_put(PICO_DEFAULT_LED_PIN, 1); // Set pin 25 to high

    alarm_in_us(500 * 1000);

    #endif

    adc_init();
    adc_set_temp_sensor_enabled(true);
    adc_select_input(4);
    float temperature = read_onboard_temperature();
    sleep_ms(100);
    temperature = read_onboard_temperature();
    printf("Temp = %.02fC\n", temperature);

    if (sizeof(ee_ptr_int) != sizeof(ee_u8 *))
    {
        ee_printf(
            "ERROR! Please define ee_ptr_int to a type that holds a "
            "pointer!\n");
    }
    if (sizeof(ee_u32) != 4)
    {
        ee_printf("ERROR! Please define ee_u32 to a 32b unsigned type!\n");
    }
    p->portable_id = 1;
}


/* Function : portable_fini
        Target specific final code
*/
void portable_fini(core_portable *p)
{
    float temperature = read_onboard_temperature();
    printf("Temp = %.02fC\n", temperature);

    multicore_reset_core1();

#if 0
    p->portable_id = 0;

    ee_printf("\n\nProgram Ended\n");

    cancel_repeating_timer(&timer);
    gpio_put(PICO_DEFAULT_LED_PIN, 1);

    while (true) {
        // Do nothing
    }
#else
    int c = stdio_getchar_timeout_us(100);

    if (c == 'C') {
        watchdog_reboot(0, 0, 0);
    } else if (c == 'R') {
        rom_reset_usb_boot_extra(-1, 0, false);
    }
#endif
}
/* Function : core1_func 
    This function is the entry point for core 1
*/

//volatile core_results *global_results;
//volatile bool core1_finished = false;
extern void *iterate(void *pres);

#if 0
/* Function : test_global_results
        Test that every value in struct global_results matches that in struct results[1]
*/
void test_global_results(core_results *results)
{
    ee_printf("Test volatile structs\n");
    if (global_results->seed1 != results[1].seed1)
        ee_printf("Mismatch in seed1\n");
    if (global_results->seed2 != results[1].seed2)
        ee_printf("Mismatch in seed2\n");
    if (global_results->seed3 != results[1].seed3)
        ee_printf("Mismatch in seed3\n");
    if (global_results->size != results[1].size)
        ee_printf("Mismatch in size\n");
    if (global_results->iterations != results[1].iterations)
        ee_printf("Mismatch in iterations\n");
    if (global_results->execs != results[1].execs)
        ee_printf("Mismatch in execs\n");
    if (global_results->crc != results[1].crc)
        ee_printf("Mismatch in crc\n");
    if (global_results->crclist != results[1].crclist)
        ee_printf("Mismatch in crclist\n");
    if (global_results->crcmatrix != results[1].crcmatrix)
        ee_printf("Mismatch in crcmatrix\n");
    if (global_results->crcstate != results[1].crcstate)
        ee_printf("Mismatch in crcstate\n");
    if (global_results->err != results[1].err)
        ee_printf("Mismatch in err\n");
    // Add more checks as needed for other fields in the struct
}
#endif

void core1_func(void)
{
    core_results *results = (core_results *)multicore_fifo_pop_blocking();
    iterate(results); // TODO: Fix the errors its running this
    multicore_fifo_push_blocking(0);
}

/* Function : core_start_parallel
        Start the parallel core
*/
void core_start_parallel(ee_u16 core_index, core_results *results)
{
    // Core index 0 is started first, and this function is expected to exit in 
    // order to start the next core, so start core 1 when core index is 0.
    if (core_index == 0)
    {
        ee_printf("Starting core 1 iterations\n");
        //global_results = (volatile core_results*) &results;
        multicore_launch_core1(core1_func);
        multicore_fifo_push_blocking((uintptr_t)results);
    }
    else
    {
        ee_printf("Starting core 0 iterations\n");
        iterate(results);
    }
}

/* Function : core_end_parallel
        End the parallel core
*/
void core_end_parallel(ee_u16 core_index)
{
    if (core_index == 0) {
        multicore_fifo_pop_blocking();
    }
    
    ee_printf("Core %d finished\n", core_index ^ 1);
}
