// ======================================================================
// midi4cx-3
//
// Add a MIDI output to the most famous compact hammond clone!
//
// Copyright (C) 2011 Michael Wiebusch
// Visit http://acidbourbon.wordpress.com for more information to this
// and other projects of mine!
//
// This is free software, licensed under the terms of the GNU General
// Public License as published by the Free Software Foundation.
// ======================================================================

#define F_CPU 16e6 // use 16 MHz quartz or crystal resonator ONLY, timing is critical!
 
#include <avr/io.h> 
#include <util/delay.h>
#include <avr/interrupt.h>


// defines the input pins for the serial data signals e.g. clock (C), data (D), sync (U)
#define KORG_PORT PORTB 
#define KORG_PIN PINB
#define KORG_DDR DDRB
#define KORG_D 3
#define KORG_C 2
#define KORG_U 4
#define KORG_PED 1



// initializes the USART, sets the baudrate compliant with MIDI standard
void init_USART(void)
{
	UCSRB |= (1<<TXEN);
	UCSRC |= (3<<UCSZ0); //(1<<URSEL)|
	UBRRH = 0;
	UBRRL = 31;
}


// ############################# buffer stuff ##############################
// basic buffer read and write functions

#define SUCCESS 1
#define FAIL 0
 
#define BUFFER_SIZE 64 // size has to be 2^n (8, 16, 32, 64 ...)
#define BUFFER_MASK (BUFFER_SIZE-1)
 
struct Buffer {
  uint8_t data[BUFFER_SIZE];
  uint8_t read; 
  uint8_t write; 
} buffer = {{}, 0, 0};
 
uint8_t BufferIn(uint8_t byte)
{
  uint8_t next = ((buffer.write + 1) & BUFFER_MASK);
  if (buffer.read == next)
    return FAIL;
  buffer.data[buffer.write] = byte;
  // buffer.data[buffer.write & BUFFER_MASK] = byte; // more secure
  buffer.write = next;
  return SUCCESS;
}
 
uint8_t BufferOut(uint8_t *pByte)
{
  if (buffer.read == buffer.write)
    return FAIL;
  *pByte = buffer.data[buffer.read];
  buffer.read = (buffer.read+1) & BUFFER_MASK;
  return SUCCESS;
}


//############################ main #####################################
 
int main (void) {          

// initialize our variables
uint8_t temp,temp_,dummy,counter,kdata, memory, event_flag, idle_flag, note, transpose, i, temp_ped;
uint8_t keys_pressed[9];

// set data direction for the port we use
KORG_DDR &= ~( 1<<KORG_C || 1<<KORG_D || 1<<KORG_U || 1<<KORG_PED ); // all inputs
KORG_PORT &= ~( 1<<KORG_C || 1<<KORG_D || 1<<KORG_U); // data lines no pullup
KORG_PORT |= 1<<KORG_PED; // pedal with pullup

temp = 0;
temp_ = 0;
counter =0;
kdata =0;
event_flag = 0;
keys_pressed[0] = 0;
transpose = 0;
temp_ped = 0xFF;

//activate serial transmitter
init_USART();
_delay_ms(300);

//main loop
while(1)
{

	// read in Messages from Korg-Organ keyboard serial data stream
	temp_ = temp;
	temp = ~(KORG_PIN); // invert back to revert effect of hardware inverters

	// wait for the sync signal, then continue your usual business
	//(falling edge of KORG_U marks the beginning of the 65 bit serial data package)
	// falling edge comes around every 1.2 ms
	if( counter == 0 )
	{
		while( temp & (1<<KORG_U) )
		{
		temp_ = temp;
		temp = ~(KORG_PIN);
		}
		counter = 1;
	}	
	
	// look for rising edge of clock and then read in state of current key / key no. counter
	// rising edge comes around every 20µs
	if( ((temp&(temp^temp_))&(1<<KORG_C)) )
	{
		
		
		// write serial data in storage array to compare with next round
		if(counter%8 == 0 || counter ==1)
		{
			memory = keys_pressed[counter/8];
			keys_pressed[counter/8] = 0;
		}
		kdata = ((temp&(1<<KORG_D))>>KORG_D) << (counter%8);
		keys_pressed[counter/8] |=  kdata ;
		
		
		// have keys been pressed or released in meantime? ignore the non-key-specific bits in the sequence
		if(  (kdata ^ memory)& (1<<(counter%8)) && ((counter <=63) && (counter >=1) && ((counter <=31)||(counter>=34)) ) )
		{	
			// set program flow flags
			event_flag = 1;
			idle_flag = 0;
			
			// convert counter data to midi note (Korg scan code specific)
			/* this is a bit odd here, explaination:
			the µC running at 16 MHz is too slow to read and store the status of a single key
			in the given 20 µs.
			The loop takes a bit longer than 20 µs but in every case less than 40 µs. So 
			every second clock pulse is skipped, resulting in reading only the even keys.
			Due to the sequence being 65 bit long (an odd number), the sync signal is ignored
			and now the odd keys are read. 

			So in short: 2 scan cycles by the KORG equals one "interlaced" scan cycle by the
			microcontroller.
			*/
			
			if(counter <= 31)
			{
				note = transpose + 98 - counter*2;
			}
			else
			{
				note = transpose + 101 -(counter-31)*2;
			}

			if(kdata)
			{	
			// event on key on: write MIDI data equivalent to the pressed key into the buffer
				BufferIn(0x90); //Note ON
				BufferIn(note); //Note
				BufferIn(64); //Velocity 0-127
			}
			else
			{
			//event on key off: same thing, send "key relesed"-signal to the buffer

				BufferIn(0x80); //Note OFF
				BufferIn(note); //Note
				BufferIn(0x00); //Velocity: 0
			}
		}
		else if ( counter == 64 )
		{
			if  ((temp^temp_ped)&(1<<KORG_PED)) 
			{
				// set program flow flags
				event_flag = 1;
				idle_flag = 0;
				if (temp&(1<<KORG_PED))
				{
					BufferIn(0b10110000); //Control Change
					BufferIn(64); //Sustain/Damper Pedal
					BufferIn(127); //off

				}
				else
				{
					BufferIn(0b10110000); //Control Change
					BufferIn(64); //Sustain/Damper Pedal
					BufferIn(0); //on

				}
			}

			temp_ped = temp;
		}
		else 
		{
			idle_flag = 1;
		}

		counter = (counter + 1) % 65;
	
	
	} 


	// Send Midi to USART
	// controlled by even_flag and idle_flag: every time that at the current scan position
	// no key event has occured, check the midi data buffer for content, and if there is 
	// something send it via the USART
	if(event_flag && idle_flag && counter)
	{
		if(UCSRA & (1<<UDRE) )
		{
			if( BufferOut(&dummy) )
			{
			UDR = dummy;
			}
			else
			{
			event_flag=0;
			}
		}
	}


}


	

   return 0;              
}






