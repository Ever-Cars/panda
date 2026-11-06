#pragma once

// TCP echo service on port 7 for bring-up and throughput tests.
// Only built into debug (ALLOW_DEBUG) firmware.
void net_echo_init(void);
