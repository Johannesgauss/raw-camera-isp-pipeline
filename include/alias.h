#ifndef UTIL_ALIAS_H
#define UTIL_ALIAS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef int8_t byte;
typedef uint8_t ubyte;

typedef int8_t tiny;
typedef uint8_t utiny;

typedef unsigned short ushort;
typedef unsigned int uint;

typedef long long int max;
typedef unsigned long long int umax;

#define global static
#define intern static
#define local static

#endif // UTIL_ALIAS_H
