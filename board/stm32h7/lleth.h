#pragma once

// STM32H7 Ethernet MAC/MTL/DMA driver (RMII, polled, single DMA channel).
// Normal descriptors, one buffer per frame. Descriptors and buffers live in
// D2 SRAM1/2 where the Ethernet DMA can reach them. The D-cache is not
// enabled on panda, so no cache maintenance is needed.

#define LLETH_RX_DESC_CNT 4U
#define LLETH_TX_DESC_CNT 3U  // one slot always stays free, see lleth_tx_buffer_get()
#define LLETH_BUF_SIZE 1536U  // multiple of 4, fits a 1522 byte frame

// MACCR bits this driver controls; the rest (incl. read-only bit 15) are left alone
#define LLETH_MACCR_MASK (ETH_MACCR_ACS | ETH_MACCR_CST | ETH_MACCR_FES | ETH_MACCR_DM | ETH_MACCR_TE | ETH_MACCR_RE)

#define LLETH_HCLK_MHZ (CORE_FREQ / 2U)  // ETH sits on AHB1, see stm32h7/clock.h
#define LLETH_TIMEOUT_US 10000U

// Descriptor bits (RM0468 "Ethernet DMA descriptors")
#define LLETH_DES3_OWN (1UL << 31)
#define LLETH_DES3_CTXT (1UL << 30)
#define LLETH_DES3_FD (1UL << 29)
#define LLETH_DES3_LD (1UL << 28)
#define LLETH_RDES3_IOC (1UL << 30)  // read format: raise RI when this buffer completes
#define LLETH_RDES3_BUF1V (1UL << 24)
#define LLETH_RDES3_ES (1UL << 15)
#define LLETH_RDES3_PL_MASK 0x7FFFU
#define LLETH_TDES3_CIC_FULL (3UL << 16)  // insert IP header and payload checksums
#define LLETH_TDES2_B1L_MASK 0x3FFFU

#define LLETH_MDIO_GOC_READ (3UL << 2)  // MACMDIOAR GOC field

typedef struct {
  volatile uint32_t des0;
  volatile uint32_t des1;
  volatile uint32_t des2;
  volatile uint32_t des3;
} lleth_desc_t;

__attribute__((section(".sram12"), aligned(32))) static lleth_desc_t lleth_rx_desc[LLETH_RX_DESC_CNT];
__attribute__((section(".sram12"), aligned(32))) static lleth_desc_t lleth_tx_desc[LLETH_TX_DESC_CNT];
__attribute__((section(".sram12"), aligned(32))) static uint8_t lleth_rx_buf[LLETH_RX_DESC_CNT][LLETH_BUF_SIZE];
__attribute__((section(".sram12"), aligned(32))) static uint8_t lleth_tx_buf[LLETH_TX_DESC_CNT][LLETH_BUF_SIZE];

static uint32_t lleth_rx_idx = 0U;
static uint32_t lleth_tx_idx = 0U;

static bool lleth_wait_clear(volatile const uint32_t *reg, uint32_t mask) {
  uint32_t start = microsecond_timer_get();
  while (((*reg & mask) != 0U) && (get_ts_elapsed(microsecond_timer_get(), start) <= LLETH_TIMEOUT_US)) {}

  // Check the bits once more: they may have cleared just as the timeout expired
  return (*reg & mask) == 0U;
}

static uint32_t lleth_desc_addr(const volatile lleth_desc_t *desc) {
  return (uint32_t)desc;
}

static void lleth_rx_desc_arm(uint32_t idx) {
  lleth_rx_desc[idx].des0 = (uint32_t)&lleth_rx_buf[idx][0];
  lleth_rx_desc[idx].des1 = 0U;
  lleth_rx_desc[idx].des2 = 0U;
  __DMB();  // buffer address must be visible before ownership moves to the DMA
  lleth_rx_desc[idx].des3 = LLETH_DES3_OWN | LLETH_RDES3_IOC | LLETH_RDES3_BUF1V;
}

static void lleth_rings_init(void) {
  for (uint32_t i = 0U; i < LLETH_TX_DESC_CNT; i++) {
    lleth_tx_desc[i].des0 = 0U;
    lleth_tx_desc[i].des1 = 0U;
    lleth_tx_desc[i].des2 = 0U;
    lleth_tx_desc[i].des3 = 0U;
  }

  for (uint32_t i = 0U; i < LLETH_RX_DESC_CNT; i++) {
    lleth_rx_desc_arm(i);
  }

  lleth_rx_idx = 0U;
  lleth_tx_idx = 0U;
  __DSB();

  // Ring base addresses and lengths can only change while the DMA is stopped
  ETH->DMACTDLAR = lleth_desc_addr(&lleth_tx_desc[0]);
  ETH->DMACTDRLR = LLETH_TX_DESC_CNT - 1U;
  ETH->DMACTDTPR = lleth_desc_addr(&lleth_tx_desc[0]);  // tail == head: nothing to send
  ETH->DMACRDLAR = lleth_desc_addr(&lleth_rx_desc[0]);
  ETH->DMACRDRLR = LLETH_RX_DESC_CNT - 1U;
  ETH->DMACRDTPR = lleth_desc_addr(&lleth_rx_desc[LLETH_RX_DESC_CNT - 1U]);
}

// The receive interrupt only wakes the main loop out of WFI (power save);
// frames are processed there. It masks itself until lleth_rx_irq_arm(), so
// it fires at most once per main loop pass however busy the network is.
static void lleth_irq_handler(void) {
  ETH->DMACIER = ETH_DMACIER_NIE;
  ETH->DMACSR = ETH_DMACSR_NIS | ETH_DMACSR_RI;
}

void lleth_rx_irq_arm(void) {
  ETH->DMACIER = ETH_DMACIER_NIE | ETH_DMACIER_RIE;
}

// Select RMII, clock and reset the MAC, then configure MAC/MTL/DMA. The PHY
// must already be out of reset and driving REF_CLK, or the DMA reset hangs.
bool lleth_init(const uint8_t mac[ETH_MAC_LEN]) {
  bool ret;

  // The PHY interface can only be selected while the MAC is held in reset
  register_set_bits(&(RCC->AHB1RSTR), RCC_AHB1RSTR_ETH1MACRST);
  register_set(&(SYSCFG->PMCR), SYSCFG_PMCR_EPIS_SEL_2, SYSCFG_PMCR_EPIS_SEL);  // 100: RMII
  register_set_bits(&(RCC->AHB1ENR), RCC_AHB1ENR_ETH1MACEN | RCC_AHB1ENR_ETH1TXEN | RCC_AHB1ENR_ETH1RXEN);
  register_clear_bits(&(RCC->AHB1RSTR), RCC_AHB1RSTR_ETH1MACRST);

  ETH->DMAMR = ETH_DMAMR_SWR;
  ret = lleth_wait_clear(&(ETH->DMAMR), ETH_DMAMR_SWR);

  if (ret) {
    // The MMC statistics counters raise ETH interrupts at half/full scale that
    // nothing here would clear: mask them all (as ST's HAL does)
    register_set_bits(&(ETH->MMCRIMR), ETH_MMCRIMR_RXLPITRCIM | ETH_MMCRIMR_RXLPIUSCIM | ETH_MMCRIMR_RXUCGPIM |
                                       ETH_MMCRIMR_RXALGNERPIM | ETH_MMCRIMR_RXCRCERPIM);
    register_set_bits(&(ETH->MMCTIMR), ETH_MMCTIMR_TXLPITRCIM | ETH_MMCTIMR_TXLPIUSCIM | ETH_MMCTIMR_TXGPKTIM |
                                       ETH_MMCTIMR_TXMCOLGPIM | ETH_MMCTIMR_TXSCOLGPIM);

    // MDC = HCLK / 62 (HCLK in 100-150 MHz), 1 us tick for the MAC
    ETH->MACMDIOAR = ETH_MACMDIOAR_CR_DIV62;
    register_set(&(ETH->MAC1USTCR), LLETH_HCLK_MHZ - 1U, ETH_MAC1USTCR_TIC1USCNTR);

    // Strip FCS on receive. Speed, duplex and RE/TE are set in lleth_start().
    register_set(&(ETH->MACCR), ETH_MACCR_ACS | ETH_MACCR_CST, LLETH_MACCR_MASK);

    // Unicast to us, broadcast and all multicast (IPv6 neighbor discovery)
    register_set(&(ETH->MACPFR), ETH_MACPFR_PM, 0xFFFFFFFFU);
    register_set(&(ETH->MACA0HR), ((uint32_t)mac[5] << 8) | mac[4], 0xFFFFU);
    register_set(&(ETH->MACA0LR), ((uint32_t)mac[3] << 24) | ((uint32_t)mac[2] << 16) | ((uint32_t)mac[1] << 8) | mac[0], 0xFFFFFFFFU);

    // Store-and-forward (required for TX checksum insertion). Queue sizes are
    // fixed at 2 KB on this part (read-only TQS/RQS).
    register_set_bits(&(ETH->MTLTQOMR), ETH_MTLTQOMR_TSF);
    register_set_bits(&(ETH->MTLRQOMR), ETH_MTLRQOMR_RSF);

    // Address-aligned fixed bursts of 32 beats, 16 byte descriptors (no skip)
    register_set(&(ETH->DMASBMR), ETH_DMASBMR_AAL | ETH_DMASBMR_FB, 0xFFFFFFFFU);
    register_set(&(ETH->DMACCR), 0U, ETH_DMACCR_DSL);
    register_set(&(ETH->DMACTCR), 32UL << ETH_DMACTCR_TPBL_Pos, ETH_DMACTCR_TPBL);
    register_set(&(ETH->DMACRCR), (32UL << ETH_DMACRCR_RPBL_Pos) | (LLETH_BUF_SIZE << ETH_DMACRCR_RBSZ_Pos),
                 ETH_DMACRCR_RPBL | ETH_DMACRCR_RBSZ);

    REGISTER_INTERRUPT(ETH_IRQn, lleth_irq_handler, 20000U, FAULT_INTERRUPT_RATE_ETH)
    NVIC_EnableIRQ(ETH_IRQn);
  }

  return ret;
}

void lleth_start(bool speed_100m, bool full_duplex) {
  lleth_rings_init();

  uint32_t maccr = ETH_MACCR_ACS | ETH_MACCR_CST;
  if (speed_100m) {
    maccr |= ETH_MACCR_FES;
  }
  if (full_duplex) {
    maccr |= ETH_MACCR_DM;
  }
  register_set(&(ETH->MACCR), maccr, LLETH_MACCR_MASK);

  // Clear stale status, flush the TX queue, then start DMA before the MAC
  ETH->DMACSR = 0xFFFFFFFFU;
  ETH->MTLTQOMR |= ETH_MTLTQOMR_FTQ;
  (void)lleth_wait_clear(&(ETH->MTLTQOMR), ETH_MTLTQOMR_FTQ);
  ETH->DMACTCR |= ETH_DMACTCR_ST;
  ETH->DMACRCR |= ETH_DMACRCR_SR;
  register_set(&(ETH->MACCR), maccr | ETH_MACCR_TE | ETH_MACCR_RE, LLETH_MACCR_MASK);
}

// Stop DMA and MAC. Returns false if a DMA engine did not reach the stopped
// state; the rings must then not be reprogrammed without a full lleth_init().
bool lleth_stop(void) {
  ETH->DMACTCR &= ~ETH_DMACTCR_ST;
  bool ok = lleth_wait_clear(&(ETH->DMADSR), ETH_DMADSR_TPS);

  register_set(&(ETH->MACCR), ETH_MACCR_ACS | ETH_MACCR_CST, LLETH_MACCR_MASK);

  ETH->DMACRCR &= ~ETH_DMACRCR_SR;
  ok = lleth_wait_clear(&(ETH->DMADSR), ETH_DMADSR_RPS) && ok;

  return ok;
}

// Returns the DMA channel status and clears it (write-1-to-clear).
uint32_t lleth_dma_status(void) {
  uint32_t status = ETH->DMACSR;
  ETH->DMACSR = status;
  return status;
}

bool lleth_mdio_read(uint32_t phy, uint32_t phy_reg, uint16_t *value) {
  bool ret = lleth_wait_clear(&(ETH->MACMDIOAR), ETH_MACMDIOAR_MB);
  if (ret) {
    ETH->MACMDIOAR = (phy << ETH_MACMDIOAR_PA_Pos) | (phy_reg << ETH_MACMDIOAR_RDA_Pos) |
                     ETH_MACMDIOAR_CR_DIV62 | LLETH_MDIO_GOC_READ | ETH_MACMDIOAR_MB;
    ret = lleth_wait_clear(&(ETH->MACMDIOAR), ETH_MACMDIOAR_MB);
  }

  if (ret) {
    *value = (uint16_t)(ETH->MACMDIODR & 0xFFFFU);
  }

  return ret;
}

// Give the current RX descriptor back to the DMA and resume reception.
void lleth_rx_frame_release(void) {
  lleth_rx_desc_arm(lleth_rx_idx);
  __DSB();
  ETH->DMACSR = ETH_DMACSR_RBU;  // receive buffer unavailable: tail write resumes
  ETH->DMACRDTPR = lleth_desc_addr(&lleth_rx_desc[lleth_rx_idx]);
  lleth_rx_idx = (lleth_rx_idx + 1U) % LLETH_RX_DESC_CNT;
}

// Next complete, error-free received frame, or NULL. Bad descriptors are
// recycled here and counted in *dropped.
const uint8_t *lleth_rx_frame_get(uint16_t *len, uint32_t *dropped) {
  const uint8_t *frame = NULL;
  bool done = false;
  for (uint32_t i = 0U; (i < LLETH_RX_DESC_CNT) && !done; i++) {
    uint32_t des3 = lleth_rx_desc[lleth_rx_idx].des3;
    if ((des3 & LLETH_DES3_OWN) != 0U) {
      done = true;  // still owned by the DMA: nothing (more) received
    } else {
      // A whole frame in one buffer (first and last segment), no context descriptor, no errors
      bool ok = ((des3 & (LLETH_DES3_FD | LLETH_DES3_LD)) == (LLETH_DES3_FD | LLETH_DES3_LD)) &&
                ((des3 & (LLETH_DES3_CTXT | LLETH_RDES3_ES)) == 0U);
      uint16_t length = (uint16_t)(des3 & LLETH_RDES3_PL_MASK);
      if (ok && (length > 0U) && (length <= LLETH_BUF_SIZE)) {
        *len = length;
        frame = &lleth_rx_buf[lleth_rx_idx][0];
        done = true;
      } else {
        (*dropped)++;
        lleth_rx_frame_release();
      }
    }
  }

  return frame;
}

// Free TX buffer, or NULL while the ring is full. The descriptor after the one
// handed out must also be free: the tail pointer may never catch up with an
// owned descriptor, or the DMA could not tell a full ring from an empty one.
uint8_t *lleth_tx_buffer_get(void) {
  uint8_t *buf = NULL;
  uint32_t next = (lleth_tx_idx + 1U) % LLETH_TX_DESC_CNT;
  if (((lleth_tx_desc[lleth_tx_idx].des3 & LLETH_DES3_OWN) == 0U) &&
      ((lleth_tx_desc[next].des3 & LLETH_DES3_OWN) == 0U)) {
    buf = &lleth_tx_buf[lleth_tx_idx][0];
  }
  return buf;
}

// Queue the frame in the buffer from lleth_tx_buffer_get(). The MAC pads short
// frames, appends the FCS and inserts IPv4/TCP/UDP/ICMP checksums.
void lleth_tx_frame_send(uint16_t len) {
  lleth_desc_t *desc = &lleth_tx_desc[lleth_tx_idx];
  desc->des0 = (uint32_t)&lleth_tx_buf[lleth_tx_idx][0];
  desc->des1 = 0U;
  desc->des2 = (uint32_t)len & LLETH_TDES2_B1L_MASK;
  __DMB();
  desc->des3 = LLETH_DES3_OWN | LLETH_DES3_FD | LLETH_DES3_LD | LLETH_TDES3_CIC_FULL | len;
  __DSB();

  lleth_tx_idx = (lleth_tx_idx + 1U) % LLETH_TX_DESC_CNT;
  ETH->DMACSR = ETH_DMACSR_TBU;
  ETH->DMACTDTPR = lleth_desc_addr(&lleth_tx_desc[lleth_tx_idx]);
}
