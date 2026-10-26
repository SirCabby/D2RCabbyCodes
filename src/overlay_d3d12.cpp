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
// The panel goes into the swap chain whose frames reach the screen: the game's own, until another swap chain
// presents to the game's window while the panel's has gone quiet or presents to some other window. Frame
// generation in the display driver (NVIDIA Smooth Motion) is reported to work that way: its hooks take the
// game's presents, and the game's frames and the ones it makes go out through a second swap chain of its own,
// from another thread. The panel then follows the frames there (follow), and the log says so.
//
// Where another hook takes Present's entry and leaves the panel's out (RTSS does), the panel is drawn from the game's
// own present routine instead (game_present_begin), which the loader hooks: it hands every frame to Present.
//
// d3d12 and dxgi are looked up at run time, never imported (d3d_shims.cpp).
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <cwctype>
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
CreateFactoryFn g_create_factory = nullptr;  // dxgi's, for the name of the GPU the game renders on (watch)

// Where Present and Present1 were hooked and what MinHook wrote there, for the log's look at whether something
// wrote over them since (watch).
uintptr_t g_present_at = 0, g_present1_at = 0;
unsigned char g_present_patch[8] = {}, g_present1_patch[8] = {};

// What CreateSwapChain* saw: the queue handed to DXGI and the swap chain it made.
CRITICAL_SECTION g_cs;
bool g_cs_ready = false;
struct Made {
  IDXGISwapChain* sc;
  ID3D12CommandQueue* queue;
};
std::vector<Made> g_made;

IDXGISwapChain* g_sc = nullptr;    // the panel's swap chain: the first that presents to a visible window, then
                                   // whichever the game's frames are seen to go through (follow)
IDXGISwapChain3* g_sc3 = nullptr;  // ... as the interface that names the back buffer (no reference kept)
HWND g_sc_wnd = nullptr;           // the game's window, which the panel's size and input come from
ID3D12CommandQueue* g_queue = nullptr;

// What has come through the Present hooks: how many, and when the last came (GetTickCount64). For the toggle
// key's line, and for the log's account when the panel is up and nothing is drawn (watch). Written by whatever
// thread presents; a count read a frame late is of no matter.
volatile LONG64 g_calls = 0;   // every call of the hooked Present or Present1
volatile LONG64 g_tests = 0;   // ... that only asked whether a frame could be shown (DXGI_PRESENT_TEST)
volatile LONG64 g_nested = 0;  // ... made from inside the panel's own work on that thread
volatile LONG64 g_ours = 0;    // the panel's swap chain's
volatile LONG64 g_others = 0;  // any other swap chain's
volatile LONG64 g_drawn = 0;   // frames the panel or the health bars went into
volatile ULONGLONG g_last_call = 0, g_last_ours = 0, g_last_other = 0, g_last_drawn = 0;
volatile DWORD g_ours_thread = 0, g_other_thread = 0;
IDXGISwapChain* volatile g_other = nullptr;      // the last other swap chain that presented
IDXGISwapChain* volatile g_elsewhere = nullptr;  // a swap chain last seen presenting to a window not the game's
IDXGISwapChain* volatile g_remade = nullptr;     // a swap chain made where the panel's was: the game's made again
LUID g_adapter{};                                // the GPU the game's queue renders on (its device's)
volatile LONG g_adapter_known = 0;

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

// The panel's own work presents nothing, but another overlay's hook it calls into might: such a call is passed
// straight on. The depth is put back however the frame is left.
thread_local int t_depth = 0;
struct Depth {
  Depth() { ++t_depth; }
  ~Depth() { --t_depth; }
  Depth(const Depth&) = delete;
  Depth& operator=(const Depth&) = delete;
};
bool g_device_removed = false;

// --- the renderer ---------------------------------------------------------------------
struct Frame {
  ID3D12CommandAllocator* alloc = nullptr;
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
  bool init = false, checked = false, failed = false;
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

// Every frame the panel handed the GPU is done (the panel's own fence says so), waited for up to 2 s. Nothing
// is asked of the queue, which may be gone with its swap chain.
bool dx12_idle() {
  if (!g12.fence || g12.fence->GetCompletedValue() >= g12.fence_value) return true;
  return SUCCEEDED(g12.fence->SetEventOnCompletion(g12.fence_value, g12.event)) &&
         WaitForSingleObject(g12.event, 2000) == WAIT_OBJECT_0;
}

// The renderer let go of, all of it: the swap chain it drew into changed its buffers or format, or the panel
// follows the game's frames into another (follow). The next frame makes it again for the swap chain presenting
// then (dx12_init). Lock held.
void dx12_shutdown() {
  if (!dx12_idle()) {
    log_warn("dx12: the panel's last frame is not done on the GPU after 2 s - its renderer is left as it is, and the "
             "panel stops");
    g12.failed = true;
    return;
  }
  if (g12.init && ImGui::GetCurrentContext()) ImGui_ImplDX12_Shutdown();
  for (Frame& f : g12.frames)
    if (f.alloc) f.alloc->Release();
  if (g12.list) g12.list->Release();
  if (g12.fence) g12.fence->Release();
  if (g12.event) CloseHandle(g12.event);
  if (g12.rtv_heap) g12.rtv_heap->Release();
  if (g12.srv_heap) g12.srv_heap->Release();
  if (g12.dev) g12.dev->Release();
  g12 = Dx12{};
  g_sc3 = nullptr;
}

// A frame the panel could not be drawn into says why, once a reason (it would at every frame).
enum DrawFailure { kTargetsDesc, kBackBuffer, kSwapChain3, kBackBufferIndex, kListReset, kListClose, kDrawFailures };
volatile long g_draw_said[kDrawFailures] = {};
void draw_failed(DrawFailure why, const char* what, HRESULT hr) {
  if (log_once(&g_draw_said[why], LogLevel::kWarning))
    log_warn("dx12: the panel is not drawn: %s (hr 0x%08lX)", what, static_cast<unsigned long>(hr));
}

bool dx12_init(IDXGISwapChain* sc, ID3D12CommandQueue* queue) {
  if (g12.failed) return false;
  if (g12.init) return true;
  DXGI_SWAP_CHAIN_DESC sd{};
  const HRESULT desc = sc->GetDesc(&sd);
  if (FAILED(desc)) {
    log_error("dx12: the game's swap chain gives no description (hr 0x%08lX) - no panel",
              static_cast<unsigned long>(desc));
    g12.failed = true;
    return false;
  }
  // The device the panel's objects are made on: the swap chain's, else the one its queue belongs to. DXGI is
  // handed a queue, not a device, when a D3D12 swap chain is made, and need not answer for ID3D12Device; the
  // queue's own GetDevice is D3D12's.
  const char* device_from = "swap chain";
  const HRESULT from_sc = sc->GetDevice(__uuidof(ID3D12Device), reinterpret_cast<void**>(&g12.dev));
  if (FAILED(from_sc) || !g12.dev) {
    g12.dev = nullptr;
    device_from = "queue";
    const HRESULT from_queue = queue->GetDevice(__uuidof(ID3D12Device), reinterpret_cast<void**>(&g12.dev));
    if (FAILED(from_queue) || !g12.dev) {
      log_error("dx12: neither the game's swap chain (hr 0x%08lX) nor its command queue (hr 0x%08lX) gives its D3D12 "
                "device - no panel",
                static_cast<unsigned long>(from_sc), static_cast<unsigned long>(from_queue));
      g12.dev = nullptr;
      g12.failed = true;
      return false;
    }
    log_warn("dx12: the game's swap chain gives no D3D12 device (hr 0x%08lX) - the panel takes its command queue's",
             static_cast<unsigned long>(from_sc));
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
  g12.checked = true;
  logf("dx12: panel renderer ready (device %p, the %s's, queue %p, %u back buffers, format %d)",
       static_cast<void*>(g12.dev), device_from, static_cast<void*>(queue), n, static_cast<int>(g12.format));
  return true;
}

// The swap chain still has the buffers and the format the renderer was made for: asked again after it was
// resized (before_resize). When they changed (the game's HDR switched, another buffer count), the renderer is
// let go of and made again for them at the next frame.
bool dx12_matches(IDXGISwapChain* sc) {
  if (g12.checked) return true;
  DXGI_SWAP_CHAIN_DESC sd{};
  const HRESULT desc = sc->GetDesc(&sd);
  if (FAILED(desc)) {
    draw_failed(kTargetsDesc, "the swap chain gives no description", desc);
    return false;
  }
  if (sd.BufferCount != g12.frames.size() || sd.BufferDesc.Format != g12.format) {
    logf("dx12: the swap chain changed to %u buffers, format %d (was %u, %d) - the panel's renderer is made again "
         "for it",
         sd.BufferCount, static_cast<int>(sd.BufferDesc.Format), static_cast<unsigned>(g12.frames.size()),
         static_cast<int>(g12.format));
    dx12_shutdown();
    return false;
  }
  g12.checked = true;
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

void dx12_frame(IDXGISwapChain* sc, bool panel, bool hud, ULONGLONG now) {
  if (!g_queue || !dx12_init(sc, g_queue) || !dx12_matches(sc)) return;
  if (!g_sc3) {
    // Asked once: the swap chain is the panel's for as long as it presents, and this runs inside its Present.
    IDXGISwapChain3* sc3 = nullptr;
    const HRESULT qi = sc->QueryInterface(__uuidof(IDXGISwapChain3), reinterpret_cast<void**>(&sc3));
    if (FAILED(qi) || !sc3) {
      draw_failed(kSwapChain3, "the swap chain does not say which back buffer is next (no IDXGISwapChain3)", qi);
      return;
    }
    sc3->Release();
    g_sc3 = sc3;
  }
  const UINT bi = g_sc3->GetCurrentBackBufferIndex();
  if (bi >= g12.frames.size()) {
    if (log_once(&g_draw_said[kBackBufferIndex], LogLevel::kWarning))
      log_warn("dx12: the panel is not drawn: the next back buffer (%u) is past the %u it has", bi,
               static_cast<unsigned>(g12.frames.size()));
    return;
  }
  // The back buffer is the panel's for this frame alone: a reference kept would make a ResizeBuffers of the swap
  // chain fail, and not every one comes through the hook (whatever presents the game's frames may resize its own).
  ID3D12Resource* back = nullptr;
  const HRESULT buffer = sc->GetBuffer(bi, __uuidof(ID3D12Resource), reinterpret_cast<void**>(&back));
  if (FAILED(buffer) || !back) {
    draw_failed(kBackBuffer, "a back buffer of the swap chain is not to be had", buffer);
    return;
  }
  Frame& fr = g12.frames[bi];
  if (g12.fence->GetCompletedValue() < fr.fence && SUCCEEDED(g12.fence->SetEventOnCompletion(fr.fence, g12.event)))
    WaitForSingleObject(g12.event, 2000);
  HRESULT reset = fr.alloc->Reset();
  if (SUCCEEDED(reset)) reset = g12.list->Reset(fr.alloc, nullptr);
  if (FAILED(reset)) {
    back->Release();
    draw_failed(kListReset, "the panel's command list could not be started", reset);
    return;
  }
  D3D12_CPU_DESCRIPTOR_HANDLE rtv = g12.rtv_heap->GetCPUDescriptorHandleForHeapStart();
  rtv.ptr += static_cast<SIZE_T>(bi) * g12.rtv_step;
  g12.dev->CreateRenderTargetView(back, nullptr, rtv);
  ImGui_ImplDX12_NewFrame();
  if (panel) ImGui_ImplWin32_NewFrame();
  else frame_without_input();
  ImGui::NewFrame();
  if (hud) draw_hud();
  if (panel) draw_panel();
  ImGui::Render();
  barrier(back, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
  g12.list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
  ID3D12DescriptorHeap* heaps[] = {g12.srv_heap};
  g12.list->SetDescriptorHeaps(1, heaps);
  ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g12.list);
  barrier(back, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
  const HRESULT closed = g12.list->Close();
  if (FAILED(closed)) {
    back->Release();
    draw_failed(kListClose, "the panel's command list could not be closed", closed);
    return;
  }
  ID3D12CommandList* lists[] = {g12.list};
  g_queue->ExecuteCommandLists(1, lists);
  g_queue->Signal(g12.fence, ++g12.fence_value);
  fr.fence = g12.fence_value;
  back->Release();  // the swap chain keeps its own
  InterlockedIncrement64(&g_drawn);
  g_last_drawn = now;
}

// --- a wrapper's objects ----------------------------------------------------------------------
// A dxgi.dll in the game folder (ReShade's, most often) stands between the game and the system's DXGI: the game
// holds its proxies, and its swap chain hands each frame, after its own work on it, to the system's one it wraps.
// ReShade's proxies (6.6 and later) hand out the object they wrap when asked for this interface; the system's
// objects, DXVK's and vkd3d's, and older ReShade's proxies answer that they have none.
constexpr GUID kUnwrappedObject = {0x7f2c9a11, 0x3b4e, 0x4d6a, {0x81, 0x2f, 0x5e, 0x9c, 0xd3, 0x7a, 0x1b, 0x42}};
bool g_wrapped = false;  // a wrapper is about (install): the swap chains seen being made are asked what they wrap
HMODULE g_reshade = nullptr;  // ReShade's module, when it is in the process (install)

// The object a proxy wraps, with a reference for the caller; nullptr when `p` is no proxy that says.
template <typename T>
T* wrapped(T* p) {
  IUnknown* inner = nullptr;
  if (!p || FAILED(p->QueryInterface(kUnwrappedObject, reinterpret_cast<void**>(&inner))) || !inner) return nullptr;
  T* t = nullptr;
  const HRESULT hr = inner->QueryInterface(__uuidof(T), reinterpret_cast<void**>(&t));
  inner->Release();
  if (SUCCEEDED(hr) && t && t != p) return t;
  if (t) t->Release();
  return nullptr;
}

// ReShade's version when the module is ReShade (it exports the string as ReShadeVersion), else "".
std::string reshade_version(HMODULE m) {
  const uintptr_t at = m ? reinterpret_cast<uintptr_t>(GetProcAddress(m, "ReShadeVersion")) : 0;
  const uintptr_t s = at ? mem::read_ptr(at) : 0;
  std::string v;
  char c = 0;
  for (uintptr_t i = 0; s && i < 31 && mem::read_safe(s + i, &c) && c >= ' ' && c <= '~'; ++i) v += c;
  return v;
}

// Whether a module's file is called `name`.
bool module_named(HMODULE m, const wchar_t* name) {
  wchar_t path[MAX_PATH] = {};
  const DWORD n = GetModuleFileNameW(m, path, MAX_PATH);
  const wchar_t* base = path;
  for (DWORD i = 0; i < n; ++i)
    if (path[i] == L'\\' || path[i] == L'/') base = path + i + 1;
  return n && _wcsicmp(base, name) == 0;
}

// The ReShade module in the process, whatever name it was loaded under (`first` is looked at first), and its
// version; nullptr when there is none.
HMODULE find_reshade(HMODULE first, std::string* version) {
  *version = reshade_version(first);
  if (!version->empty()) return first;
  using EnumFn = BOOL(WINAPI*)(HANDLE, HMODULE*, DWORD, DWORD*);
  const auto enum_modules =
      reinterpret_cast<EnumFn>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "K32EnumProcessModules"));
  HMODULE mods[1024];
  DWORD needed = 0;
  if (!enum_modules || !enum_modules(GetCurrentProcess(), mods, sizeof(mods), &needed)) return nullptr;
  for (DWORD i = 0; i < std::min<DWORD>(needed / sizeof(HMODULE), 1024); ++i)
    if (!(*version = reshade_version(mods[i])).empty()) return mods[i];
  return nullptr;
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
  // A wrapper's swap chain seen being made (the hooks in front of the wrapper's) presents through the one it wraps,
  // on the queue its queue wraps: that pair is kept too, without references either.
  IDXGISwapChain* inner_sc = g_wrapped ? wrapped(sc) : nullptr;
  ID3D12CommandQueue* inner_q = inner_sc ? wrapped(q) : nullptr;
  if (inner_sc) inner_sc->Release();
  if (inner_q) inner_q->Release();
  if (!inner_q) inner_sc = nullptr;
  if (g_cs_ready) {
    EnterCriticalSection(&g_cs);
    g_made.push_back({sc, q});
    if (inner_sc) g_made.push_back({inner_sc, inner_q});
    LeaveCriticalSection(&g_cs);
  }
  // The panel's swap chain was let go of, and the new one came at its address: the panel starts over on it.
  if (sc == g_sc) g_remade = sc;
  else if (inner_sc && inner_sc == g_sc) g_remade = inner_sc;
  if (inner_sc)
    logf("overlay: a D3D12 swap chain was made (%s): %p on queue %p, a wrapper's around the system's %p on queue %p",
         how, static_cast<void*>(sc), static_cast<void*>(q), static_cast<void*>(inner_sc), static_cast<void*>(inner_q));
  else
    logf("overlay: a D3D12 swap chain was made (%s): %p on queue %p", how, static_cast<void*>(sc), static_cast<void*>(q));
}

// The GPU the game renders on, for the log (watch names it from the watchdog thread): its queue's device's.
void note_adapter(ID3D12CommandQueue* q) {
  ID3D12Device* dev = nullptr;
  if (FAILED(q->GetDevice(__uuidof(ID3D12Device), reinterpret_cast<void**>(&dev))) || !dev) return;
  g_adapter = dev->GetAdapterLuid();
  dev->Release();
  InterlockedExchange(&g_adapter_known, 1);
}

// The queue a swap chain presents on: seen as the swap chain was made, else read out of it.
ID3D12CommandQueue* queue_of(IDXGISwapChain* sc, const char** how) {
  ID3D12CommandQueue* q = queue_of_made(sc);
  *how = "seen as it was made";
  if (!q) {
    q = queue_by_layout(sc);
    *how = "read out of the swap chain";
  }
  return q;
}

// Another swap chain than the panel's presented: counted, and the first few described.
void another_presents(IDXGISwapChain* sc, ULONGLONG now) {
  InterlockedIncrement64(&g_others);
  g_last_other = now;
  g_other_thread = GetCurrentThreadId();
  if (sc == g_other) return;
  g_other = sc;
  static volatile long said = 0;
  if (!log_first(&said, 4)) return;
  DXGI_SWAP_CHAIN_DESC sd{};
  char cls[64] = "?";
  const bool desc = SUCCEEDED(sc->GetDesc(&sd));
  if (desc && sd.OutputWindow) GetClassNameA(sd.OutputWindow, cls, sizeof(cls));
  logf("overlay: swap chain %p presents through the hook too (%ux%u, %u buffers, format %d, window %p '%s', %s; "
       "thread %lu) - the panel's is %p (window %p)",
       static_cast<void*>(sc), sd.BufferDesc.Width, sd.BufferDesc.Height, sd.BufferCount,
       static_cast<int>(sd.BufferDesc.Format), static_cast<void*>(sd.OutputWindow), cls,
       !desc ? "no description" : sd.OutputWindow && IsWindowVisible(sd.OutputWindow) ? "visible" : "not visible",
       GetCurrentThreadId(), static_cast<void*>(g_sc), static_cast<void*>(g_sc_wnd));
}

// Once a second: whether the panel's swap chain still presents to the game's window (frame generation in the
// display driver is said to move the game's swap chain onto a window nobody sees: follow), and still has the
// buffers, the format and the queue the renderer was made for - asked again at the next frame, else made anew
// (a swap chain made again where the last one was, and not seen being made, is the same pointer). Lock held.
void look_where(IDXGISwapChain* sc, ULONGLONG now) {
  static ULONGLONG last = 0;
  if (now < last + 1000) return;
  last = now;
  DXGI_SWAP_CHAIN_DESC sd{};
  const bool desc = SUCCEEDED(sc->GetDesc(&sd));
  if (desc && g12.init && (sd.BufferCount != g12.frames.size() || sd.BufferDesc.Format != g12.format))
    g12.checked = false;
  ID3D12CommandQueue* const q = g12.init ? queue_by_layout(sc) : nullptr;
  if (q && q != g_queue) {
    logf("overlay: the panel's swap chain %p presents on queue %p now, not %p - the panel's renderer is made again "
         "for it", static_cast<void*>(sc), static_cast<void*>(q), static_cast<void*>(g_queue));
    dx12_shutdown();
    g_queue = q;
  }
  if (desc && sd.OutputWindow == g_sc_wnd && IsWindowVisible(g_sc_wnd)) {
    if (g_elsewhere == sc) g_elsewhere = nullptr;
    return;
  }
  if (g_elsewhere == sc) return;
  g_elsewhere = sc;
  static volatile long said = 0;
  if (log_first(&said, 2))
    logf("overlay: the panel's swap chain %p now presents to window %p (%s), not to the game's window %p - should "
         "another swap chain present to the game's window, the panel follows it",
         static_cast<void*>(sc), static_cast<void*>(sd.OutputWindow),
         !desc ? "no description" : sd.OutputWindow && IsWindowVisible(sd.OutputWindow) ? "visible" : "not visible",
         static_cast<void*>(g_sc_wnd));
}

// The game's window, or a visible one of this process that is just as large: where the game's frames are seen.
bool shows_the_game(HWND w, const DXGI_SWAP_CHAIN_DESC& sd) {
  if (!w || !IsWindowVisible(w)) return false;
  if (w == g_sc_wnd) return true;
  DWORD pid = 0;
  GetWindowThreadProcessId(w, &pid);
  RECT rc{};
  return pid == GetCurrentProcessId() && GetClientRect(g_sc_wnd, &rc) &&
         sd.BufferDesc.Width == static_cast<UINT>(rc.right - rc.left) &&
         sd.BufferDesc.Height == static_cast<UINT>(rc.bottom - rc.top);
}

// The game's frames reach the screen through `sc` now, another swap chain than the panel's: the panel's has not
// presented through the hook for a second, or presents to a window that is not the game's, while `sc` presents
// to the game's window on a queue that is known. The panel follows (its renderer made again on the new one).
// Looked at twice a second at most, and only while the panel's swap chain is quiet or elsewhere.
bool follow(IDXGISwapChain* sc, ULONGLONG now) {
  const ULONGLONG last = g_last_ours;  // another thread's clock may be a little ahead of this one's
  const bool quiet = now > last && now - last >= 1000;
  if (!quiet && g_elsewhere != g_sc) return false;
  static ULONGLONG last_try = 0;
  if (now < last_try + 500) return false;
  last_try = now;
  static volatile long refused = 0;
  DXGI_SWAP_CHAIN_DESC sd{};
  if (FAILED(sc->GetDesc(&sd)) || !shows_the_game(sd.OutputWindow, sd)) {
    if (log_first(&refused, 2))
      logf("overlay: swap chain %p presents (window %p, %ux%u) while the panel's %p %s, but not to the game's window "
           "%p - the panel stays with %p",
           static_cast<void*>(sc), static_cast<void*>(sd.OutputWindow), sd.BufferDesc.Width, sd.BufferDesc.Height,
           static_cast<void*>(g_sc), quiet ? "is quiet" : "presents to another window", static_cast<void*>(g_sc_wnd),
           static_cast<void*>(g_sc));
    return false;
  }
  const char* how = "";
  ID3D12CommandQueue* q = queue_of(sc, &how);
  if (!q) {
    if (log_first(&refused, 2))
      log_warn("overlay: the game's frames now go through swap chain %p, whose command queue is unknown - the panel "
               "cannot follow them there", static_cast<void*>(sc));
    return false;
  }
  ImGuiLock guard;
  IDXGISwapChain* const old = g_sc;
  if (!old || old == sc) return old == sc;  // another thread got there first
  dx12_shutdown();
  char why[64] = "it presents to another window";
  if (quiet)
    std::snprintf(why, sizeof(why), "quiet through the hook for %llu ms", static_cast<unsigned long long>(now - last));
  g_sc = sc;
  g_queue = q;
  g_elsewhere = nullptr;
  g_remade = nullptr;
  g_last_ours = now;
  static volatile long followed = 0;
  if (log_first(&followed, 4))
    logf("overlay: the game's frames now reach the screen through swap chain %p (%ux%u, %u buffers, format %d, "
         "window %p, thread %lu), no longer through %p (%s) - the panel follows them; queue %p %s",
         static_cast<void*>(sc), sd.BufferDesc.Width, sd.BufferDesc.Height, sd.BufferCount,
         static_cast<int>(sd.BufferDesc.Format), static_cast<void*>(sd.OutputWindow), GetCurrentThreadId(),
         static_cast<void*>(old), why, static_cast<void*>(q), how);
  return true;
}

void note_runtime(ID3D12CommandQueue* q);

void on_present(IDXGISwapChain* sc, ULONGLONG now) {
  if (g_device_removed) return;
  if (g_remade == sc && sc == g_sc) {
    ImGuiLock guard;
    if (g_remade == sc && sc == g_sc) {
      g_remade = nullptr;
      dx12_shutdown();
      g_sc = nullptr;  // found again below, as the first one was
      g_elsewhere = nullptr;
      logf("overlay: the game made its swap chain again, where the last one was (%p) - the panel starts over on it",
           static_cast<void*>(sc));
    }
  }
  if (g_sc && sc != g_sc) {
    another_presents(sc, now);
    if (!follow(sc, now)) return;
  }
  perf::Timer timer(perf::kPresent);
  // The game's swap chain is found once: the first that presents to a visible window. From then on a frame
  // asks nothing of the swap chain or the window before there is something to draw (but for a look a second).
  if (!g_sc) {
    DXGI_SWAP_CHAIN_DESC sd{};
    if (FAILED(sc->GetDesc(&sd)) || !sd.OutputWindow || sd.OutputWindow == g_probe_wnd ||
        !IsWindowVisible(sd.OutputWindow))
      return;
    const char* how = "";
    ID3D12CommandQueue* q = queue_of(sc, &how);
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
    note_adapter(q);
    note_runtime(q);
    logf("overlay: the game presents through swap chain %p (%ux%u, %u buffers, format %d, window %p, thread %lu); "
         "queue %p %s",
         static_cast<void*>(sc), sd.BufferDesc.Width, sd.BufferDesc.Height, sd.BufferCount,
         static_cast<int>(sd.BufferDesc.Format), static_cast<void*>(sd.OutputWindow), GetCurrentThreadId(),
         static_cast<void*>(q), how);
  }
  InterlockedIncrement64(&g_ours);
  g_last_ours = now;
  g_ours_thread = GetCurrentThreadId();
  ImGuiLock guard;
  if (sc != g_sc || !ensure_context(g_sc_wnd)) return;  // the panel followed another swap chain meanwhile
  look_where(sc, now);
  // The panel when it is up; the health bars whenever they have something to show.
  const bool panel = wants_draw();
  const bool hud = hud_wanted();
  if (panel || hud) {
    perf::Timer drawing(perf::kOverlayFrame);
    dx12_frame(sc, panel, hud, now);
  }
}

void note_result(HRESULT hr) {
  if ((hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) && !g_device_removed) {
    g_device_removed = true;
    log_error("overlay: the game's device was removed (hr 0x%08lX) - the panel stops", static_cast<unsigned long>(hr));
  }
}

// --- the game's present: where the panel goes when the Present hooks are passed by ------------------
// Another hook can take Present's entry and leave the panel's out of the chain: RTSS (RivaTuner Statistics Server)
// puts its own jump back wherever it finds it moved, and the game's frames then reach DXGI unseen. The game hands each
// frame to Present from one routine of its renderer (hooked through the loader, hooks_game.cpp), on the thread that
// presents. Once it has handed on 8 frames in a row with no present at all coming through the Present hooks, the
// panel is drawn there, just before the game calls Present, until a present comes through the hooks again. Frames
// that reach the hooks from another thread or through another swap chain (follow) count as coming through. Where the
// hooks are not passed by they keep drawing: beneath a wrapper such as ReShade, after its work on the frame.
constexpr int kUnseenFrames = 8;
volatile LONG g_passed_by = 0;                     // the game's frames go past the Present hooks
volatile LONG64 g_game_frames = 0;                 // frames the game's present routine handed on
volatile LONG64 g_game_drawn = 0;                  // ... and the panel went into from there
LONG64 g_seen_calls = 0;                           // the Present hooks' count at the routine's last frame
int g_unseen = 0;                                  // the routine's frames in a row since a present came through
IDXGISwapChain* volatile g_routine_drew = nullptr;  // the swap chain the routine drew its last frame into

void arrived(UINT flags, ULONGLONG now) {
  InterlockedIncrement64(&g_calls);
  g_last_call = now;
  if (flags & DXGI_PRESENT_TEST) InterlockedIncrement64(&g_tests);
  else if (t_depth) InterlockedIncrement64(&g_nested);
}

// The frame coming through a Present hook had the panel drawn into it from the game's present routine already (the
// hooks were passed by until this frame): it is not drawn again.
bool drawn_by_the_routine(IDXGISwapChain* sc) {
  IDXGISwapChain* const drew = g_routine_drew;
  return drew && drew == sc &&
         InterlockedCompareExchangePointer(reinterpret_cast<void* volatile*>(&g_routine_drew), nullptr, sc) == sc;
}

HRESULT STDMETHODCALLTYPE hk_present(IDXGISwapChain* sc, UINT sync, UINT flags) {
  const ULONGLONG now = GetTickCount64();
  arrived(flags, now);
  if (t_depth == 0 && !(flags & DXGI_PRESENT_TEST) && !drawn_by_the_routine(sc)) {
    Depth depth;
    on_present(sc, now);
  }
  const HRESULT hr = g_orig_present ? g_orig_present(sc, sync, flags) : DXGI_ERROR_INVALID_CALL;
  if (sc == g_sc) note_result(hr);
  return hr;
}

HRESULT STDMETHODCALLTYPE hk_present1(IDXGISwapChain1* sc, UINT sync, UINT flags, const DXGI_PRESENT_PARAMETERS* p) {
  const ULONGLONG now = GetTickCount64();
  arrived(flags, now);
  if (t_depth == 0 && !(flags & DXGI_PRESENT_TEST) && !drawn_by_the_routine(sc)) {
    Depth depth;
    on_present(sc, now);
  }
  const HRESULT hr = g_orig_present1 ? g_orig_present1(sc, sync, flags, p) : DXGI_ERROR_INVALID_CALL;
  if (sc == g_sc) note_result(hr);
  return hr;
}

// The panel's swap chain is about to be resized: the panel holds none of its buffers, but its last frame on the
// GPU is let finish first, and the buffers and the format are asked again at the next frame.
void before_resize(IDXGISwapChain* sc) {
  if (sc != g_sc) return;
  ImGuiLock guard;
  if (sc != g_sc) return;
  dx12_idle();
  g12.checked = false;
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

// --- the log's account of where the frames go ---------------------------------------------------------------
// "dxgi.dll+0x1234" for an address in a module, else the address alone (code a hook made at run time).
std::string where(uintptr_t a) {
  char out[MAX_PATH + 32];
  HMODULE m = nullptr;
  if (a && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              reinterpret_cast<LPCWSTR>(a), &m) && m) {
    wchar_t path[MAX_PATH] = {};
    const DWORD n = GetModuleFileNameW(m, path, MAX_PATH);
    const wchar_t* name = path;
    for (DWORD i = 0; i < n; ++i)
      if (path[i] == L'\\' || path[i] == L'/') name = path + i + 1;
    char utf8[MAX_PATH] = "?";
    WideCharToMultiByte(CP_UTF8, 0, name, -1, utf8, sizeof(utf8), nullptr, nullptr);
    std::snprintf(out, sizeof(out), "%s+0x%llX", utf8,
                  static_cast<unsigned long long>(a - reinterpret_cast<uintptr_t>(m)));
  } else {
    std::snprintf(out, sizeof(out), "0x%llX", static_cast<unsigned long long>(a));
  }
  return out;
}

// The file a module was loaded from (UTF-8), for the log.
std::string module_path(HMODULE m) {
  wchar_t path[MAX_PATH] = {};
  char utf8[MAX_PATH * 3] = "?";
  if (m && GetModuleFileNameW(m, path, MAX_PATH))
    WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8, sizeof(utf8), nullptr, nullptr);
  return utf8;
}

HMODULE module_of(uintptr_t a) {
  HMODULE m = nullptr;
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                     reinterpret_cast<LPCWSTR>(a), &m);
  return m;
}

// Where the jump at `at` goes, for the forms a hook writes at a function's entry; 0 when there is none.
uintptr_t jump_target(uintptr_t at) {
  unsigned char b[16];
  if (!mem::copy_from(b, at, sizeof(b))) return 0;
  int32_t rel = 0;
  uint64_t abs = 0;
  if (b[0] == 0xE9) {  // jmp rel32
    std::memcpy(&rel, b + 1, 4);
    return at + 5 + static_cast<intptr_t>(rel);
  }
  if (b[0] == 0xEB) return at + 2 + static_cast<intptr_t>(static_cast<int8_t>(b[1]));  // jmp rel8
  if (b[0] == 0xFF && b[1] == 0x25) {  // jmp [rip+disp32]
    std::memcpy(&rel, b + 2, 4);
    return mem::read_ptr(at + 6 + static_cast<intptr_t>(rel));
  }
  if ((b[0] == 0x48 && b[1] == 0xB8 && b[10] == 0xFF && b[11] == 0xE0) ||                 // mov rax, imm64; jmp rax
      (b[0] == 0x49 && b[1] == 0xBB && b[10] == 0x41 && b[11] == 0xFF && b[12] == 0xE3)) {  // mov r11, imm64; jmp r11
    std::memcpy(&abs, b + 2, 8);
    return static_cast<uintptr_t>(abs);
  }
  if (b[0] == 0x68 && b[5] == 0xC7 && b[6] == 0x44 && b[7] == 0x24 && b[8] == 0x04 && b[13] == 0xC3) {  // push; ret
    uint32_t lo = 0, hi = 0;
    std::memcpy(&lo, b + 1, 4);
    std::memcpy(&hi, b + 9, 4);
    return (static_cast<uintptr_t>(hi) << 32) | lo;
  }
  return 0;
}

// What the code at a hooked entry does now: as the hook left it or written over since, and where its jumps lead
// (up to four), down to the panel's hook (`detour`) when they still get there.
std::string entry_state(uintptr_t at, const unsigned char* patch, uintptr_t detour) {
  if (!at) return "is not hooked";
  unsigned char now[8];
  if (!mem::copy_from(now, at, sizeof(now))) return "cannot be read";
  std::string s = std::memcmp(now, patch, sizeof(now)) == 0 ? "is as the hook left it" : "was written over since";
  uintptr_t p = at;
  bool ours = false;
  for (int hop = 0; hop < 4 && !ours; ++hop) {
    const uintptr_t t = jump_target(p);
    if (!t) break;
    s += p == at ? ": it jumps to " : ", then ";
    s += where(t);
    ours = t == detour;
    p = t;
  }
  if (p == at) s += ": no jump there";
  else s += ours ? " (the panel's hook)" : " (not the panel's hook)";
  return s;
}

// The D3D12 runtime the game's command queue belongs to (the file its methods are in), and whether it is D3D12 on
// Windows 7's (the game folder ships one), whose queues present without DXGI: ID3D12CommandQueueDownlevel::Present,
// past the panel's hooks. The game asks every queue it makes for that interface and presents through it when it
// answers.
void note_runtime(ID3D12CommandQueue* q) {
  static const GUID kQueueDownlevel = {0x38a8c5ef, 0x7ccb, 0x4e81, {0x91, 0x4f, 0xa6, 0xe9, 0xd0, 0x72, 0xc4, 0x94}};
  const uintptr_t table = mem::read_ptr(reinterpret_cast<uintptr_t>(q));
  const uintptr_t fn = table ? mem::read_ptr(table) : 0;
  const HMODULE in = module_of(fn);
  const std::string runtime = module_path(in);
  IUnknown* downlevel = nullptr;
  const bool on7 = SUCCEEDED(q->QueryInterface(kQueueDownlevel, reinterpret_cast<void**>(&downlevel))) && downlevel;
  if (downlevel) downlevel->Release();
  if (on7)
    log_warn("overlay: the game's command queue is D3D12 on Windows 7's (%s; it answers ID3D12CommandQueueDownlevel) - "
             "the game presents its frames without DXGI there, past the panel's hooks", runtime.c_str());
  else if (in && in == g_reshade)
    logf("overlay: the game's command queue is ReShade's proxy (%s) - the panel draws through ReShade's objects",
         runtime.c_str());
  else
    logf("overlay: the game's command queue is the D3D12 runtime's in %s", runtime.c_str());
}

// What the panel's swap chain's own table of methods holds at a slot: the hooked function, or another one.
std::string slot_state(IDXGISwapChain* sc, int slot, uintptr_t hooked) {
  const uintptr_t table = mem::read_ptr(reinterpret_cast<uintptr_t>(sc));
  const uintptr_t fn = table ? mem::read_ptr(table + static_cast<uintptr_t>(slot) * sizeof(void*)) : 0;
  if (!fn) return "cannot be read";
  if (fn == hooked) return "is the hooked function";
  return "is " + where(fn) + ", not the hooked " + where(hooked);
}

// The modules loaded from anywhere but Windows' own folders: the game's, the loader's, the plugins', the display
// driver's (the driver store, and System32's own nv*, amd*, ati* and ig* ones) and any overlay's - those that
// could stand between the game and the screen.
std::vector<std::string> foreign_modules() {
  std::vector<std::string> out;
  using EnumFn = BOOL(WINAPI*)(HANDLE, HMODULE*, DWORD, DWORD*);
  const auto enum_modules =
      reinterpret_cast<EnumFn>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "K32EnumProcessModules"));
  std::vector<HMODULE> mods(1024);
  DWORD needed = 0;
  if (!enum_modules ||
      !enum_modules(GetCurrentProcess(), mods.data(), static_cast<DWORD>(mods.size() * sizeof(HMODULE)), &needed))
    return out;
  mods.resize(std::min<size_t>(mods.size(), needed / sizeof(HMODULE)));
  wchar_t win[MAX_PATH] = {}, sys[MAX_PATH] = {};
  const size_t win_len = GetWindowsDirectoryW(win, MAX_PATH), sys_len = GetSystemDirectoryW(sys, MAX_PATH);
  for (wchar_t* c = win; *c; ++c) *c = static_cast<wchar_t>(std::towlower(*c));
  for (wchar_t* c = sys; *c; ++c) *c = static_cast<wchar_t>(std::towlower(*c));
  for (HMODULE m : mods) {
    wchar_t path[MAX_PATH] = {};
    const DWORD n = GetModuleFileNameW(m, path, MAX_PATH);
    if (!n || n >= MAX_PATH) continue;
    for (wchar_t* c = path; *c; ++c) *c = static_cast<wchar_t>(std::towlower(*c));
    const wchar_t* name = path;
    for (DWORD i = 0; i < n; ++i)
      if (path[i] == L'\\') name = path + i + 1;
    const bool windows = win_len && std::wcsncmp(path, win, win_len) == 0 && path[win_len] == L'\\';
    const bool driver = std::wcsstr(path, L"\\driverstore\\") != nullptr;
    const bool in_system32 = sys_len && std::wcsncmp(path, sys, sys_len) == 0 && name == path + sys_len + 1;
    const bool vendor = in_system32 && (std::wcsncmp(name, L"nv", 2) == 0 || std::wcsncmp(name, L"amd", 3) == 0 ||
                                        std::wcsncmp(name, L"ati", 3) == 0 || std::wcsncmp(name, L"ig", 2) == 0);
    if (windows && !driver && !vendor) continue;
    char utf8[MAX_PATH * 3] = "?";
    WideCharToMultiByte(CP_UTF8, 0, name, -1, utf8, sizeof(utf8), nullptr, nullptr);
    out.emplace_back(utf8);
  }
  return out;
}

// "N ms ago", or "never".
void ago(char* out, size_t n, ULONGLONG then, ULONGLONG now) {
  if (!then) std::snprintf(out, n, "never");
  else std::snprintf(out, n, "%llu ms ago", static_cast<unsigned long long>(now - std::min(then, now)));
}

// Once a session, the first time the panel is up and nothing has been drawn for a second: what came through the hooks,
// what the hooked entries and the panel's swap chain hold now, and the modules that could stand between the game
// and the screen.
void report(ULONGLONG now, ULONGLONG quiet_ms) {
  char last_call[32], last_ours[32], last_other[32];
  ago(last_call, sizeof(last_call), g_last_call, now);
  ago(last_ours, sizeof(last_ours), g_last_ours, now);
  ago(last_other, sizeof(last_other), g_last_other, now);
  log_warn("overlay: the panel is up and nothing has been drawn for %llu ms. The Present hooks saw %lld frames (the "
           "last %s; %lld only tests, %lld from inside the panel's own work): %lld of the panel's swap chain %p (the "
           "last %s, thread %lu), %lld of others (the last %p, %s, thread %lu); %lld frames drawn in all",
           static_cast<unsigned long long>(quiet_ms), g_calls, last_call, g_tests, g_nested, g_ours,
           static_cast<void*>(g_sc), last_ours, g_ours_thread, g_others, static_cast<void*>(g_other), last_other,
           g_other_thread, g_drawn);
  log_warn("overlay: the game's present routine handed on %lld frames, the last %s the panel's Present hook; %lld drawn "
           "from there", g_game_frames, g_passed_by ? "past" : "through", g_game_drawn);
  log_warn("overlay: Present's entry %s; Present1's %s",
           entry_state(g_present_at, g_present_patch, reinterpret_cast<uintptr_t>(&hk_present)).c_str(),
           entry_state(g_present1_at, g_present1_patch, reinterpret_cast<uintptr_t>(&hk_present1)).c_str());
  if (g_sc)
    log_warn("overlay: the panel's swap chain's own Present %s; its Present1 %s",
             slot_state(g_sc, kPresent, g_present_at).c_str(), slot_state(g_sc, kPresent1, g_present1_at).c_str());
  const std::vector<std::string> mods = foreign_modules();
  std::string line;
  int part = 0;
  for (size_t i = 0; i < mods.size(); ++i) {
    if (!line.empty()) line += ", ";
    line += mods[i];
    if (line.size() > 800 || i + 1 == mods.size()) {
      log_warn("overlay: modules from outside Windows' own folders (%d): %s", ++part, line.c_str());
      line.clear();
    }
  }
}

// The GPU the game renders on, named once (the adapter of its queue's device).
void describe_adapter() {
  static volatile long said = 0;
  if (!g_adapter_known || !g_create_factory || !log_once(&said)) return;
  IDXGIFactory4* f = nullptr;
  if (FAILED(g_create_factory(__uuidof(IDXGIFactory4), reinterpret_cast<void**>(&f))) || !f) return;
  IDXGIAdapter1* a = nullptr;
  if (SUCCEEDED(f->EnumAdapterByLuid(g_adapter, __uuidof(IDXGIAdapter1), reinterpret_cast<void**>(&a))) && a) {
    DXGI_ADAPTER_DESC1 d{};
    char name[256] = "?";
    if (SUCCEEDED(a->GetDesc1(&d)))
      WideCharToMultiByte(CP_UTF8, 0, d.Description, -1, name, sizeof(name), nullptr, nullptr);
    LARGE_INTEGER umd{};
    if (SUCCEEDED(a->CheckInterfaceSupport(__uuidof(IDXGIDevice), &umd)))
      logf("overlay: the game renders on %s (vendor 0x%04X, device 0x%04X; driver %u.%u.%u.%u)", name, d.VendorId,
           d.DeviceId, HIWORD(umd.HighPart), LOWORD(umd.HighPart), HIWORD(umd.LowPart), LOWORD(umd.LowPart));
    else
      logf("overlay: the game renders on %s (vendor 0x%04X, device 0x%04X)", name, d.VendorId, d.DeviceId);
    a->Release();
  }
  f->Release();
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

volatile long g_unseen_said = 0;

}  // namespace

bool install() {
  if (g_hooked) return true;
  if (!g_cs_ready) {
    InitializeCriticalSection(&g_cs);
    g_cs_ready = true;
  }
  // dxgi.dll as the game itself gets it: by name, the way its renderer asks for it (pm_dxgi::Instance::Init), so
  // that the game folder comes first and a dxgi.dll the player put there (ReShade, Special K, OptiScaler: wrappers
  // of the system's) loads as it does without the plugin. Windows hands out a DLL already loaded under the name
  // asked for, whatever folder it came from: System32's, loaded here by full path (the plugin's first way),
  // was the one the game was handed, and the wrapper never loaded. d3d12.dll is the system's by full path: the game
  // folder ships a Windows 7 d3d12.dll that a bare LoadLibrary would try first (the game's own loader goes on to
  // System32's when that one refuses to load).
  wchar_t sysdir[MAX_PATH] = {};
  GetSystemDirectoryW(sysdir, MAX_PATH);
  const std::wstring system_dxgi = std::wstring(sysdir) + L"\\dxgi.dll";
  HMODULE dxgi = GetModuleHandleW(L"dxgi.dll");
  if (!dxgi && !(dxgi = LoadLibraryW(L"dxgi.dll"))) {
    // One that does not load (the game's own LoadLibrary would fail on it alike): the system's.
    const DWORD error = GetLastError();
    dxgi = LoadLibraryW(system_dxgi.c_str());
    log_warn("overlay: the dxgi.dll the game would get does not load (error %lu) - the system's is taken%s", error,
             dxgi ? "" : ", and does not load either");
  }
  HMODULE d3d12 = GetModuleHandleW(L"d3d12.dll");
  if (!d3d12) d3d12 = LoadLibraryW((std::wstring(sysdir) + L"\\d3d12.dll").c_str());
  for (int i = 0; (!dxgi || !d3d12) && i < 1500; ++i) {  // else wait for the game to load them (30 s)
    Sleep(20);
    if (!dxgi) dxgi = GetModuleHandleW(L"dxgi.dll");
    if (!d3d12) d3d12 = GetModuleHandleW(L"d3d12.dll");
  }
  // ReShade, by the module that exports its version, whatever name it was loaded under. Once ReShade as dxgi.dll has
  // loaded the system's behind it, Wine may answer "dxgi.dll" with the system's (see the end of this): ReShade's is
  // the one the game has.
  std::string reshade;
  const HMODULE reshade_module = find_reshade(dxgi, &reshade);
  g_reshade = reshade_module;
  if (reshade_module && reshade_module != dxgi && module_named(reshade_module, L"dxgi.dll")) dxgi = reshade_module;
  auto create_factory = dxgi ? reinterpret_cast<CreateFactoryFn>(GetProcAddress(dxgi, "CreateDXGIFactory1")) : nullptr;
  auto create_device = d3d12 ? reinterpret_cast<CreateDeviceFn>(GetProcAddress(d3d12, "D3D12CreateDevice")) : nullptr;
  if (!create_factory || !create_device) {
    log_error("overlay: dxgi.dll (%p) / d3d12.dll (%p) not usable (CreateDXGIFactory1 %p, D3D12CreateDevice %p, error %lu) - no panel",
              static_cast<void*>(dxgi), static_cast<void*>(d3d12), reinterpret_cast<void*>(create_factory),
              reinterpret_cast<void*>(create_device), GetLastError());
    return false;
  }
  g_create_factory = create_factory;
  logf("overlay: dxgi.dll %p (%s), d3d12.dll %p (%s)", static_cast<void*>(dxgi), module_path(dxgi).c_str(),
       static_cast<void*>(d3d12), module_path(d3d12).c_str());
  // A dxgi.dll that is not the system's stands between the game and DXGI, as it does without the plugin.
  const bool wrapper = dxgi != GetModuleHandleW(system_dxgi.c_str());
  const std::string who = reshade_module ? "ReShade " + reshade : "the game folder's dxgi.dll";
  if (wrapper)
    logf("overlay: the game's dxgi.dll is not the system's but %s - it stands between the game and DXGI",
         reshade_module == dxgi ? who.c_str() : "a wrapper's");
  else if (reshade_module)
    logf("overlay: %s is loaded (%s)", who.c_str(), module_path(reshade_module).c_str());
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
  // A wrapper's proxies in place of the system's factory and device (ReShade makes them of every factory and
  // device): the probe is made of what they wrap, so that the hooks go on the system's swap chain and factory
  // classes, beneath the wrapper. The panel is then drawn after the wrapper's work on each frame (ReShade's
  // effects), into the system's swap chain the wrapper hands the frames to. Both or neither: the system's factory is
  // not to be handed a wrapper's queue.
  IDXGIFactory2* const factory_proxy = factory;  // let go of last, after the objects of the system's they wrap
  ID3D12Device* const dev_proxy = dev;
  IDXGIFactory2* const inner_factory = wrapped(factory);
  ID3D12Device* const inner_dev = wrapped(dev);
  const bool beneath = inner_factory && inner_dev;
  if (beneath) {
    factory = inner_factory;
    dev = inner_dev;
  } else {
    if (inner_factory) inner_factory->Release();
    if (inner_dev) inner_dev->Release();
  }
  g_wrapped = wrapper || reshade_module || beneath;
  const auto let_go = [&](IDXGISwapChain1* s, ID3D12CommandQueue* q) {
    if (s) s->Release();
    if (q) q->Release();
    if (beneath) {
      dev->Release();
      factory->Release();
    }
    dev_proxy->Release();
    factory_proxy->Release();
  };
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
    let_go(nullptr, queue);
    return false;
  }
  learn_queue_layout(sc, queue);
  bool ok = true;
  void* const present_at = reinterpret_cast<void*>(method<PresentFn>(sc, kPresent));
  void* present1_at = nullptr;
  ok &= hook(present_at, reinterpret_cast<void*>(&hk_present), reinterpret_cast<void**>(&g_orig_present),
             "IDXGISwapChain::Present");
  ok &= hook(reinterpret_cast<void*>(method<ResizeBuffersFn>(sc, kResizeBuffers)), reinterpret_cast<void*>(&hk_resize),
             reinterpret_cast<void**>(&g_orig_resize), "IDXGISwapChain::ResizeBuffers");
  IDXGISwapChain3* sc3 = nullptr;
  if (SUCCEEDED(sc->QueryInterface(__uuidof(IDXGISwapChain3), reinterpret_cast<void**>(&sc3))) && sc3) {
    present1_at = reinterpret_cast<void*>(method<Present1Fn>(sc3, kPresent1));
    ok &= hook(present1_at, reinterpret_cast<void*>(&hk_present1), reinterpret_cast<void**>(&g_orig_present1),
               "IDXGISwapChain1::Present1");
    ok &= hook(reinterpret_cast<void*>(method<ResizeBuffers1Fn>(sc3, kResizeBuffers1)), reinterpret_cast<void*>(&hk_resize1),
               reinterpret_cast<void**>(&g_orig_resize1), "IDXGISwapChain3::ResizeBuffers1");
    sc3->Release();
  } else {
    log_warn("overlay: the probe swap chain is no IDXGISwapChain3 - Present1 and ResizeBuffers1 are not hooked");
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
  // What the hooks wrote, for the look at whether anything wrote over them later (watch).
  g_present_at = reinterpret_cast<uintptr_t>(present_at);
  g_present1_at = reinterpret_cast<uintptr_t>(present1_at);
  if (g_present_at) mem::copy_from(g_present_patch, g_present_at, sizeof(g_present_patch));
  if (g_present1_at) mem::copy_from(g_present1_patch, g_present1_at, sizeof(g_present1_patch));
  let_go(sc, queue);
  g_hooked = ok;
  const HMODULE present_module = module_of(g_present_at);
  logf("overlay: swap chain class %s (Present at %s, Present1 at %s; %s)",
       ok ? "hooked: Present, Present1, ResizeBuffers, CreateSwapChain*" : "NOT hooked",
       where(g_present_at).c_str(), present1_at ? where(g_present1_at).c_str() : "-",
       module_path(present_module).c_str());
  if (beneath)
    logf("overlay: %s hands out the objects it wraps - the panel's hooks are on the system's swap chain beneath it, "
         "and the panel is drawn after its work on each frame", who.c_str());
  else if ((wrapper || reshade_module) && (present_module == dxgi || present_module == reshade_module))
    logf("overlay: the swap chain class hooked is %s's own - the panel is drawn before its work on each frame%s",
         who.c_str(), reshade_module ? " (ReShade's effects go over it; ReShade 6.6 and later hand out the swap chains "
                                       "they wrap, and the panel goes on top)" : "");
  // Wine (Proton) answers a DLL asked for by name from a one-entry cache of the module looked at last, before its
  // list in load order (Windows has only the list). The wrapper loaded the system's dxgi.dll behind it during the
  // probe, and the lines above looked at that one: the game asking for "dxgi.dll" next would be handed it, and the
  // wrapper's proxies (the device, the queue: its hooks on d3d12.dll make them) would meet the system's factory. The
  // game's own dxgi.dll is the module looked at last.
  if (wrapper) GetProcAddress(dxgi, "CreateDXGIFactory1");
  return ok;
}

void uninstall() {
  if (g_minhook) {
    MH_DisableHook(MH_ALL_HOOKS);
    MH_RemoveHook(MH_ALL_HOOKS);
  }
  g_hooked = false;
  if (g12.init) {
    ImGuiLock guard;
    dx12_shutdown();
  }
  if (g_probe_wnd) {
    DestroyWindow(g_probe_wnd);
    g_probe_wnd = nullptr;
  }
}

bool presenting() { return g_sc != nullptr; }

void presents_note(char* out, size_t n) {
  const ULONGLONG now = GetTickCount64();
  char ours[32], other[32], game[96] = "";
  ago(ours, sizeof(ours), g_last_ours, now);
  if (g_passed_by)
    std::snprintf(game, sizeof(game), "; the game's present routine %lld, %lld of them drawn there", g_game_frames,
                  g_game_drawn);
  if (g_others) {
    ago(other, sizeof(other), g_last_other, now);
    std::snprintf(out, n, " (the Present hooks saw %lld frames: the panel's swap chain's last %s, another's %s%s)",
                  g_calls, ours, other, game);
  } else {
    std::snprintf(out, n, " (the Present hooks saw %lld frames, the panel's swap chain's last %s%s)", g_calls, ours,
                  game);
  }
}

void watch(ULONGLONG now, bool up) {
  describe_adapter();
  static ULONGLONG up_since = 0;
  if (!up) {
    up_since = 0;
    return;
  }
  if (!up_since) up_since = now;
  const ULONGLONG drawn = g_last_drawn;
  const ULONGLONG since = std::max(up_since, drawn);  // a frame drawn after `now` was taken: since > now
  if (now < since + 1000) return;
  if (log_once(&g_unseen_said, LogLevel::kWarning)) report(now, now - since);
}

void game_present_begin(void* swap_chain) {
  InterlockedIncrement64(&g_game_frames);
  // Whether a present came through the hooks since the routine's last frame (the game's own frames run on this
  // thread, one at a time).
  const LONG64 calls = g_calls;
  const bool came = calls != g_seen_calls;
  g_seen_calls = calls;
  g_unseen = came ? 0 : std::min(g_unseen + 1, kUnseenFrames);
  static volatile long said_by = 0, said_back = 0;
  if (!g_passed_by && g_unseen >= kUnseenFrames) {
    InterlockedExchange(&g_passed_by, 1);
    if (log_first(&said_by, 4))
      log_warn("overlay: the game's frames go to DXGI past the panel's Present hooks (%d in a row; Present's entry %s) - "
               "the panel is drawn from the game's present routine", kUnseenFrames,
               entry_state(g_present_at, g_present_patch, reinterpret_cast<uintptr_t>(&hk_present)).c_str());
  } else if (g_passed_by && came) {
    InterlockedExchange(&g_passed_by, 0);
    if (log_first(&said_back, 4))
      logf("overlay: presents come through the panel's Present hooks again - they draw the panel");
  }
  IDXGISwapChain* sc = static_cast<IDXGISwapChain*>(swap_chain);
  if (!g_passed_by || !sc || t_depth) return;
  // The swap chain the game holds may be a wrapper's: the panel goes into the system's one it wraps, the one the
  // Present hooks see.
  if (g_wrapped) {
    if (IDXGISwapChain* inner = wrapped(sc)) {
      inner->Release();
      sc = inner;
    }
  }
  const LONG64 before = g_drawn;
  {
    Depth depth;
    on_present(sc, GetTickCount64());
  }
  if (g_drawn == before) return;
  InterlockedIncrement64(&g_game_drawn);
  g_routine_drew = sc;  // should this frame come through a Present hook after all, it is not drawn twice
}

}  // namespace d2rcc::overlay::d3d
