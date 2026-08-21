#pragma once
#include <stdint.h>
#include <stddef.h>

#define LAPIC_VECTOR_SPURIOUS 0xFF
#define LAPIC_VECTOR_TIMER 0x20
#define LAPIC_VECTOR_ERROR 0xFE

void lapic_init(void);
void lapic_timer_start(uint32_t frequency_hz, uint8_t vector);
void lapic_timer_stop(void);
void lapic_eoi(void);
uint32_t lapic_get_id(void);
uint64_t lapic_get_ticks_per_ms(void);
