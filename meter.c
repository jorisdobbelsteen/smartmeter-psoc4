/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2021, Joris Dobbelsteen. */

#include "meter.h"
#include "common.h"
#include "parser.h"
#include "dsmr.h"
#include <project.h>

#include <stdio.h>

static enum METER_STATE_T
{
    METER_STATE_STOPPED,   // Functionality disabled
    METER_STATE_SLEEP,     // Waiting between two intervals
    METER_STATE_RECEIVING  // Communication and request active
} meter_state = METER_STATE_STOPPED;

static enum METER_ISR_STATE_T
{
    METER_ISR_RECEIVE,
    METER_ISR_WAITFORSTART
} meter_isr_state = METER_ISR_WAITFORSTART;

#define UART_BUFFERSIZE 256
static volatile char uart_buffer[UART_BUFFERSIZE];
static uint8 uart_read_loc = 0;
static volatile uint8 uart_write_loc = 0;
static volatile uint8 wdt_triggered = 0;

static const int uart_receive_timeout = 10;

CY_ISR_PROTO(ISR_UART_Meter_Interrupt);
CY_ISR_PROTO(Meter_Wdt_Timer2_Callback);

static void Meter_Dsmr_Received(struct dsmr_data_t*);
static void Meter_Dsmr_ParserError();

static void(*Meter_Dsmr_ReceivedHandler)(struct dsmr_data_t*) = NULL;

static void Meter_Receive_Start();
static void Meter_Receive_Stop();

// Trigger in at least delay time (but not too much more)
static void Meter_Wdt_TriggerIn(int delay);

void Meter_Start(void)
{
    CYASSERT(meter_state == METER_STATE_STOPPED);

    // Set LED to on as we start receiving
    LED_Meter_Write(LED_ON);
    
    // ISR init
    meter_isr_state = METER_ISR_WAITFORSTART;

    // Uart init
    UART_Meter_SetCustomInterruptHandler(ISR_UART_Meter_Interrupt);
    Meter_Parser_SetReceivedHandler(Meter_Dsmr_Received);
    Meter_Parser_SetErrorHandler(Meter_Dsmr_ParserError);
    
    // Enable receiving mode
    Meter_Receive_Start();
    // Set timeout
    Meter_Wdt_TriggerIn(uart_receive_timeout);
    meter_state = METER_STATE_RECEIVING;

    // Enable WDT2
    CySysWdtSetIsrCallback(CY_SYS_WDT_COUNTER2, Meter_Wdt_Timer2_Callback);
    CySysWdtEnableCounterIsr(CY_SYS_WDT_COUNTER2);
}

enum METER_POWER_STATE_T Meter_GetPowerState()
{
    if (meter_state == METER_STATE_STOPPED 
        || (meter_state == METER_STATE_SLEEP && wdt_triggered != 0))
    {
        // UART inactive
        return METER_POWER_STATE_DEEPSLEEP;
    }
    else if (uart_read_loc != uart_write_loc
            || wdt_triggered == 0)
    {
        // Need to call Meter_ProcessEvents
        return METER_POWER_STATE_ACTIVE;
    }
    else
    {
        // UART active, keeps clock device active so deep sleep is not possible
        return METER_POWER_STATE_SLEEP;
    }
}

void Meter_ProcessEvents()
{
    switch(meter_state)
    {
    case METER_STATE_STOPPED:
        CYASSERT(0);
        break;
    case METER_STATE_SLEEP:
        if (wdt_triggered == 0)
        {
            //printf("Sleep timeout %lu\n", CySysWdtGetCount(CY_SYS_WDT_COUNTER2));
            // Start receiving
            LED_Meter_Write(LED_ON);
            Meter_Receive_Start();
            // Set timeout
            Meter_Wdt_TriggerIn(uart_receive_timeout);
            meter_state = METER_STATE_RECEIVING;
        }
        break;
    case METER_STATE_RECEIVING:
        while (uart_read_loc != uart_write_loc)
        {
            // parse character
            Meter_Parser_Parse(uart_buffer[uart_read_loc]);
            uart_read_loc++;
        }
        // Meter_Parser_Parse might call Meter_Dsmr_Received, which will disable wdt_triggered
        if (wdt_triggered == 0)
        {
            //printf("Receive timeout %lu\n", CySysWdtGetCount(CY_SYS_WDT_COUNTER2));
            // Leave meter LED on
            Meter_Receive_Stop();
            // Set timeout
            Meter_Wdt_TriggerIn(30 - uart_receive_timeout);
            meter_state = METER_STATE_SLEEP;
        }
        break;
    }
}

static void Meter_Receive_Start()
{
    //printf("Meter Receive Start\n");
    // Restore XOR
    Meter_Invert_VALUE_Write(1);
    Meter_Parser_Reset();
//    Meter_Invert_IN_Write(1); // Pull high, switch on resistive pull-up
//    Meter_Invert_IN_SetDriveMode(Meter_Invert_IN_DM_RES_UPDWN);
    // What about the OUT and UART_in pins?
    UART_Meter_Start();
    // Initiate request, enable strong drive to 5 volt
    Meter_Request_OUT_Write(3);
}

static void Meter_Receive_Stop()
{
    //printf("Meter Receive Stop\n");
    UART_Meter_Stop();
    // Disable request, lower request line, disables drive
    Meter_Request_OUT_Write(0);
    //Meter_Invert_VALUE_Sleep(); // after deep sleep, its set anyways
    // Pin to High-Z, disable pull-up
//    Meter_Invert_IN_SetDriveMode(Meter_Invert_IN_DM_DIG_HIZ);
    // What about the OUT and UART_in pins? Floating I/O can consume power, minimum leakage is better.
}

static void Meter_Wdt_TriggerIn(int delay)
{
    //printf("TriggerIn %d at %lu\n", delay, CySysWdtGetCount(CY_SYS_WDT_COUNTER2));
    // reset trigger, ensure no WDT interrupt happens here
    uint8 intStatus = CyEnterCriticalSection();
    wdt_triggered = (delay + 1) >> 1;
    CyExitCriticalSection(intStatus);
}


static void Meter_Dsmr_Received(struct dsmr_data_t* data)
{
    //printf("Parsed data at %lu\n", CySysWdtGetCount(CY_SYS_WDT_COUNTER2));
    if (Meter_Dsmr_ReceivedHandler)
    {
        Meter_Dsmr_ReceivedHandler(data);
    }
    
    if (meter_state == METER_STATE_RECEIVING)
    {
        LED_Meter_Write(LED_OFF); // Success, turn LED off
        Meter_Receive_Stop();
        meter_state = METER_STATE_SLEEP;
        
        int delay = 30;
        if (data->timestamp.second < 60)
        {
            delay = 60 - data->timestamp.second;
            if (delay > 30)
                delay -= 30;
        }
        Meter_Wdt_TriggerIn(delay);
    }
}

static void Meter_Dsmr_ParserError()
{
    printf("Parsing failed\n");

    // Ignored, as these might be suprious.
    // Handled with timeout instead
}

void Meter_SetReceivedDsmrHandler(void(*handler)(struct dsmr_data_t*))
{
    Meter_Dsmr_ReceivedHandler = handler;
}

CY_ISR(ISR_UART_Meter_Interrupt)
{
    /* Returns the status/identity of which enabled RX interrupt source caused interrupt event */
    uint32 source = UART_Meter_GetRxInterruptSource();
	
	/* Checks for "RX FIFO AND not empty" interrupt */
    if(UART_Meter_INTR_RX_NOT_EMPTY & source)
    {
        /* Get the character from terminal */
        do {
            char data = UART_Meter_SpiUartReadRxData();
            if (meter_isr_state == METER_ISR_RECEIVE || data == '/')
            {
                uart_buffer[uart_write_loc++] = data;
                meter_isr_state = METER_ISR_RECEIVE;
            }
        } while (UART_Meter_SpiUartGetRxBufferSize());
                
        /* Clear UART "RX FIFO not empty interrupt" */
        UART_Meter_ClearRxInterruptSource(UART_Meter_INTR_RX_NOT_EMPTY);
    }
    if ((UART_Meter_INTR_RX_OVERFLOW | UART_Meter_INTR_RX_FRAME_ERROR) & source) {
        meter_isr_state = METER_ISR_WAITFORSTART;
        UART_Meter_ClearRxInterruptSource(UART_Meter_INTR_RX_OVERFLOW | UART_Meter_INTR_RX_FRAME_ERROR);
        // TODO: Manage receive error when they happen, signal upstream...
    }
    UART_Meter_ClearPendingInt();
}

CY_ISR(Meter_Wdt_Timer2_Callback)
{
    // set flag
    uint8 value = wdt_triggered;
    if (value > 0)
        value--;
    wdt_triggered = value;
}
