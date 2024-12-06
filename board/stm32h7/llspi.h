#if defined(ENABLE_SPI) || defined(BOOTSTUB)
// master -> panda DMA start
void llspi_mosi_dma(uint8_t *addr, int len) {
  // disable DMA + SPI
  register_clear_bits(&(SPI1->CFG1), SPI_CFG1_RXDMAEN);
  DMA2_Stream2->CR &= ~DMA_SxCR_EN;
  register_clear_bits(&(SPI1->CR1), SPI_CR1_SPE);

  // drain the bus
  while ((SPI1->SR & SPI_SR_RXP) != 0U) {
    volatile uint8_t dat = SPI1->RXDR;
    (void)dat;
  }

  // clear all pending
  SPI1->IFCR |= (0x1FFU << 3U);
  register_set(&(SPI1->IER), 0, 0x3FFU);

  // setup destination and length
  register_set(&(DMA2_Stream2->M0AR), (uint32_t)addr, 0xFFFFFFFFU);
  DMA2_Stream2->NDTR = len;

  // enable DMA + SPI
  DMA2_Stream2->CR |= DMA_SxCR_EN;
  register_set_bits(&(SPI1->CFG1), SPI_CFG1_RXDMAEN);
  register_set_bits(&(SPI1->CR1), SPI_CR1_SPE);
}

// panda -> master DMA start
void llspi_miso_dma(uint8_t *addr, int len) {
  // disable DMA + SPI
  DMA2_Stream3->CR &= ~DMA_SxCR_EN;
  register_clear_bits(&(SPI1->CFG1), SPI_CFG1_TXDMAEN);
  register_clear_bits(&(SPI1->CR1), SPI_CR1_SPE);

  // setup source and length
  register_set(&(DMA2_Stream3->M0AR), (uint32_t)addr, 0xFFFFFFFFU);
  DMA2_Stream3->NDTR = len;

  // clear under-run while we were reading
  SPI1->IFCR |= (0x1FFU << 3U);

  // setup interrupt on TXC
  register_set(&(SPI1->IER), (1U << SPI_IER_EOTIE_Pos), 0x3FFU);

  // enable DMA + SPI
  register_set_bits(&(SPI1->CFG1), SPI_CFG1_TXDMAEN);
  DMA2_Stream3->CR |= DMA_SxCR_EN;
  register_set_bits(&(SPI1->CR1), SPI_CR1_SPE);
}

static bool spi_tx_dma_done = false;
// master -> panda DMA finished
static void DMA2_Stream2_IRQ_Handler(void) {
  // Clear interrupt flag
  DMA2->LIFCR = DMA_LIFCR_CTCIF2;

  spi_rx_done();
}

// panda -> master DMA finished
static void DMA2_Stream3_IRQ_Handler(void) {
  ENTER_CRITICAL();

  DMA2->LIFCR = DMA_LIFCR_CTCIF3;
  spi_tx_dma_done = true;

  EXIT_CRITICAL();
}

// panda TX finished
static void SPI1_IRQ_Handler(void) {
  // clear flag
  SPI1->IFCR |= (0x1FFU << 3U);

  if (spi_tx_dma_done && ((SPI1->SR & SPI_SR_TXC) != 0U)) {
    spi_tx_dma_done = false;
    spi_tx_done(false);
  }
}


void llspi_init(void) {
  REGISTER_INTERRUPT(SPI1_IRQn, SPI1_IRQ_Handler, (SPI_IRQ_RATE * 2U), FAULT_INTERRUPT_RATE_SPI)
  REGISTER_INTERRUPT(DMA2_Stream2_IRQn, DMA2_Stream2_IRQ_Handler, SPI_IRQ_RATE, FAULT_INTERRUPT_RATE_SPI_DMA)
  REGISTER_INTERRUPT(DMA2_Stream3_IRQn, DMA2_Stream3_IRQ_Handler, SPI_IRQ_RATE, FAULT_INTERRUPT_RATE_SPI_DMA)

  // Setup MOSI DMA
  register_set(&(DMAMUX1_Channel10->CCR), 37U, 0xFFFFFFFFU);
  register_set(&(DMA2_Stream2->CR), (DMA_SxCR_MINC | DMA_SxCR_TCIE), 0x1E077EFEU);
  register_set(&(DMA2_Stream2->PAR), (uint32_t)&(SPI1->RXDR), 0xFFFFFFFFU);

  // Setup MISO DMA, memory -> peripheral
  register_set(&(DMAMUX1_Channel11->CCR), 38U, 0xFFFFFFFFU);
  register_set(&(DMA2_Stream3->CR), (DMA_SxCR_MINC | DMA_SxCR_DIR_0 | DMA_SxCR_TCIE), 0x1E077EFEU);
  register_set(&(DMA2_Stream3->PAR), (uint32_t)&(SPI1->TXDR), 0xFFFFFFFFU);

  // Enable SPI
  register_set(&(SPI1->IER), 0, 0x3FFU);
  register_set(&(SPI1->CFG1), (7U << SPI_CFG1_DSIZE_Pos), SPI_CFG1_DSIZE_Msk);
  register_set(&(SPI1->UDRDR), 0xcd, 0xFFFFU);  // set under-run value for debugging
  register_set(&(SPI1->CR1), SPI_CR1_SPE, 0xFFFFU);
  register_set(&(SPI1->CR2), 0, 0xFFFFU);

  NVIC_EnableIRQ(DMA2_Stream2_IRQn);
  NVIC_EnableIRQ(DMA2_Stream3_IRQn);
  NVIC_EnableIRQ(SPI1_IRQn);
}
#endif
