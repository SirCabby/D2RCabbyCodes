// The D3D12 side of the panel: the swap chain hooks and the renderer.
//
// A throwaway D3D12 device, queue and swap chain on a hidden window give us the
// swap chain class's Present and ResizeBuffers functions (the same class serves
// every swap chain a process makes: dxgi.dll's on Windows, DXVK's under
// Proton). Those functions are hooked with MinHook - a jump at their entry -
// which is what MapSense and Floating Damage do too, so whichever plugin hooks
// second chains after the first. The game's command queue is taken from its
// CreateSwapChain* call: DXGI is handed the queue, not the device, when a D3D12
// swap chain is made, and the factory class's methods are hooked the same way.
//
// d3d12 and dxgi are looked up at run time, never imported (d3d_shims.cpp).
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>

#include <cstring>
#include <string>
#include <vector>

#include "MinHook.h"
#include "backends/imgui_impl_dx12.h"
#include "backends/imgui_impl_win32.h"
#include "imgui.h"
#include "log.h"
#include "mem.h"
#include "overlay.h"
#include "perf.h"

namespace d2rcc::overlay::d3d {
namespace {

using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using Present1Fn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
using ResizeBuffersFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
using ResizeBuffers1Fn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain3*, UINT, UINT, UINT, DXGI_FORMAT, UINT, const UINT*,
                                                     IUnknown* const*);
using CreateSwapChainFn = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**);
using CreateSwapChainForHwndFn = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*, IUnknown*, HWND, const DXGI_SWAP_CHAIN_DESC1*,
                                                             const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*,
                                                             IDXGISwapChain1**);
using CreateSwapChainForCoreWindowFn = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*, IUnknown*, IUnknown*,
                                                                   const DXGI_SWAP_CHAIN_DESC1*, IDXGIOutput*,
                                                                   IDXGISwapChain1**);
using CreateSwapChainForCompositionFn = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*, IUnknown*, const DXGI_SWAP_CHAIN_DESC1*,
                                                                    IDXGIOutput*, IDXGISwapChain1**);
using CreateFactoryFn = HRESULT(WINAPI*)(REFIID, void**);
using CreateDeviceFn = HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);

constexpr int kPresent = 8, kResizeBuffers = 13, kPresent1 = 22, kResizeBuffers1 = 39;  // IDXGISwapChain3
constexpr int kCreateSwapChain = 10, kCreateForHwnd = 15, kCreateForCoreWindow = 16, kCreateForComposition = 24;

PresentFn g_orig_present = nullptr;
Present1Fn g_orig_present1 = nullptr;
ResizeBuffersFn g_orig_resize = nullptr;
ResizeBuffers1Fn g_orig_resize1 = nullptr;
CreateSwapChainFn g_orig_create = nullptr;
CreateSwapChainForHwndFn g_orig_create_hwnd = nullptr;
CreateSwapChainForCoreWindowFn g_orig_create_core = nullptr;
CreateSwapChainForCompositionFn g_orig_create_comp = nullptr;

bool g_minhook = false;
bool g_hooked = false;
HWND g_probe_wnd = nullptr;

// What CreateSwapChain* saw: the queue handed to DXGI and the swap chain it made.
CRITICAL_SECTION g_cs;
bool g_cs_ready = false;
struct Made {
  IDXGISwapChain* sc;
  ID3D12CommandQueue* queue;
};
std::vector<Made> g_made;

IDXGISwapChain* g_sc = nullptr;  // the game's swap chain (the first one that presents a visible window)
IDXGISwapChain3* g_sc3 = nullptr;  // ... as the interface that names the back buffer (no reference kept)
HWND g_sc_wnd = nullptr;           // ... and the window it presents to
ID3D12CommandQueue* g_queue = nullptr;

// A swap chain made before the hooks were in never passed through CreateSwapChain*,
// so its queue is read out of the object instead: the probe swap chain shows
// where the class keeps its queue pointer (directly, or one pointer deeper under
// Proton, where DXVK's swap chain wraps vkd3d-proton's), and the pointer found
// in the game's is trusted only when it has a real queue's vtable.
int g_q_outer = -1;
int g_q_inner = -1;
uintptr_t g_queue_vt = 0;

void learn_queue_layout(IDXGISwapChain* sc, ID3D12CommandQueue* queue) {
  const uintptr_t q = reinterpret_cast<uintptr_t>(queue);
  const uintptr_t s = reinterpret_cast<uintptr_t>(sc);
  for (int i = 0; i < 512 * 8 && g_q_inner < 0; i += 8)
    if (mem::read_ptr(s + i) == q) g_q_inner = i;
  for (int base = 0; base < 512 * 8 && g_q_inner < 0; base += 8) {
    const uintptr_t inner = mem::read_ptr(s + base);
    if (!inner) continue;
    for (int i = 0; i < 512 * 8; i += 8) {
      uintptr_t v = 0;
      if (!mem::read_safe(inner + i, &v)) break;
      if (v == q) {
        g_q_outer = base;
        g_q_inner = i;
        break;
      }
    }
  }
  g_queue_vt = mem::read_ptr(q);
  if (g_q_inner < 0) log_warn("overlay: the probe swap chain does not keep its queue where it can be found - only a swap chain made after the hooks gets a panel");
  else if (g_q_outer < 0) logf("overlay: a swap chain keeps its command queue at +0x%X", g_q_inner);
  else logf("overlay: a swap chain keeps its command queue at [+0x%X]+0x%X (a wrapped swap chain - Proton)", g_q_outer, g_q_inner);
}

ID3D12CommandQueue* queue_by_layout(IDXGISwapChain* sc) {
  if (g_q_inner < 0) return nullptr;
  uintptr_t base = reinterpret_cast<uintptr_t>(sc);
  if (g_q_outer >= 0) base = mem::read_ptr(base + g_q_outer);
  const uintptr_t q = base ? mem::read_ptr(base + g_q_inner) : 0;
  if (!q || mem::read_ptr(q) != g_queue_vt) return nullptr;
  return reinterpret_cast<ID3D12CommandQueue*>(q);
}
thread_local int t_depth = 0;
bool g_device_removed = false;

// --- the renderer ---------------------------------------------------------------------
struct Frame {
  ID3D12CommandAllocator* alloc = nullptr;
  ID3D12Resource* back = nullptr;
  D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
  UINT64 fence = 0;
};
struct Dx12 {
  ID3D12Device* dev = nullptr;
  ID3D12CommandQueue* queue = nullptr;
  ID3D12DescriptorHeap* rtv_heap = nullptr;
  ID3D12DescriptorHeap* srv_heap = nullptr;
  ID3D12GraphicsCommandList* list = nullptr;
  ID3D12Fence* fence = nullptr;
  HANDLE event = nullptr;
  UINT64 fence_value = 0;
  std::vector<Frame> frames;
  UINT rtv_step = 0, srv_step = 0;
  DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
  bool init = false, targets = false, failed = false;
  std::vector<int> srv_free;
} g12;
constexpr int kSrvDescriptors = 64;

void srv_alloc(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE* cpu, D3D12_GPU_DESCRIPTOR_HANDLE* gpu) {
  if (g12.srv_free.empty()) {
    log_error("dx12: the panel ran out of texture descriptors");
    cpu->ptr = 0;
    gpu->ptr = 0;
    return;
  }
  const int i = g12.srv_free.back();
  g12.srv_free.pop_back();
  cpu->ptr = g12.srv_heap->GetCPUDescriptorHandleForHeapStart().ptr + static_cast<SIZE_T>(i) * g12.srv_step;
  gpu->ptr = g12.srv_heap->GetGPUDescriptorHandleForHeapStart().ptr + static_cast<UINT64>(i) * g12.srv_step;
}

void srv_free(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE) {
  const SIZE_T start = g12.srv_heap->GetCPUDescriptorHandleForHeapStart().ptr;
  if (cpu.ptr >= start && g12.srv_step) g12.srv_free.push_back(static_cast<int>((cpu.ptr - start) / g12.srv_step));
}

void dx12_wait_idle() {
  if (!g12.queue || !g12.fence) return;
  const UINT64 v = ++g12.fence_value;
  if (FAILED(g12.queue->Signal(g12.fence, v))) return;
  if (g12.fence->GetCompletedValue() < v && SUCCEEDED(g12.fence->SetEventOnCompletion(v, g12.event)))
    WaitForSingleObject(g12.event, 2000);
}

void dx12_release_targets() {
  if (!g12.init) return;
  dx12_wait_idle();
  for (Frame& f : g12.frames)
    if (f.back) {
      f.back->Release();
      f.back = nullptr;
    }
  g12.targets = false;
}

bool dx12_init(IDXGISwapChain* sc, ID3D12CommandQueue* queue) {
  if (g12.init) return true;
  if (g12.failed) return false;
  DXGI_SWAP_CHAIN_DESC sd{};
  if (FAILED(sc->GetDesc(&sd)) || FAILED(sc->GetDevice(__uuidof(ID3D12Device), reinterpret_cast<void**>(&g12.dev)))) {
    g12.failed = true;
    return false;
  }
  g12.queue = queue;
  g12.format = sd.BufferDesc.Format;
  const UINT n = sd.BufferCount;
  D3D12_DESCRIPTOR_HEAP_DESC rh{};
  rh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  rh.NumDescriptors = n;
  D3D12_DESCRIPTOR_HEAP_DESC sh{};
  sh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  sh.NumDescriptors = kSrvDescriptors;
  sh.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  bool ok = SUCCEEDED(g12.dev->CreateDescriptorHeap(&rh, __uuidof(ID3D12DescriptorHeap), reinterpret_cast<void**>(&g12.rtv_heap))) &&
            SUCCEEDED(g12.dev->CreateDescriptorHeap(&sh, __uuidof(ID3D12DescriptorHeap), reinterpret_cast<void**>(&g12.srv_heap))) &&
            SUCCEEDED(g12.dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), reinterpret_cast<void**>(&g12.fence)));
  g12.frames.assign(n, Frame{});
  for (UINT i = 0; ok && i < n; ++i)
    ok = SUCCEEDED(g12.dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator),
                                                   reinterpret_cast<void**>(&g12.frames[i].alloc)));
  ok = ok && SUCCEEDED(g12.dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g12.frames[0].alloc, nullptr,
                                                  __uuidof(ID3D12GraphicsCommandList), reinterpret_cast<void**>(&g12.list)));
  if (ok) g12.list->Close();
  g12.event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
  if (!ok || !g12.event) {
    log_error("dx12: the panel's device objects could not be made");
    g12.failed = true;
    return false;
  }
  g12.rtv_step = g12.dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  g12.srv_step = g12.dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  g12.srv_free.clear();
  for (int i = kSrvDescriptors - 1; i >= 0; --i) g12.srv_free.push_back(i);
  ImGui_ImplDX12_InitInfo ii;
  ii.Device = g12.dev;
  ii.CommandQueue = queue;
  ii.NumFramesInFlight = static_cast<int>(n);
  ii.RTVFormat = g12.format;
  ii.DSVFormat = DXGI_FORMAT_UNKNOWN;
  ii.SrvDescriptorHeap = g12.srv_heap;
  ii.SrvDescriptorAllocFn = &srv_alloc;
  ii.SrvDescriptorFreeFn = &srv_free;
  if (!ImGui_ImplDX12_Init(&ii)) {
    log_error("dx12: ImGui's DX12 backend did not start");
    g12.failed = true;
    return false;
  }
  g12.init = true;
  logf("dx12: panel renderer ready (device %p, queue %p, %u back buffers, format %d)", static_cast<void*>(g12.dev),
       static_cast<void*>(queue), n, static_cast<int>(g12.format));
  return true;
}

bool dx12_targets(IDXGISwapChain* sc) {
  if (g12.targets) return true;
  DXGI_SWAP_CHAIN_DESC sd{};
  if (FAILED(sc->GetDesc(&sd))) return false;
  if (sd.BufferCount != g12.frames.size() || sd.BufferDesc.Format != g12.format) {
    logf("dx12: the swap chain changed to %u buffers, format %d (was %u, %d) - the panel stops until the game restarts",
         sd.BufferCount, static_cast<int>(sd.BufferDesc.Format), static_cast<unsigned>(g12.frames.size()),
         static_cast<int>(g12.format));
    g12.failed = true;
    return false;
  }
  for (UINT i = 0; i < sd.BufferCount; ++i) {
    Frame& f = g12.frames[i];
    if (FAILED(sc->GetBuffer(i, __uuidof(ID3D12Resource), reinterpret_cast<void**>(&f.back))) || !f.back) return false;
    f.rtv.ptr = g12.rtv_heap->GetCPUDescriptorHandleForHeapStart().ptr + static_cast<SIZE_T>(i) * g12.rtv_step;
    g12.dev->CreateRenderTargetView(f.back, nullptr, f.rtv);
  }
  g12.targets = true;
  return true;
}

void barrier(ID3D12Resource* res, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
  D3D12_RESOURCE_BARRIER b{};
  b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  b.Transition.pResource = res;
  b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  b.Transition.StateBefore = from;
  b.Transition.StateAfter = to;
  g12.list->ResourceBarrier(1, &b);
}

// A frame with the health bars alone takes no input: it needs the window's size and a time step, and none of
// what the Win32 backend asks the system at every frame for the panel's sake (the foreground window alone is a
// call to the wineserver under Proton, some 4 us).
void frame_without_input() {
  ImGuiIO& io = ImGui::GetIO();
  RECT rc{};
  GetClientRect(g_sc_wnd, &rc);
  io.DisplaySize = ImVec2(static_cast<float>(rc.right - rc.left), static_cast<float>(rc.bottom - rc.top));
  io.DeltaTime = 1.0f / 60.0f;
}

void dx12_frame(IDXGISwapChain* sc, bool panel, bool hud) {
  if (!g_queue || !dx12_init(sc, g_queue) || !dx12_targets(sc)) return;
  if (!g_sc3) {
    // Asked once: the swap chain is the game's for as long as it presents, and this runs inside its Present.
    IDXGISwapChain3* sc3 = nullptr;
    if (FAILED(sc->QueryInterface(__uuidof(IDXGISwapChain3), reinterpret_cast<void**>(&sc3))) || !sc3) return;
    sc3->Release();
    g_sc3 = sc3;
  }
  const UINT bi = g_sc3->GetCurrentBackBufferIndex();
  if (bi >= g12.frames.size()) return;
  Frame& fr = g12.frames[bi];
  if (g12.fence->GetCompletedValue() < fr.fence && SUCCEEDED(g12.fence->SetEventOnCompletion(fr.fence, g12.event)))
    WaitForSingleObject(g12.event, 2000);
  if (FAILED(fr.alloc->Reset()) || FAILED(g12.list->Reset(fr.alloc, nullptr))) return;
  ImGui_ImplDX12_NewFrame();
  if (panel) ImGui_ImplWin32_NewFrame();
  else frame_without_input();
  ImGui::NewFrame();
  if (hud) draw_hud();
  if (panel) draw_panel();
  ImGui::Render();
  barrier(fr.back, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
  g12.list->OMSetRenderTargets(1, &fr.rtv, FALSE, nullptr);
  ID3D12DescriptorHeap* heaps[] = {g12.srv_heap};
  g12.list->SetDescriptorHeaps(1, heaps);
  ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g12.list);
  barrier(fr.back, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
  if (FAILED(g12.list->Close())) return;
  ID3D12CommandList* lists[] = {g12.list};
  g_queue->ExecuteCommandLists(1, lists);
  g_queue->Signal(g12.fence, ++g12.fence_value);
  fr.fence = g12.fence_value;
}

// --- the hooks ---------------------------------------------------------------------------
ID3D12CommandQueue* queue_of_made(IDXGISwapChain* sc) {
  if (!g_cs_ready) return nullptr;
  EnterCriticalSection(&g_cs);
  ID3D12CommandQueue* q = nullptr;
  for (const Made& m : g_made)
    if (m.sc == sc) q = m.queue;
  LeaveCriticalSection(&g_cs);
  return q;
}

void remember(IUnknown* device, IDXGISwapChain* sc, const char* how) {
  if (!sc || !device) return;
  ID3D12CommandQueue* q = nullptr;
  if (FAILED(device->QueryInterface(__uuidof(ID3D12CommandQueue), reinterpret_cast<void**>(&q))) || !q) return;
  q->Release();  // the game owns it for as long as it presents; a reference of ours would only delay teardown
  if (g_cs_ready) {
    EnterCriticalSection(&g_cs);
    g_made.push_back({sc, q});
    LeaveCriticalSection(&g_cs);
  }
  logf("overlay: a D3D12 swap chain was made (%s): %p on queue %p", how, static_cast<void*>(sc), static_cast<void*>(q));
}

void on_present(IDXGISwapChain* sc) {
  if (g_sc && sc != g_sc) return;
  if (g_device_removed) return;
  perf::Timer timer(perf::kPresent);
  // The game's swap chain is found once: the first that presents to a visible window. From then on a frame
  // asks nothing of the swap chain or the window before there is something to draw.
  if (!g_sc) {
    DXGI_SWAP_CHAIN_DESC sd{};
    if (FAILED(sc->GetDesc(&sd)) || !sd.OutputWindow || sd.OutputWindow == g_probe_wnd ||
        !IsWindowVisible(sd.OutputWindow))
      return;
    ID3D12CommandQueue* q = queue_of_made(sc);
    const char* how = "seen as it was made";
    if (!q) {
      q = queue_by_layout(sc);
      how = "read out of the swap chain";
    }
    if (!q) {
      static bool said = false;
      if (!said) {
        said = true;
        log_warn("overlay: the game's swap chain %p presented but its command queue is unknown - no panel",
                 static_cast<void*>(sc));
      }
      return;
    }
    g_sc = sc;
    g_sc_wnd = sd.OutputWindow;
    g_queue = q;
    logf("overlay: the game presents through swap chain %p (%ux%u, %u buffers, format %d, window %p); queue %p %s",
         static_cast<void*>(sc), sd.BufferDesc.Width, sd.BufferDesc.Height, sd.BufferCount,
         static_cast<int>(sd.BufferDesc.Format), static_cast<void*>(sd.OutputWindow), static_cast<void*>(q), how);
  }
  ImGuiLock guard;
  if (!ensure_context(g_sc_wnd)) return;
  // The panel when it is up; the health bars whenever they have something to show.
  const bool panel = wants_draw();
  const bool hud = hud_wanted();
  if (panel || hud) {
    perf::Timer drawing(perf::kOverlayFrame);
    dx12_frame(sc, panel, hud);
  }
}

void note_result(HRESULT hr) {
  if ((hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) && !g_device_removed) {
    g_device_removed = true;
    log_error("overlay: the game's device was removed (hr 0x%08lX) - the panel stops", static_cast<unsigned long>(hr));
  }
}

HRESULT STDMETHODCALLTYPE hk_present(IDXGISwapChain* sc, UINT sync, UINT flags) {
  if (t_depth == 0 && !(flags & DXGI_PRESENT_TEST)) {
    ++t_depth;
    on_present(sc);
    --t_depth;
  }
  const HRESULT hr = g_orig_present ? g_orig_present(sc, sync, flags) : DXGI_ERROR_INVALID_CALL;
  if (sc == g_sc) note_result(hr);
  return hr;
}

HRESULT STDMETHODCALLTYPE hk_present1(IDXGISwapChain1* sc, UINT sync, UINT flags, const DXGI_PRESENT_PARAMETERS* p) {
  if (t_depth == 0 && !(flags & DXGI_PRESENT_TEST)) {
    ++t_depth;
    on_present(sc);
    --t_depth;
  }
  const HRESULT hr = g_orig_present1 ? g_orig_present1(sc, sync, flags, p) : DXGI_ERROR_INVALID_CALL;
  if (sc == g_sc) note_result(hr);
  return hr;
}

void before_resize(IDXGISwapChain* sc) {
  if (sc != g_sc) return;
  ImGuiLock guard;
  dx12_release_targets();
}

HRESULT STDMETHODCALLTYPE hk_resize(IDXGISwapChain* sc, UINT n, UINT w, UINT h, DXGI_FORMAT f, UINT flags) {
  before_resize(sc);
  const HRESULT hr = g_orig_resize ? g_orig_resize(sc, n, w, h, f, flags) : DXGI_ERROR_INVALID_CALL;
  if (sc == g_sc) logf("overlay: the game resized its swap chain to %ux%u (hr 0x%08lX)", w, h, static_cast<unsigned long>(hr));
  return hr;
}

HRESULT STDMETHODCALLTYPE hk_resize1(IDXGISwapChain3* sc, UINT n, UINT w, UINT h, DXGI_FORMAT f, UINT flags,
                                     const UINT* masks, IUnknown* const* queues) {
  before_resize(sc);
  const HRESULT hr = g_orig_resize1 ? g_orig_resize1(sc, n, w, h, f, flags, masks, queues) : DXGI_ERROR_INVALID_CALL;
  if (sc == g_sc) logf("overlay: the game resized its swap chain to %ux%u (ResizeBuffers1, hr 0x%08lX)", w, h,
                       static_cast<unsigned long>(hr));
  return hr;
}

HRESULT STDMETHODCALLTYPE hk_create(IDXGIFactory* f, IUnknown* device, DXGI_SWAP_CHAIN_DESC* desc, IDXGISwapChain** out) {
  const HRESULT hr = g_orig_create ? g_orig_create(f, device, desc, out) : DXGI_ERROR_INVALID_CALL;
  if (SUCCEEDED(hr) && out && *out) remember(device, *out, "CreateSwapChain");
  return hr;
}

HRESULT STDMETHODCALLTYPE hk_create_hwnd(IDXGIFactory2* f, IUnknown* device, HWND hwnd, const DXGI_SWAP_CHAIN_DESC1* desc,
                                         const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fs, IDXGIOutput* output,
                                         IDXGISwapChain1** out) {
  const HRESULT hr = g_orig_create_hwnd ? g_orig_create_hwnd(f, device, hwnd, desc, fs, output, out) : DXGI_ERROR_INVALID_CALL;
  if (SUCCEEDED(hr) && out && *out) remember(device, *out, "CreateSwapChainForHwnd");
  return hr;
}

HRESULT STDMETHODCALLTYPE hk_create_core(IDXGIFactory2* f, IUnknown* device, IUnknown* window,
                                         const DXGI_SWAP_CHAIN_DESC1* desc, IDXGIOutput* output, IDXGISwapChain1** out) {
  const HRESULT hr = g_orig_create_core ? g_orig_create_core(f, device, window, desc, output, out) : DXGI_ERROR_INVALID_CALL;
  if (SUCCEEDED(hr) && out && *out) remember(device, *out, "CreateSwapChainForCoreWindow");
  return hr;
}

HRESULT STDMETHODCALLTYPE hk_create_comp(IDXGIFactory2* f, IUnknown* device, const DXGI_SWAP_CHAIN_DESC1* desc,
                                         IDXGIOutput* output, IDXGISwapChain1** out) {
  const HRESULT hr = g_orig_create_comp ? g_orig_create_comp(f, device, desc, output, out) : DXGI_ERROR_INVALID_CALL;
  if (SUCCEEDED(hr) && out && *out) remember(device, *out, "CreateSwapChainForComposition");
  return hr;
}

// --- the probe ----------------------------------------------------------------------------
template <typename Fn>
Fn method(void* object, int slot) {
  return reinterpret_cast<Fn>(reinterpret_cast<void**>(*reinterpret_cast<void***>(object))[slot]);
}

bool hook(void* target, void* detour, void** original, const char* what) {
  const MH_STATUS c = MH_CreateHook(target, detour, original);
  if (c != MH_OK && c != MH_ERROR_ALREADY_CREATED) {
    log_error("overlay: MinHook could not hook %s (%s)", what, MH_StatusToString(c));
    return false;
  }
  return true;
}

HWND make_probe_window() {
  WNDCLASSW wc{};
  wc.lpfnWndProc = DefWindowProcW;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"D2RCabbyCodesProbe";
  RegisterClassW(&wc);
  return CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPED, 0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
}

}  // namespace

bool install() {
  if (g_hooked) return true;
  if (!g_cs_ready) {
    InitializeCriticalSection(&g_cs);
    g_cs_ready = true;
  }
  // The game's own copies if it has them, else the system's by full path: the
  // game folder ships a Windows 7 d3d12.dll that a bare LoadLibrary would pick.
  HMODULE dxgi = GetModuleHandleW(L"dxgi.dll");
  HMODULE d3d12 = GetModuleHandleW(L"d3d12.dll");
  if (!dxgi || !d3d12) {
    wchar_t sysdir[MAX_PATH] = {};
    GetSystemDirectoryW(sysdir, MAX_PATH);
    std::wstring path = std::wstring(sysdir) + L"\\dxgi.dll";
    if (!dxgi) dxgi = LoadLibraryW(path.c_str());
    path = std::wstring(sysdir) + L"\\d3d12.dll";
    if (!d3d12) d3d12 = LoadLibraryW(path.c_str());
  }
  for (int i = 0; (!dxgi || !d3d12) && i < 1500; ++i) {  // else wait for the game to load them (30 s)
    Sleep(20);
    if (!dxgi) dxgi = GetModuleHandleW(L"dxgi.dll");
    if (!d3d12) d3d12 = GetModuleHandleW(L"d3d12.dll");
  }
  auto create_factory = dxgi ? reinterpret_cast<CreateFactoryFn>(GetProcAddress(dxgi, "CreateDXGIFactory1")) : nullptr;
  auto create_device = d3d12 ? reinterpret_cast<CreateDeviceFn>(GetProcAddress(d3d12, "D3D12CreateDevice")) : nullptr;
  if (!create_factory || !create_device) {
    log_error("overlay: dxgi.dll (%p) / d3d12.dll (%p) not usable (CreateDXGIFactory1 %p, D3D12CreateDevice %p, error %lu) - no panel",
              static_cast<void*>(dxgi), static_cast<void*>(d3d12), reinterpret_cast<void*>(create_factory),
              reinterpret_cast<void*>(create_device), GetLastError());
    return false;
  }
  logf("overlay: dxgi.dll %p, d3d12.dll %p", static_cast<void*>(dxgi), static_cast<void*>(d3d12));
  if (!g_minhook) {
    const MH_STATUS st = MH_Initialize();
    if (st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED) {
      log_error("overlay: MinHook did not initialise (%s)", MH_StatusToString(st));
      return false;
    }
    g_minhook = true;
  }
  IDXGIFactory2* factory = nullptr;
  if (FAILED(create_factory(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(&factory))) || !factory) {
    log_error("overlay: CreateDXGIFactory1 failed - no panel");
    return false;
  }
  ID3D12Device* dev = nullptr;
  HRESULT hr = create_device(nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), reinterpret_cast<void**>(&dev));
  if (FAILED(hr) || !dev) {
    log_error("overlay: the probe D3D12 device could not be made (hr 0x%08lX) - no panel", static_cast<unsigned long>(hr));
    factory->Release();
    return false;
  }
  D3D12_COMMAND_QUEUE_DESC qd{};
  qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  ID3D12CommandQueue* queue = nullptr;
  hr = dev->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), reinterpret_cast<void**>(&queue));
  g_probe_wnd = make_probe_window();
  IDXGISwapChain1* sc = nullptr;
  if (SUCCEEDED(hr) && queue && g_probe_wnd) {
    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Width = 64;
    sd.Height = 64;
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    hr = factory->CreateSwapChainForHwnd(queue, g_probe_wnd, &sd, nullptr, nullptr, &sc);
  }
  if (FAILED(hr) || !sc) {
    log_error("overlay: the probe swap chain could not be made (hr 0x%08lX) - no panel", static_cast<unsigned long>(hr));
    if (queue) queue->Release();
    dev->Release();
    factory->Release();
    return false;
  }
  learn_queue_layout(sc, queue);
  bool ok = true;
  ok &= hook(reinterpret_cast<void*>(method<PresentFn>(sc, kPresent)), reinterpret_cast<void*>(&hk_present),
             reinterpret_cast<void**>(&g_orig_present), "IDXGISwapChain::Present");
  ok &= hook(reinterpret_cast<void*>(method<ResizeBuffersFn>(sc, kResizeBuffers)), reinterpret_cast<void*>(&hk_resize),
             reinterpret_cast<void**>(&g_orig_resize), "IDXGISwapChain::ResizeBuffers");
  IDXGISwapChain3* sc3 = nullptr;
  if (SUCCEEDED(sc->QueryInterface(__uuidof(IDXGISwapChain3), reinterpret_cast<void**>(&sc3))) && sc3) {
    ok &= hook(reinterpret_cast<void*>(method<Present1Fn>(sc3, kPresent1)), reinterpret_cast<void*>(&hk_present1),
               reinterpret_cast<void**>(&g_orig_present1), "IDXGISwapChain1::Present1");
    ok &= hook(reinterpret_cast<void*>(method<ResizeBuffers1Fn>(sc3, kResizeBuffers1)), reinterpret_cast<void*>(&hk_resize1),
               reinterpret_cast<void**>(&g_orig_resize1), "IDXGISwapChain3::ResizeBuffers1");
    sc3->Release();
  }
  ok &= hook(reinterpret_cast<void*>(method<CreateSwapChainFn>(factory, kCreateSwapChain)), reinterpret_cast<void*>(&hk_create),
             reinterpret_cast<void**>(&g_orig_create), "IDXGIFactory::CreateSwapChain");
  ok &= hook(reinterpret_cast<void*>(method<CreateSwapChainForHwndFn>(factory, kCreateForHwnd)),
             reinterpret_cast<void*>(&hk_create_hwnd), reinterpret_cast<void**>(&g_orig_create_hwnd),
             "IDXGIFactory2::CreateSwapChainForHwnd");
  ok &= hook(reinterpret_cast<void*>(method<CreateSwapChainForCoreWindowFn>(factory, kCreateForCoreWindow)),
             reinterpret_cast<void*>(&hk_create_core), reinterpret_cast<void**>(&g_orig_create_core),
             "IDXGIFactory2::CreateSwapChainForCoreWindow");
  ok &= hook(reinterpret_cast<void*>(method<CreateSwapChainForCompositionFn>(factory, kCreateForComposition)),
             reinterpret_cast<void*>(&hk_create_comp), reinterpret_cast<void**>(&g_orig_create_comp),
             "IDXGIFactory2::CreateSwapChainForComposition");
  const MH_STATUS en = MH_EnableHook(MH_ALL_HOOKS);
  if (en != MH_OK) {
    log_error("overlay: MinHook could not enable the hooks (%s)", MH_StatusToString(en));
    ok = false;
  }
  sc->Release();
  queue->Release();
  dev->Release();
  factory->Release();
  g_hooked = ok;
  logf("overlay: swap chain class %s (Present at %p)", ok ? "hooked: Present, ResizeBuffers, CreateSwapChain*" : "NOT hooked",
       reinterpret_cast<void*>(g_orig_present));
  return ok;
}

void uninstall() {
  if (g_minhook) {
    MH_DisableHook(MH_ALL_HOOKS);
    MH_RemoveHook(MH_ALL_HOOKS);
  }
  g_hooked = false;
  if (g12.init) {
    dx12_wait_idle();
    ImGui_ImplDX12_Shutdown();
    g12.init = false;
  }
  if (g_probe_wnd) {
    DestroyWindow(g_probe_wnd);
    g_probe_wnd = nullptr;
  }
}

bool presenting() { return g_sc != nullptr; }

}  // namespace d2rcc::overlay::d3d
