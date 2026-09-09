# lwIP Integration Plan

Branch: `feat/lwip` · Target: `panda_h7` (Richie rev3) · Written 2026-08-27 · Revised 2026-09-08

Goal: integrate the `board/lwip/` tree (ST lwIP + HAL ETH demo drop) into the panda firmware as a
static library with a small wrapper API, removing everything that duplicates the existing codebase.
Changes outside `lwip/` are kept minimal (SConscript hook, a few guarded lines in `board/main.c`,
and one CAN buffer constant — see Phase 5).

**Revision note (2026-09-08):** this document has been corrected against both codebases. Three of
the original "verified facts" about RAM were wrong, one hard blocker (panda's register integrity
checker) was missing, and the document had drifted from the working tree, where `app/lwip.c` and
`inc/lwip_app.h` already exist. Corrections are marked **[CORRECTED]** and **[NEW]** below.

---

## Verified facts this plan is built on

- **Build model**: each firmware is a single translation unit (`board/main.c` + all headers) built by
  `panda/SConscript` → `build_project()`. `panda_h7` is already the Richie build (`-DRICHIE`).
  Flags: `-nostdlib -fno-builtin -Werror -Wextra -Wstrict-prototypes -fmax-errors=1`, no
  `--gc-sections` — so every symbol referenced by a linked object must exist, and non-`static`
  functions in panda's headers (e.g. `print` in `drivers/uart.h:114`, `microsecond_timer_get` in
  `drivers/timers.h:23`, the `set_gpio_*` helpers in `drivers/gpio.h`) are **linkable external
  symbols** the lwip library can call.
- **Caches are off** (no `SCB_EnableI/DCache` anywhere outside lwip) → no MPU regions, no
  cache-coherency work. The demo's cache-maintenance calls in `ethernetif.c` are harmless: CMSIS
  guards them on `__DCACHE_PRESENT` (a compile-time core property), so they execute, but
  clean/invalidate against a disabled cache is architecturally a no-op.
- **`.bss`/`.data` live in DTCM (0x20000000), which ETH DMA cannot access.** Neither can it reach
  ITCM. The linker script (`board/stm32h7/stm32h7x5_flash.ld`) provides `.axisram` (0x24000000,
  320K), `.sram12` (0x30000000, 32K) and `.sram4` (0x38000000, 16K) output sections, collected by
  `*(.axisram*)` / `*(.sram12*)` / `*(.sram4*)`, so dotted sub-section names like
  `.sram12.eth_desc` already match. `stm32h7/peripherals.h:105` enables the D2 SRAM clocks.
  **[CORRECTED] Every DMA-reachable region is nearly full — see "RAM budget" below.**
- **ETH runs in polling mode** (`HAL_ETH_Start` + `HAL_ETH_ReadData` from `ethernetif_input`) →
  **no ETH interrupt, no `SysTick`, no NVIC work needed**. The only time base needed is
  `HAL_GetTick()`, and it can be served from the free-running TIM2, so it works before
  `enable_interrupts()`.
- The RMII pin map in `HAL_ETH_MspInit` is already adapted for the Richie rev3 board (PE0=PHY reset,
  PE5=PHY power, PB10/PB11/PG13/PG14 TX side) and does **not** conflict with any pin used in
  `boards/richie.h`. All of PA1/PA2/PA7/PA8, PB10/PB11, PC1/PC4/PC5, PE0/PE5, PG13/PG14 are free.
  **[NEW] But the pins cannot be configured with `HAL_GPIO_Init` — see blocker below.**
- Panda's `stm32h7/inc/stm32h7xx.h` selects the device from `-DSTM32H735xx` (already passed) and
  has the `#if defined (USE_HAL_DRIVER) #include "stm32h7xx_hal.h"` block at line 222, so passing
  `-DUSE_HAL_DRIVER` to lwip sources only (Phase 4) works; panda's TU never sees that define.

### Known landmines

- `app_ethernet.c` calls `sprintf` (won't link under `-nostdlib`).
- `cc.h` maps `LWIP_PLATFORM_ASSERT` to `printf` (same problem).
- `lwipopts.h` hardcoded the heap at `0x30004000` (already fixed in the working tree).
- `microsecond_timer_get()` wraps every ~71.6 min (a naive `/1000` tick breaks HAL timeouts).
- **[NEW]** `stdlib_minimal.c` defines `int rand(void)`. Declaring `unsigned int rand(void)` in
  `cc.h` is a conflicting declaration and will not compile. Use `int rand(void);`.
- **[NEW]** `PBUF_POOL_SIZE` is unset, so lwIP defaults to 16; with `PBUF_POOL_BUFSIZE 1536` that
  is ~24.8K of DTCM `.bss` the zero-copy RX path never touches.

---

## [NEW] Blocker: panda's register integrity checker

Panda's GPIO helpers record whole registers into `register_map` with a **full `0xFFFFFFFF` mask**:

```c
// board/drivers/gpio.h:18 (set_gpio_mode), :48 (set_gpio_alternate), :58 (set_gpio_pullup)
register_set(&(GPIO->MODER), tmp, 0xFFFFFFFFU);
```

`check_registers()` re-verifies every recorded register at 1 Hz from the tick handler
(`board/main.c:258`) and calls `fault_occurred(FAULT_REGISTER_DIVERGENT)` on any mismatch.

`HAL_ETH_MspInit`'s `HAL_GPIO_Init` touches **GPIOA** (PA1/PA2/PA7, plus PA8 for MCO1), **GPIOB**
(PB10/PB11) and **GPIOE** (PE0/PE5). All three banks are already in the register map from
`gpio_usb_init`, `richie_init`, the FDCAN pins, `led_init` and `gpio_spi_init`. The firmware would
fault **within one second** of bringing Ethernet up.

**Resolution (panda's system configuration wins):** configure every ETH pin through panda's helpers
so the register map stays in sync. Two supporting notes:

- Panda has no `OSPEEDR` helper, and RMII needs `VERY_HIGH`. Writing `GPIOx->OSPEEDR` directly is
  safe: panda only `register_set_bits` it for GPIOE pins 11-14 (`peripherals.h`), so every ETH pin
  falls outside the recorded mask.
- `HAL_RCC_MCOConfig` writes `RCC->CFGR` bits 18-24, while panda's
  `register_set(&RCC->CFGR, RCC_CFGR_SW_PLL1, 0x7U)` masks only bits 0-2 — that call is already
  safe and can stay.

---

## [CORRECTED] RAM budget

All figures are analytical; confirm against the real `.map` at the first successful link.
`sizeof(CANPacket_t)` is 72 (verified with the compiler against the local opendbc checkout the
build actually uses: 6-byte head + 64-byte data, `__attribute__((packed, aligned(4)))`).

**AXISRAM — 320K (327,680 B), currently 319,500 used, ~8.0K free.** The original plan claimed
"~150K+ free"; that was wrong by a factor of ~19, and the 14,848-byte lwIP heap overflows the
region at link time.

- `elems_rx_q[4096]` — `can_common.h:25` — 4096 × 72 = **294,912 B**
- `elems_isotp_tx_q[3]` + `elems_isotp_rx_q[3]` — `isotp.h:153,161` — 2 × 3 × 4,098 = **24,588 B**

**SRAM12 — 32K (32,768 B), currently 20,488 used, ~12.3K free.**

- `spi_buf_rx` + `spi_buf_tx` — `drivers/spi.h:7-8` — **8,192 B**
- `isotp_read_staging_buffer` **4,100 B** + `isotp_write_staging_buffer` **8,196 B** — `isotp.h:169-170`

**SRAM4 — 16K, currently 14,192 used, ~2.2K free.** `stm32h7/sound.h:5-8` puts four audio/mic DMA
buffers here. They are dead weight on Richie (`board_richie` sets `.set_amp_enabled =
unused_set_amp_enabled` and never calls `sound_init()`), but `sound.h` is included unconditionally
from `stm32h7/board.h` and `sound_tick()` is called unconditionally, so the linker keeps them.
**Not used by this plan** — left alone deliberately, and it remains the natural reclaim if more
DMA-reachable RAM is ever needed.

**ITCM — 64K**, holds `can_tx1_q` + `can_tx2_q` (59,904 B). Not DMA-reachable, irrelevant here.

**What lwIP needs**, against ~22.6K of total free DMA-reachable RAM — a ~6.5K shortfall that must
be closed regardless of placement:

- `lwip_ram_heap[MEM_SIZE + 512]` — **14,848 B**, DMA-read (TX pbufs)
- `memp_memory_RX_POOL_base` — `ETH_RX_BUFFER_CNT` × `sizeof(RxBuff_t)` (1,568 B), DMA-written
- `DMARxDscrTab` + `DMATxDscrTab` — 2 × 4 × 24 B + alignment ≈ **224 B**

### Resolution

1. **Reduce the CAN RX ring** in `board/drivers/can_common.h:20`:
   `#define CAN_RX_BUFFER_SIZE 3840U` (was `4096U`), freeing **18,432 B** of AXISRAM.
   Safe: `can_push`/`can_pop` use explicit compare-and-wrap (`if ((q->w_ptr + 1U) == q->fifo_size)`),
   not modulo, so a non-power-of-two size is fine, and the constant is referenced nowhere else.
2. **Heap stays in AXISRAM** (`.axisram.lwip_heap`) at the full `MEM_SIZE` of 14K → **~11.5K spare**.
3. **RX pool stays in SRAM12** (D2-local, on the ETH DMA's own bus matrix — the line-rate DMA-write
   path is where domain locality matters most) but at **`ETH_RX_BUFFER_CNT = 6`**, not 9. At 9 the
   pool is 14,112 B and overflows SRAM12 by ~2K; at 6 it is 9,408 B and the region ends with ~2.6K
   spare. 6 is still above `ETH_RX_DESC_CNT` (4), which ST requires.
4. **`PBUF_POOL_SIZE 0`** reclaims ~24.8K of DTCM `.bss` that the zero-copy design never uses.

Escape hatch: AXISRAM has ~11.5K spare after step 2, enough to host the RX pool at CNT=6 if SRAM12
turns out tighter than calculated. Prefer keeping it in SRAM12.

**Step 0 (do first): commit the working tree as the reviewable baseline** — the untracked
`generate_compile_commands.py`, the modified `SConstruct`, `board/lwip/app/lwip.c`,
`board/lwip/inc/lwip_app.h`, and the `ethernetif.c` / `lwipopts.h` edits — so every deletion and
rename below is reviewable as a diff.

---

## Phase 1 — Delete duplicates (use the panda codebase instead)

| Delete from `lwip/` | Replaced by | Notes |
|---|---|---|
| `app/main.c` | existing `app/lwip.c` | `MPU_Config`/`CPU_CACHE_Enable`/`SystemClock_Config`/`HAL_Init` all drop — panda's `clock_init()`/`early_init` own that. `Netif_Config`, the poll-loop body and the MCO1 config already live in `lwip.c`. |
| `app/stm32h7xx_it.c`, `inc/stm32h7xx_it.h` | `stm32h7/interrupt_handlers.h`, `sys/faults.h` | Kills the `OTG_HS_IRQHandler` and core-exception-handler symbol clashes, and the `SysTick_Handler`/`HAL_IncTick` time base we replace in Phase 2. |
| `drivers/usb/` (entire dir: `usb_core.c/.h`, `usb_debug.c/.h`, `usb_hw.h`) | `drivers/usb.h` + `stm32h7/llusb.h`; `print` from `drivers/uart.h` | Kills `usb_init`/`usb_irqhandler` clashes. lwip files get `extern void print(const char *a);` via the new port header. |
| `drivers/bsp/` (entire dir: `richie.c/.h`, `richie_errno.h`) | `drivers/led.h` + `boards/richie.h` | BSP LED driver duplicates panda's LED driver on the same PE2/3/4 pins. `BSP_MCO1_Init` content is already folded into `lwip.c`. Also resolves the `richie.h` filename collision. |
| `drivers/cmsis/` (entire dir, incl. `system_stm32h7xx.c`) | `stm32h7/inc/` via include path | CMSIS headers are byte-identical duplicates. The three objects `hal_rcc.c` needs from `system_stm32h7xx.c` (`SystemCoreClock`, `SystemD2Clock`, `D1CorePrescTable`) move into the port file (Phase 2). This also guarantees `SystemInit` never reappears (panda's startup deliberately doesn't call it). |
| `middlewares/libc/`: `memcpy.c`, `memset.c`, `memcpy-armv7m.S`, `strlen-armv7.S`, `strcmp-armv7.S`, `aeabi_memclr.c`, `aeabi_memcpy.c`, `aeabi_memmove.S`, `aeabi_memset.S` | `libc.h` (`memcpy`/`memset`/`memcmp`) | Direct symbol clashes (C and asm variants). GCC with panda's flags emits plain `memcpy`/`memset` calls, never `__aeabi_*`, so the aeabi shims go too. **Keep**: `memmove.c`, `strcmp.c`, `strlen.c`, `strncmp.c`, `stdlib_minimal.c` (provides `rand` for `LWIP_RAND`), `arm_asm.h`, `newlib_string_local.h` — panda has no implementations of these. |
| `middlewares/sys/` (`syscalls.c`, `sysmem.c`) | nothing (newlib scaffolding for `printf`/`malloc`, unused once `sprintf` is removed) | |
| `middlewares/lwip/system/OS/sys_arch.c` | nothing | Entire body is `#if !NO_SYS` + needs `cmsis_os.h` which doesn't exist here; `sys_now()` is already provided by `ethernetif.c`. |
| `inc/richie_conf.h`, `inc/utilities_conf.h` | nothing (unreferenced after BSP deletion) | |
| HAL sources except three: delete `stm32h7xx_hal.c`, `_cortex.c`, `_pwr_ex.c`, `_rcc_ex.c` | panda register-level drivers / the port file | **Keep only** `stm32h7xx_hal_eth.c`, `stm32h7xx_hal_gpio.c`, `stm32h7xx_hal_rcc.c` (`hal_rcc.c` is needed for `HAL_RCC_GetHCLKFreq` — MDIO clock divider — and `HAL_RCC_MCOConfig`; it reads the real prescalers from registers, so panda-configured clocks give the right 120 MHz answer). `hal_gpio.c` stays even though ETH pins now go through panda's helpers: `HAL_GPIO_WritePin`/`ReadPin` are still used for the PHY reset/power sequencing. |
| HAL headers: prune `stm32h7xx_hal_conf.h` module list to `HAL_MODULE_ENABLED`, `ETH`, `GPIO`, `RCC`, `FLASH` (FLASH header-only: `hal_rcc.c` uses `__HAL_FLASH_*` latency macros); then delete headers outside the resulting include closure (`_cortex.h`, `_dma*.h`, `_exti.h`, `_pwr*.h`, `Legacy/*eth*_legacy.h`) | | Keep: `stm32h7xx_hal.h`, `_def.h`, `_eth.h`, `_rcc.h` + `_rcc_ex.h` (included by `_rcc.h`), `_gpio.h` + `_gpio_ex.h`, `_flash.h` + `_flash_ex.h`, `Legacy/stm32_hal_legacy.h` (included by `_def.h`). Let compile errors finalize the exact closure — re-keep a header if demanded, but never add a fourth `.c`. |
| Optional cruft: `middlewares/lwip/src/apps/http/` (`fs.c`, `fsdata.c/.h`) | nothing — HTTP demo leftovers, unreferenced | Delete or leave unbuilt. `src/api/` (netconn/sockets) stays on disk but is **not compiled** (`NO_SYS=1`); keep for a future RTOS move. |

**Intentional non-dedup**: `stm32h7xx_hal_def.h` and `stm32h7xx_hal_gpio_ex.h` exist in *both*
`lwip/drivers/hal/inc/` and `stm32h7/inc/`. These are related (same ST lineage, different
snapshots), so they aren't renamed — but they also shouldn't be merged: the HAL `.c` files must see
their own matching versions. Handled purely by include-path ordering (Phase 4):
`lwip/drivers/hal/inc` comes **before** `stm32h7/inc` for lwip-library compilation only; the panda
TU never sees the lwip HAL dir.

---

## Phase 2 — New and existing wrapper files (all inside `lwip/`)

### `lwip/app/lwip.c` + `lwip/inc/lwip_app.h` — **already written**

**[CORRECTED]** The original plan proposed `app/eth_main.c` and `inc/eth_main.h`. Those names are
dropped: the working tree already has `app/lwip.c` and `inc/lwip_app.h` providing
`lwip_stack_init()`, `lwip_poll()`, `lwip_clock_init()` and the `lwip_ram_heap` symbol, plus a
runtime `lwip_dma_memory_init()` that range-checks every DMA buffer against its intended region —
a genuine improvement over the original design, since a mis-sectioned buffer returns `false`
instead of failing silently.

Two bugs to fix in that existing code:

1. `lwip_app.h` declares `void lwip_platform_clock_init(bool enable_phy_mco, bool
   enable_io_compensation);` but `lwip.c` defines `void lwip_clock_init(void)`. Reconcile to one
   name and signature; today any caller of the declared name gets an undefined symbol.
2. `lwip_clock_init()` calls `HAL_RCC_MCOConfig` but the PA8 pin setup is missing (it was in the
   deleted `BSP_MCO1_Init`). Add it using panda's helpers per the blocker section.

`lwip_dma_memory_init()`'s `SRAM12_START` / `AXISRAM_START` constants stay as-is under the Phase 1
resolution (heap in AXISRAM, descriptors and RX pool in SRAM12).

### `lwip/app/lwip_port.c` — everything the deleted HAL/CMSIS scaffolding used to provide

```c
#include "stm32h7xx_hal.h"
extern uint32_t microsecond_timer_get(void);   // panda, drivers/timers.h (TIM at 1MHz, wraps ~71.6min)

// wrap-safe ms tick; called only from the main-loop polling context.
// Works before enable_interrupts(): TIM2 free-runs, it is not interrupt-driven.
uint32_t HAL_GetTick(void) {
  static uint32_t last_us = 0, carry_us = 0, ms = 0;
  uint32_t now = microsecond_timer_get();
  carry_us += (now - last_us);      // unsigned math survives the 32-bit wrap
  last_us = now;
  ms += carry_us / 1000U;
  carry_us %= 1000U;
  return ms;
}
void HAL_Delay(uint32_t d) { uint32_t s = HAL_GetTick(); while ((HAL_GetTick() - s) < (d + 1U)); }

// referenced by linked-but-unused hal_rcc.c code (no --gc-sections)
uint32_t SystemCoreClock = 240000000U;
uint32_t SystemD2Clock  = 120000000U;
const uint8_t D1CorePrescTable[16] = {0,0,0,0,1,2,3,4,1,2,3,4,6,7,8,9};
uint32_t uwTickPrio = 0U;
HAL_StatusTypeDef HAL_InitTick(uint32_t prio) { (void)prio; return HAL_OK; }
```

Note the heap array itself already lives in `lwip.c`, not here.

### `lwip/inc/lwip_port.h`

Declares the panda symbols lwip code needs, so no lwip file has to include panda headers (which
would drag in the whole firmware world):

```c
extern void print(const char *a);                      // drivers/uart.h

// drivers/gpio.h — used instead of HAL_GPIO_Init so panda's register_map stays in sync.
// Constants mirror board/drivers/gpio.h; keep in step if that file ever changes.
#define PANDA_MODE_OUTPUT 1U
#define PANDA_MODE_ALTERNATE 2U
#define PANDA_PULL_NONE 0U
#define PANDA_OUTPUT_TYPE_PUSH_PULL 0U
extern void set_gpio_mode(GPIO_TypeDef *GPIO, unsigned int pin, unsigned int mode);
extern void set_gpio_output(GPIO_TypeDef *GPIO, unsigned int pin, bool enabled);
extern void set_gpio_output_type(GPIO_TypeDef *GPIO, unsigned int pin, unsigned int output_type);
extern void set_gpio_alternate(GPIO_TypeDef *GPIO, unsigned int pin, unsigned int mode);
extern void set_gpio_pullup(GPIO_TypeDef *GPIO, unsigned int pin, unsigned int mode);
```

---

## Phase 3 — Edits to existing `lwip/` files

1. **[CORRECTED] `inc/main.h`**: the original plan renamed this to `eth_main.h`. Instead just
   **delete it** — `lwip_app.h` is the public header, and `lwip.c` already carries its own
   `LWIP_IP_ADDR*`/`NETMASK`/`GATEWAY` macros. Update the `#include "main.h"` in `app_ethernet.c`
   to `"lwip_app.h"`. `lwip_app.h` must stay free of HAL/lwip includes so panda's TU (compiled
   `-Werror -Wstrict-prototypes`) can include it safely; it already is.
2. **`app/ethernetif.c`**:
   - Descriptor sections `.RxDescripSection`/`.TxDescripSection` → `.sram12.eth_desc` — **done**.
   - **Still outstanding**: move the `__attribute__((aligned(32), section(".sram12.eth_rx"))) extern
     u8_t memp_memory_RX_POOL_base[];` declaration (currently line 105) **above**
     `LWIP_MEMPOOL_DECLARE(RX_POOL, ...)` (currently line 95). GCC ignores a section attribute
     applied to an already-defined object, so as it stands the pool lands in DTCM where ETH DMA
     cannot reach it. Drop the IAR/MDK branches while you're there.
   - **[CORRECTED]** Set `ETH_RX_BUFFER_CNT` to **6** (from 9) — required, not a fallback. See the
     RAM budget.
   - **[NEW]** Replace the `HAL_GPIO_Init` calls in `HAL_ETH_MspInit` with panda's helpers:
     `set_gpio_alternate(GPIOx, pin, 11)` + `set_gpio_pullup(..., PANDA_PULL_NONE)` for the ten
     RMII pins (PA1/PA2/PA7, PB10/PB11, PC1/PC4/PC5, PG13/PG14), and
     `set_gpio_output` / `set_gpio_output_type` for PE5 (PHY power) and PE0 (PHY reset). Write
     `GPIOx->OSPEEDR` directly for `VERY_HIGH` speed. Keep the `__HAL_RCC_ETH1*_CLK_ENABLE()` calls
     and the `HAL_GPIO_WritePin` reset sequencing as-is.
   - Keep `#include <string.h>`; `memset`/`offsetof` calls resolve to panda's symbols at link.
3. **`app/app_ethernet.c`**: replace `#include "usb_debug.h"` and `#include "main.h"` with
   `"lwip_port.h"`/`"lwip_app.h"`; **delete all `BSP_LED_*` calls** (panda's main loop owns the
   LEDs; link status is visible via prints); **remove `sprintf`** — `ip4addr_ntoa()` already returns
   a printable static string:
   `print("IP: "); print(ip4addr_ntoa(netif_ip4_addr(netif))); print("\n");`.
4. **`system/arch/cc.h`**: remove `#include <stdio.h>`/`<stdlib.h>`/`<sys/time.h>`; replace the
   `printf`-based `LWIP_PLATFORM_ASSERT` with `extern void print(const char*);` + print-and-hang
   (drop `__FILE__`/`__LINE__` formatting); keep `LWIP_RAND() ((u32_t)rand())` and add
   **[CORRECTED]** `int rand(void);` — **not** `unsigned int`, which conflicts with the definition
   in `stdlib_minimal.c` and will not compile.
5. **`inc/lwipopts.h`**:
   - The `LWIP_RAM_HEAP_POINTER` symbol replacement is **done**.
   - **[NEW]** Add `#define PBUF_POOL_SIZE 0` (or 2 as a safety net) — the default of 16 costs
     ~24.8K of DTCM `.bss` that the zero-copy RX path never uses.
   - All other memp pools (TCP PCBs etc., never DMA-touched) stay in DTCM `.bss` — no change.
6. **`inc/stm32h7xx_hal_conf.h`**: trim modules per Phase 1; `HSE_VALUE` is already 25000000
   (matches panda's crystal per `clock.h`) — leave it.

---

## Phase 4 — Build integration

### New file `board/lwip/SConscript`

```python
Import('env')

lenv = env.Clone()
# vendor code: keep ABI/codegen flags, relax panda's strictness
lenv['CFLAGS'] = [f for f in lenv['CFLAGS'] if f not in ('-Werror', '-Wextra', '-Wstrict-prototypes', '-fmax-errors=1')]
lenv.Append(CFLAGS=['-Wno-unused-parameter', '-DUSE_HAL_DRIVER'])
lenv.Prepend(CPPPATH=[
  Dir('inc'),
  Dir('app'),
  Dir('drivers/hal/inc'),        # must precede board/stm32h7/inc (hal_def.h / hal_gpio_ex.h basename overlap)
  Dir('drivers/lan8742'),
  Dir('middlewares/lwip/src/include'),
  Dir('middlewares/lwip/system'),
])

sources = [
  'app/lwip.c', 'app/lwip_port.c', 'app/app_ethernet.c', 'app/ethernetif.c', 'app/tcp_echoserver.c',
  'drivers/lan8742/lan8742.c',
  'drivers/hal/src/stm32h7xx_hal_eth.c', 'drivers/hal/src/stm32h7xx_hal_gpio.c', 'drivers/hal/src/stm32h7xx_hal_rcc.c',
  'middlewares/libc/memmove.c', 'middlewares/libc/strcmp.c', 'middlewares/libc/strlen.c',
  'middlewares/libc/strncmp.c', 'middlewares/libc/stdlib_minimal.c',
] + [f'middlewares/lwip/src/core/{f}' for f in (
  'init.c', 'def.c', 'inet_chksum.c', 'ip.c', 'mem.c', 'memp.c', 'netif.c', 'pbuf.c',
  'stats.c', 'sys.c', 'tcp.c', 'tcp_in.c', 'tcp_out.c', 'timeouts.c', 'udp.c', 'raw.c', 'dns.c',
)] + [f'middlewares/lwip/src/core/ipv4/{f}' for f in (
  'autoip.c', 'dhcp.c', 'etharp.c', 'icmp.c', 'igmp.c', 'ip4.c', 'ip4_addr.c', 'ip4_frag.c',
)] + ['middlewares/lwip/src/netif/ethernet.c']

lwip_objs = [lenv.Object(s) for s in sources]
Return('lwip_objs')
```

(`raw.c`/`dns.c`/`autoip.c`/`igmp.c`/`stats.c` compile to empty objects with the current opts —
harmless, and they light up by flipping one lwipopt later. No two sources share a basename, which
matters because `OBJPREFIX` is a directory path — verify object placement on the first build.)

### `panda/SConscript` changes (~6 lines)

```python
def build_project(project_name, project, main, extra_flags, lwip=False):
  ...
  extra_objs = []
  if lwip:
    extra_objs = SConscript('./board/lwip/SConscript', exports={'env': env})
  main_elf = env.Program(f"{project_dir}/main.elf", [startup, main] + extra_objs, ...)
...
build_project("panda_h7", base_project_h7, "./board/main.c", ['-DRICHIE', '-DENABLE_ETHERNET'], lwip=True)
```

The cloned env inherits `OBJPREFIX` (objects land under `board/obj/panda_h7/`), the exact
`-mcpu/-mfpu/-mhard-float/-Os` codegen flags, `-DSTM32H735xx`, and `CPPPATH` root +
`board/stm32h7/inc` (the CMSIS dedup target). The bootstub env is untouched — lwip is linked into
the app image only. Jungle/body builds pass `lwip=False` implicitly.

---

## Phase 5 — Changes outside `lwip/` (the complete list)

1. `panda/SConscript` — the hook above.
2. **[NEW]** `board/drivers/can_common.h:20` — `CAN_RX_BUFFER_SIZE` `4096U` → `3840U`, one line.
   Without this the link fails with `region AXISRAM overflowed`. See the RAM budget.
3. `board/main.c` — guarded, ~8 lines total:
   - top: `#ifdef ENABLE_ETHERNET` → `#include "board/lwip/inc/lwip_app.h"`
   - **[CORRECTED]** call `lwip_stack_init();` **after `enable_interrupts()`**, not after
     `spi_init()`. `eth_init` blocks ~200 ms in the PHY reset, and `wd_state.last_ts` is stamped
     back at `simple_watchdog_init()` (`main.c:341`, 375 ms threshold) — placing it before
     `enable_interrupts()` means the first `simple_watchdog_kick()` sees ~200 ms of PHY reset plus
     up to 125 ms of tick latency. Under threshold, but with no margin. Alternatively trim the two
     `HAL_Delay(100)` calls in `HAL_ETH_MspInit` to ~30 ms.
   - `lwip_poll();` at the top of the `while (true)` loop **and once inside each LED-fade `for`
     loop** (otherwise polling gaps reach hundreds of ms and TCP crawls). Power-save `__WFI()`
     wakes at the 8 Hz tick, so polling continues there without changes.
4. Nothing else — no linker script, no libc.h, no driver changes.
5. **[CORRECTED]** ~~CI hygiene: add `board/lwip/` to MISRA suppressions~~ — **not needed**.
   `tests/misra/test_misra.sh` only scans `board/main.c`, and cppcheck already passes
   `--suppress=*:*inc/*`, which `board/lwip/inc/lwip_app.h` matches.

---

## Phase 6 — Bring-up & verification order

1. `scons` → iterate compile errors (mostly HAL header closure; re-keep any header it demands).
   Consider dropping `-fmax-errors=1` from the lwip env while iterating.
2. Link → resolve undefineds by adding the specific `middlewares/libc` file or port-file symbol;
   **if `__aeabi_uldivmod`/similar appears** (64-bit division somewhere in lwip), append `-lgcc` to
   the program's LINKFLAGS rather than importing more newlib files.
3. **[CORRECTED]** Inspect `main.elf` map against the RAM budget above:
   - `.axisram` ≤ 320K: CAN ring 276,480 + ISO-TP 24,588 + heap 14,848 = 315,916, ~11.5K spare
   - `.sram12` ≤ 32K: SPI 8,192 + ISO-TP staging 12,296 + descriptors ~224 + RX pool 9,408 = 30,120, ~2.6K spare
   - DTCM `.bss` should *shrink* relative to a naive integration thanks to `PBUF_POOL_SIZE 0`
   If SRAM12 still overflows, move the RX pool to `.axisram.eth_rx` (there is room) rather than
   dropping `ETH_RX_BUFFER_CNT` below 6.
4. Sanity-audit symbols: `arm-none-eabi-nm board/obj/panda_h7/*.o | grep ' U '` on lwip objects —
   `memcpy`/`memset`/`print`/`microsecond_timer_get`/`set_gpio_*` should be `U` (resolved from the
   main TU), and `nm main.elf` should show exactly one `main`, one `usb_init`, one `memcpy`.
5. Flash; confirm `lwip_dma_memory_init()` returns **true** (it validates every DMA buffer's region
   at runtime — a `false` means a section attribute didn't take). Watch the panda debug console for
   the DHCP/link prints; then link up → `ping <ip>` → `nc <ip> 7` (echo server binds port 7).
6. **[NEW]** Confirm no `FAULT_REGISTER_DIVERGENT` after ~5 s of uptime — that is the signal that
   the GPIO-helper conversion in `HAL_ETH_MspInit` was complete. `check_registers()` runs at 1 Hz.
7. Regression: CAN (with the smaller RX ring), USB enum, SPI (its buffers are SRAM12 neighbours of
   the new ETH data), heartbeat/safety ticks — confirm `lwip_stack_init` and per-loop `lwip_poll`
   don't trip `FAULT_HEARTBEAT_LOOP_WATCHDOG` (fed at 8 Hz from the tick interrupt, so it shouldn't).

---

## Open items needing schematic/hardware confirmation

- **PHY clocking**: the wrapper reproduces the demo's MCO1 = HSE/1 = 25 MHz on PA8. Confirm rev3
  actually feeds the LAN8742 XI from PA8 (vs. its own crystal) — if it has a crystal, delete the
  MCO block.
- **PE5 (RMII_PWR_EN) / PE0 (NRST) polarity**, and whether the commented-out `DOIP_EN` (PB4,
  active-low in `boards/richie.h`) must also be asserted to power the ETH/DoIP circuit — if so, add
  it to `lwip_stack_init` via panda's GPIO helpers (stays inside `lwip/`). Note PB4 **is** in use on
  Richie as a CAN2 transceiver enable, so this needs care.
- **MAC address** is hardcoded `02:00:00:00:00:00` in `stm32h7xx_hal_conf.h` — fine for bringup;
  later derive it from the panda serial/provisioning.
- DHCP is enabled with a 192.168.0.10 static fallback — decide the addressing scheme for the
  nRF9151/DoIP use case.
- **[NEW]** Is the CAN RX ring reduction (4096 → 3840 frames, ~6%) acceptable for the intended
  traffic? If not, the alternative reclaim is guarding `stm32h7/sound.h`'s SRAM4 buffers behind a
  board capability (14.2K, genuinely dead on Richie) and moving the heap there instead.

## Top risks

1. **H735 device-header compatibility**: panda's CMSIS snapshot vs. what this HAL revision expects —
   a compile error there is the signal; fallback is resurrecting `lwip/drivers/cmsis/inc` for
   lwip-internal use only.
2. **[CORRECTED] RAM budget**: no longer a single knob. AXISRAM needs the CAN ring reduction and
   SRAM12 needs `ETH_RX_BUFFER_CNT = 6`; both are hard link errors if missed, with the RX pool's
   AXISRAM relocation as the remaining slack.
3. **[NEW] Register-map divergence**: if any ETH pin is still configured with `HAL_GPIO_Init`, the
   board faults at 1 Hz with `FAULT_REGISTER_DIVERGENT`. Symptom is a healthy link that faults a
   second after `lwip_stack_init()`.

Everything else is mechanical.
