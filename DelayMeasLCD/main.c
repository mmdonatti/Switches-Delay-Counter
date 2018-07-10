/*
* Delay Measurement
*
* Codigo para medidas de intervalo de tempo da ordem de ms entre fechamento de duas chaves.
* 
* Author : mauricio.donatti
* E-mail: mauricio.donatti@lnls.br
*
* Created: 25/10/2017 08:39:54
*
* Links Uteis:
* http://www.atmel.com/Images/Atmel-42735-8-bit-AVR-Microcontroller-ATmega328-328P_Datasheet.pdf
* http://www.newhavendisplay.com/specs/NHD-0216K1Z-FS_RGB_FBW-REV1.pdf
* https://sites.google.com/site/qeewiki/books/avr-guide/external-interrupts-on-the-atmega328
* https://sites.google.com/site/qeewiki/books/avr-guide/timers-on-the-atmega328
* https://sites.google.com/site/qeewiki/books/avr-guide/digital-outputs
* https://sites.google.com/site/qeewiki/books/avr-guide/counter-atmega328
*
* https://www.digikey.com/product-detail/en/newhaven-display-intl/NHD-0216K1Z-FS-RGB-FBW-REV1/NHD-0216K1Z-FS-RGB-FBW-REV1-ND/2172437
*
*/ 

/*
LCD Pinout (model 1602K1) - Backlight RGB

1	-----------------	VSS		(gnd)		
2	-----------------	VDD		(5V)	
3	-----------------	V0		(Contrast Adjust - Potentiometer (~10k between 5V and GND)
4	-----------------	RS		arduino pin 8
5	-----------------	RW		(write - grounded)	
6	-----------------	E		arduino pin 9
7	-----------------	DB0		not connected
8	-----------------	DB1		not connected
9	-----------------	DB2		not connected
10	-----------------	DB3		not connected
11	-----------------	DB4		arduino pin 4
12	-----------------	DB5		arduino pin 5
13	-----------------	DB6		arduino pin 6
14	-----------------	DB7		arduino pin 7
15	-----------------	K		LEDs cathode - gnd
16	-----------------	A-RED		red led anode - arduino pin 10
17	-----------------	A-GREEN		green led anode - arduino pin 11
18	-----------------	A-BLUE		blue led anode - arduino pin 12

*/

#define F_CPU 16000000UL	//definição da frequência de clock

#include <avr/io.h>			//biblioteca para acesso aos registradores do uC
#include <util/delay.h>		//biblioteca para uso da funcao delay (usada no init e operacao do display)
#include <avr/interrupt.h>	//biblioteca para configuracao das interrupcoes

//Conexoes display - Arduino
#define D4	eS_PORTD4		//arduino pin 4
#define D5	eS_PORTD5		//arduino pin 5
#define D6	eS_PORTD6		//arduino pin 6
#define D7	eS_PORTD7		//arduino pin 7
#define RS	eS_PORTB0		//arduino pin 8
#define EN	eS_PORTB1		//arduino pin 9
#define BR	eS_PORTB2		//arduino pin 10
#define BG	eS_PORTB3		//arduino pin 11
#define BB	eS_PORTB4		//arduino pin 12

//Input to determine rising or falling edge
#define EDGE	eS_PORTB5		//arduino pin 13

#include "lcd.h" //Can be download from the bottom of this article

#define FREQ 16				//frequenciq para contas (MHz)
#define PRE 1024			//Prescaler

unsigned char flag;			//flag de sinalizacao da primeira rampa encontrada
unsigned char control;		//flag de controle para identificar a origem da primeira rampa (CH1 ou CH2)
unsigned char edge;			//flag de controle para selecionar rampa

long int counter;			//variavel do contador do timer (atualizado ao encontrar a segunda rampa) 
long int delay_time;		//variavel para calculo do tempo de atraso

int i;						//contador de uso generico
unsigned char cursor;		//variavel do cursor do display

//Desliga o backlight para reduçao de consumo. Backlight off =~ 55mA em 9V
void Backlight_off()
{
	PORTB &= ~0x1C; //The same result but faster than: /*pinChange(BR,0); pinChange(BG,0); pinChange(BB,0);*/
}

void Backlight(unsigned char R,unsigned char G,unsigned char B)
{
	pinChange(BR,R); 
	pinChange(BG,G); 
	pinChange(BB,B);
}

//Liga o backlight para visualização. Backlight on =~ 200mA em 9V
void Backlight_all()
{
	PORTB |= 0x1C; //The same result but faster than: /*pinChange(BR,1); pinChange(BG,1); pinChange(BB,1);*/
}

//Liga o backlight para visualização. Backlight on =~ 200mA em 9V
void Backlight_green()
{
	PORTB |= 0x08; //The same result but faster than: /*pinChange(BR,0); pinChange(BG,1); pinChange(BB,0);*/
}


int main(void)
{
	DDRD = 0xF0;		//Inicializa os pinos do PORT D usados no display como saida e os pinos de interrupcao externa como entrada
	DDRB = 0xDF;		//Inicializa o PORT B como saida (display e backlight), e o pino PB6 como entrada

	//Inicializa os flags
	flag = 0;	
	control = 0;
	
	//Configuracao do Timer
	OCR1A = 0xB500;							//Set Comparison Limit - 0xB500 resulta em 3s para f=16MHz e prescaler de 1024
	TCNT1 = 0;								//Zera o contador
	TCCR1B |= (1 << WGM12);					//Mode 4, CTC on OCR1A
	TIMSK1 |= (1 << OCIE1A);				//Set interrupt on compare match
	TCCR1B |= (1 << CS12) | (1 << CS10);	//set prescaler to 1024 (start timer)
	
	//Pull-Up nos pinos de interrupcao
	PORTD |= (1 << PORTD2);    // turn On the PD2 Pull-up
	PORTD |= (1 << PORTD3);    // turn On the PD3 Pull-up
	
	//Inicializacao do LCD
	Lcd4_Init();
	Lcd4_Clear();
	Lcd4_Set_Cursor(1,1);

	edge = ((PINB & (1<<PINB5)) > 0); //edge assume 1 se D13 HIGH e 0 se D13 low
	if(edge)
	{
		EICRA |= (1 << ISC00);    // set INT0 to trigger on RISING edge
		EICRA |= (1 << ISC01);
		EICRA |= (1 << ISC10);    // set INT1 to trigger on RISING edge
		EICRA |= (1 << ISC11);	
		Lcd4_Write_String("Rising Edge");	//Escreve rising edge

	}
	else
	{
		EICRA &= ~(1 << ISC00);    // set INT0 to trigger on FALLING edge
		EICRA |= (1 << ISC01);
		EICRA &= ~(1 << ISC10);    // set INT1 to trigger on FALLING edge
		EICRA |= (1 << ISC11);	
		Lcd4_Write_String("Falling Edge");	//Escreve rising edge
	}
	
	EIMSK |= (1 << INT0);     // Turns on INT0
	EIMSK |= (1 << INT1);     // Turns on INT1

	sei(); // enable interrupts

	//Escreve string "Ready", aguardando rampa. Backlight sera desligada no overflow do timer (~4s)
	Lcd4_Set_Cursor(2,1);
	Lcd4_Write_String("Ready!");
	Backlight_green();
	
	while(1)
	{
		if(((PINB & (1<<PINB5)) > 0) != edge)
		{
			edge = ((PINB & (1<<PINB5)) > 0); //edge assume 1 se D13 HIGH e 0 se D13 low
			Lcd4_Clear();						//clear display
			Backlight_green();					//acende o backlight
			Lcd4_Set_Cursor(1,1);				//Posiciona o cursor na primeira linha e no primeiro caractere
			
			if(edge)
			{
				EICRA |= (1 << ISC00);    // set INT0 to trigger on RISING edge
				EICRA |= (1 << ISC01);
				EICRA |= (1 << ISC10);    // set INT1 to trigger on RISING edge
				EICRA |= (1 << ISC11);
				Lcd4_Write_String("Rising Edge");	//Escreve rising edge
			}
			else
			{
				EICRA &= ~(1 << ISC00);    // set INT0 to trigger on FALLING edge
				EICRA |= (1 << ISC01);
				EICRA &= ~(1 << ISC10);    // set INT1 to trigger on FALLING edge
				EICRA |= (1 << ISC11);
				Lcd4_Write_String("Falling Edge");	//Escreve rising edge
			}
			TCNT1 = 0;				  //Zera contador do timer
			flag = 0;				  //Reset flag
			counter = 0;			  //Reset counter
			EIFR |= (1 << INTF1);	  // Clear Interrupt Flag
			EIFR |= (1 << INTF0);	  // Clear Interrupt Flag
			EIMSK |= (1 << INT0);     // Turns on INT0
			EIMSK |= (1 << INT1);     // Turns on INT1
		}
		_delay_ms(100);		//delay para permitir que a variavel control seja atualizada na interrupcao
		
		if(control != 0)	//control != 0 indica que a sequencia de rampas foi encontrada
		{
			delay_time = (counter*PRE)/FREQ;	//delay time in microseconds
			Lcd4_Clear();						//clear display
			Backlight_green();					//acende o backlight
			if(control == 0x01) //First Pin3 - Second Pin2
			{
				Lcd4_Set_Cursor(1,1);				//Posiciona o cursor na primeira linha e no primeiro caractere
				Lcd4_Write_String("First: CH 2");	//Escreve o canal adiantado CH2
			}
			else //First Pin2 - Second Pin3
			{
				Lcd4_Set_Cursor(1,1);				//Posiciona o cursor na primeira linha e no primeiro caractere
				Lcd4_Write_String("First: CH 1");	//Escreve o canal adiantado CH1
			}
			Lcd4_Set_Cursor(2,1);					//Posiciona o cursor n segunda linha
			Lcd4_Write_String("Time: ");			//Escreve time: (ocupando 6 posicoes)
			cursor = 7;								//posiciona o cursor na posicao 7
			delay_time=delay_time/1000;				//converte o tempo para ms
			for(i=cursor+3;i>=cursor;i--)			//divide em 4 digitos a variavel delay time (em ms)
			{
				Lcd4_Set_Cursor(2,i);					//posiciona o cursor
				Lcd4_Write_Char(0x30+(delay_time%10));	//escreve digito a digito (0x30 e a conversao para ASCII)
				delay_time=delay_time/10;				//Divide delay por 10, preparando para a proxima iteracao
			}
			Lcd4_Set_Cursor(2,cursor+4);				//posiciona o cursor apos os 4 digitos com um espaco
			Lcd4_Write_String("ms");					//escreve ms (unidade)
			control = 0;								//Indica que o comando foi tratado
		}
	}
}

//Interrupcao em PD2 (arduino pin 2)
ISR (INT0_vect)
{
	//Se nenhuma rampa acionada
	if(!flag)
	{
		TCNT1 = 0;								//Zera contador do timer
		TCCR1B |= (1 << CS12) | (1 << CS10);	//set prescaler to 1024 (start timer)
		//TCCR1B |= (1 << CS12);				//set prescaler to 256 (start timer) - Outra opcao
		//TCCR1B |= (1 << CS11) | (1 << CS10);	//set prescaler to 64 (start timer)		 - Outra opcao	
		flag = 1;								//aciona o flag de primeira rampa encontrada
	}
	else //first was Pin 3
	{
		//TCCR1B &= 0xF8;	//disable timer
		counter = TCNT1;						//Salva valor do timer na variavel counter
		TCCR1B |= (1 << CS12) | (1 << CS10);	//set prescaler to 1024 (start timer)
		TCNT1 = 0;								//reseta o timer
		control = 0x01;							//seta variavel de controle com 0x01m indicando que o pino PD3 gerou a interrupcao primeiro
	}
	EIMSK &= ~(1 << INT0);						//Desablita a interrupcao em PD2 (Arduino pin 2)
}

//Interrupcao em PD3 (arduino pin 3)
ISR (INT1_vect)
{
	//Se nenhuma rampa acionada
	if(!flag)
	{
		TCNT1 = 0;								//Zera contador do timer
		TCCR1B |= (1 << CS12) | (1 << CS10);	//set prescaler to 1024 (start timer)
		//TCCR1B |= (1 << CS12);				//set prescaler to 256 (start timer)
		//TCCR1B |= (1 << CS11) | (1 << CS10);	//set prescaler to 64 (start timer)
		flag = 1;								//aciona o flag de primeira rampa encontrada
	}
	else //first was Pin 2
	{
		//TCCR1B &= 0xF8;	//disable timer
		counter = TCNT1;						//Salva valor do timer na variavel counter
		TCCR1B |= (1 << CS12) | (1 << CS10);	//set prescaler to 1024 (start timer)
		TCNT1 = 0;								//reseta o timer
		control = 0x02;							//seta variavel de controle com 0x01m indicando que o pino PD2 gerou a interrupcao primeiro
	}
	EIMSK &= ~(1 << INT1);						//Desablita a interrupcao em PD3 (Arduino pin 3)	
}

//Interrupcao de timer overflow
ISR (TIMER1_COMPA_vect)
{
	// action to be done every OCR1A*Prescaler*(1/16MHz) (now ~ 3s)
	//This block reset the reading state
	flag = 0;				  //Reset flag
	counter = 0;			  //Reset counter
	EIFR |= (1 << INTF1);	  // Clear Interrupt Flag
	EIFR |= (1 << INTF0);	  // Clear Interrupt Flag
	EIMSK |= (1 << INT0);     // Turns on INT0
	EIMSK |= (1 << INT1);     // Turns on INT1
	
	Backlight_off();		  //Desliga backlight para economia de energia
}