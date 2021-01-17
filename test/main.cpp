extern "C" {
#include "parser.h"
#include "dsmr.h"
}
#include <cstdio>
#include <iostream>

const char* input30 =
"/ISk5\2MT382-1000\r\n"
"\r\n"
"0-0:96.1.1(4B384547303034303436333935353037)\r\n"
"1-0:1.8.1(12345.678*kWh)\r\n"
"1-0:1.8.2(12345.678*kWh)\r\n"
"1-0:2.8.1(12345.678*kWh)\r\n"
"1-0:2.8.2(12345.678*kWh)\r\n"
"0-0:96.14.0(0002)\r\n"
"1-0:1.7.0(001.19*kW)\r\n"
"1-0:2.7.0(000.00*kW)\r\n"
"0-0:17.0.0(016*A)\r\n"
"0-0:96.3.10(1)\r\n"
"0-0:96.13.1(303132333435363738)\r\n"
"0-0:96.13.0(303132333435363738393A3B3C3D3E3F303132333435363738393A3B3C3D3E3F303132333435363738393A3B3C3D3E3F303132333435363738393A3B3C3D3E3F303132333435363738393A3B3C3D3E3F)\r\n"
"0-1:96.1.0(3232323241424344313233343536373839)\r\n"
"0-1:24.1.0(03)\r\n"
"0-1:24.3.0(090212160000)(00)(60)(1)(0-1:24.2.1)(m3)(00000.000)\r\n"
"0-1:24.4.0(1)\r\n"
"!\r\n";


const char* input40 =
"/ISk5\2MT382-1000\r\n"
"\r\n"
"1-3:0.2.8(40)\r\n"
"0-0:1.0.0(101209113020W)\r\n"
"0-0:96.1.1(4B384547303034303436333935353037)\r\n"
"1-0:1.8.1(123456.789*kWh)\r\n"
"1-0:1.8.2(123456.789*kWh)\r\n"
"1-0:2.8.1(123456.789*kWh)\r\n"
"1-0:2.8.2(123456.789*kWh)\r\n"
"0-0:96.14.0(0002)\r\n"
"1-0:1.7.0(01.193*kW)\r\n"
"1-0:2.7.0(00.000*kW)\r\n"
"0-0:17.0.0(016.1*kW)\r\n"
"0-0:96.3.10(1)\r\n"
"0-0:96.7.21(00004)\r\n"
"0-0:96.7.9(00002)\r\n"
"1-0:99.97.0(2)(0-0:96.7.19)(101208152415W)(0000000240*s)(101208151004W)(00000000301*s)\r\n"
"1-0:32.32.0(00002)\r\n"
"1-0:52.32.0(00001)\r\n"
"1-0:72.32.0(00000)\r\n"
"1-0:32.36.0(00000)\r\n"
"1-0:52.36.0(00003)\r\n"
"1-0:72.36.0(00000)\r\n"
"0-0:96.13.1(3031203631203831)\r\n"
"0-0:96.13.0(303132333435363738393A3B3C3D3E3F303132333435363738393A3B3C3D3E3F303132333435363738393A3B3C3D3E3F303132333435363738393A3B3C3D3E3F303132333435363738393A3B3C3D3E3F)\r\n"
"0-1:24.1.0(03)\r\n"
"0-1:96.1.0(3232323241424344313233343536373839)\r\n"
"0-1:24.2.1(101209110000W)(12785.123*m3)\r\n"
"0-1:24.4.0(1)\r\n"
"!522B\r\n";

const char* input50 =
"/ISk5\2MT382-1000\r\n"
"\r\n"
"1-3:0.2.8(50)\r\n"
"0-0:1.0.0(101209113020W)\r\n"
"0-0:96.1.1(4B384547303034303436333935353037)\r\n"
"1-0:1.8.1(123456.789*kWh)\r\n"
"1-0:1.8.2(123456.789*kWh)\r\n"
"1-0:2.8.1(123456.789*kWh)\r\n"
"1-0:2.8.2(123456.789*kWh)\r\n"
"0-0:96.14.0(0002)\r\n"
"1-0:1.7.0(01.193*kW)\r\n"
"1-0:2.7.0(00.000*kW)\r\n"
"0-0:96.7.21(00004)\r\n"
"0-0:96.7.9(00002)\r\n"
"1-0:99.97.0(2)(0-0:96.7.19)(101208152415W)(0000000240*s)(101208151004W)(0000000301*s)\r\n"
"1-0:32.32.0(00002)\r\n"
"1-0:52.32.0(00001)\r\n"
"1-0:72.32.0(00000)\r\n"
"1-0:32.36.0(00000)\r\n"
"1-0:52.36.0(00003)\r\n"
"1-0:72.36.0(00000)\r\n"
"0-0:96.13.0(303132333435363738393A3B3C3D3E3F303132333435363738393A3B3C3D3E3F303132333435363738393A3B3C3D3E3F303132333435363738393A3B3C3D3E3F303132333435363738393A3B3C3D3E3F)\r\n"
"1-0:32.7.0(220.1*V)\r\n"
"1-0:52.7.0(220.2*V)\r\n"
"1-0:72.7.0(220.3*V)\r\n"
"1-0:31.7.0(001*A)\r\n"
"1-0:51.7.0(002*A)\r\n"
"1-0:71.7.0(003*A)\r\n"
"1-0:21.7.0(01.111*kW)\r\n"
"1-0:41.7.0(02.222*kW)\r\n"
"1-0:61.7.0(03.333*kW)\r\n"
"1-0:22.7.0(04.444*kW)\r\n"
"1-0:42.7.0(05.555*kW)\r\n"
"1-0:62.7.0(06.666*kW)\r\n"
"0-1:24.1.0(003)\r\n"
"0-1:96.1.0(3232323241424344313233343536373839)\r\n"
"0-1:24.2.1(101209112500W)(12785.123*m3)\r\n"
"!EF2F\r\n";


struct dsmr_data_t parsed_data;
bool parsed_got_data;
bool parsed_got_error;

void parser_packet_received_handler(struct dsmr_data_t* data)
{
	parsed_data = *data;
	parsed_got_data = true;
}
void parser_error_handler(void)
{
	parsed_got_error = true;
}

std::ostream& operator<<(std::ostream& os, struct dsmr_timestamp_t& timestamp)
{
	return os
			<< timestamp.year << "-" << (int)timestamp.month << "-" << (int)timestamp.day
			<< "_" << (int)timestamp.hour << ":" << (int)timestamp.minute << ":" << (int)timestamp.second
			<< "_" << (int)timestamp.dst;
}

void parse(const char* input)
{
	Meter_Parser_Reset();
	parsed_got_data = parsed_got_error = false;
	for (const char* i = input; *i != 0; ++i)
	{
//		std::cout << "    " << *i << std::endl;
		Meter_Parser_Parse(*i);
	}

	std::cout <<  "Got data = " << (parsed_got_data ? "yes" : "no")
			<< ", error = " << (parsed_got_error ? "yes" : "no") << std::endl;

	std::cout << "\ttimestamp   " << parsed_data.timestamp << std::endl;
	std::cout << "\ttariff      " << parsed_data.tariff << std::endl;
	std::cout << "\tP_in_total  " << parsed_data.P_in_total << std::endl;
	std::cout << "\tP_out_total " << parsed_data.P_out_total << std::endl;
	std::cout << "\tP_threshold " << parsed_data.P_threshold << std::endl;
	std::cout << "\tE_in        0: " << parsed_data.E_in[0] << "  1: " << parsed_data.E_in[1] << std::endl;
	std::cout << "\tE_out       0: " << parsed_data.E_out[0] << "  1: " << parsed_data.E_out[1] << std::endl;
	std::cout << "\tV           0: " << parsed_data.V[0] << "  1: " << parsed_data.V[1] << "  2: " << parsed_data.V[2] << std::endl;
	std::cout << "\tI           0: " << parsed_data.I[0] << "  1: " << parsed_data.I[1] << "  2: " << parsed_data.I[2] << std::endl;
	std::cout << "\tP_in        0: " << parsed_data.P_in[0] << "  1: " << parsed_data.P_in[1] << "  2: " << parsed_data.P_in[2] << std::endl;
	std::cout << "\tP_out       0: " << parsed_data.P_out[0] << "  1: " << parsed_data.P_out[1] << "  2: " << parsed_data.P_out[2] << std::endl;
	std::cout << "\tG_timestamp " << parsed_data.gas_timestamp << std::endl;
	std::cout << "\tG_in        " << parsed_data.gas_in << std::endl;
}

int main()
{
	printf("test\n");

	Meter_Parser_SetReceivedHandler(&parser_packet_received_handler);
	Meter_Parser_SetErrorHandler(&parser_error_handler);

	parse(input30);
	parse(input40);
	parse(input50);
}
