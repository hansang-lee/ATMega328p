#include "uart.h"
#include <avr/interrupt.h>

/* Turns on LEDs : PB4 and PB5 */
#define LED_A_TOGGLE()              (PORTB ^= (1 << PB5))
#define LED_B_TOGGLE()              (PORTB ^= (1 << PB4))

/* Send Data Signal : PB2 > PD4 */
#define SEND_DATA_ONE()             (PORTB |= (1 << PB2))
#define SEND_DATA_ZERO()            (PORTB &= ~(1 << PB2))

/* Send Clock Signal for Pin-Change : PB1 > PD3 */
#define PIN_CHANGE()                (PORTB ^= (1 << PB1))
#define RECEIVED_DATA()             (PIND & (1 << PD4))

/* How often run Interrups? : 1000 = 1s */
#define INTERRUPT_PERIOD            100

/* Flags at Transmitter Part */
#define FLAG_GENERATING_CRC         0
#define FLAG_SENDING_PREAMBLE       1
#define FLAG_SENDING_DESTINATION    2
#define FLAG_SENDING_SOURCE         3
#define FLAG_SENDING_CRC            4
#define FLAG_SENDING_DLC            5
#define FLAG_SENDING_PAYLOAD        6

/* Flags at Receiver Part */
#define FLAG_DETECTING_PREAMBLE     0
#define FLAG_RECEIVING_DESTINATION  1
#define FLAG_RECEIVING_SOURCE       2
#define FLAG_RECEIVING_CRC          3
#define FLAG_RECEIVING_DLC          4
#define FLAG_RECEIVING_PAYLOAD      5
#define FLAG_CHECKING_CRC           6

#define SIZE_OF_PREAMBLE            8
#define SIZE_OF_CRC                 32
#define SIZE_OF_DLC                 8
#define SIZE_OF_POLYNOMIAL          33
#define SIZE_OF_PAYLOAD             32
#define SIZE_OF_ADDRESS             8

typedef struct
{
    uint8_t destination[(SIZE_OF_ADDRESS/8)];
    uint8_t source[(SIZE_OF_ADDRESS/8)];
    uint8_t crc32[(SIZE_OF_CRC/8)];
    uint8_t dlc[(SIZE_OF_DLC/8)];
    uint8_t payload[(SIZE_OF_PAYLOAD/8)];
} frame_t;

void interrupt_setup();
void pin_change_setup();
void init_frame(frame_t*);

/* Global Variables for TRANSMITTER */
volatile uint32_t timerA = (INTERRUPT_PERIOD/2);
volatile uint32_t tFlag = FLAG_GENERATING_CRC;
volatile uint32_t tCounter = 0;
uint8_t tPreambleBuffer[(SIZE_OF_PREAMBLE/8)] = { 0b01111110 };
uint8_t tDestination[(SIZE_OF_ADDRESS/8)]     = { 0b00010011 };
uint8_t tSource[(SIZE_OF_ADDRESS/8)]          = { 0b00001111 };
uint8_t tCrcBuffer[(SIZE_OF_CRC/8)]           = { 0x00000000 };
uint8_t tDlcBuffer[(SIZE_OF_DLC/8)]           = { 0b00100000 };
uint8_t tPayloadBuffer[(SIZE_OF_PAYLOAD/8)]   = { 0b01110100, 
                                                  0b01100101,
                                                  0b01110011,
                                                  0b01110100 };
uint8_t tPolynomial[5]                        = { 0b10000010,
                                                  0b01100000,
                                                  0b10001110,
                                                  0b11011011,
                                                  0b10000000 };

/* Global Variables for RECEIVER */
volatile uint32_t timerB = 0;
volatile uint32_t rFlag = FLAG_DETECTING_PREAMBLE;
volatile uint32_t rCounter = 0;

uint8_t rQueue[(SIZE_OF_PREAMBLE/8)]        = { 0 };
uint8_t rDestination[(SIZE_OF_ADDRESS/8)]   = { 0 };
uint8_t rSource[(SIZE_OF_ADDRESS/8)]        = { 0 };
uint8_t rCrcBuffer[(SIZE_OF_CRC/8)]         = { 0 };
uint8_t rDlcBuffer[(SIZE_OF_DLC/8)]         = { 0 };
uint8_t rPayloadBuffer[(SIZE_OF_PAYLOAD/8)] = { 0 };
const uint8_t logMsg_preamble[18]  = "Preamble Detected";
const uint8_t logMsg_dst[20]       = "Destination Received";
const uint8_t logMsg_src[15]       = "Source Received";
const uint8_t logMsg_crc[13]       = "CRC Received";
const uint8_t logMsg_crc_true[14]  = "CRC is correct";
const uint8_t logMsg_crc_false[16] = "CRC is incorrect";
const uint8_t logMsg_dlc[13]       = "DLC Received";
const uint8_t logMsg_payload[17]   = "Payload Received";

void interrupt_setup()
{
	TIMSK0 |= (1 << OCIE0A);    // Interrupt TimerCounter0 Compare Match A
	TIMSK0 |= (1 << OCIE0B);    // Interrupt TimerCounter0 Compare Match B
	TCCR0A |= (1 << WGM01);     // CTC Mode
	TCCR0B |= (1 << CS02);      // Clock/256 = 46875
	OCR0A = 0x2f;               // 1/46875*47 = 0.001 seconds per tick
}

void pin_change_setup()
{
	PCMSK2 |= (1 << PCINT19);   // PCMSK[0;2] : Pin Change Mast Register
                                // PCINT19 bit : PD3
	PCICR |= (1 << PCIE2);      // PCICR : Pin Change Interrupt Control Register
                                // PCIE2 bit : Enabling PCINT[16;23] cause an interrupt
}

void frame_init(frame_t* frame)
{
    for(int i=0; i<(SIZE_OF_CRC/8); i++) {frame->crc32[i] = 0x00;}
    for(int i=0; i<(SIZE_OF_DLC/8); i++) {frame->dlc[i] = 0x00;}
    for(int i=0; i<(SIZE_OF_PAYLOAD/8); i++) {frame->payload[i] = 0x00;}
}