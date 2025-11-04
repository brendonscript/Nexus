///----------------------------------------------------------------------------------------------------
/// Copyright (c) Raidcore.GG - All rights reserved.
///
/// Name         :  Hooks.cpp
/// Description  :  Implementation for hooked/detoured functions.
/// Authors      :  K. Bieniek
///----------------------------------------------------------------------------------------------------

#include "Hooks.h"

#include <mutex>

#include "minhook/mh_hook.h"

#include "Core/Context.h"
#include "Core/Main.h"
#include "Engine/DataLink/DlApi.h"
#include "Engine/Events/EvtApi.h"
#include "Engine/Inputs/InputBinds/IbApi.h"
#include "Engine/Inputs/RawInput/RiApi.h"
#include "Engine/Loader/Loader.h"
#include "Engine/Loader/NexusLinkData.h"
#include "Engine/Renderer/RdrContext.h"
#include "Engine/Textures/TxLoader.h"
#include "GW2/Inputs/MouseResetFix.h"
#include "HkConst.h"
#include "UI/UiContext.h"
#include "Util/CmdLine.h"
#include "Util/Memory.h"

namespace Hooks
{
	void HookIDXGISwapChain()
	{
		if (CmdLine::HasArgument("-ggvanilla"))                             { return; }
		if (Hooks::Target::DXGIPresent && Hooks::Target::DXGIResizeBuffers) { return; }

		/* Create and register window class for temp window. */
		WNDCLASSEXA wc{};
		wc.cbSize = sizeof(wc);
		wc.lpfnWndProc = DefWindowProcA;
		wc.hInstance = GetModuleHandleA(0);
		wc.hbrBackground = (HBRUSH)(COLOR_WINDOW);
		wc.lpszClassName = "Raidcore_Dx_Window_Class";
		RegisterClassExA(&wc);

		/* Create the temp window. */
		HWND wnd = CreateWindowExA(0, wc.lpszClassName, 0, WS_OVERLAPPED, 0, 0, 1280, 720, 0, 0, wc.hInstance, 0);

		/* If window creation failed, deregister the class and log an error. */
		if (!wnd)
		{
			UnregisterClassA(wc.lpszClassName, wc.hInstance);

			CContext* ctx    = CContext::GetContext();
			CLogApi*  logger = ctx->GetLogger();

			logger->Critical(CH_CORE, "Failed creating temporary window.");
			return;
		}

		/* Create description for our temp swapchain. */
		DXGI_SWAP_CHAIN_DESC swapChainDesc{};
		swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swapChainDesc.BufferCount = 1;
		swapChainDesc.SampleDesc.Count = 1;
		swapChainDesc.OutputWindow = wnd;
		swapChainDesc.Windowed = TRUE;

		/* Temporary interface pointers used to hook. */
		ID3D11Device*        device;
		ID3D11DeviceContext* context;
		IDXGISwapChain*      swap;

		/* Create the swapchain. */
		if (SUCCEEDED(D3D11CreateDeviceAndSwapChain(0, D3D_DRIVER_TYPE_HARDWARE, 0, 0, 0, 0, D3D11_SDK_VERSION, &swapChainDesc, &swap, &device, 0, &context)))
		{
			LPVOID* vtbl = *(LPVOID**)swap;

			/* Create and enable VT hooks. */
			/* Follow the jump chain to work nicely with various other hooks. */
			MH_CreateHook(Memory::FollowJmpChain((PBYTE)vtbl[8]),  (LPVOID)&Detour::DXGIPresent,       (LPVOID*)&Target::DXGIPresent      );
			MH_CreateHook(Memory::FollowJmpChain((PBYTE)vtbl[13]), (LPVOID)&Detour::DXGIResizeBuffers, (LPVOID*)&Target::DXGIResizeBuffers);
			MH_EnableHook(MH_ALL_HOOKS);

			/* Release the temporary interfaces. */
			context->Release();
			device->Release();
			swap->Release();
		}
		else
		{
			CContext* ctx = CContext::GetContext();
			CLogApi* logger = ctx->GetLogger();

			logger->Critical(CH_CORE, "Failed to create D3D11 device and swapchain.");
		}

		/* Destroy the temporary window. */
		DestroyWindow(wnd);
	}

	namespace Target
	{
		WNDPROC         WndProc           = nullptr;
		DXPRESENT       DXGIPresent       = nullptr;
		DXRESIZEBUFFERS DXGIResizeBuffers = nullptr;
	}

	namespace Detour
	{
		LRESULT __stdcall WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
		{
			static CContext*      s_Context      = CContext::GetContext();
			static CInputBindApi* s_InputBindApi = s_Context->GetInputBindApi();
			static CRawInputApi*  s_RawInputApi  = s_Context->GetRawInputApi();
			static CUiContext*    s_UIContext    = s_Context->GetUIContext();

			// don't pass to game if loader
			if (Loader::WndProc(hWnd, uMsg, wParam, lParam) == 0) { return 0; }

			// don't pass to game if custom wndproc
			if (s_RawInputApi->WndProc(hWnd, uMsg, wParam, lParam) == 0) { return 0; }

			// don't pass to game if gui
			if (s_UIContext->WndProc(hWnd, uMsg, wParam, lParam) == 0) { return 0; }

			// don't pass to game if InputBind
			if (s_InputBindApi->WndProc(hWnd, uMsg, wParam, lParam) == 0) { return 0; }

			if (uMsg == WM_DESTROY)
			{
				Main::Shutdown(uMsg);
			}

			/* offset of 7997, if uMsg in that range it's a nexus game only message */
			if (uMsg >= WM_PASSTHROUGH_FIRST && uMsg <= WM_PASSTHROUGH_LAST)
			{
				/* modify the uMsg code to the original code */
				uMsg -= WM_PASSTHROUGH_FIRST;
			}

			MouseResetFix(hWnd, uMsg, wParam, lParam);

			return CallWindowProcA(Target::WndProc, hWnd, uMsg, wParam, lParam);
		}

		HRESULT __stdcall DXGIPresent(IDXGISwapChain* pChain, UINT SyncInterval, UINT Flags)
		{
			// Thread-safe initialization for NVIDIA Smooth Motion / Frame Generation compatibility
			static std::once_flag s_InitFlag;
			static CContext*        s_Context       = nullptr;
			static RenderContext_t* s_RenderCtx     = nullptr;
			static CTextureLoader*  s_TextureLoader = nullptr;
			static CUiContext*      s_UIContext     = nullptr;

			// Initialize once using std::call_once for thread safety
			std::call_once(s_InitFlag, []() {
				s_Context = CContext::GetContext();
				if (s_Context)
				{
					s_RenderCtx     = s_Context->GetRendererCtx();
					s_TextureLoader = s_Context->GetTextureService();
					s_UIContext     = s_Context->GetUIContext();
				}
			});

			// If initialization failed, pass through without modification
			if (!s_RenderCtx || !s_TextureLoader || !s_UIContext)
			{
				return Target::DXGIPresent(pChain, SyncInterval, Flags);
			}

			/* The swap chain we used to hook is different than the one the game created.
			 * NVIDIA Smooth Motion may present with the same swap chain from different threads,
			 * so only initialize if swap chain actually changed, and don't block on it. */
			if (s_RenderCtx->SwapChain != pChain)
			{
				// Use atomic compare-exchange to avoid blocking NVIDIA's frame generation
				IDXGISwapChain* expected = s_RenderCtx->SwapChain;
				if (expected != pChain)
				{
					s_RenderCtx->SwapChain = pChain;

					if (s_RenderCtx->Device)
					{
						s_RenderCtx->Device->Release();
						s_RenderCtx->Device = nullptr;
					}

					if (s_RenderCtx->DeviceContext)
					{
						s_RenderCtx->DeviceContext->Release();
						s_RenderCtx->DeviceContext = nullptr;
					}

					if (pChain)
					{
						HRESULT hr = pChain->GetDevice(__uuidof(ID3D11Device), (void**)&s_RenderCtx->Device);
						if (SUCCEEDED(hr) && s_RenderCtx->Device)
						{
							s_RenderCtx->Device->GetImmediateContext(&s_RenderCtx->DeviceContext);

							DXGI_SWAP_CHAIN_DESC swapChainDesc{};
							pChain->GetDesc(&swapChainDesc);

							s_RenderCtx->Window.Handle = swapChainDesc.OutputWindow;
							Target::WndProc = (WNDPROC)SetWindowLongPtr(s_RenderCtx->Window.Handle, GWLP_WNDPROC, (LONG_PTR)Detour::WndProc);

							Loader::Initialize();
						}
					}
				}
			}

			// Only process queue, textures, and UI if we have a valid device
			// NVIDIA frame generation may call this before device is ready
			if (s_RenderCtx->Device && s_RenderCtx->DeviceContext)
			{
				Loader::ProcessQueue();
				s_TextureLoader->Advance();
				s_UIContext->Render();
				s_RenderCtx->Metrics.FrameCount++;
			}

			return Target::DXGIPresent(pChain, SyncInterval, Flags);
		}

		HRESULT __stdcall DXGIResizeBuffers(IDXGISwapChain* pChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags)
		{
			static CContext*        s_Context   = CContext::GetContext();
			static CDataLinkApi*    s_DataLink  = s_Context->GetDataLink();
			static CEventApi*       s_EventApi  = s_Context->GetEventApi();
			static RenderContext_t* s_RenderCtx = s_Context->GetRendererCtx();
			static CUiContext*      s_UIContext = s_Context->GetUIContext();

			s_UIContext->Shutdown();

			/* Cache window dimensions */
			s_RenderCtx->Window.Width = Width;
			s_RenderCtx->Window.Height = Height;

			NexusLinkData_t* nexuslink = (NexusLinkData_t*)s_DataLink->GetResource(DL_NEXUS_LINK);

			if (nexuslink)
			{
				/* Already write to nexus link, as addons depend on that and the next frame isn't called yet so no update to values */
				nexuslink->Width = Width;
				nexuslink->Height = Height;
			}

			s_EventApi->Raise(EV_WINDOW_RESIZED);

			return Target::DXGIResizeBuffers(pChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);
		}
	}
}
