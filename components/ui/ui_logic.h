#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void ui_logic_init(void);

/**
 * Aggiorna il valore TDS mostrato nella UI.
 *
 * ppm   = valore 0...999
 * valid = false mostra "--"
 */
void ui_logic_set_tds(int ppm, bool valid);

/**
 * Aggiorna il valore pressione mostrato nella UI.
 *
 * bar   = pressione in bar
 * valid = false mostra "-- bar" e "-- psi"
 */
void ui_logic_set_pressure(float bar, bool valid);

/**
 * Aggiorna il flusso partendo dalla frequenza ricevuta
 * dal Sensor Node.
 *
 * hz    = frequenza del sensore in Hz
 * valid = false mostra "-- l/h" e "-- gpd"
 */
void ui_logic_set_flow_hz(float hz, bool valid);

#ifdef __cplusplus
}
#endif