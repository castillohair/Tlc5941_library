/*
 * Tlc5940.c
 *
 * Created: 28/02/2014 04:58:43 PM
 *  Author: Sebastian Castillo
 */ 

#include <avr/interrupt.h>
#include <util/delay.h>

#include "Tlc5941.h"

// Variable declarations
uint8_t Tlc5941_dcData[Tlc5941_dcDataSize];
uint8_t Tlc5941_gsData[Tlc5941_gsDataSize];
volatile uint8_t Tlc5941_dcUpdateFlag;
volatile uint8_t Tlc5941_gsUpdateFlag;
#if Tlc5941_GS_BACKUP
uint8_t Tlc5941_gsDataBackup[Tlc5941_gsDataSize];
#endif

void Tlc5941_Init(void) {
	// Define direction of pins
	Tlc5941_setOutput(Tlc5941_GSCLK_DDR, Tlc5941_GSCLK_PIN);
	Tlc5941_setOutput(Tlc5941_SCLK_DDR, Tlc5941_SCLK_PIN);
	Tlc5941_setOutput(Tlc5941_MODE_DDR, Tlc5941_MODE_PIN);
	Tlc5941_setOutput(Tlc5941_XLAT_DDR, Tlc5941_XLAT_PIN);
	Tlc5941_setOutput(Tlc5941_BLANK_DDR, Tlc5941_BLANK_PIN);
	Tlc5941_setOutput(Tlc5941_SIN_DDR, Tlc5941_SIN_PIN);
	
	// Set initial values of pins
	Tlc5941_setLow(Tlc5941_GSCLK_PORT, Tlc5941_GSCLK_PIN);
	Tlc5941_setLow(Tlc5941_SCLK_PORT, Tlc5941_SCLK_PIN);
	Tlc5941_setLow(Tlc5941_MODE_PORT, Tlc5941_MODE_PIN);
	Tlc5941_setLow(Tlc5941_XLAT_PORT, Tlc5941_XLAT_PIN);
	Tlc5941_setLow(Tlc5941_BLANK_PORT, Tlc5941_BLANK_PIN);
	
	// SPI configuration
	#if Tlc5941_USART_SPI == 0 // Use SPI module
		// Enable SPI, Master, set clock rate fosc/2
		SPCR = (1 << SPE) | (1 << MSTR);
		SPSR = (1 << SPI2X);
	#else // Use USART in SPI mode
		UBRR0 = 0;
		// Set MSPI mode of operation
		UCSR0C = (1<<UMSEL01)|(1<<UMSEL00);
		// Enable transmitter
		UCSR0B = (1<<TXEN0);
		// Set baud rate fosc/2
		UBRR0 = 0;
	#endif
	
	// Set timer for grayscale value transmission
	#if Tlc5941_TIMER == 0
	// CTC with OCR0A as TOP
	TCCR0A = (1 << WGM01);
	// clk_io/1024 (From prescaler)
	TCCR0B = ((1 << CS02) | (1 << CS00));
	// Generate an interrupt every 4096 clock cycles
	OCR0A = 3;
	// Enable Timer/Counter0 Compare Match A interrupt
	TIMSK0 |= (1 << OCIE0A);
	#elif Tlc5941_TIMER == 2
	// CTC with OCR0A as TOP
	TCCR2A = (1 << WGM21);
	// clk_io/1024 (From prescaler)
	TCCR2B = ((1 << CS22) | (1 << CS21)| (1 << CS20));
	// Generate an interrupt every 4096 clock cycles
	OCR2A = 3;
	// Enable Timer/Counter0 Compare Match A interrupt
	TIMSK2 |= (1 << OCIE2A);
	#endif
}

void Tlc5941_SetAllGS(uint16_t value) {
	// Sets all grayscale values to the same input value.
	uint8_t tmp1 = (value >> 4);
	uint8_t tmp2 = (uint8_t)(value << 4) | (tmp1 >> 4);
	Tlc5941_gsData_t i = 0;
	do {
		Tlc5941_gsData[i++] = tmp1; // bits: 11 10 09 08 07 06 05 04
		Tlc5941_gsData[i++] = tmp2; // bits: 03 02 01 00 11 10 09 08
		Tlc5941_gsData[i++] = (uint8_t)value; // bits: 07 06 05 04 03 02 01 00
	} while (i < Tlc5941_gsDataSize);
}

#if Tlc5941_GS_BACKUP

void Tlc5941_BackupGS() {
	for (Tlc5941_gsData_t i = 0; i < Tlc5941_gsDataSize; i++)
	{
		Tlc5941_gsDataBackup[i] = Tlc5941_gsData[i];
	}
}

void Tlc5941_RestoreGS() {
	for (Tlc5941_gsData_t i = 0; i < Tlc5941_gsDataSize; i++)
	{
		Tlc5941_gsData[i] = Tlc5941_gsDataBackup[i];
	}
}

#endif

void Tlc5941_SetGS(Tlc5941_channel_t channel, uint16_t value) {
	// Sets the grayscale value of a particular channel
	channel = Tlc5941_numChannels - 1 - channel;
	Tlc5941_channel3_t i = (Tlc5941_channel3_t)channel * 3 / 2;
	switch (channel % 2) {
		case 0:
			Tlc5941_gsData[i] = (value >> 4);
			i++;
			Tlc5941_gsData[i] = (Tlc5941_gsData[i] & 0x0F) | (uint8_t)(value << 4);
			break;
		default: // case 1:
			Tlc5941_gsData[i] = (Tlc5941_gsData[i] & 0xF0) | (value >> 8);
			i++;
			Tlc5941_gsData[i] = (uint8_t)value;
			break;
	}
}

#if (Tlc5941_MANUAL_DC_FUNCS)
void Tlc5941_ClockInDC(void) {
	// Change programming mode
	Tlc5941_setHigh(Tlc5941_MODE_PORT, Tlc5941_MODE_PIN);

	// Write dummy empty byte
	#if Tlc5941_USART_SPI == 0 // Use SPI module
		// Start transmission
		SPDR = 0;
		// Wait for transmission complete
		while (!(SPSR & (1 << SPIF)));
	#else // Use USART in SPI mode
		// Start transmission
		UDR0 = 0;
		// Wait for transmission complete
		while (!(UCSR0A & (1 << UDRE0)));
	#endif

	// Perform data transmission
	for (Tlc5941_dcData_t i = 0; i < Tlc5941_dcDataSize; i++) {
		#if Tlc5941_USART_SPI == 0 // Use SPI module
			// Start transmission
			SPDR = Tlc5941_dcData[i];
			// Wait for transmission complete
			while (!(SPSR & (1 << SPIF)));
		#else // Use USART in SPI mode
			// Start transmission
			UDR0 = Tlc5941_dcData[i];
			// Wait for transmission complete
			while (!(UCSR0A & (1 << UDRE0)));
		#endif
	}

	Tlc5941_pulse(Tlc5941_XLAT_PORT, Tlc5941_XLAT_PIN);
}

void Tlc5941_SetAllDC(uint8_t value) {
	// Sets all dot correction values to the same input value.
	uint8_t tmp1 = (uint8_t)(value << 2);
	uint8_t tmp2 = (uint8_t)(tmp1 << 2);
	uint8_t tmp3 = (uint8_t)(tmp2 << 2);
	tmp1 |= (value >> 4);
	tmp2 |= (value >> 2);
	tmp3 |= value;
	Tlc5941_dcData_t i = 0;
	do {
		Tlc5941_dcData[i++] = tmp1; // bits: 05 04 03 02 01 00 05 04
		Tlc5941_dcData[i++] = tmp2; // bits: 03 02 01 00 05 04 03 02
		Tlc5941_dcData[i++] = tmp3; // bits: 01 00 05 04 03 02 01 00
	} while (i < Tlc5941_dcDataSize);
}

void Tlc5941_SetDC(Tlc5941_channel_t channel, uint8_t value) {
	// Sets the dot correction value of a particular channel
	channel = Tlc5941_numChannels - 1 - channel;
	Tlc5941_channel_t i = (Tlc5941_channel3_t)channel * 3 / 4;
	switch (channel % 4) {
		case 0:
			Tlc5941_dcData[i] = (Tlc5941_dcData[i] & 0x03) | (uint8_t)(value << 2);
			break;
		case 1:
			Tlc5941_dcData[i] = (Tlc5941_dcData[i] & 0xFC) | (value >> 4);
			i++;
			Tlc5941_dcData[i] = (Tlc5941_dcData[i] & 0x0F) | (uint8_t)(value << 4);
			break;
		case 2:
			Tlc5941_dcData[i] = (Tlc5941_dcData[i] & 0xF0) | (value >> 2);
			i++;
			Tlc5941_dcData[i] = (Tlc5941_dcData[i] & 0x3F) | (uint8_t)(value << 6);
			break;
		default: // case 3:
			Tlc5941_dcData[i] = (Tlc5941_dcData[i] & 0xC0) | (value);
			break;
	}
}
#endif // #if (Tlc5941_MANUAL_DC_FUNCS)

#if Tlc5941_TIMER == 0
ISR(TIMER0_COMPA_vect) {
#elif Tlc5941_TIMER == 2
ISR(TIMER2_COMPA_vect) {
#endif
	static uint8_t xlatNeedsPulse = 0;
	
	Tlc5941_setHigh(Tlc5941_BLANK_PORT, Tlc5941_BLANK_PIN);
	
	// Make TLC load new values
	if (xlatNeedsPulse) {
		Tlc5941_pulse(Tlc5941_XLAT_PORT, Tlc5941_XLAT_PIN);
		xlatNeedsPulse = 0;
	}
	
	if (Tlc5941_outputState(Tlc5941_MODE_PORT, Tlc5941_MODE_PIN)) {
		// Dot correction mode
		// Change to grayscale mode
		Tlc5941_setLow(Tlc5941_MODE_PORT, Tlc5941_MODE_PIN);
		// Send one additional SPI clock signal
		Tlc5941_pulse(Tlc5941_SCLK_PORT, Tlc5941_SCLK_PIN);
	}
	
	Tlc5941_setLow(Tlc5941_BLANK_PORT, Tlc5941_BLANK_PIN);

	// Send dot correction data if dcUpdateFlag is set
	if (Tlc5941_dcUpdateFlag) {
		// Change mode to DC
		Tlc5941_setHigh(Tlc5941_MODE_PORT, Tlc5941_MODE_PIN);

		// Write dummy empty byte
		#if Tlc5941_USART_SPI == 0 // Use SPI module
			// Start transmission
			SPDR = 0;
			// Wait for transmission complete
			while (!(SPSR & (1 << SPIF)));
		#else // Use USART in SPI mode
			// Start transmission
			UDR0 = 0;
			// Wait for transmission complete
			while (!(UCSR0A & (1 << UDRE0)));
		#endif

		// Perform data transmission
		for (Tlc5941_dcData_t i = 0; i < Tlc5941_dcDataSize; i++) {
			#if Tlc5941_USART_SPI == 0 // Use SPI module
				// Start transmission
				SPDR = Tlc5941_dcData[i];
				// Wait for transmission complete
				while (!(SPSR & (1 << SPIF)));
			#else // Use USART in SPI mode
				// Start transmission
				UDR0 = Tlc5941_dcData[i];
				// Wait for transmission complete
				while (!(UCSR0A & (1 << UDRE0)));
			#endif
		}
		
		xlatNeedsPulse = 1;
		Tlc5941_dcUpdateFlag = 0;
	}
	// Send grayscale data if gsUpdateFlag is set
	else if (Tlc5941_gsUpdateFlag) {
		// Below this we have 4096 cycles to shift in the data for the next cycle
		for (Tlc5941_gsData_t i = 0; i < Tlc5941_gsDataSize; i++) {
			#if Tlc5941_USART_SPI == 0 // Use SPI module
				SPDR = Tlc5941_gsData[i];
				while (!(SPSR & (1 << SPIF)));
			#else // Use USART in SPI mode
				UDR0 = Tlc5941_gsData[i];
				while (!(UCSR0A & (1 << UDRE0)));
			#endif
		}
		xlatNeedsPulse = 1;
		Tlc5941_gsUpdateFlag = 0;
	}
}