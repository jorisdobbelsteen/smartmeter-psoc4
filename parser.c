/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2021, Joris Dobbelsteen. */

//#pragma GCC diagnostic ignored "-Wunused-label"
//#pragma GCC diagnostic ignored "-Wunused-variable"
    
#include "dsmr.h"
#include <stdint.h>
#include <stdlib.h>

#if defined(NDEBUG) || __ARM_ARCH_6M__ || __ARM_ARCH_7M__ || __ARM_ARCH_7EM__
#  define DEBUGLOG(format, ...)
#else
#  include <stdio.h>
#  define DEBUGLOG(...) do { printf(__VA_ARGS__ ); printf("\n"); } while(0)
#endif

/*
/ISk5\2MT382-1000

1-3:0.2.8(40)
0-0:1.0.0(101209113020W)
0-0:96.1.1(4B384547303034303436333935353037)
1-0:1.8.1(123456.789*kWh)
1-0:1.8.2(123456.789*kWh)
1-0:2.8.1(123456.789*kWh)
1-0:2.8.2(123456.789*kWh)
0-0:96.14.0(0002)
1-0:1.7.0(01.193*kW)
1-0:2.7.0(00.000*kW)
0-0:17.0.0(016.1*kW)
0-0:96.3.10(1)
0-0:96.7.21(00004)
0-0:96.7.9(00002)
1-0:99.97.0(2)(0-0:96.7.19)(101208152415W)(0000000240*s)(101208151004W)(00000000301*s)
1-0:32.32.0(00002)
1-0:52.32.0(00001)
1-0:72.32.0(00000)
1-0:32.36.0(00000)
1-0:52.36.0(00003)
1-0:72.36.0(00000)
0-0:96.13.1(3031203631203831)
0-0:96.13.0(303132333435363738393A3B3C3D3E3F303132333435363738393A3B3C3D3E3F303132333435363738393A3B3C3D3E3F303132333435363738393A3B3C3D3E3F303132333435363738393A3B3C3D3E3F)
0-1:24.1.0(03)
0-1:96.1.0(3232323241424344313233343536373839)
0-1:24.2.1(101209110000W)(12785.123*m3)
0-1:24.4.0(1)
!522B

Definitions:
packet = "/" manufacturer versiondigit identifier CRLF CRLF packet_data "!" CRC CRLF
manufacturer = alphanum alphanum alphanum
CRC = 4 hex characters (MSB first) over data (from "/" to "!" inclusive) using CRC16 with polynomal x^16+x^15+x^2+1. No XOR in, no XOR out, LSB first
packet_data = line*
line = OBIS_id ( "(" value ( "*" unit )? ")" )+

// OBIS: A-B:C.D.E*F
//       1 2 2 2 1 3
// A = concept or physical medium (0 = abstract, 1 = electricity, 2 = heating, 3 = cooling, 4 = heat, 5 = gas, 6 = water, 7 = warm water)
// B = channel (0 = not used, d = difference, 1 = tentative, 2 = final, p = processing status
// C = type (0 = general purpose, 1 = imported active power/energy, 2 exported acgtive power/energy, 14 = frequency
//           21/41/61  phase A/B/C imported active power energy, 22/42/62 phase A/B/C exported active power energy
//           31/51/71 phase A/B/C current, 32/52/72 phase A/B/C voltage
//           C = service variable, F = error message, L = list object, ...
// D = measurement type (8 = energy, 7 = instantaneous, ...
// E = tariff (0 = total, other is tariff value)
// F = optional = billing period?
// https://github.com/lvzon/dsmr-p1-parser/blob/master/doc/IEC-62056-21-notes.md

OBIS_id = obis_concept "-" obis_channel ":" obis
*/

static enum parser_state_t {
    sreset,  // search for start of packet
    // header
    shstart,
    // line
    slerror, // line error, wait for CR
    slstart,
	slobisa,
	slobisab,
	slobisb,
	slobisbc,
	slobisc,
	slobiscd,
	slobisd,
	slobisde,
	slobise,
	slobisef,
	slobisf,
	// line data
	sldatanone,
	sldatauint32,
	sldatauint32d,
	sldataunit,
	sldatatimestamp,
	sldatatimestampy2,
	sldatatimestampmo1,
	sldatatimestampmo2,
	sldatatimestampd1,
	sldatatimestampd2,
	sldatatimestamph1,
	sldatatimestamph2,
	sldatatimestampmi1,
	sldatatimestampmi2,
	sldatatimestamps1,
	sldatatimestamps2,
	sldatatimestampdst,
	sldatatimestampend,
	// end of packet
    seend, // either CRC or CR
    secrc2,
    secrc3,
    secrc4,
    self
} parser_state;
static uint16_t parser_obis_a, parser_obis_b, parser_obis_c, parser_obis_d, parser_obis_e, parser_obis_f, parser_obis_field;

static union {
	struct {
		uint32_t uint;
		uint8_t decimal;
		uint8_t size; // 0 = uint32, 1 = uint8, 2 = uint16
	};
	struct dsmr_timestamp_t timestamp;
} parser_v;
static union {
	uint8_t* uint8;
	uint32_t* uint32;
	struct dsmr_timestamp_t* timestamp;
} parser_set;

static struct dsmr_data_t dsmr;
static void(*parser_error)(void) = NULL;
static void(*parser_packet_received)(struct dsmr_data_t*) = NULL;

static void Meter_Parser_ClearDSMR()
{
	dsmr_timestamp_clear(&dsmr.timestamp);
	dsmr_timestamp_clear(&dsmr.gas_timestamp);
	dsmr.tariff = 0;
    dsmr.E_in[0] = dsmr.E_in[1] = UINT32_MAX;
	dsmr.E_out[0] = dsmr.E_out[1]  = UINT32_MAX;
	dsmr.P_in_total = dsmr.P_out_total = dsmr.P_threshold = UINT32_MAX;
    dsmr.I[0] = dsmr.I[1] = dsmr.I[2] = UINT32_MAX;
	dsmr.V[0] = dsmr.V[1] = dsmr.V[2] = UINT32_MAX;
	dsmr.P_in[0] = dsmr.P_in[1] = dsmr.P_in[2] = UINT32_MAX;
    dsmr.P_out[0] = dsmr.P_out[1] = dsmr.P_out[2] = UINT32_MAX;
    dsmr.gas_in = UINT32_MAX;
}

void Meter_Parser_SetReceivedHandler(void(*packet_received_func)(struct dsmr_data_t*))
{
	parser_packet_received = packet_received_func;
}

void Meter_Parser_SetErrorHandler(void(*error_func)(void))
{
	parser_error = error_func;
}

void Meter_Parser_Reset()
{
    parser_state = sreset;
}

static enum parser_state_t parser_get_data_start()
{
	parser_v.uint = 0; parser_v.size = 0;
	DEBUGLOG("%d-%d:%d.%d.%d*%d #%d", parser_obis_a, parser_obis_b, parser_obis_c, parser_obis_d, parser_obis_e, parser_obis_f, parser_obis_field);
	// 1-0 (no fields)
	if (parser_obis_a == 1 && parser_obis_b == 0 && parser_obis_field == 0)
	{
		// 1-0:[12].8.[12](123456.789*kWh)
		if (parser_obis_d == 8 && parser_obis_e >= 1  && parser_obis_e <= MAX_TARIFFS) {
			parser_v.decimal = 3;
			if (parser_obis_c == 1) { DEBUGLOG(" E_in"); parser_set.uint32 = &(dsmr.E_in[parser_obis_e - 1]); return sldatauint32; }
			if (parser_obis_c == 2) { DEBUGLOG(" E_out"); parser_set.uint32 = &(dsmr.E_out[parser_obis_e - 1]); return sldatauint32; }
		}
		// 1-0:x.7.0
		if (parser_obis_d == 7 && parser_obis_e == 0) {
			parser_v.decimal = 3;
			switch(parser_obis_c) {
			// 1-0:[12].7.0(01.193*kW)
			case 1: DEBUGLOG(" P_in"); parser_set.uint32 = &(dsmr.P_in_total); return sldatauint32;
			case 2: DEBUGLOG(" P_out"); parser_set.uint32 = &(dsmr.P_out_total); return sldatauint32;
			// 1-0:[357]1.7.0(220.1*V) Current
			case 31: DEBUGLOG(" I1"); parser_set.uint32 = &(dsmr.I[0]); return sldatauint32;
			case 51: DEBUGLOG(" I2"); parser_set.uint32 = &(dsmr.I[1]); return sldatauint32;
			case 71: DEBUGLOG(" I3"); parser_set.uint32 = &(dsmr.I[2]); return sldatauint32;
			// 1-0:[357]2.7.0(001*A) Voltage
			case 32: DEBUGLOG(" V1"); parser_set.uint32 = &(dsmr.V[0]); return sldatauint32;
			case 52: DEBUGLOG(" V2"); parser_set.uint32 = &(dsmr.V[1]); return sldatauint32;
			case 72: DEBUGLOG(" V3"); parser_set.uint32 = &(dsmr.V[2]); return sldatauint32;
			// 1-0:[246]1.7.0(01.111*kW) P_in
			case 21: DEBUGLOG(" P_in1"); parser_set.uint32 = &(dsmr.P_in[0]); return sldatauint32;
			case 41: DEBUGLOG(" P_in2"); parser_set.uint32 = &(dsmr.P_in[1]); return sldatauint32;
			case 61: DEBUGLOG(" P_in3"); parser_set.uint32 = &(dsmr.P_in[2]); return sldatauint32;
			// 1-0:[246]2.7.0(04.444*kW) P_out
			case 22: DEBUGLOG(" P_out1"); parser_set.uint32 = &(dsmr.P_out[0]); return sldatauint32;
			case 42: DEBUGLOG(" P_out1"); parser_set.uint32 = &(dsmr.P_out[1]); return sldatauint32;
			case 62: DEBUGLOG(" P_out1"); parser_set.uint32 = &(dsmr.P_out[2]); return sldatauint32;
			default: break;
			}
		}
	}
	// 0-0 (no fields)
	if (parser_obis_a == 0 && parser_obis_b == 0 && parser_obis_field == 0) {
		// 0-0:1.0.0(101209113020W)
		if (parser_obis_c == 1 && parser_obis_d == 0 && parser_obis_e == 0) {
			DEBUGLOG(" timestamp");
			dsmr_timestamp_clear(&(parser_v.timestamp)); parser_set.timestamp = &(dsmr.timestamp); return sldatatimestamp;
		}
		// 0-0:17.0.0(016.1*kW)
		if (parser_obis_c == 17 && parser_obis_d == 0 && parser_obis_e == 0) {
			DEBUGLOG(" P_threshold");
			parser_v.decimal = 3; parser_set.uint32 = &(dsmr.P_threshold); return sldatauint32;
		}
		// 0-0:96.14.0
		if (parser_obis_c == 96 && parser_obis_d == 14 && parser_obis_e == 0) {
			DEBUGLOG(" tariff");
			parser_v.decimal = 0; parser_set.uint32 = &(dsmr.tariff); return sldatauint32;
		}
	}
	// 0-1 !!! fields !!!
	if (parser_obis_a == 0 && parser_obis_b == 1) {
		// 0-1:24.2.1(101209112500W)(12785.123*m3)
		if (parser_obis_c == 24 && parser_obis_d == 2 && parser_obis_e == 1) {
			if (parser_obis_field == 0) {
				DEBUGLOG(" gas_timestamp");
				dsmr_timestamp_clear(&(parser_v.timestamp)); parser_set.timestamp = &(dsmr.gas_timestamp); return sldatatimestamp;
			}
			if (parser_obis_field == 1) {
				DEBUGLOG(" gas_in");
				parser_v.decimal = 3; parser_set.uint32 = &(dsmr.gas_in); return sldatauint32;
			}
		}
		// 0-1:24.3.0(090212160000)(00)(60)(1)(0-1:24.2.1)(m3)(00000.000)
		// DSMR 2.2 only (legacy standard) -> if not set before
		if (parser_obis_c == 24 && parser_obis_d == 3 && parser_obis_e == 0 && dsmr.gas_in == UINT32_MAX) {
			if (parser_obis_field == 0) {
				DEBUGLOG(" gas_timestamp (legacy)");
				dsmr_timestamp_clear(&(parser_v.timestamp)); parser_set.timestamp = &(dsmr.gas_timestamp); return sldatatimestamp;
			}
			if (parser_obis_field == 6) {
				DEBUGLOG(" gas_in (legacy)");
				parser_v.decimal = 3; parser_set.uint32 = &(dsmr.gas_in); return sldatauint32;
			}
		}
	}
	return sldatanone;
}

static void parser_store_uint32()
{
	while (parser_v.decimal--)
		parser_v.uint *= 10;
	*parser_set.uint32 = parser_v.uint;
}

static void parser_store_timestamp()
{
	if (parser_v.timestamp.year < 100) parser_v.timestamp.year += 2000;
	*(parser_set.timestamp) = parser_v.timestamp;
}

void Meter_Parser_Parse(char c)
{
    // start of packet detection
    if (c == '/')
    {
        if (parser_state != sreset)
        {
            if (parser_error) parser_error();    
        }
        Meter_Parser_ClearDSMR();
        parser_state = shstart;
        DEBUGLOG("Start of packet");
        return;
    }
    
    // digit 0 - 9;
    uint8_t digit = (uint8_t)(c - '0');
#	define is_digit (digit < 10)
#	define add_digit(x) do { (x = (x * 10) + digit); }  while(0)

    enum parser_state_t prev_state = parser_state;
    switch(parser_state)
    {
        // sreset is ignored
    case shstart: // header start, ignore entire header
		if (c == '\r') { parser_state = slstart; DEBUGLOG("Start of line"); }
		break;
    case slerror: // ignore rest of line
    	if (prev_state != slerror) DEBUGLOG("Line error");
		if (c == '\r') parser_state = slstart;
		break;
    case slstart: // line start
		if (c == '\n') parser_state = slstart; // ignore LF
		else if (is_digit) { parser_obis_a = digit; parser_state = slobisa; } // obis_a
		else if (c == '!') parser_state = seend; // end of packet
		else parser_state = slerror;
		break;
    case slobisa:
    	if (is_digit) add_digit(parser_obis_a);
    	else if (c == '-') parser_state = slobisab;
    	else parser_state = slerror;
    	break;
    case slobisab:
		if (is_digit) { parser_obis_b = digit; parser_state = slobisb; }
		else parser_state = slerror;
		break;
    case slobisb:
    	if (is_digit) add_digit(parser_obis_b);
    	else if (c == ':') { parser_state = slobisbc; }
    	else parser_state = slerror;
    	break;
    case slobisbc:
		if (is_digit) { parser_obis_c = digit; parser_state = slobisc; }
		else parser_state = slerror;
		break;
    case slobisc:
    	if (is_digit) add_digit(parser_obis_c);
    	else if (c == '.') { parser_state = slobiscd; }
    	else parser_state = slerror;
    	break;
    case slobiscd:
		if (is_digit) { parser_obis_d = digit; parser_state = slobisd; }
		else parser_state = slerror;
		break;
    case slobisd:
    	if (is_digit) add_digit(parser_obis_d);
    	else if (c == '.') { parser_state = slobisde; }
    	else parser_state = slerror;
    	break;
    case slobisde:
    	parser_obis_field = 0; // reset value
		if (is_digit) { parser_obis_e = digit; parser_state = slobise; }
		else parser_state = slerror;
		break;
    case slobise:
    	if (is_digit) add_digit(parser_obis_e);
    	else if (c == '*') { parser_state = slobisef; }
    	else if (c == '(') { parser_obis_f = 255; parser_state = parser_get_data_start(); }
    	else parser_state = slerror;
    	break;
    case slobisef:
		if (is_digit) { parser_obis_f = digit; parser_state = slobisf; }
		else parser_state = slerror;
		break;
    case slobisf:
    	if (is_digit) add_digit(parser_obis_f);
    	else if (c == '(') parser_state = parser_get_data_start();
		else parser_state = slerror;
		break;
	// data
    case sldatanone:
		if (c == '\r') { parser_state = slstart; }
		else if (c == '(') { parser_obis_field++; parser_state = parser_get_data_start(); }
    	break;
    case sldatauint32:
    	if (is_digit) add_digit(parser_v.uint);
    	else if (c == '.') { parser_state = sldatauint32d; }
    	else if (c == '*') { parser_store_uint32(); parser_state = sldataunit; }
    	else if (c == ')') { parser_store_uint32(); parser_state = sldatanone; }
		else parser_state = slerror;
		break;
    case sldatauint32d:
    	if (is_digit) { if(parser_v.decimal--) add_digit(parser_v.uint); }
    	else if (c == '*') { parser_store_uint32(); parser_state = sldataunit; }
    	else if (c == ')') { parser_store_uint32(); parser_state = sldatanone; }
		else parser_state = slerror;
		break;
    case sldataunit:
    	if (c == ')') { parser_state = sldatanone; }
    	// ignore everything else
		break;
    case sldatatimestamp:
    case sldatatimestampy2:
    	if (is_digit) { add_digit(parser_v.timestamp.year); parser_state++; }
		else parser_state = slerror;
    	break;
    case sldatatimestampmo1:
    case sldatatimestampmo2:
    	if (is_digit) { add_digit(parser_v.timestamp.month); parser_state++; }
		else parser_state = slerror;
    	break;
    case sldatatimestampd1:
    case sldatatimestampd2:
    	if (is_digit) { add_digit(parser_v.timestamp.day); parser_state++; }
		else parser_state = slerror;
    	break;
    case sldatatimestamph1:
    case sldatatimestamph2:
    	if (is_digit) { add_digit(parser_v.timestamp.hour); parser_state++; }
		else parser_state = slerror;
    	break;
    case sldatatimestampmi1:
    case sldatatimestampmi2:
    	if (is_digit) { add_digit(parser_v.timestamp.minute); parser_state++; }
		else parser_state = slerror;
    	break;
    case sldatatimestamps1:
    case sldatatimestamps2:
    	if (is_digit) { add_digit(parser_v.timestamp.second); parser_state++; }
		else parser_state = slerror;
    	break;
    case sldatatimestampdst:
    	if (c == 'W') { parser_v.timestamp.dst = 1; parser_state = sldatatimestampend; }
    	else if (c == 'S') { parser_v.timestamp.dst = 2; parser_state = sldatatimestampend; }
    	else if (c == ')') { parser_store_timestamp(); parser_state = sldatanone; }
		else parser_state = slerror;
    	break;
    case sldatatimestampend:
    	if (c == ')') { parser_store_timestamp(); parser_state = sldatanone; }
		else parser_state = slerror;
    	break;
	// end
    case seend: // either CRC or CR
		DEBUGLOG("End of packet");
		if (c == '\r') parser_state = self;
		else parser_state = secrc2;
		break;
    case secrc2:
		parser_state = secrc3;
		break;
    case secrc3:
		parser_state = secrc4;
		break;
    case secrc4:
		parser_state = self;
		break;
    case self:
		if (parser_packet_received) parser_packet_received(&dsmr);
		parser_state = sreset;
		break;
    case sreset:
		// ignore
		break;
    default:
		DEBUGLOG("Invalid state %d", parser_state);
		parser_state = sreset;
		break;
    }
#	undef is_digit
}
