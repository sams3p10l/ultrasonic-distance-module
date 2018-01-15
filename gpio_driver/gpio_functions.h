#ifndef _GPIO_FUNCTIONS_H_
#define _GPIO_FUNCTIONS_H_

#include "defines.h"

unsigned int GetGPFSELReg(char pin);
char GetGPIOPinOffset(char pin);
void SetInternalPullUpDown(char pin, PUD pull);
void SetGpioPinDirection(char pin, DIRECTION direction);
void SetGpioPin(char pin);
void ClearGpioPin(char pin);
char GetGpioPinValue(char pin);

#endif
