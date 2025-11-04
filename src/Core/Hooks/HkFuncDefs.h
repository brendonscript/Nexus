///----------------------------------------------------------------------------------------------------
/// Copyright (c) Raidcore.GG - All rights reserved.
///
/// Name         :  HkFuncDefs.h
/// Description  :  Function definitions for hooked/detoured functions.
/// Authors      :  K. Bieniek
///----------------------------------------------------------------------------------------------------

#ifndef HKFUNCDEFS_H
#define HKFUNCDEFS_H

#include <dxgi.h>
#include <dxgi1_2.h>

typedef HRESULT (__stdcall* DXPRESENT)      (IDXGISwapChain* pChain, UINT SyncInterval, UINT Flags);
typedef HRESULT (__stdcall* DXPRESENT1)     (IDXGISwapChain1* pChain, UINT SyncInterval, UINT PresentFlags, const DXGI_PRESENT_PARAMETERS* pPresentParameters);
typedef HRESULT (__stdcall* DXRESIZEBUFFERS)(IDXGISwapChain* pChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);

#endif
