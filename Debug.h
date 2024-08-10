#pragma once
#include <cstdio>
#include <Windows.h>

#if defined(_DEBUG) || defined(DEBUG)

#define ASSERT(expr, msg) \
	if (!(expr)) \
	{ \
		fprintf(stderr, "%s\n", (msg)); \
		DebugBreak(); \
	} \

#endif