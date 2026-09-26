#pragma once

#include <RE/Oblivion.h>
#include <OBSE/OBSE.h>

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>

// wingdi.h defines ERROR as 0, which turns every REX::ERROR log call into a syntax error
#ifdef ERROR
#	undef ERROR
#endif

#include <atomic>
#include <mutex>
#include <string>
#include <vector>
