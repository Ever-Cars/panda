// master -> panda DMA start
void llspi_mosi_dma(uint8_t *addr, int len) {
  // disable DMA + SPI
  register_clear_bits(&(SPI4->CFG1), SPI_CFG1_RXDMAEN);
  DMA2_Stream2->CR &= ~DMA_SxCR_EN;
  register_clear_bits(&(SPI4->CR1), SPI_CR1_SPE);

  // drain the bus
  while ((SPI4->SR & SPI_SR_RXP) != 0U) {
    volatile uint8_t dat = SPI4->RXDR;
    (void)dat;
  }

  // clear all pending
  SPI4->IFCR |= (0x1FFU << 3U);
  register_set(&(SPI4->IER), 0, 0x3FFU);

  // setup destination and length
  register_set(&(DMA2_Stream2->M0AR), (uint32_t)addr, 0xFFFFFFFFU);
  DMA2_Stream2->NDTR = len;

  // enable DMA + SPI
  DMA2_Stream2->CR |= DMA_SxCR_EN;
  register_set_bits(&(SPI4->CFG1), SPI_CFG1_RXDMAEN);
  register_set_bits(&(SPI4->CR1), SPI_CR1_SPE);
}

// panda -> master DMA start
void llspi_miso_dma(uint8_t *addr, int len) {
  // disable DMA + SPI
  DMA2_Stream3->CR &= ~DMA_SxCR_EN;
  register_clear_bits(&(SPI4->CFG1), SPI_CFG1_TXDMAEN);
  register_clear_bits(&(SPI4->CR1), SPI_CR1_SPE);

  // setup source and length
  register_set(&(DMA2_Stream3->M0AR), (uint32_t)addr, 0xFFFFFFFFU);
  DMA2_Stream3->NDTR = len;

  // clear under-run while we were reading
  SPI4->IFCR |= (0x1FFU << 3U);

  // setup interrupt on TXC
  register_set(&(SPI4->IER), (1U << SPI_IER_EOTIE_Pos), 0x3FFU);

  // enable DMA + SPI
  register_set_bits(&(SPI4->CFG1), SPI_CFG1_TXDMAEN);
  DMA2_Stream3->CR |= DMA_SxCR_EN;
  register_set_bits(&(SPI4->CR1), SPI_CR1_SPE);
}

static bool spi_tx_dma_done = false;
// master -> panda DMA finished
static void DMA2_Stream2_IRQ_Handler(void) {
#ifdef RICHIE
  set_gpio_output(GPIOA, 3, true);
#endif
  // Clear interrupt flag
  DMA2->LIFCR = DMA_LIFCR_CTCIF2;

  if ((SPI4->SR & SPI_SR_TXC) == 0U) {
    // TX FIFO is not empty, restart transfer
    spi_tx_done(true);
  } else {
    // all data received
    spi_rx_done();
  }
#ifdef RICHIE
  set_gpio_output(GPIOA, 3, false);
#endif
}

// panda -> master DMA finished
static void DMA2_Stream3_IRQ_Handler(void) {
  ENTER_CRITICAL();

  DMA2->LIFCR = DMA_LIFCR_CTCIF3;
  spi_tx_dma_done = true;

  EXIT_CRITICAL();
}

// panda TX finished
static void SPI4_IRQ_Handler(void) {
#ifdef RICHIE
  set_gpio_output(GPIOA, 3, true);
#endif
  // clear flag
  SPI4->IFCR |= (0x1FFU << 3U);

  if (spi_tx_dma_done && ((SPI4->SR & SPI_SR_TXC) != 0U)) {
    spi_tx_dma_done = false;
    spi_tx_done(false);
  }
#ifdef RICHIE
  set_gpio_output(GPIOA, 3, false);
#endif
}


void llspi_init(void) {
  REGISTER_INTERRUPT(SPI4_IRQn, SPI4_IRQ_Handler, (SPI_IRQ_RATE * 2U), FAULT_INTERRUPT_RATE_SPI)
  REGISTER_INTERRUPT(DMA2_Stream2_IRQn, DMA2_Stream2_IRQ_Handler, SPI_IRQ_RATE, FAULT_INTERRUPT_RATE_SPI_DMA)
  REGISTER_INTERRUPT(DMA2_Stream3_IRQn, DMA2_Stream3_IRQ_Handler, SPI_IRQ_RATE, FAULT_INTERRUPT_RATE_SPI_DMA)

  // Setup MOSI DMA
  register_set(&(DMAMUX1_Channel10->CCR), 83U, 0xFFFFFFFFU);
  register_set(&(DMA2_Stream2->CR), (DMA_SxCR_MINC | DMA_SxCR_TCIE), 0x1E077EFEU);
  register_set(&(DMA2_Stream2->PAR), (uint32_t)&(SPI4->RXDR), 0xFFFFFFFFU);

  // Setup MISO DMA, memory -> peripheral
  register_set(&(DMAMUX1_Channel11->CCR), 84U, 0xFFFFFFFFU);
  register_set(&(DMA2_Stream3->CR), (DMA_SxCR_MINC | DMA_SxCR_DIR_0 | DMA_SxCR_TCIE), 0x1E077EFEU);
  register_set(&(DMA2_Stream3->PAR), (uint32_t)&(SPI4->TXDR), 0xFFFFFFFFU);
  register_set(&(DMA2_Stream3->FCR), (DMA_SxFCR_DMDIS | DMA_SxFCR_FTH_0), 0x87U); // Set FIFO mode, 1/2 full threshold

  // Enable SPI
  register_set(&(SPI4->IER), 0, 0x3FFU);
  register_set(&(SPI4->CFG1), (SPI_CFG1_DSIZE_0 | SPI_CFG1_DSIZE_1 | SPI_CFG1_DSIZE_2 | SPI_CFG1_UDRDET_0), 0x181FU);
  register_set(&(SPI4->UDRDR), 0xcd, 0xFFFFU);  // set under-run value for debugging
  register_set(&(SPI4->CR1), SPI_CR1_SPE, 0xFFFFU);
  register_set(&(SPI4->CR2), 0, 0xFFFFU);

  NVIC_EnableIRQ(DMA2_Stream2_IRQn);
  NVIC_EnableIRQ(DMA2_Stream3_IRQn);
  NVIC_EnableIRQ(SPI4_IRQn);
  // Set priorities before FDCAN to avoid a noisy CAN blocking SPI
  NVIC_SetPriority(DMA2_Stream2_IRQn, 23);
  NVIC_SetPriority(DMA2_Stream3_IRQn, 24);
  NVIC_SetPriority(SPI4_IRQn, 25);
}
