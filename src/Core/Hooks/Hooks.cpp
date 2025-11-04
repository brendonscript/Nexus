///----------------------------------------------------------------------------------------------------
/// Copyright (c) Raidcore.GG - All rights reserved.
///
/// Name         :  Hooks.cpp
/// Description  :  Implementation for hooked/detoured functions.
/// Authors      :  K. Bieniek
///----------------------------------------------------------------------------------------------------

#include "Hooks.h"

#include <atomic>
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

			/* Try to hook Present1 for IDXGISwapChain1 (NVIDIA Smooth Motion compatibility) */
			IDXGISwapChain1* swap1 = nullptr;
			if (SUCCEEDED(swap->QueryInterface(__uuidof(IDXGISwapChain1), (void**)&swap1)))
			{
				CContext* ctx = CContext::GetContext();
				CLogApi* logger = ctx->GetLogger();

				LPVOID* vtbl1 = *(LPVOID**)swap1;
				EMHStatus status = MH_CreateHook(Memory::FollowJmpChain((PBYTE)vtbl1[22]), (LPVOID)&Detour::DXGIPresent1, (LPVOID*)&Target::DXGIPresent1);

				if (logger)
				{
					if (status == EMHStatus::MH_OK)
					{
						logger->Info(CH_CORE, "Successfully created Present1 hook (vtable[22]: %p)", vtbl1[22]);
					}
					else
					{
						logger->Warning(CH_CORE, "Failed to create Present1 hook! EMHStatus: %d", static_cast<int>(status));
					}
				}

				swap1->Release();
			}
			else
			{
				CContext* ctx = CContext::GetContext();
				CLogApi* logger = ctx->GetLogger();
				if (logger)
				{
					logger->Warning(CH_CORE, "Failed to QueryInterface for IDXGISwapChain1! Present1 hook not installed.");
				}
			}

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
		DXPRESENT1      DXGIPresent1      = nullptr;
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
			static CLogApi*         s_Logger        = nullptr;
			static bool             s_LoggedInit    = false;

			static std::atomic<uint64_t> s_FrameCounter(0);
			static std::atomic<uint64_t> s_RenderCounter(0);
			static std::atomic<uint64_t> s_SkipCounter(0);

			// Track frame calls
			uint64_t currentFrame = s_FrameCounter.fetch_add(1);
			// Initialize once using std::call_once for thread safety
			std::call_once(s_InitFlag, []() {
				s_Context = CContext::GetContext();
				if (s_Context)
				{
					s_RenderCtx     = s_Context->GetRendererCtx();
					s_TextureLoader = s_Context->GetTextureService();
					s_UIContext     = s_Context->GetUIContext();
					s_Logger        = s_Context->GetLogger();

					if (s_Logger)
					{
						s_Logger->Info(CH_CORE, "DXGIPresent: Initialized (Thread: %lu)", GetCurrentThreadId());
					}
					s_LoggedInit = true;
				}
			});

			// If initialization failed, pass through without modification
			if (!s_RenderCtx || !s_TextureLoader || !s_UIContext)
			{
				if (s_Logger && !s_LoggedInit)
				{
					s_Logger->Critical(CH_CORE, "DXGIPresent: Initialization failed! Contexts are null.");
					s_LoggedInit = true;
				}
				return Target::DXGIPresent(pChain, SyncInterval, Flags);
			}

			// Log every 120 frames (~2 seconds at 60fps) to track activity
			if (s_Logger && currentFrame % 120 == 0)
			{
				s_Logger->Info(CH_CORE, "DXGIPresent: Frame %llu (Thread: %lu, Rendered: %llu, Skipped: %llu)",
					currentFrame, GetCurrentThreadId(), s_RenderCounter.load(), s_SkipCounter.load());
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
					if (s_Logger)
					{
						s_Logger->Info(CH_CORE, "DXGIPresent: SwapChain changed %p -> %p (Thread: %lu)",
							expected, pChain, GetCurrentThreadId());
					}

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
							
							if (s_Logger)
							{
								s_Logger->Info(CH_CORE, "DXGIPresent: Device: %p, DeviceContext: %p (Thread: %lu)",
									s_RenderCtx->Device, s_RenderCtx->DeviceContext, GetCurrentThreadId());
							}

							DXGI_SWAP_CHAIN_DESC swapChainDesc{};
							pChain->GetDesc(&swapChainDesc);

							s_RenderCtx->Window.Handle = swapChainDesc.OutputWindow;
							Target::WndProc = (WNDPROC)SetWindowLongPtr(s_RenderCtx->Window.Handle, GWLP_WNDPROC, (LONG_PTR)Detour::WndProc);

							if (s_Logger)
							{
								s_Logger->Info(CH_CORE, "DXGIPresent: Device acquired successfully (Thread: %lu)", GetCurrentThreadId());
							}

							Loader::Initialize();
						}
						else
						{
							if (s_Logger)
							{
								s_Logger->Warning(CH_CORE, "DXGIPresent: Failed to get Device from SwapChain! HRESULT: 0x%08X (Thread: %lu)",
									hr, GetCurrentThreadId());
							}
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
				s_RenderCounter.fetch_add(1);
			}
			else
			{
				// Log first few times we skip rendering due to invalid device
				static int skipCount = 0;
				s_SkipCounter.fetch_add(1);
				if (skipCount < 5 && s_Logger)
				{
					s_Logger->Warning(CH_CORE, "DXGIPresent: Skipping render - Device: %p, DeviceContext: %p (Thread: %lu)",
						s_RenderCtx->Device, s_RenderCtx->DeviceContext, GetCurrentThreadId());
					skipCount++;
				}
			}

			return Target::DXGIPresent(pChain, SyncInterval, Flags);
		}

		HRESULT __stdcall DXGIPresent1(IDXGISwapChain1* pChain, UINT SyncInterval, UINT PresentFlags, const DXGI_PRESENT_PARAMETERS* pPresentParameters)
		{
			// Log first call to verify this function is being used
			static std::atomic<bool> s_FirstCall(true);
			if (s_FirstCall.exchange(false))
			{
				// Emergency log before any initialization
				CContext* tempCtx = CContext::GetContext();
				if (tempCtx)
				{
					CLogApi* tempLogger = tempCtx->GetLogger();
					if (tempLogger)
					{
						tempLogger->Critical(CH_CORE, "!!! DXGIPresent1 CALLED !!! Thread: %lu, SwapChain: %p", GetCurrentThreadId(), pChain);
					}
				}
			}

			// Thread-safe initialization for NVIDIA Smooth Motion / Frame Generation compatibility
			static std::once_flag s_InitFlag;
			static CContext*        s_Context       = nullptr;
			static RenderContext_t* s_RenderCtx     = nullptr;
			static CTextureLoader*  s_TextureLoader = nullptr;
			static CUiContext*      s_UIContext     = nullptr;
			static CLogApi*         s_Logger        = nullptr;
			static bool             s_LoggedInit    = false;
			static std::atomic<uint64_t> s_FrameCounter(0);
			static std::atomic<uint64_t> s_RenderCounter(0);
			static std::atomic<uint64_t> s_SkipCounter(0);

			// Track frame calls
			uint64_t currentFrame = s_FrameCounter.fetch_add(1);

			// Initialize once using std::call_once for thread safety
			std::call_once(s_InitFlag, []() {
				s_Context = CContext::GetContext();
				if (s_Context)
				{
					s_RenderCtx     = s_Context->GetRendererCtx();
					s_TextureLoader = s_Context->GetTextureService();
					s_UIContext     = s_Context->GetUIContext();
					s_Logger        = s_Context->GetLogger();

					if (s_Logger)
					{
						s_Logger->Info(CH_CORE, "DXGIPresent1: Initialized (Thread: %lu)", GetCurrentThreadId());
					}
					s_LoggedInit = true;
				}
			});

			// If initialization failed, pass through without modification
			if (!s_RenderCtx || !s_TextureLoader || !s_UIContext)
			{
				if (s_Logger && !s_LoggedInit)
				{
					s_Logger->Critical(CH_CORE, "DXGIPresent1: Initialization failed! Contexts are null.");
					s_LoggedInit = true;
				}
				return Target::DXGIPresent1(pChain, SyncInterval, PresentFlags, pPresentParameters);
			}

			// Log every 120 frames (~2 seconds at 60fps) to track activity
			if (s_Logger && currentFrame % 120 == 0)
			{
				s_Logger->Info(CH_CORE, "DXGIPresent1: Frame %llu (Thread: %lu, Rendered: %llu, Skipped: %llu)",
					currentFrame, GetCurrentThreadId(), s_RenderCounter.load(), s_SkipCounter.load());
			}

			/* The swap chain we used to hook is different than the one the game created.
			 * NVIDIA Smooth Motion may present with the same swap chain from different threads,
			 * so only initialize if swap chain actually changed, and don't block on it. */
			if (s_RenderCtx->SwapChain != (IDXGISwapChain*)pChain)
			{
				// Use atomic compare-exchange to avoid blocking NVIDIA's frame generation
				IDXGISwapChain* expected = s_RenderCtx->SwapChain;
				if (expected != (IDXGISwapChain*)pChain)
				{
					if (s_Logger)
					{
						s_Logger->Info(CH_CORE, "DXGIPresent1: SwapChain changed %p -> %p (Thread: %lu)",
							expected, pChain, GetCurrentThreadId());
					}

					s_RenderCtx->SwapChain = (IDXGISwapChain*)pChain;

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

							if (s_Logger)
							{
								s_Logger->Info(CH_CORE, "DXGIPresent1: Device: %p, DeviceContext: %p (Thread: %lu)",
									s_RenderCtx->Device, s_RenderCtx->DeviceContext, GetCurrentThreadId());
							}

							DXGI_SWAP_CHAIN_DESC swapChainDesc{};
							((IDXGISwapChain*)pChain)->GetDesc(&swapChainDesc);

							s_RenderCtx->Window.Handle = swapChainDesc.OutputWindow;
							Target::WndProc = (WNDPROC)SetWindowLongPtr(s_RenderCtx->Window.Handle, GWLP_WNDPROC, (LONG_PTR)Detour::WndProc);

							if (s_Logger)
							{
								s_Logger->Info(CH_CORE, "DXGIPresent1: Device acquired successfully (Thread: %lu)", GetCurrentThreadId());
							}

							Loader::Initialize();
						}
						else
						{
							if (s_Logger)
							{
								s_Logger->Warning(CH_CORE, "DXGIPresent1: Failed to get Device from SwapChain! HRESULT: 0x%08X (Thread: %lu)",
									hr, GetCurrentThreadId());
							}
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
				s_RenderCounter.fetch_add(1);
			}
			else
			{
				s_SkipCounter.fetch_add(1);
				// Log first few times we skip rendering due to invalid device
				static int skipCount = 0;
				if (skipCount < 5 && s_Logger)
				{
					s_Logger->Warning(CH_CORE, "DXGIPresent1: Skipping render - Device: %p, DeviceContext: %p (Thread: %lu)",
						s_RenderCtx->Device, s_RenderCtx->DeviceContext, GetCurrentThreadId());
					skipCount++;
				}
			}

			return Target::DXGIPresent1(pChain, SyncInterval, PresentFlags, pPresentParameters);
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
