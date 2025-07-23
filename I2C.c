#include <rf430frl152h.h>
#include <stdint.h>
#include "rom.h"

#define FDC1004_ADDR 0x50          // Adres I2C układu FDC1004
#define FDC1004_REG_MEAS1_MSB 0xFF // Rejestr do odczytu (tu: testowy, niepoprawny)

// Bufory do przechowywania wyniku i flagi zakończenia I2C
volatile uint16_t i2c_result = 0xFFFF;
volatile uint8_t i2c_done = 0;

// Sekcja wymagana przez ROM bootloadera RF430
__attribute__((section(".firmwarecontrolbyte")))
const uint8_t firmwarecontrolbyte = 0x7F;

// Sekcja z danymi konfiguracyjnymi dla stosu NFC
__attribute__((section(".earlyrom")))
const uint8_t earlydata[] = {
    0x3d, 0xc7, 0x88, 0x13, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x62, 0xc2, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// Obliczanie CRC dla pamięci ROM, wymagane przez TI ROM patch
uint16_t crc_calculate(uint16_t *src, uint16_t wordcount)
{
  CRCINIRES = 0xFFFF;
  for (uint16_t count = 0; count < wordcount; count++)
    CRCDI = src[count];
  return CRCINIRES;
}

void crc_fixup()
{
  // Obliczenie CRC dla dwóch bloków konfiguracyjnych
  SYSCNF_H &= 0xF0;
  *((uint16_t *)(0xF860 - 8)) = crc_calculate((uint16_t *)(0xF862 - 8), 0xB);
  *((uint16_t *)(0xF878 - 8)) = crc_calculate((uint16_t *)(0xF87A - 8), 0x93);
  SYSCNF_H |= 0x0F;
}

// Obsługa diody debugowej na P1.5
void led_on()
{
  P1SEL0 &= ~BIT5;
  P1SEL1 &= ~BIT5;
  P1DIR |= BIT5;
  P1OUT |= BIT5;
}

void led_off()
{
  P1DIR |= BIT5;
  P1OUT &= ~BIT5;
}

// Inicjalizacja I2C w trybie Master
__attribute__((section(".fram_driver_code"))) void i2c_init()
{
  P1SEL0 |= BIT0 | BIT1; // Przełącz piny P1.0/P1.1 na funkcję I2C (SDA, SCL)
  P1SEL1 &= ~(BIT0 | BIT1);

  UCB0CTL1 |= UCSWRST;                    // Wejście w tryb reset
  UCB0CTLW0 = UCSWRST | UCMODE_3 |        // Tryb I2C Master, tryb 3
              UCSSEL__ACLK | UCMST;       // Źródło zegara: ACLK
  UCB0BRW = 3;                            // Dzielnik zegara I2C
  UCB0I2CSA = FDC1004_ADDR;               // Ustawienie adresu slave
  UCB0CTL1 &= ~UCSWRST;                   // Wyjście z resetu
  UCB0IE |= UCNACKIE | UCTXIE0 | UCRXIE0; // Włączenie przerwań: NACK, TX, RX
}

// Proste miganie diodą - debug etapów
void blink(uint8_t times)
{
  for (uint8_t i = 0; i < times; i++)
  {
    led_on();
    __delay_cycles(120000); //
    led_off();
    __delay_cycles(120000);
  }
}

__attribute__((section(".fram_driver_code"))) void i2c_start_read(uint8_t reg)
{
  i2c_done = 0;
  blink(1); // Etap 1: Start transmisji

  while (UCB0CTL1 & UCTXSTP) // Czekaj na koniec poprzedniego STOPa
    ;

  UCB0CTL1 |= UCTR | UCTXSTT; // Wysyłanie: START + Write

  uint32_t timeout = 1000;
  while (!(UCB0IFG & UCTXIFG0) && timeout--)
    ;
  if (!timeout)
    goto timeout_exit;

  UCB0TXBUF = reg; // Wyślij adres rejestru

  timeout = 1000;
  while (!(UCB0IFG & UCTXIFG0) && timeout--)
    ;
  if (!timeout)
    goto timeout_exit;

  blink(2); // Etap 2: adres wysłany

  UCB0CTL1 &= ~UCTR;   // Przełącz na Read
  UCB0CTL1 |= UCTXSTT; // Wysyłka Repeated START

  timeout = 1000;
  while ((UCB0CTL1 & UCTXSTT) && timeout--)
    ;
  if (!timeout)
    goto timeout_exit;

  blink(3); // Etap 3: Po repeated START

  UCB0CTL1 |= UCTXSTP; // Wygeneruj STOP
  return;

timeout_exit:
  i2c_result = 0xFFFF; // W przypadku timeoutu - wynik domyślny
  i2c_done = 1;
  blink(4); // Etap 4: Timeout sygnalizowany LED
}

// Obsługa przerwania I2C - odbiór danych z slave
__attribute__((interrupt(USCI_B0_VECTOR))) void i2c_isr(void)
{
  static uint8_t byte_index = 0;
  static uint16_t result = 0;
  switch (__even_in_range(UCB0IV, USCI_I2C_UCBIT9IFG))
  {
  case USCI_I2C_UCNACKIFG:
    led_off();
    i2c_result = 0xFFFF;
    i2c_done = 1;
    __bic_SR_register_on_exit(LPM3_bits);
    break;
  case USCI_I2C_UCRXIFG0:
    if (byte_index == 0)
    {
      result = UCB0RXBUF << 8; // Odbiór MSB
      byte_index++;
    }
    else
    {
      result |= UCB0RXBUF; // Odbiór LSB
      i2c_result = result;
      byte_index = 0;
      i2c_done = 1;
      led_off();
    }
    __bic_SR_register_on_exit(LPM3_bits);
    break;
  default:
    __bic_SR_register_on_exit(LPM3_bits);
    break;
  }
}

// Komenda A5 – inicjalizacja magistrali I2C
uint16_t __attribute__((noinline)) cmd_a5()
{
  i2c_init();
  RF13MTXF_L = 2;
  RF13MTXF = 0xA5;
  RF13MTXF = 0x0001;
  return 0;
}

// Komenda A6 – odczyt danych z FDC1004 przez I2C
uint16_t __attribute__((noinline)) cmd_a6()
{
  i2c_start_read(FDC1004_REG_MEAS1_MSB);

  // Max 50ms czekania
  uint32_t timeout = 1000;
  while (!i2c_done && timeout--) // Czekaj na zakończenie transmisji
    ;

  // Bezpieczny wynik
  uint16_t result = i2c_done ? i2c_result : 0xFFFB;

  // Dla pewności – zakończ transmisję STOP
  if (UCB0CTL1 & UCTXSTT)
    UCB0CTL1 &= ~UCTXSTT;
  UCB0CTL1 |= UCTXSTP; // Zakończenie transmisji

  RF13MTXF_L = 2;
  RF13MTXF = 0xA6;
  RF13MTXF = result; // Wysłanie wyniku przez RF
  UCB0IE = 0;        // Wyłącz przerwania I2C
  // Czyść flagi
  UCB0IFG &= ~(UCRXIFG0 | UCTXIFG0 | UCNACKIFG);
  i2c_done = 0;
  return 0;
}

// Komenda A0 – test ping: LED + odpowiedź
uint16_t __attribute__((noinline)) cmd_a0()
{
  led_on();
  __delay_cycles(50000);
  led_off();

  RF13MTXF_L = 2;
  RF13MTXF = 0xA0;
  RF13MTXF = 0xFFFA;

  return 0;
}

int main(void)
{
  WDTCTL = WDTPW | WDTHOLD; // Zatrzymaj Watchdog
  crc_fixup();              // Naprawa CRC dla ROM

  while (1)
  {
    __bis_SR_register(LPM3_bits | GIE); // Wejście w tryb niskiego poboru mocy + przerwania
  }
}
// Patchtable – definiuje funkcje przypisane do komend A6, A5, A0

__attribute__((section(".rompatch")))
const uint16_t patchtable[0x12] = {
    0xCECE, 0xCECE, 0xCECE,
    0xCECE, 0xCECE, 0xCECE,
    0xCECE, 0xCECE, 0xCECE,
    0xCECE, 0xCECE,
    (uint16_t)cmd_a6, 0x00A6,
    (uint16_t)cmd_a5, 0x00A5,
    (uint16_t)cmd_a0, 0x00A0,
    0xCECE};
