#include <rf430frl152h.h>
#include <stdint.h>
#include "rom.h"

// --- Stałe i dane ---
__attribute__((section(".firmwarecontrolbyte")))
const uint8_t firmwarecontrolbyte = 0x7F;

__attribute__((section(".earlyrom")))
const uint8_t earlydata[] = {
    0x3d, 0xc7, 0x88, 0x13, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x62, 0xc2, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// --- Funkcje pomocnicze ---
uint16_t crc_calculate(uint16_t *src, uint16_t wordcount)
{
  CRCINIRES = 0xFFFF;
  for (uint16_t count = 0; count < wordcount; count++)
    CRCDI = src[count];
  return CRCINIRES;
}

void crc_fixup()
{
  SYSCNF_H &= 0xF0;
  *((uint16_t *)(0xF860 - 8)) = crc_calculate((uint16_t *)(0xF862 - 8), 0xB);
  *((uint16_t *)(0xF878 - 8)) = crc_calculate((uint16_t *)(0xF87A - 8), 0x93);
  SYSCNF_H |= 0x0F;
}

// Komenda A1: ustawienie P1.5 na wysoki ---
uint16_t __attribute__((noinline)) cmd_a1()
{
  P1SEL0 &= ~BIT5;
  P1SEL1 &= ~BIT5;
  P1DIR |= BIT5;
  P1OUT |= BIT5;

  RF13MTXF_L = 2;
  RF13MTXF = 0xA1;
  RF13MTXF = 0x0001;
  return 0;
}

// Komenda A4: ustawienie P1.5 na niski ---
uint16_t __attribute__((noinline)) cmd_a4()
{
  P1SEL0 &= ~BIT5;
  P1SEL1 &= ~BIT5;
  P1DIR |= BIT5;
  P1OUT &= ~BIT5;

  RF13MTXF_L = 2;
  RF13MTXF = 0xA4;
  RF13MTXF = 0x0000;
  return 0;
}

// Komenda A3: konfiguracja SD14 ---
uint16_t __attribute__((noinline)) cmd_a3()
{

  P1SEL0 &= ~BIT5;
  P1SEL1 &= ~BIT5;
  P1DIR |= BIT5;
  P1OUT |= BIT5;

  SD14CTL0 = 0;
  SD14CTL1 = SD14INCH__ADC0 | SD14INTDLY__1st | SD14RATE__CIC64 | SD14FILT__CIC | SD14GAIN__2 | SD14UNI;

  __delay_cycles(80000);
  P1OUT &= ~BIT5;

  RF13MTXF_L = 2;
  RF13MTXF = 0xA3;
  RF13MTXF = SD14CTL1;

  return 0;
}

// Komenda A2: pomiar z aktualną konfiguracją ---
uint16_t __attribute__((noinline)) cmd_a2()
{
  uint16_t result = 0xFFFF;

  if ((SD14CTL1 & 0x0007) != SD14INCH__ADC0)
  {
    RF13MTXF_L = 2;
    RF13MTXF = 0xA2;
    RF13MTXF = 0xFFFF;
    return 0;
  }

  SD14CTL0 &= ~SD14IFG;
  SD14CTL0 |= SD14EN | SD14SSEL__ACLK | VIRTGND;
  SD14CTL0 |= SD14SC;

  while (!(SD14CTL0 & SD14IFG))
    ;

  result = SD14MEM0;

  RF13MTXF_L = 2;
  RF13MTXF = 0xA2;
  RF13MTXF = result;
  return 0;
}

// Główna pętla
int main(void)
{
  while (1)
  {
    crc_fixup();
    __bis_SR_register(LPM3_bits | GIE);
    return 0;
  }
}

// Patchtable
__attribute__((section(".rompatch")))
const uint16_t patchtable[0x12] = {
    0xCECE, 0xCECE, 0xCECE,
    0xCECE, 0xCECE, 0xCECE,
    0xCECE, 0xCECE, 0xCECE,
    (uint16_t)cmd_a4, 0x00A4,
    (uint16_t)cmd_a3, 0x00A3,
    (uint16_t)cmd_a2, 0x00A2,
    (uint16_t)cmd_a1, 0x00A1,
    0xCECE};
