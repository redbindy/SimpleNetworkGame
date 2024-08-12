#pragma once

#include <iostream>

#include <WinSock2.h>
#pragma comment(lib, "ws2_32.lib")

#include <Windows.h>

#include "Debug.h"

#define SERVER (true)

namespace App
{
	void Initialize();
	void Destroy();
	int Run();

	template <class COM>
	static inline void ReleaseCOM(COM*& com)
	{
		if (com != nullptr)
		{
			com->Release();
			com = nullptr;
		}
	}
}
