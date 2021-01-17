/*
 * Copyright 2016, Cypress Semiconductor Corporation.
 * This file contains functions for printf functionality.
 */

#include "UART_Debug_SPI_UART.h"

/* For GCC compiler revise _write() function for printf functionality */
int _write(int file, char *ptr, int len)
{
    int i;
    (void)file;
    for (i = 0; i < len; i++)
    {
        UART_Debug_UartPutChar(*ptr++);
    }
    return len;
}
