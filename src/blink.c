#include "ch32v003fun.h"
#include <stdio.h>

// #ifdef funDigitalWrite
// 	#undef funDigitalWrite
// 	#define funDigitalWrite( pin, value ) { value ? (GpioOf( pin )->BSHR = 1<<((!(value))*16 + ((pin) & 0xf))) : (GpioOf( pin )->BCR = 1<<((!(value))*16 + ((pin) & 0xf))); }

// #endif

#define GPS_UART_BR 9600
#define NINEHUNDREDTH_OF_SECOND Ticks_from_Ms(1)
#define A_SECOND Ticks_from_Ms(1000)
#define FIVE_SECONDS Ticks_from_Ms(5000)
#define HUNDRED_MS Ticks_from_Ms(100)
#define THIRTY_SECONDS Ticks_from_Ms(1000*30)
uint32_t last_second_ticks = 0;
uint32_t next_parse_time = 0;
uint32_t epoch = 0;
uint8_t hours = 21;
uint8_t minutes = 37;
uint8_t seconds = 0;
const char GPRMC[] = "$GPRMC,";
#define STRLEN(s) (sizeof(s)/sizeof(s[0])-1)

typedef struct bank_and_shift{
	GPIO_TypeDef *bank;
	uint8_t shift;
} bank_and_shift_t;

// const bank_and_shift_t pinsmap[12] = {
// 	{ .bank = GPIOC, .shift = 2 },
// 	{ .bank = GPIOD, .shift = 4 },
// 	{ .bank = GPIOD, .shift = 3 },
// 	{ .bank = GPIOD, .shift = 2 },
// 	{ .bank = GPIOD, .shift = 0 },
// 	{ .bank = GPIOC, .shift = 7 },
// 	{ .bank = GPIOC, .shift = 6 },
// 	{ .bank = GPIOC, .shift = 5 },
// 	{ .bank = GPIOC, .shift = 0 },

// 	{ .bank = GPIOC, .shift = 3 },
// 	{ .bank = GPIOC, .shift = 4 },
// 	{ .bank = GPIOC, .shift = 1 },
// };

// const int segments[12] = {
// 	PC2,PD4,PD3,PD2,PD0,PC7,PC6,PC5,PC0, /* seg a-h */
// 	PC3,PC4,PC1, /* SRCLK, SER, OE */
// };
const int segments[12] = {
	PC0,PC1,PC2,PC3,PC4,PC5,PC6,PC7, /* seg a-h */
	PD2,PD3, /* SRCLK, SER */
};

const uint8_t sevensegmap[13] = {
	0b11111100, // 0
	0b01100000, // 1
	0b11011010, // 2
	0b11110010, // 3
	0b01100110, // 4
	0b10110110, // 5
	0b10111110, // 6
	0b11100000, // 7
	0b11111110, // 8
	0b11110110, // 9
	0b00000001, // . = 10
	0b00000010, // - = 11
	0b00000000, // blank = 12
};

uint8_t digits[9] = {
	12,12,12,12,12,2,1,3,7,
};

#define UART_BUF_LEN 255
uint8_t uart_data[UART_BUF_LEN] = {0};

void increment_time()
{
	seconds++;
	if (seconds >= 60) {
		seconds -= 60;
		minutes++;
	}
	if (minutes >= 60) {
		minutes -= 60;
		hours++;
	}
	if (hours > 24) {
		hours -= 24;
	}
}

void display_time()
{
	digits[0] = 12;
	digits[1] = hours/10;
	digits[2] = (hours%10);
	digits[3] = minutes/10;
	digits[4] = (minutes%10);
	digits[5] = seconds/10;
	digits[6] = (seconds%10);
	digits[7] = 12;
	digits[8] = 12;
}

int parse_time() {
	uint8_t chars = 0;

	for (int i=0; i < UART_BUF_LEN; i++)
	{
		uint32_t j = ((UART_BUF_LEN - DMA1_Channel5->CNTR) + i) % UART_BUF_LEN;
		// printf("%c", uart_data[j]);
		if (chars < STRLEN(GPRMC))
		{
			// printf("a(%d)", chars);
			if (GPRMC[chars] == uart_data[j]) {
				chars++;
			}
			else {
				chars = 0;
			}
		}
		else if (chars < STRLEN(GPRMC)+6)
		{
			// printf("b");
			if (uart_data[j] >= '0' && uart_data[j] <= '9')
			{
				chars++;
			}
			else {
				chars = 0;
			}
		}
		else
		{
			// printf("c");
			if (chars == STRLEN(GPRMC)+6 && uart_data[j] == '.')
			{
				hours = 10*(uart_data[ ((UART_BUF_LEN+j-6) % UART_BUF_LEN) ]-'0') + (uart_data[ ((UART_BUF_LEN+j-5) % UART_BUF_LEN) ]-'0');
				minutes = 10*(uart_data[ ((UART_BUF_LEN+j-4) % UART_BUF_LEN) ]-'0') + (uart_data[ ((UART_BUF_LEN+j-3) % UART_BUF_LEN) ]-'0');
				seconds = 10*(uart_data[ ((UART_BUF_LEN+j-2) % UART_BUF_LEN) ]-'0') + (uart_data[ ((UART_BUF_LEN+j-1) % UART_BUF_LEN) ]-'0');
				return 1;
			}
		}
	}
	return 0;
}

// void bDigitalWrite(uint8_t digit, uint8_t state)
// {
	
// 	if (state)
// 	{
// 		pinsmap[digit].bank->BSHR |= 1 << pinsmap[digit].shift;
// 	} else
// 	{
// 		pinsmap[digit].bank->BCR |= 1 << pinsmap[digit].shift;
// 	}

// }

void uart_setup()
{
	// enable uart
	RCC->APB2PCENR |= RCC_APB2Periph_GPIOD | RCC_APB2Periph_USART1;
	// Push-Pull, 10MHz Output on D5, with AutoFunction
	GPIOD->CFGLR |= ~(0xf<<(4*6));
	GPIOD->CFGLR |= (GPIO_Speed_10MHz | GPIO_CNF_OUT_PP_AF)<<(4*6);

	USART1->CTLR1 = USART_WordLength_8b | USART_Parity_No | USART_Mode_Rx;
	USART1->CTLR2 = USART_StopBits_1;
	// enable dma rx
	USART1->CTLR3 = USART_DMAReq_Rx;
	// set baudrate
	USART1->BRR = (FUNCONF_SYSTEM_CORE_CLOCK + GPS_UART_BR/2) / GPS_UART_BR;
	// enable uart
	USART1->CTLR1 |= CTLR1_UE_Set;

	// dma
	RCC->AHBPCENR = RCC_AHBPeriph_SRAM | RCC_AHBPeriph_DMA1;
	DMA1_Channel5->CFGR &= ~DMA_CFGR1_EN;
	DMA1_Channel5->PADDR = (uint32_t)&USART1->DATAR;
	DMA1_Channel5->CFGR = DMA_CFGR1_MINC | DMA_CFGR1_CIRC;
	DMA1_Channel5->CNTR = UART_BUF_LEN;
	DMA1_Channel5->MADDR = (uint32_t)uart_data;
	NVIC_EnableIRQ(DMA1_Channel5_IRQn);
	DMA1_Channel5->CFGR |= DMA_CFGR1_EN;
}

void interrupt_setup()
{
	/* clear systick settings */
	SysTick->CTLR = 0;

	NVIC_EnableIRQ(SysTicK_IRQn);
	/* set tick interval */
	SysTick->CMP = NINEHUNDREDTH_OF_SECOND-1;	
	/* reset count */
	SysTick->CNT = 0;
	// systick_cnt = 0;
	/* enable systick counter, irq, hclk/1 */
	SysTick->CTLR = SYSTICK_CTLR_STE | SYSTICK_CTLR_STIE | SYSTICK_CTLR_STCLK;
}

uint8_t idig = 0;

void SysTick_Handler(void) __attribute__((interrupt));
void SysTick_Handler(void)
{
	/* set new interrupt */
	SysTick->CMP = SysTick->CNT + NINEHUNDREDTH_OF_SECOND;
	/* clear irq */
	SysTick->SR = 0;

	/* do one digit */
	/* at the end of digits sweep send a single pulse to the shift register */
	if (idig == 8) {
		/* set SER */
		GPIOD->BSHR |= 1 << 3;
	} else {
		GPIOD->BCR |= 1 << 3;
	}
	/* clear segments */
	GPIOC->BCR = 0xff;
	/* tick SRCLK */
	GPIOD->BSHR |= 1 << 2;
	GPIOD->BCR |= 1 << 2;
	/* set segments */
	GPIOC->BSHR = sevensegmap[digits[idig]] | ((idig == 2 || idig == 4) ? 1 : 0);
	/* increment digit */
	idig = (idig+1)%9;
}

int main()
{
	SystemInit();
	uart_setup();

	printf("yukkuri\n");

	// Enable GPIOs
	funGpioInitAll();
	
	// set all gpio in segments array as output
	for (uint8_t i=0; i<11; i++)
	{
		funPinMode( segments[i], GPIO_Speed_10MHz | GPIO_CNF_OUT_PP );
	}

	interrupt_setup();
	next_parse_time = SysTick->CNT + FIVE_SECONDS;
	
	GPIOC->BSHR = 0xff;

	printf("nijika wa daisuki\n");

	display_time();
	while (1) {
		/* move time forward */
		if ( (SysTick->CNT - last_second_ticks) > A_SECOND )
		{
			increment_time();
			display_time();
			last_second_ticks += A_SECOND;
		}
		if ( (int32_t)(SysTick->CNT - next_parse_time) > 0)
		{
			if (parse_time()) {
				next_parse_time = SysTick->CNT + FIVE_SECONDS;
			}
			else {
				/* more frequent spamming since 255 dma buffer fits only a part of the messages sent every second*/
				next_parse_time = SysTick->CNT + HUNDRED_MS;
			}
			printf("ua%drt %s lol\n", DMA1_Channel5->CNTR, uart_data);
		}
		
		
		
	}




	// while(1)
	// {
	// 	for (uint8_t idig = 0; idig < 9; idig++)
	// 	{
	// 		// // disable digit
	// 		// funDigitalWrite( segments[(9+idig-1) % 9], 0);
	// 		// if ( idig != 0 )
	// 		bDigitalWrite( (9+idig-1) % 9, 0);
	// 		// disable OE
	// 		bDigitalWrite(11, 1);
	// 		// update shift reg
	// 		for (uint8_t i = 0; i < 8; i++) {
	// 			if ( (sevensegmap[ digits[idig] ] >> i) & 1 )
	// 			{
	// 				// funDigitalWrite(PC4, 1);
	// 				bDigitalWrite(10, 1);
	// 			} else
	// 			{
	// 				// funDigitalWrite(PC4, 0);
	// 				bDigitalWrite(10, 0);
	// 			}
	// 			// funDigitalWrite(PC3, 1);
	// 			bDigitalWrite(9, 1);
	// 			// Delay_Us( 1 );
	// 			// funDigitalWrite(PC3, 0);
	// 			bDigitalWrite(9, 0);
	// 			// Delay_Us( 1 );
	// 			// Delay_Ms( 10 );
	// 		}
	// 		// funDigitalWrite(PC3, 1);
	// 		bDigitalWrite(9, 1);
	// 		// Delay_Us( 1 );
	// 		// funDigitalWrite(PC3, 0);
	// 		bDigitalWrite(9, 0);


	// 		if ( idig != 8 )
	// 		{
	// 			// funDigitalWrite(segments[idig], 1);
	// 			bDigitalWrite(idig, 1);
	// 		}
	// 		// enable OE
	// 		bDigitalWrite(11, 0);
	// 		// // select digit
	// 		// set_digit(idig, 1);
			

	// 		// printf("nijika wa daisuki ");
	// 		// for (uint8_t i=0;i<9;i++)
	// 		// {
	// 		// 	printf(" %d", digits[i]);
	// 		// }
	// 		// printf("\n");
	// 		// digits[(idig+1)%9] = 9;
	// 		Delay_Ms( 1 );
			
	// 		// if ((SysTick->CNT - last_second_ticks) > THIRTY_SECONDS)
	// 		// {
	// 		// 	printf("uart %s lol\n", uart_data);
	// 		// 	increment_time();
	// 		// 	display_time();
	// 		// 	last_second_ticks += THIRTY_SECONDS;
	// 		// }
	// 	} /* for 9 times a second - every digit */

	// 	// any work goes here or at the end of for
	// 	// uint8_t temp = digits[8];
	// 	// for (int i=0; i<8; i++)
	// 	// {
	// 	// 	digits[8-i] = digits[7-i];
	// 	// }
	// 	// digits[0] = temp;
		

	// }

}




// void SysTick_Handler(void) __attribute__((interrupt));
// void SysTick_Handler(void)
// {
// 	uint32_t tenter = SysTick->CNT;

// 	// printf("nijika wa daisuki\n");
// 	/* set new interrupt*/
// 	SysTick->CMP = SysTick->CNT + NINEHUNDREDTH_OF_SECOND;
// 	/* clear irq */
// 	SysTick->SR = 0;

// 	/* do one digit */
// 	/* disable digit */
// 	bDigitalWrite( (9+idig-1) % 9, 0);
// 	// /* disable OE */
// 	// bDigitalWrite(11, 1);
// 	for (uint8_t i = 0; i < 8; i++) {
// 		if ( (sevensegmap[ digits[idig] ] >> i) & 1 ) {
// 			bDigitalWrite(10, 1);
// 		} else {
// 			bDigitalWrite(10, 0);
// 		}
// 		/* tick */
// 		bDigitalWrite(9, 1);
// 		bDigitalWrite(9, 0);
// 	}
// 	/* tick */
// 	bDigitalWrite(9, 1);
// 	bDigitalWrite(9, 0);
// 	/* enable digit */
// 	bDigitalWrite(idig, 1);
// 	// /* enable OE */
// 	// bDigitalWrite(11, 0);
// 	idig = (idig+1)%9;

// 	tenter = (int32_t)(SysTick->CNT - tenter);
// 	printf("time %d\n", tenter);
// }
