// picoUART print functions
#pragma once
#include <avr/pgmspace.h>

extern "C" void prints(const char* s);
extern "C" void prints_P(const char* s);
