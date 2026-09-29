# lwIP Integration

Branch: `feat/lwip` · Target: `panda_h7` (Richie rev3, STM32H735) · Revised 2026-09-28

Goal: integrate the `board/lwip/` tree (ST lwIP + HAL ETH demo drop) into the panda firmware as a
static library with a small wrapper API, removing everything that duplicates the existing codebase.
Changes outside `lwip/` are kept minimal (SConscript hook, a few guarded lines in `board/main.c`,
and one CAN buffer constant — see Phase 5).

The `board/lwip/` tree started as an ST lwIP + HAL ETH demo drop. It is now built as a set of extra
objects linked into the panda **app** image only (never the bootstub), with a small wrapper API.
Everything that duplicated panda was deleted. Changes outside `lwip/` are limited to the list in
"Changes outside `board/lwip/`".

---

## Architecture at a glance

- **lwIP raw API, `NO_SYS = 1`, `SYS_LIGHTWEIGHT_PROT = 0`.** No RTOS and no internal locking, so
  every lwIP call must come from one context: the panda main loop.
- **Polled Ethernet.** No ETH interrupt. `lwip_poll()` drains the RX ring, runs lwIP timers, and
  runs the link and DHCP state machines.
- **Zero-copy RX.** The ETH DMA writes straight into buffers from a dedicated lwIP memory pool
  (`RX_POOL`), which are handed to the stack as custom pbufs. TX pbufs come from the lwIP heap and
  are read directly by the DMA. Both therefore live in DMA-reachable RAM.
- **Hardware checksums** for IP/UDP/TCP (`CHECKSUM_BY_HARDWARE`); ICMP checksums are generated in
  software.
- **Time base:** SysTick at 1 kHz, owned by the lwip port (`HAL_GetTick()` returns the counter).
- **Features enabled:** IPv4, ARP, ICMP, UDP, TCP (10 PCBs, MSS 1460, 4×MSS send buffer and
  window), DHCP with a static fallback, link-state callback. Netconn and socket APIs are off.
  **IPv6 is not enabled**; see "Enabling IPv6".
- **Demo service:** the ST TCP echo server on port 7 (`tcp_echoserver.c`) is still started.

---

## Source layout (what is compiled)

| Path | Role |
|---|---|
| `inc/lwip_app.h` | Public API included by `board/main.c`: `lwip_stack_init()`, `lwip_poll()`, `lwip_clock_init()`, static-IP / netmask / gateway macros, `CORE_CLOCK_HZ`. Kept free of HAL and lwIP includes so panda's strict TU can include it. |
| `inc/lwip_port.h` | `extern` prototypes for the panda symbols the lwip tree calls (`print`, `set_gpio_*`, `register_set`, `register_set_bits`), so no lwip file includes panda headers. |
| `inc/lwipopts.h` | lwIP configuration. |
| `inc/stm32h7xx_hal_conf.h` | HAL config: `HSE_VALUE` 25 MHz, 4 RX + 4 TX descriptors, MAC address. |
| `src/lwip_app.c` | Stack bring-up and poll loop; ETH clock/MCO setup; SysTick setup; owns `lwip_ram_heap`. |
| `src/lwip_port.c` | Replaces the deleted HAL/CMSIS scaffolding: `SysTick_Handler`, `HAL_GetTick`, `HAL_Delay`, `HAL_InitTick` stub, and `SystemCoreClock` / `SystemD2Clock` / `D1CorePrescTable` / `uwTickPrio`. |
| `src/ethernetif.c` | lwIP netif driver: descriptors, RX pool, `HAL_ETH_MspInit` (pins, PHY power/reset), PHY I/O glue, link-state handling. |
| `src/app_ethernet.c` | Link-status and DHCP state machine, with prints through panda's `print()`. |
| `src/tcp_echoserver.c` | ST demo echo server (port 7). |
| `drivers/lan8742/` | LAN8742 PHY driver. |
| `drivers/hal/src/` | Only `stm32h7xx_hal_eth.c`, `stm32h7xx_hal_rcc.c`, `stm32h7xx_hal_gpio.c`. |
| `middlewares/libc/` | Only `memmove.c`, `strcmp.c`, `strlen.c`, `strncmp.c`, `stdlib_minimal.c` (for `rand`) are compiled. |
| `middlewares/lwip/src/core`, `core/ipv4`, `netif/ethernet.c` | lwIP core. `raw.c`, `dns.c`, `autoip.c`, `igmp.c` and `stats.c` compile to near-empty objects with the current options. |

Why these three HAL sources:

- `hal_eth.c` is the MAC/DMA driver.
- `hal_rcc.c` provides `HAL_RCC_GetHCLKFreq()`, which `HAL_ETH_SetMDIOClockRange()` uses to pick
  the MDC divider. It reads the real prescalers from the registers, so panda's clock setup gives
  the correct answer.
- `hal_gpio.c` is not called by the lwip code. It is linked only because `hal_rcc.c`'s
  (unused) `HAL_RCC_MCOConfig()` references `HAL_GPIO_Init()`, and the build has no
  `--gc-sections`.

The HAL headers under `drivers/hal/inc/` intentionally shadow the same-named ones in
`board/stm32h7/inc/` (`stm32h7xx_hal_def.h`, `stm32h7xx_hal_gpio_ex.h`), so the HAL `.c` files see
their matching versions. This is handled by include-path order in `board/lwip/SConscript`, and
panda's TU never sees the lwip HAL directory. CMSIS and device headers come from
`board/stm32h7/inc/`.

### Removed from the original drop

| Deleted | Replaced by |
|---|---|
| `app/main.c` | `src/lwip_app.c`; panda's `clock_init()` / startup own the MPU, caches, clocks and HAL init |
| `app/stm32h7xx_it.c`, `inc/stm32h7xx_it.h` | panda's interrupt handlers and faults (this also removed the `OTG_HS_IRQHandler` and core-exception symbol clashes) |
| `drivers/usb/` | panda's USB stack, and `print()` from `drivers/uart.h` |
| `drivers/bsp/` (Richie BSP, LEDs, MCO1) | panda's LED driver and `boards/richie.h`; MCO1 setup moved into `lwip_clock_init()` |
| `drivers/cmsis/` (including `system_stm32h7xx.c`) | `board/stm32h7/inc/`; the three globals `hal_rcc.c` needs live in `lwip_port.c` |
| `drivers/hal/src/stm32h7xx_hal.c`, `_cortex.c`, `_pwr_ex.c`, `_rcc_ex.c` | `lwip_port.c` and direct register writes (for example, `HAL_SYSCFG_ETHInterfaceSelect()` in `hal_eth.c` became a `MODIFY_REG` on `SYSCFG->PMCR`) |
| `middlewares/sys/` (`syscalls.c`, `sysmem.c`) | nothing (newlib scaffolding, unused) |
| `inc/main.h`, `inc/richie_conf.h`, `inc/utilities_conf.h` | `inc/lwip_app.h`; nothing for the other two |

The demo sources were also moved from `app/` to `src/`.

---

## Build integration

### `board/lwip/SConscript`

It clones the panda firmware env, so objects inherit the exact codegen flags
(`-mcpu=cortex-m7 -mhard-float -mfpu=fpv5-d16 -Os`), `-DSTM32H735xx`, the board defines
(including `-DHAS_DOIP`), and the `board/stm32h7/inc` include path. For the vendor code it then:

- removes `-Werror`, `-Wextra`, `-Wstrict-prototypes` and `-fmax-errors=1`;
- adds `-Wno-unused-parameter` and `-DUSE_HAL_DRIVER` (lwip objects only, so panda's TU never sees
  the HAL);
- prepends `inc`, `drivers/hal/inc`, `drivers/lan8742`, `middlewares/lwip/src/include` and
  `middlewares/lwip/system` to `CPPPATH` (`drivers/hal/inc` must come before
  `board/stm32h7/inc`).

It returns the list of objects.

Object placement: `OBJPREFIX` is the project directory **without a trailing slash**, so objects
land flat as `board/obj/panda_h7<basename>.o`. No two compiled sources may share a basename.

### `panda/SConscript`

- `build_project(..., lwip=False)` gains a flag. When it is set, the lwip `SConscript` is called
  with the app `env` and its objects are appended to the `main.elf` sources. The bootstub env is
  untouched.
- `-Wl,--print-memory-usage` is added to the app link, and a `$SIZE` post-action prints section
  sizes after every link. Use these to watch the RAM budget.
- The `panda_h7` build passes `['-DRICHIE', '-DRICHIE_REV3', '-DHAS_DOIP']` with `lwip=True`.

### Tooling (not firmware)

- `generate_compile_commands.py`, called from `SConstruct`, replaces SCons' `compilation_db`
  tool. The stock tool only records linked objects and misses the lwip tree.
- `.clangd` force-includes `board/main.c` when parsing panda headers, excluding `board/lwip/`.
- `.gitignore` now ignores `.cache` (the clangd index).

---

## Runtime integration

### `board/main.c` (all guarded by `#ifdef HAS_DOIP`)

- `#include "board/lwip/inc/lwip_app.h"`.
- `lwip_stack_init()` is called **after `enable_interrupts()`**. This is required: `HAL_Delay()` and
  every HAL timeout depend on the SysTick interrupt.
- `lwip_poll()` is called at the top of the `while (true)` loop and once per iteration of each of
  the two LED-fade loops. In power-save mode the loop sits in `__WFI()`, which SysTick wakes every
  1 ms, so polling continues.

Blocking inside `lwip_stack_init()` (about 200 ms of PHY reset) is harmless to the heartbeat
watchdog: `simple_watchdog_kick()` runs from the 8 Hz tick interrupt, not from the main loop.

### `lwip_stack_init()`

1. `lwip_clock_init()`:
   - selects RMII in `SYSCFG->PMCR`;
   - enables `ETH1MAC`, `ETH1TX` and `ETH1RX` in `RCC->AHB1ENR` (through `register_set_bits`);
   - drives MCO1 = HSE/1 = 25 MHz on PA8 (AF0, very-high speed) as the LAN8742 clock. The
     `RCC->CFGR` MCO1 bits are set with `register_set_bits`.
2. SysTick: reload `CORE_CLOCK_HZ / 1000`, lowest priority, interrupt enabled.
3. `lwip_init()` and `netif_add(..., ethernetif_init, ethernet_input)`. With DHCP enabled the
   netif starts with a zero address.
4. `ethernetif_init()` calls `low_level_init()`:
   - `HAL_ETH_Init()` calls `HAL_ETH_MspInit()`, which configures the pins and powers and resets
     the PHY (PE5 high, then PE0 high/low/high with 100 ms steps). It then resets the DMA, which
     needs the PHY's 50 MHz REF_CLK.
   - The RX pool is initialized, the TX config is set to checksum and CRC/pad insertion, and
     `LAN8742_Init()` scans MDIO addresses for the PHY. It has no multi-second wait.
   - `ethernet_link_check_state()` starts the MAC/DMA only once the PHY reports link.
5. The netif is set as the default, the link callback is registered, and the echo server is
   started.

### `lwip_poll()`

- `ethernetif_input()` reads frames until the RX ring is empty.
- `sys_check_timeouts()` runs lwIP's TCP, ARP, DHCP and IP-reassembly timers.
- `Ethernet_Link_Periodic_Handle()` checks the PHY link over MDIO every 100 ms. On link loss it
  calls `HAL_ETH_Stop()` and brings the netif down. On link gain it applies the negotiated
  speed/duplex and calls `HAL_ETH_Start()`.
- `DHCP_Periodic_Handle()` runs the DHCP state machine every 500 ms. After 10 failed tries it
  falls back to the static address `192.168.0.10/24` with gateway `192.168.0.1`.

### Time base (`lwip_port.c`)

`SysTick_Handler` increments a `volatile uint32_t systick`, and `HAL_GetTick()` returns it.
`sys_now()`, the PHY driver tick, and all HAL timeouts use it. It replaced a panda-side
`millisecond_timer_init()` / `SysTick_Handler` that previously lived in `drivers/timers.h`.

Consequences:

- `HAL_GetTick()` / `HAL_Delay()` never advance before `enable_interrupts()`, or inside
  `ENTER_CRITICAL()`. Calling a HAL function with a timeout from either place hangs.
- SysTick is a core exception, not an NVIC IRQ. It bypasses panda's `REGISTER_INTERRUPT`, so it
  has no rate-limit fault and is not counted in `interrupt_load`. At 1 kHz with a one-line body,
  this is negligible.

---

## Panda register integrity checker

Panda records GPIO/RCC/timer register writes in `register_map`, and `check_registers()`
re-verifies them at 1 Hz, raising `FAULT_REGISTER_DIVERGENT` on any mismatch. The GPIO helpers
record `MODER`, `AFR` and `PUPDR` with a full `0xFFFFFFFF` mask. Any write to those registers that
bypasses the helpers (for example, `HAL_GPIO_Init()`) makes the board fault within a second.

As implemented, every pin is configured through panda's helpers:

- RMII pins PA1 (REF_CLK), PA2 (MDIO), PA7 (CRS_DV), PB10 (RXER), PB11 (TX_EN), PC1 (MDC),
  PC4/PC5 (RXD0/1) and PG13/PG14 (TXD0/1) use `set_gpio_pullup(NOPULL)` and
  `set_gpio_alternate(AF11)`. Very-high speed is set through `register_set_bits(&GPIOx->OSPEEDR, ...)`.
- PE5 (PHY power) and PE0 (PHY reset) use `set_gpio_mode(output)` and `set_gpio_output()`.
- PA8 (MCO1) uses `set_gpio_alternate(AF0)`, and its speed is set through `register_set_bits`.
- RCC bits (`AHB1ENR` ETH gates, `CFGR` MCO1) are set through `register_set_bits`, which only adds
  those bits to the check mask.

`SYSCFG->PMCR` is written directly with `MODIFY_REG`, both in `lwip_clock_init()` and in
`HAL_ETH_Init()`. That is safe on Richie: only the jungle board records `PMCR` in the map. The
`RCC->APB4ENR` write that `HAL_ETH_Init()` makes to enable the SYSCFG clock is also safe, because
panda writes that register directly and never records it.

---

## Memory placement (measured)

ETH DMA cannot reach DTCM (0x20000000, where `.bss`/`.data` live) or ITCM. Commit `01213b18`
also established on hardware that it did not work from SRAM4, so the DMA buffers live in AXISRAM
and SRAM12. Caches are disabled in panda. The demo's cache clean/invalidate calls in
`ethernetif.c` now run only if `SCB->CCR.DC` is set, so they are inert today and become correct
if the D-cache is ever enabled (at which point MPU regions would also be needed).

| Region | Used / size | Free | Contents |
|---|---|---|---|
| AXISRAM `0x24000000` | 316,128 / 327,680 B (96.5%) | 11,552 B | CAN RX ring `elems_rx_q` 276,480 (3840 × 72 B); ISO-TP queues 24,588; **`lwip_ram_heap` 14,848** (`MEM_SIZE` 14K + 512 B headroom); **`DMARxDscrTab` / `DMATxDscrTab` 96 B each** |
| SRAM12 `0x30000000` | 29,923 / 32,768 B (91.3%) | 2,845 B | ISO-TP staging buffers 8,200 + 4,100; SPI RX/TX 4,096 each; **`memp_memory_RX_POOL_base` 9,411** (6 × 1,568 B `RxBuff_t`) |
| SRAM4 `0x38000000` | 0 / 16,384 B | 16,384 B | empty (see sound stubs below) |
| DTCM `0x20000000` | 116,960 / 131,072 B (89.2%) | ~14,100 B | panda `.data`/`.bss`, plus about 28.7 KB of lwip `.bss` (all other memp pools, `EthHandle`, netif). The free space is the main stack; the linker only reserves 1.5 KB for heap and stack. |
| ITCM | 59,904 / 65,536 B | | CAN TX queues; not DMA-reachable |
| Flash (app) | 281,132 B (26.8%) | | lwip adds about 65 KB of text |

How the budget was made to fit:

1. **`CAN_RX_BUFFER_SIZE` reduced from 4096 to 3840** (`board/drivers/can_common.h`), freeing
   18,432 B of AXISRAM for the heap and descriptors. Non-power-of-two is safe: `can_push` /
   `can_pop` use compare-and-wrap, not modulo.
2. **`ETH_RX_BUFFER_CNT` reduced from 9 to 6** so the RX pool fits in SRAM12. It must stay above
   `ETH_RX_DESC_CNT` (4).
3. The heap and descriptors use `section(".axisram")`. The RX pool uses `section(".sram12.eth_rx")`,
   which the linker script collects with `*(.sram12*)`.

Notes:

- `.axisram` and `.sram12` are **NOLOAD**: the startup code does not zero them. That is fine for
  these buffers, because `mem_init()`, `memp_init_pool()` and the HAL descriptor-list init all
  initialize their memory. Do not put anything there that relies on zero-initialization.
- The RX pool's section attribute sits on an `extern` redeclaration *after*
  `LWIP_MEMPOOL_DECLARE`. With the current GCC it takes effect: `ethernetif.o` has a
  `.sram12.eth_rx` section, and the pool links at `0x30005020`. It is still order-dependent; see
  cleanup below.
- `PBUF_POOL_SIZE` is unset, so it defaults to 16: `memp_memory_PBUF_POOL_base` is 24,835 B of
  DTCM. The zero-copy RX path never allocates from it. The only `PBUF_POOL` user in the compiled
  sources is a `udp.c` path gated behind `SO_REUSE`, which is off.

### Sound stubs (Richie)

`board/stm32h7/board.h` includes `board/stm32h7/sound_stubs.h` instead of `sound.h` under `RICHIE`.
The stubs provide empty `sound_tick()`, `sound_init()`, `sound_init_dac()` and `sound_stop_dac()`,
plus `sound_output_level`. This removes the four audio/mic DMA buffers that occupied about 14 KB
of SRAM4. Richie never enables the amplifier, so no functionality is lost.

---

## libc and the `-nostdlib` build

- `memcpy`, `memset` and `memcmp` resolve to panda's `libc.h`. The lwip tree adds only `memmove`,
  `strcmp`, `strlen`, `strncmp`, and `rand`/`srand` (`stdlib_minimal.c`). The newlib-derived
  `memcpy.c`, `memset.c`, `aeabi_*` and `*.S` files remain on disk but are not built.
- `sprintf` was removed from `app_ethernet.c`; addresses are printed with
  `print(ip4addr_ntoa(...))`.
- `cc.h`:
  - `LWIP_PLATFORM_ASSERT` prints the message, line and file through `print` / `puth`, then
    **continues**.
  - `LWIP_NO_CTYPE_H` avoids newlib's unlinked `_ctype_`.
  - `LWIP_RAND()` is `rand()`.
  - The `<stdio.h>`, `<stdlib.h>` and `<sys/time.h>` includes remain; they are header-only and
    link nothing.
- `lwip/arch.h` includes `lwip_port.h` (it previously included `usb_debug.h`).
  `LWIP_PLATFORM_DIAG` still uses `vsnprintf`. It compiles away only because `LWIP_DEBUG` is
  unset. **Enabling `LWIP_DEBUG` fails the link** unless `lwip_platform_diag` is first rewritten
  to use `print`.
- The build needs no `-lgcc`; no 64-bit division helpers are referenced.

### Where debug output goes

`print()` writes into panda's 1 KB `uart_ring_debug` software ring. Characters are dropped silently
when the ring is full. The host drains the ring over USB control request `0xe0` with `param1 = 0`,
the same channel as panda's boot banner, so lwIP and link/DHCP prints interleave with panda's
output. Anything printed before USB enumeration survives only if it fits in the ring.

---

## Design decision: poll from the main loop, not from an ISR

We considered calling `lwip_poll()` from the 1 kHz ISO-TP timer ISR and rejected it:

1. The raw API is not reentrant (`NO_SYS = 1`, no locking). An ISR-driven stack would forbid any
   `tcp_write` / `udp_send` from thread context, such as DoIP handlers. Violations cause silent
   pbuf/memp corruption.
2. All relevant IRQs (TIM23, TIM12 tick, FDCAN, OTG_HS) share priority 0. A long poll would stall
   CAN RX, USB, and the 8 Hz tick that kicks the heartbeat watchdog.
3. `lwip_poll()` is unbounded work: it drains the whole RX ring, handles TCP retransmits, and
   does MDIO busy-polls every 100 ms.
4. ISO-TP ticks would be swallowed silently, because the handler clears `SR` unconditionally.
   `interrupt_load` would also report near-total load.

If polling cadence ever becomes insufficient, the fallbacks in order are:

1. Have an ISR set a `volatile` flag that the main loop services.
2. Shorten or compile out the LED fade in the Ethernet build.
3. Use a `REGISTER_INTERRUPT(ETH_IRQn, ...)` handler that only clears the DMA status and sets a
   flag.

---

## Changes outside `board/lwip/` (complete list)

| File | Change |
|---|---|
| `SConscript` | lwip hook, `HAS_DOIP`, memory-usage print and size post-action (**uncommitted**) |
| `SConstruct`, `generate_compile_commands.py`, `.clangd`, `.gitignore` | clangd tooling |
| `board/main.c` | `HAS_DOIP`-guarded include, init and poll calls |
| `board/drivers/can_common.h` | `CAN_RX_BUFFER_SIZE` 4096 → 3840 |
| `board/drivers/timers.h` | removed `millisecond_timer_init()` and `SysTick_Handler` (moved to lwip) |
| `board/libc.h` | removed `delay_ms()` (depended on the removed ms timer) |
| `board/stm32h7/stm32h7_config.h` | removed `CORE_CLOCK_HZ` (now in `lwip_app.h`) |
| `board/stm32h7/board.h`, `board/stm32h7/sound_stubs.h` | sound stubs for Richie |

The jungle and body firmware still build with these changes (verified against `HEAD`'s
`SConscript`).

MISRA: `tests/misra/test_misra.sh` runs cppcheck on `board/main.c` without `-DHAS_DOIP`. The
Ethernet code paths are therefore not MISRA-checked at all, and no suppressions were needed.

---

## Verification

Build:

- `scons` links with `--print-memory-usage`; compare against the table above.
- The only lwip-side warning is an unused `GPIO_InitStructure` in `HAL_ETH_MspInit`.

On hardware (done):

- DHCP lease obtained; traffic works.

Regression checklist for each change to this area:

1. No `FAULT_REGISTER_DIVERGENT` after about 5 s of uptime (proves every pin write went through
   the helpers).
2. No `FAULT_HEARTBEAT_LOOP_WATCHDOG`.
3. `ping <ip>`, and `nc <ip> 7` echoes.
4. Unplugging and replugging the cable prints link-down, then a new DHCP lease.
5. CAN RX under load (smaller ring), USB enumeration, and SPI (its buffers neighbour the RX pool in
   SRAM12).

---

## Enabling IPv6 (not implemented)

The stack is IPv4-only today:

- `lwipopts.h` does not set `LWIP_IPV6`, so the `opt.h` default of `0` applies.
- The ST drop does not contain `middlewares/lwip/src/core/ipv6/`. Only the IPv6 headers under
  `src/include/lwip/` are present.
- `main.elf` contains no `ip6`, `nd6`, `mld6` or `icmp6` symbols.
- `ethernet_input()` drops incoming IPv6 frames.

DoIP (ISO 13400) permits IPv6, but IPv4 is the norm for testers and vehicles. Only do this if the
use case needs it. The steps below give a dual stack (IPv4 + IPv6).

### 1. Add the IPv6 sources

Copy `src/core/ipv6/` from upstream lwIP **2.1.2**, the version in `lwip/init.h`, into
`middlewares/lwip/src/core/ipv6/`. Mixing versions breaks against the existing headers. Add the
files to `board/lwip/SConscript`:

```python
] + [f'middlewares/lwip/src/core/ipv6/{f}' for f in (
  'dhcp6.c', 'ethip6.c', 'icmp6.c', 'inet6.c', 'ip6.c', 'ip6_addr.c', 'ip6_frag.c', 'mld6.c', 'nd6.c',
)] + ['middlewares/lwip/src/netif/ethernet.c']
```

None of these basenames collide with existing objects, which matters because objects land flat in
`board/obj/`.

### 2. `inc/lwipopts.h`

```c
#define LWIP_IPV6                       1
#define LWIP_IPV6_AUTOCONFIG            1   /* SLAAC from router advertisements */
#define LWIP_IPV6_DHCP6                 0   /* enable only if the network requires DHCPv6 */
#define LWIP_IPV6_NUM_ADDRESSES         3   /* link-local + up to 2 global */

/* Trim the defaults (10/10/20) to keep DTCM usage down */
#define LWIP_ND6_NUM_NEIGHBORS          4
#define LWIP_ND6_NUM_DESTINATIONS       4
#define MEMP_NUM_ND6_QUEUE              4
#define MEMP_NUM_MLD6_GROUP             4

/* Keep ICMPv6 checksums in software, matching the existing ICMP choice */
#define CHECKSUM_GEN_ICMP6              1
#define CHECKSUM_CHECK_ICMP6            1
```

TCP and UDP over IPv6 keep using hardware checksums. The `CHECKSUM_*_TCP/UDP 0` settings already
cover both IP versions, and the H7 MAC's checksum offload handles IPv6 payloads.

### 3. `src/ethernetif.c`, in `ethernetif_init()` / `low_level_init()`

```c
#if LWIP_IPV6
  netif->output_ip6 = ethip6_output;
  netif->flags |= NETIF_FLAG_MLD6;
  netif_set_mld_mac_filter(netif, ethernetif_mld_mac_filter);  /* see step 4 */
#endif
```

Add `#include "lwip/ethip6.h"`.

### 4. Let IPv6 multicast through the MAC

Neighbour discovery depends on multicast frames:

- solicited-node `33:33:ff:xx:xx:xx`;
- all-nodes `33:33:00:00:00:01`.

`HAL_ETH_Init()` never writes `MACPFR`, so the MAC stays in perfect-filter mode and drops them.
Without this step, IPv6 fails silently: a link-local address is assigned, but no neighbour
resolution or router advertisements are ever received.

Choose one of the following:

- **Simple:** after `HAL_ETH_Init()`, call
  `HAL_ETH_GetMACFilterConfig()`, set `.PassAllMulticast = ENABLE`, then call
  `HAL_ETH_SetMACFilterConfig()`. This costs a little RX load and RX-pool pressure from unrelated
  multicast traffic.
- **Precise:** implement `ethernetif_mld_mac_filter()` to maintain the 64-bit multicast hash
  (`HashMulticast = ENABLE`, `HAL_ETH_SetHashTable()`). lwIP calls it for each MLD group it joins
  or leaves. The hash index is the upper 6 bits of the bit-reversed CRC32 of the destination MAC.

### 5. `src/lwip_app.c`

After `netif_add()` and `netif_set_default()`:

```c
#if LWIP_IPV6
  netif_create_ip6_linklocal_address(&lwip_netif, 1);   /* fe80::/64 from the MAC (EUI-64) */
  netif_set_ip6_autoconfig_enabled(&lwip_netif, 1);
#endif
```

The link-local address is derived from the MAC. That makes fixing the hardcoded
`02:00:00:00:00:00` MAC (see "Remaining work") a prerequisite: otherwise every board gets the same
`fe80::` address, and duplicate address detection marks it invalid on all but the first.

### 6. Dual-stack source fixes

With `LWIP_IPV6 1`, `ip_addr_t` becomes a tagged union, no longer an alias of `ip4_addr_t`. Calls
that pass an `ip_addr_t *` where lwIP expects an `ip4_addr_t *` compile with a warning, because
the vendor env drops `-Werror`. They happen to work only because the IPv4 member comes first. Fix
them explicitly:

- `lwip_app.c`: declare `ipaddr`, `netmask` and `gateway` as `ip4_addr_t`, and use
  `ip4_addr_set_zero()`. `IP4_ADDR()` already takes `ip4_addr_t *`.
- `app_ethernet.c`, in `DHCP_Process()`:
  - pass `ip_2_ip4(&ipaddr)` and so on to `netif_set_addr()`, or declare them as `ip4_addr_t`
    with `IP4_ADDR()`;
  - apply the same treatment to the three `ip_addr_set_zero_ip4(&netif->...)` calls.
- `tcp_echoserver.c`, and any future DoIP listener: bind with `IP_ANY_TYPE` instead of
  `IP_ADDR_ANY` to accept both IPv4 and IPv6. `IP_ADDR_ANY` is IPv4-only in dual-stack builds.
- Rebuild and check that the lwip objects produce no new `incompatible pointer type` warnings.

### 7. Budget and verify

- **Flash:** roughly 20–30 KB extra; there is plenty of room.
- **DTCM:** this is the constraint. The ND6 neighbour and destination caches, the ND6 queue,
  MLD6 groups, IPv6 reassembly and the larger `struct netif` all land in `.bss`, and only about
  14 KB is spare, which is the stack. Set `PBUF_POOL_SIZE` to 2–4 first (it returns about
  20 KB), then compare `--print-memory-usage` before and after.
- **SRAM12 and AXISRAM:** unchanged. IPv6 adds no DMA buffers.
- **Verify on hardware:**
  1. `ping6 fe80::<eui64>%<iface>` from a host on the same link.
  2. With a router advertising a prefix, check that a global address appears.
  3. Check that IPv4 DHCP still works (dual stack).
  4. Confirm no `FAULT_REGISTER_DIVERGENT`. `MACPFR` is not in panda's register map, so changing
     the filter is safe.

---

## Remaining work and concerns

### Required before merging

1. **Commit the build hook.** `HEAD` does not compile lwIP: the `SConscript` hook and
   `-DHAS_DOIP` exist only in the working tree. Also:
   - delete the stray `sconscript.lwip.diff`, which is an older variant with `lwip=False`;
   - drop the dead `# flags.append("-DHAS_DOIP")` line;
   - pass `lwip=True` by keyword;
   - restore the jungle and body builds that the working-tree change comments out. Both still
     build.
2. **MAC address.** It is hardcoded as `02:00:00:00:00:00` in `stm32h7xx_hal_conf.h`, so every
   board has the same MAC, and two boards on one network will collide in ARP and DHCP. Derive a
   locally administered MAC from the MCU UID (`UID_BASE`) or from provisioning data.
3. **`rand()` is never seeded.** `srand()` is never called, so every boot produces the same
   sequence of DHCP transaction IDs, TCP initial sequence numbers and ephemeral ports. Every board
   produces the same sequence too. Seed it in `lwip_stack_init()` from the UID mixed with
   `microsecond_timer_get()`, or use the H7 RNG peripheral.
4. **Replace the demo echo server** (TCP port 7, reachable by anyone on the network) with the
   DoIP service, or stop starting it.

### Robustness concerns

5. **Errors are ignored:**
   - `HAL_ETH_Init()`'s return value is ignored in `low_level_init()`. A DMA-reset timeout
     (no PHY REF_CLK) goes unnoticed.
   - `lwip_stack_init()`'s return value is ignored in `main.c`.
   - `low_level_output()` ignores `HAL_ETH_Transmit()`'s result and always returns `ERR_OK`.

   At minimum, print on failure. Consider a panda fault code for "Ethernet init failed".
6. **Asserts don't stop execution.** `LWIP_PLATFORM_ASSERT` prints and carries on with corrupted
   state. Either route it to `fault_occurred()` and hang, or define `LWIP_NOASSERT` for release
   builds. With 477 assert sites, `LWIP_NOASSERT` also saves flash.
7. **RAM headroom is thin:**
   - SRAM12 has 2.8 KB free, so one more RX buffer does not fit.
   - AXISRAM has 11.3 KB free.
   - DTCM has about 14 KB left for the stack.

   Cheap wins:
   - set `PBUF_POOL_SIZE` to 2–4, which returns about 18–21 KB of DTCM (stack headroom);
   - SRAM4 is now entirely free for CPU-only data.
8. **CAN RX ring is 6% smaller.** If that matters for the target traffic, it can be restored to
   4096 without touching Ethernet memory. Move one ISO-TP queue (12,294 B, CPU-only) from AXISRAM
   to the now-empty SRAM4. AXISRAM free space then rises to about 23.8 KB, more than the 18,432 B
   needed.
9. **SysTick coupling.** Nothing prevents a future caller from using a HAL timeout before
   `enable_interrupts()` or inside a critical section, which would hang. Keep all ETH/HAL calls in
   main-loop context after init.
10. **Link-flap path.** `ethernet_link_check_state()` stops and restarts the MAC/DMA from the main
    loop. This path is untested beyond cable replug; test it under traffic.

### Cleanup (no functional change)

- `board/drivers/timers.h` still defines `milliseconds_count` and `millisecond_timer_get()`.
  Nothing increments the counter any more, so the getter always returns 0. Delete both.
- `lwip_app.h` declares `lwip_dma_memory_init()`, which has no definition. Remove it.
- `lwip_app.h` leaks `CORE_CLOCK_HZ` and the `IP_ADDR*` / `NETMASK_ADDR*` / `GW_ADDR*` macros into
  panda's TU. Derive the SysTick reload from `CORE_FREQ` instead of a second hardcoded 240 MHz,
  and move the address macros into a lwip-private header.
- `lwip_port.h`: the `PANDA_MODE_*` / `PANDA_PULL_*` constants are unused.
- `ethernetif.c` uses `MODE_OUTPUT`, which resolves to the HAL's macro. It happens to equal
  panda's value (1). Use `PANDA_MODE_OUTPUT` instead.
- `lwip_port.c`: the comment on `HAL_GetTick()` ("TIM2 free-runs, works before
  enable_interrupts") is stale.
- `ethernetif.c`:
  - move the RX-pool section declaration above `LWIP_MEMPOOL_DECLARE`, or declare the pool storage
    explicitly;
  - delete the IAR/MDK `#if` branches;
  - remove the unused `GPIO_InitStructure`.
- `lwip_clock_init()` sets MCO1 bits with `register_set_bits()`. That relies on the reset value
  of `CFGR`. `register_set(&RCC->CFGR, value, MCO1 | MCO1PRE mask)` is more robust.
- `board/lwip/SConscript` still lists the removed `app/` directory in `CPPPATH`.
- Unbuilt files still on disk:
  - `middlewares/libc/{memcpy,memset}.c`, `aeabi_*`, `*.S`;
  - `middlewares/lwip/src/api/` (keep only if an RTOS move is planned);
  - `src/apps/http/`;
  - `system/OS/sys_arch.c`;
  - HAL headers `_cortex`, `_dma*`, `_exti`, `_pwr*` (with their modules still enabled in
    `stm32h7xx_hal_conf.h`).
- A stale `board/obj/panda_h7stm32h7xx_hal.o` from before the HAL deletion remains in the build
  directory. It is harmless because it is not linked.

### Open hardware and product questions

- **PHY clock:** MCO1 (25 MHz on PA8) is driven and Ethernet works. Confirm on the rev3 schematic
  that the LAN8742 XI really is fed from PA8. If the PHY has its own crystal, remove the MCO block.
- **`DOIP_EN` (PB4):** Ethernet works without asserting it. Confirm whether it gates anything else
  in the DoIP circuit. PB4 is also used as a CAN2 transceiver enable on Richie.
- **Addressing:** decide whether the DHCP-with-static-fallback scheme (`192.168.0.10`) fits the
  nRF9151/DoIP use case.
