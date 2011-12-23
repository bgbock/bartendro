#define F_CPU 16000000UL 
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>

#include <stddef.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <avr/pgmspace.h>
#include <stdarg.h>
#include <stdlib.h>

#include "packet.h"

// Bit manipulation macros
#define sbi(a, b) ((a) |= 1 << (b))       //sets bit B in variable A
#define cbi(a, b) ((a) &= ~(1 << (b)))    //clears bit B in variable A
#define tbi(a, b) ((a) ^= 1 << (b))       //toggles bit B in variable A

#define BAUD 38400
#define UBBR (F_CPU / 16 / BAUD - 1)

static uint8_t g_num_dispensers = 0;
static uint8_t g_debug = 0;

void serial_init(void)
{
    /*Set baud rate */ 
    UBRR0H = (unsigned char)(UBBR>>8); 
    UBRR0L = (unsigned char)UBBR; 
    /* Enable transmitter */ 
    UCSR0B = (1<<TXEN0)|(1<<RXEN0); 
    /* Set frame format: 8data, 1stop bit */ 
    UCSR0C = (0<<USBS0)|(3<<UCSZ00); 
}

void serial_tx(unsigned char ch)
{
    while ( !( UCSR0A & (1<<UDRE0)) );
    UDR0 = ch;
}

unsigned char serial_rx(void)
{
    while ( !(UCSR0A & (1<<RXC0))) 
        ;

    return UDR0;
}

#define MAX 80 
void dprintf(const char *fmt, ...)
{
    va_list va;
    va_start (va, fmt);
    char buffer[MAX];
    char *ptr = buffer;
    vsnprintf(buffer, MAX, fmt, va);
    va_end (va);
    for(ptr = buffer; *ptr; ptr++)
    {
        if (*ptr == '\n') serial_tx('\r');
        serial_tx(*ptr);
    }
}

void spi_master_init(void)
{
	SPCR = (1<<SPE)|(1<<MSTR)|(1<<SPR1);
}

void spi_master_stop(void)
{
    // Disable SPI
	SPCR &= ~(1<<SPE);
}

char spi_transfer(char cData)
{
	SPDR = cData;

	/* Wait for transmission complete */
	while(!(SPSR & (1<<SPIF)))
	    ;

    return SPDR;
}

void make_packet(packet *p, uint8_t addr, uint8_t type)
{
    p->header[0] = 0xFF;
    p->header[1] = 0xFF;
    p->addr = addr;
    p->type = type;
    p->payload.ch[0] = 0xA0;
    p->payload.ch[1] = 0xA1;
}

uint8_t transfer_packet(packet *tx, packet *rx)
{
    uint8_t *ptx = (uint8_t*)tx;
    uint8_t *prx = (uint8_t*)rx;
    uint8_t i, ch, received = 0;

    memset(prx, 0, sizeof(packet));
    if (g_debug)
        dprintf("# transfer packet %d\n", tx->type);
    // send the packet and possibly start receiving data back
    for(i = 0; i < sizeof(packet); i++)
    {
        ch = spi_transfer(*ptx);

        if (g_debug > 1)
            dprintf("# %02x %02x\n", *ptx, ch);
        ptx++;

        // ignore the first character since its not part of our communication
        if (i > 0)
        {
            // Look for the packet header
            if (received == 0 && ch != 0xFF)
                continue;

            *prx = ch;
            prx++;
            received++;
        }
    }

    // if we haven't received a packet worth of data,
    // transmit more zeros until a packet is read
    for(; received < sizeof(packet);)
    {
        ch = spi_transfer(0x00);
        if (g_debug > 1)
            dprintf("# %02x %02x\n", 0, ch);
        if (prx == (uint8_t*)rx && ch != 0xFF)
            continue;

        *prx = ch;
        prx++;
        received++;
    }

    if (g_debug > 1)
        dprintf("verify:\n");
    // Compare all but the last byte of the packet. For some reason the last byte gets corrupted homehow. 
    for(i = 0, prx = (uint8_t*)rx, ptx = (uint8_t*)tx; i < sizeof(packet); i++, prx++, ptx++)
    {
        if (g_debug > 1)
            dprintf("# %02x %02x\n", *ptx, *prx);
        if (((i == 4) || (i == 5)) && tx->type == PACKET_TYPE_RESPONSE)
            continue;

        if (*ptx != *prx)
        {
            if (g_debug > 1)
                dprintf("\n");
            if (g_debug)
                dprintf("# data transmission error!\n");
            return 0;
        }
    }
    if (g_debug > 1)
        dprintf("\n");
    return 1;
}

void setup(void)
{
	DDRD |= (1<<PORTD7); //LED pin
	DDRC |= (1<<PORTC0); //LED pin
	// Set SS, MOSI and SCK as outputs [MISO is PB4]
	DDRB = (1<<PORTB2)|(1<<PORTB3)|(1<<PORTB5); 
    serial_init();
}

void address_assignment(void)
{
    uint8_t i, ch = 0;

    // Now all clients should be in address assignment mode
    ch = spi_transfer(1);
    for(i = 0; ch == 0 || ch == 0xFF; i++)
    {
        ch = spi_transfer(0);
    }

    g_num_dispensers = ch - 1;
}

uint8_t turn_on(uint8_t disp)
{
    packet in, out;
    make_packet(&in, disp, PACKET_TYPE_START);
    return transfer_packet(&in, &out);
}

uint8_t turn_off(uint8_t disp)
{
    packet in, out;
    make_packet(&in, disp, PACKET_TYPE_STOP);
    return transfer_packet(&in, &out);
}

uint8_t dispense(uint8_t disp, uint16_t dur)
{
    packet in, out;
    make_packet(&in, disp, PACKET_TYPE_DISPENSE);
    in.payload.word = dur;
    return transfer_packet(&in, &out);
}

void test(void)
{
    uint8_t ch, i;

    spi_master_init();
    cbi(PORTB, 2);

    for(i = 0; i < 10; i++)
        spi_transfer(0xFF);

    for(i = 0; ; i++)
    {
        ch = spi_transfer(i);
        dprintf("%x %x\n", ch, i);
        _delay_ms(250);
        _delay_ms(250);
        _delay_ms(250);
        _delay_ms(250);
    }

    sbi(PORTB, 2);
    spi_master_stop();
}

uint8_t check(uint8_t *disp, uint8_t *err)
{
    uint8_t i, ret;
    packet in, out;

    *disp = 0;
    *err = 0;
    for(i = 1; i <= g_num_dispensers; i++)
    {
        make_packet(&in, i, PACKET_TYPE_CHECK);
        ret = transfer_packet(&in, &out);
        if (!ret)
            return ret;

        make_packet(&in, i, PACKET_TYPE_RESPONSE);
        ret = transfer_packet(&in, &out);
        if (!ret)
            return ret;

        if (out.payload.ch[0] != 0)
        {
            //dprintf("dispenser %d: %d\n", i, out.payload[0]);
            *disp = i;
            *err = out.payload.ch[0];
            return 1;
        }
    }
    return 1;
}

uint8_t get_state(uint8_t disp, uint8_t *state)
{
    uint8_t ret;
    packet in, out;

    *state = 0;
    make_packet(&in, disp, PACKET_TYPE_GETSTATE);
    ret = transfer_packet(&in, &out);
    if (!ret)
        return ret;

    make_packet(&in, disp, PACKET_TYPE_RESPONSE);
    ret = transfer_packet(&in, &out);
    if (!ret)
        return ret;

    *state = out.payload.ch[0];
    return 1;
}

#define MAX_CMD_LEN 16
void get_cmd(char cmd[MAX_CMD_LEN])
{
    uint8_t ch, count;

    cmd[0] = 0;
    for(count = 0; count < MAX_CMD_LEN - 1; count++)
    {
        ch = serial_rx();
        if (g_debug)
            serial_tx(ch);
        if (ch == '\r')
        {
            if (g_debug)
                serial_tx('\n');
            break;
        }

        cmd[count] = (char)ch;
    }
    cmd[count] = 0;
}

#define OK                        0
#define MASTER_REBOOTED           1
#define TRANSMISSION_ERROR        2
#define DISPENSER_FAULT_ERROR     3
#define INVALID_COMMAND_ERROR     4
#define INVALID_SPEED_ERROR       5
#define UNKNOWN_COMMAND_ERROR     6
#define BAD_DISPENSER_INDEX_ERROR 7

int main (void)
{
    uint8_t i;
    char cmd[MAX_CMD_LEN];

	setup();

    // Flash the LED to let us know we rebooted
    sbi(PORTC, 0);
    _delay_ms(200);
    cbi(PORTC, 0);

    dprintf("%d master booted\n", MASTER_REBOOTED);

    // This loop is a hack. For some reason at some restarts SPI communication doesn't
    // work right and we get bad data back. Resetting the communication works wonders.
    //for(;;)
    for(;;)
    {
        /* set SS hi, so that clients will stop what they are doing and reset */
        sbi(PORTB, 2);
        _delay_ms(100);

        spi_master_init();

        _delay_ms(5);

        /* select device */
        cbi(PORTB, 2);
        _delay_ms(5);

        for(i = 0; i < sizeof(packet); i++)
            spi_transfer(0);

        address_assignment();
        _delay_ms(100);

        /* This is a hack. Sometimes when the master reboots it gets the wrong
           answer from a slave. If we know the answer is bad, repeat the startup. */
        if (g_num_dispensers <= 32)
            break;
    }
    for(;0;)
    {
        turn_on(1);
        _delay_ms(500);
        turn_off(1);
        _delay_ms(500);
    }

    for(;;)
    {
        if (g_debug)
            dprintf(">");

        get_cmd(cmd);
        if (strlen(cmd) == 0)
            continue;

        if (strcasecmp(cmd, "count") == 0)
        {
            dprintf("0 %d dispensers\n", g_num_dispensers);
            continue;
        }

        if (strcasecmp(cmd, "check") == 0)
        {
            uint8_t disp, err;

            if (check(&disp, &err))
            {
                if (disp == 0)
                    dprintf("0 ok\n");
                else
                    dprintf("%d %d dispenser fault: %d\n", DISPENSER_FAULT_ERROR, disp, err);
            }
            else
                dprintf("%d transmission error\n", TRANSMISSION_ERROR);
            continue;
        }

        if (strncasecmp(cmd, "on", 2) == 0)
        {
            int     disp;
            uint8_t ret;

            ret = sscanf(cmd, "on %d", &disp);
            if (ret != 1)
            {
                dprintf("%d invalid command\n", INVALID_COMMAND_ERROR);
                continue;
            }
            if (disp < 1 || disp > (int)g_num_dispensers)
            {
                dprintf("%d invalid dispenser\n", BAD_DISPENSER_INDEX_ERROR);
                continue;
            }

            if (turn_on((uint8_t)disp))
                dprintf("0 ok\n");
            else
                dprintf("%d transmission error\n", TRANSMISSION_ERROR);
            continue;
        }

        if (strncasecmp(cmd, "off", 3) == 0)
        {
            int d = atoi(cmd + 4);
            if (d < 1 || d > g_num_dispensers)
            {
                dprintf("%d invalid dispenser\n", BAD_DISPENSER_INDEX_ERROR);
                continue;
            }
            if (turn_off(d))
                dprintf("0 ok\n");
            else
                dprintf("%d transmission error\n", TRANSMISSION_ERROR);
            continue;
        }

        if (strncasecmp(cmd, "disp", 4) == 0)
        {
            uint16_t dur, disp;
            uint8_t  ret;

            ret = sscanf(cmd, "disp %d %d", &disp, &dur);
            if (ret != 2)
            {
                dprintf("%d invalid command\n", INVALID_COMMAND_ERROR);
                continue;
            }
            if (disp < 1 || disp > (int)g_num_dispensers)
            {
                dprintf("%d invalid dispenser\n", BAD_DISPENSER_INDEX_ERROR);
                continue;
            }

            if (dispense((uint8_t)disp, dur))
                dprintf("0 ok\n");
            else
                dprintf("%d transmission error\n", TRANSMISSION_ERROR);
            continue;
        }

        if (strncasecmp(cmd, "state", 5) == 0)
        {
            uint8_t d = atoi(cmd + 6), state;
            if (d < 1 || d > g_num_dispensers)
            {
                dprintf("%d invalid dispenser\n", BAD_DISPENSER_INDEX_ERROR);
                continue;
            }
            if (get_state(d, &state))
                dprintf("0 %d ok\n", state);
            else
                dprintf("%d transmission error\n", TRANSMISSION_ERROR);
            continue;
        }

        if (strcasecmp(cmd, "debug") == 0)
        {
            if (g_debug == 0)
                dprintf("how may I do your bidding?\n");
            g_debug = 1;
            continue;
        }
        if (strcasecmp(cmd, "vdebug") == 0)
        {
            dprintf("# verbose debugging\n");
            g_debug = 2;
            continue;
        }
        if (strcasecmp(cmd, "nodebug") == 0)
        {
            dprintf("# no more debugging. Silence!\n");
            g_debug = 0;
            continue;
        }
        dprintf("%d unknown command\n", UNKNOWN_COMMAND_ERROR);
    }

    return 0;
}
