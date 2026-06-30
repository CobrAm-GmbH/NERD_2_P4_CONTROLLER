#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Starts the asynchronous Modbus TCP worker for the M31.
 *
 * M31:
 *   IP      192.168.0.90
 *   Port    502
 *   Unit ID 1
 */
void nerd_m31_start(void);

/*
 * Queues a Modbus function 05 (Write Single Coil) command.
 *
 * Returns true when the command was accepted by the local queue.
 * The actual TCP result is reported in the serial monitor.
 */
bool nerd_m31_write_coil(uint16_t coil_address, bool on);

#ifdef __cplusplus
}
#endif
