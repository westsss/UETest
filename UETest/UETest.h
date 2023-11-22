#pragma once

#include "resource.h"
#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <thread>
#include <vector>

using namespace DirectX;

// Global variables
extern HWND g_hwnd;

extern void  StartRenderThead();
extern void  WaitRenderThead();

extern bool g_bQuit;