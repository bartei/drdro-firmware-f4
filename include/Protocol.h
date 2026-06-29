/**
 * Copyright © 2026 <Stefano Bertelli>
 * MIT — see source headers.
 *
 * drDRO line protocol: text command in, key=value lines out, terminated by an
 * empty line. Replaces Modbus (see protocol_design.md). This header exposes the
 * transport-independent core; the USART1 RX/task wiring is added at switchover.
 */
#ifndef DRDRO_PROTOCOL_H
#define DRDRO_PROTOCOL_H

#include <stdint.h>
#include "stm32f4xx_hal.h"
#include "Ramps.h"                 /* rampsSharedData_t (the variable registry target) */

#ifndef FW_VERSION
#define FW_VERSION "dev"           /* overridden at build time by support/fw_version.py */
#endif

#define PROTOCOL_LINE_MAX 128      /* max command line length (excl. terminator) */

/** Bind the UART (TX/RX) and the shared-data block the registry reads/writes.
 *  (Task + RX IT are wired at the Modbus switchover.) */
void ProtocolStart(UART_HandleTypeDef *huart, rampsSharedData_t *shared);

/** Feed one received byte; assembles a line and flags it ready on \r / \n.
 *  Safe to call from the UART RX ISR (no blocking, no response emitted here). */
void ProtocolFeedByte(uint8_t b);

/** True once a complete line is buffered and ready to process. */
uint8_t ProtocolLineReady(void);

/** Process the most recently completed line: parse, dispatch, emit the response.
 *  Runs in task context (does blocking TX). Clears the ready flag. */
void ProtocolService(void);

/** Parse + dispatch + respond for an explicit line (mutated in place). Exposed for
 *  testing; `line` need not be the internal buffer. */
void ProtocolProcessLine(char *line);

#endif /* DRDRO_PROTOCOL_H */
