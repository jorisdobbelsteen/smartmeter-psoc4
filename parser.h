/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2021, Joris Dobbelsteen. */

#ifndef PARSER_H
#define PARSER_H

struct dsmr_data_t;

void Meter_Parser_Reset();
void Meter_Parser_Parse(char c);

void Meter_Parser_SetReceivedHandler(void(*parser_packet_received)(struct dsmr_data_t*));
void Meter_Parser_SetErrorHandler(void(*parser_error)(void));

#endif // PARSER_H
