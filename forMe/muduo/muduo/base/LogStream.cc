#include "muduo/base/LogStream.h"

#include <algorithm>
#include <limits>
#include <type_traits>
#include <assert.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif

#include <inttypes.h>

using namespace muduo;
using namespace muduo::detail;

// TODO: better itoa.


/*      todoList:
const char* debugString();

static void cookieStart();
static void cookieEnd();

self& operator<<(short);
self& operator<<(unsigned short);
self& operator<<(int);
self& operator<<(unsigned int);
self& operator<<(long);
self& operator<<(unsigned long);
self& operator<<(long long);
self& operator<<(unsigned long long);

self& operator<<(const void*);

self& operator<<(double);

void staticCheck();

template<typename T>
void formatInteger(T);

template<typename T>
Fmt(const char* fmt, T val);

string formatSI(int64_t n);  
string formatIEC(int64_t n); 
*/