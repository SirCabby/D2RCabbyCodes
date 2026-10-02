// Stand-ins for the two graphics functions Dear ImGui's DX12 backend links
// against, so the plugin imports no graphics DLL: dxgi.dll and d3dcompiler are
// looked up when first used. Inside the game they are already loaded (D2R
// renders through Direct3D 12), and a missing d3dcompiler only costs the panel,
// never the game.
#include <windows.h>
#include <d3dcompiler.h>
#include <dxgi.h>

namespace {

template <typename Fn>
Fn resolve(const wchar_t* const* dlls, size_t count, const char* name) {
  for (size_t i = 0; i < count; ++i) {
    HMODULE m = GetModuleHandleW(dlls[i]);
    if (!m) m = LoadLibraryW(dlls[i]);
    if (!m) continue;
    if (FARPROC p = GetProcAddress(m, name)) return reinterpret_cast<Fn>(p);
  }
  return nullptr;
}

}  // namespace

extern "C" HRESULT WINAPI CreateDXGIFactory1(REFIID riid, void** factory) {
  using Fn = HRESULT(WINAPI*)(REFIID, void**);
  static const wchar_t* const dlls[] = {L"dxgi.dll"};
  static Fn fn = resolve<Fn>(dlls, 1, "CreateDXGIFactory1");
  return fn ? fn(riid, factory) : DXGI_ERROR_UNSUPPORTED;
}

extern "C" HRESULT WINAPI D3DCompile(LPCVOID src, SIZE_T src_size, LPCSTR source_name, const D3D_SHADER_MACRO* defines,
                                     ID3DInclude* include, LPCSTR entry, LPCSTR target, UINT flags1, UINT flags2,
                                     ID3DBlob** code, ID3DBlob** errors) {
  using Fn = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT,
                              UINT, ID3DBlob**, ID3DBlob**);
  static const wchar_t* const dlls[] = {L"d3dcompiler_47.dll", L"d3dcompiler_46.dll", L"d3dcompiler_43.dll"};
  static Fn fn = resolve<Fn>(dlls, 3, "D3DCompile");
  return fn ? fn(src, src_size, source_name, defines, include, entry, target, flags1, flags2, code, errors)
            : E_NOTIMPL;
}
