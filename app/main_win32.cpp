#include <cyhwinfo/ui.hpp>

#include <cyui/Core.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace {

using Microsoft::WRL::ComPtr;

class Graphics {
public:
    bool create(HWND window) {
        DXGI_SWAP_CHAIN_DESC swap{};
        swap.BufferCount = 2;
        swap.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swap.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swap.OutputWindow = window;
        swap.SampleDesc.Count = 1;
        swap.Windowed = TRUE;
        swap.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        constexpr D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
        auto open = [&](D3D_DRIVER_TYPE type) {
            return D3D11CreateDeviceAndSwapChain(nullptr, type, nullptr, 0, levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
                &swap, swap_chain.ReleaseAndGetAddressOf(), device.ReleaseAndGetAddressOf(), nullptr,
                context.ReleaseAndGetAddressOf());
        };
        if (FAILED(open(D3D_DRIVER_TYPE_HARDWARE)) && FAILED(open(D3D_DRIVER_TYPE_WARP))) {
            return false;
        }
        return create_target();
    }

    bool resize(UINT width, UINT height) {
        context->OMSetRenderTargets(0, nullptr, nullptr);
        target.Reset();
        return SUCCEEDED(swap_chain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0)) && create_target();
    }

    HRESULT present() {
        constexpr UINT interval = 1;
        const auto color = ImGui::ColorConvertU32ToFloat4(cyui::palette().bg0);
        const float clear[]{color.x, color.y, color.z, color.w};
        auto* view = target.Get();
        context->OMSetRenderTargets(1, &view, nullptr);
        context->ClearRenderTargetView(view, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        return swap_chain->Present(interval, 0);
    }

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain> swap_chain;

private:
    bool create_target() {
        ComPtr<ID3D11Texture2D> back_buffer;
        return SUCCEEDED(swap_chain->GetBuffer(0, IID_PPV_ARGS(&back_buffer))) &&
               SUCCEEDED(device->CreateRenderTargetView(back_buffer.Get(), nullptr, target.ReleaseAndGetAddressOf()));
    }

    ComPtr<ID3D11RenderTargetView> target;
};

struct Window {
    UINT width = 0;
    UINT height = 0;
    float dpi = 0;
    bool minimized = false;
};

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* state = reinterpret_cast<Window*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (ImGui::GetCurrentContext() && ImGui_ImplWin32_WndProcHandler(window, message, wparam, lparam)) {
        return 1;
    }
    switch (message) {
    case WM_SIZE:
        if (state) {
            state->minimized = wparam == SIZE_MINIMIZED;
            if (!state->minimized) {
                state->width = LOWORD(lparam);
                state->height = HIWORD(lparam);
            }
        }
        return 0;
    case WM_GETMINMAXINFO:
        if (state) {
            const auto dpi = ImGui_ImplWin32_GetDpiScaleForHwnd(window);
            auto* limits = reinterpret_cast<MINMAXINFO*>(lparam);
            limits->ptMinTrackSize = {static_cast<LONG>(760 * dpi), static_cast<LONG>(530 * dpi)};
        }
        return 0;
    case WM_DPICHANGED:
        if (state) {
            state->dpi = static_cast<float>(HIWORD(wparam)) / 96;
            const auto& proposed = *reinterpret_cast<const RECT*>(lparam);
            SetWindowPos(window, nullptr, proposed.left, proposed.top, proposed.right - proposed.left,
                         proposed.bottom - proposed.top, SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window, message, wparam, lparam);
    }
}

void frame(cyhwinfo::ui::Dashboard& dashboard, cyhwinfo::ui::Monitor& monitor) {
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoBringToFrontOnFocus;
    const cyui::ScopedVar padding(ImGuiStyleVar_WindowPadding, {0, 0});
    const cyui::ScopedColor ground(ImGuiCol_WindowBg, cyui::palette().bg0);
    if (ImGui::Begin("CyHWInfo##dashboardHost", nullptr, flags)) {
        dashboard.draw(monitor);
    }
    ImGui::End();
    cyui::renderNotifications();
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    ImGui_ImplWin32_EnableDpiAwareness();
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_CLASSDC;
    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.lpszClassName = L"CyHWInfo.Monitor";
    if (!RegisterClassExW(&window_class)) {
        return 1;
    }

    Window state;
    const auto screen = MonitorFromPoint(POINT{}, MONITOR_DEFAULTTOPRIMARY);
    const auto dpi = ImGui_ImplWin32_GetDpiScaleForMonitor(screen);
    MONITORINFO monitor_info{sizeof(MONITORINFO)};
    const bool has_work_area = GetMonitorInfoW(screen, &monitor_info) != FALSE;
    const auto max_width = has_work_area ? monitor_info.rcWork.right - monitor_info.rcWork.left : 1360L;
    const auto max_height = has_work_area ? monitor_info.rcWork.bottom - monitor_info.rcWork.top : 880L;
    const auto width = std::min(static_cast<LONG>(1360 * dpi), max_width);
    const auto height = std::min(static_cast<LONG>(900 * dpi), max_height);
    const auto left = monitor_info.rcWork.left + (max_width - width) / 2;
    const auto top = monitor_info.rcWork.top + (max_height - height) / 2;
    const HWND window = CreateWindowExW(0, window_class.lpszClassName, L"CyHWInfo", WS_OVERLAPPEDWINDOW,
                                       left, top, width, height, nullptr, nullptr, instance, &state);
    if (!window) {
        UnregisterClassW(window_class.lpszClassName, instance);
        return 1;
    }

    Graphics graphics;
    if (!graphics.create(window)) {
        MessageBoxW(window, L"Could not initialize Direct3D 11.", L"CyHWInfo", MB_ICONERROR | MB_OK);
        DestroyWindow(window);
        UnregisterClassW(window_class.lpszClassName, instance);
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    cyui::Config config;
    config.themeKey = "amber-night";
    config.density = cyui::Density::comfortable;
    config.dpiScale = ImGui_ImplWin32_GetDpiScaleForHwnd(window);
    cyui::apply(config);
    cyui::loadFonts();
    cyhwinfo::ui::register_translations();
    const bool platform_ready = ImGui_ImplWin32_Init(window);
    const bool renderer_ready = platform_ready && ImGui_ImplDX11_Init(graphics.device.Get(), graphics.context.Get());
    if (!renderer_ready) {
        if (platform_ready) {
            ImGui_ImplWin32_Shutdown();
        }
        ImGui::DestroyContext();
        DestroyWindow(window);
        UnregisterClassW(window_class.lpszClassName, instance);
        return 1;
    }

    ShowWindow(window, show);
    UpdateWindow(window);
    cyhwinfo::ui::Dashboard dashboard;
    dashboard.select(cyhwinfo::ui::Page::sensors);
    cyhwinfo::ui::Monitor sampler;

    int exit_code = 0;
    bool running = true;
    bool occluded = false;
    while (running) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                running = false;
                break;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (!running) {
            break;
        }
        if (state.minimized || (occluded && graphics.swap_chain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED)) {
            Sleep(30);
            continue;
        }
        if (state.width != 0 && state.height != 0) {
            if (!graphics.resize(state.width, state.height)) {
                exit_code = 1;
                break;
            }
            state.width = state.height = 0;
        }
        if (state.dpi > 0) {
            cyui::applyDpi(state.dpi);
            state.dpi = 0;
        }
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        frame(dashboard, sampler);
        ImGui::Render();
        const auto status = graphics.present();
        occluded = status == DXGI_STATUS_OCCLUDED;
        if (FAILED(status)) {
            exit_code = 1;
            break;
        }
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    if (IsWindow(window)) {
        DestroyWindow(window);
    }
    UnregisterClassW(window_class.lpszClassName, instance);
    return exit_code;
}
