// Copyright 2020 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/logging/log.h"
#include "core/frontend/emu_window.h"
#include "video_core/vulkan_common/vulkan_surface.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

// Include these late to avoid polluting previous headers
#ifdef _WIN32
#include <windows.h>
// ensure include order
#include <vulkan/vulkan_win32.h>
#endif

#if defined(__APPLE__)
#include <objc/message.h>
#include <vulkan/vulkan.h>
#endif

#if !defined(_WIN32) && !defined(__APPLE__)
#include <X11/Xlib.h>
#include <vulkan/vulkan_wayland.h>
#include <vulkan/vulkan_xlib.h>
#endif

#if defined(__APPLE__)

namespace {

void* MetalLayer([[maybe_unused]] void* render_surface) {
    id view = reinterpret_cast<id>(render_surface);
    Class clsCAMetalLayer = objc_getClass("CAMetalLayer");
    if (!clsCAMetalLayer) {
        LOG_ERROR(Render_Vulkan, "Failed to get CAMetalLayer class.");
        return nullptr;
    }

    // [CAMetalLayer layer]
    id layer = reinterpret_cast<id (*)(Class, SEL)>(objc_msgSend)(objc_getClass("CAMetalLayer"),
                                                                  sel_getUid("layer"));
    if (!layer) {
        LOG_ERROR(Render_Vulkan, "Failed to create Metal layer.");
        return nullptr;
    }
    // [view setWantsLayer:YES]
    reinterpret_cast<void (*)(id, SEL, BOOL)>(objc_msgSend)(view, sel_getUid("setWantsLayer:"),
                                                            YES);
    // [view setLayer:layer]
    reinterpret_cast<void (*)(id, SEL, id)>(objc_msgSend)(view, sel_getUid("setLayer:"), layer);
    // NSScreen* screen = [NSScreen mainScreen]
    id screen = reinterpret_cast<id (*)(Class, SEL)>(objc_msgSend)(objc_getClass("NSScreen"),
                                                                   sel_getUid("mainScreen"));
    // CGFloat factor = [screen backingScaleFactor]
    double factor = reinterpret_cast<double (*)(id, SEL)>(objc_msgSend)(
        screen, sel_getUid("backingScaleFactor"));
    // layer.contentsScale = factor
    reinterpret_cast<void (*)(id, SEL, double)>(objc_msgSend)(
        layer, sel_getUid("setContentsScale:"), factor);

    // Store layer ptr, so MoltenVK doesn't call [NSView layer] outside main thread.
    render_surface = layer;

    return layer;
}
#endif

}

namespace Vulkan {

vk::SurfaceKHR CreateSurface(const vk::Instance& instance,
                             const Core::Frontend::EmuWindow& emu_window) {
    [[maybe_unused]] const vk::InstanceDispatch& dld = instance.Dispatch();
    [[maybe_unused]] const auto& window_info = emu_window.GetWindowInfo();
    VkSurfaceKHR unsafe_surface = nullptr;

#ifdef _WIN32
    if (window_info.type == Core::Frontend::WindowSystemType::Windows) {
        const HWND hWnd = static_cast<HWND>(window_info.render_surface);
        const VkWin32SurfaceCreateInfoKHR win32_ci{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
                                                   nullptr, 0, nullptr, hWnd};
        const auto vkCreateWin32SurfaceKHR = reinterpret_cast<PFN_vkCreateWin32SurfaceKHR>(
            dld.vkGetInstanceProcAddr(*instance, "vkCreateWin32SurfaceKHR"));
        if (!vkCreateWin32SurfaceKHR ||
            vkCreateWin32SurfaceKHR(*instance, &win32_ci, nullptr, &unsafe_surface) != VK_SUCCESS) {
            LOG_ERROR(Render_Vulkan, "Failed to initialize Win32 surface");
            throw vk::Exception(VK_ERROR_INITIALIZATION_FAILED);
        }
    }
#endif
#if defined(__APPLE__)
    if (window_info.type == Core::Frontend::WindowSystemType::MacOs) {
        const CAMetalLayer *layer = static_cast<CAMetalLayer*>(MetalLayer(window_info.render_surface));
        const VkMetalSurfaceCreateInfoEXT metal_ci{VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT,
                                                   nullptr, 0, layer};
        const auto vkCreateMetalSurfaceEXT = reinterpret_cast<PFN_vkCreateMetalSurfaceEXT>(
            dld.vkGetInstanceProcAddr(*instance, "vkCreateMetalSurfaceEXT"));
        if (!vkCreateMetalSurfaceEXT ||
            vkCreateMetalSurfaceEXT(*instance, &metal_ci, nullptr, &unsafe_surface) != VK_SUCCESS) {
            LOG_ERROR(Render_Vulkan, "Failed to initialize metal surface");
            throw vk::Exception(VK_ERROR_INITIALIZATION_FAILED);
        }
    }
#endif
#if !defined(_WIN32) && !defined(__APPLE__)
    if (window_info.type == Core::Frontend::WindowSystemType::X11) {
        const VkXlibSurfaceCreateInfoKHR xlib_ci{
            VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR, nullptr, 0,
            static_cast<Display*>(window_info.display_connection),
            reinterpret_cast<Window>(window_info.render_surface)};
        const auto vkCreateXlibSurfaceKHR = reinterpret_cast<PFN_vkCreateXlibSurfaceKHR>(
            dld.vkGetInstanceProcAddr(*instance, "vkCreateXlibSurfaceKHR"));
        if (!vkCreateXlibSurfaceKHR ||
            vkCreateXlibSurfaceKHR(*instance, &xlib_ci, nullptr, &unsafe_surface) != VK_SUCCESS) {
            LOG_ERROR(Render_Vulkan, "Failed to initialize Xlib surface");
            throw vk::Exception(VK_ERROR_INITIALIZATION_FAILED);
        }
    }
    if (window_info.type == Core::Frontend::WindowSystemType::Wayland) {
        const VkWaylandSurfaceCreateInfoKHR wayland_ci{
            VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR, nullptr, 0,
            static_cast<wl_display*>(window_info.display_connection),
            static_cast<wl_surface*>(window_info.render_surface)};
        const auto vkCreateWaylandSurfaceKHR = reinterpret_cast<PFN_vkCreateWaylandSurfaceKHR>(
            dld.vkGetInstanceProcAddr(*instance, "vkCreateWaylandSurfaceKHR"));
        if (!vkCreateWaylandSurfaceKHR ||
            vkCreateWaylandSurfaceKHR(*instance, &wayland_ci, nullptr, &unsafe_surface) !=
                VK_SUCCESS) {
            LOG_ERROR(Render_Vulkan, "Failed to initialize Wayland surface");
            throw vk::Exception(VK_ERROR_INITIALIZATION_FAILED);
        }
    }
#endif
    if (!unsafe_surface) {
        LOG_ERROR(Render_Vulkan, "Presentation not supported on this platform");
        throw vk::Exception(VK_ERROR_INITIALIZATION_FAILED);
    }
    return vk::SurfaceKHR(unsafe_surface, *instance, dld);
}

} // namespace Vulkan
