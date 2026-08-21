#pragma once
#include <stdint.h>
#include <stddef.h>

void hpet_init(void);
uint64_t hpet_read_counter(void);
uint64_t hpet_get_frequency(void);
uint64_t hpet_get_nanos(void);
uint64_t hpet_get_millis(void);
void hpet_sleep_us(uint64_t us);
void hpet_sleep_ms(uint64_t ms);
