/* Minimal stand-in for psxrecomp's cpu_state.h, for the turn-file simulator. */
#ifndef CPU_STATE_H
#define CPU_STATE_H
#include <stdint.h>
typedef struct CPUState {
    uint32_t gpr[32];
    uint32_t pc;
    uint32_t (*read_word)(uint32_t addr);
    void     (*write_word)(uint32_t addr, uint32_t value);
    uint16_t (*read_half)(uint32_t addr);
    void     (*write_half)(uint32_t addr, uint16_t value);
    uint8_t  (*read_byte)(uint32_t addr);
    void     (*write_byte)(uint32_t addr, uint8_t value);
} CPUState;
#endif
