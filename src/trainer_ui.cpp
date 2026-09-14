#include "trainer_ui.h"

#include <Windows.h>
#include <wincodec.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

#include <imgui.h>

#include "resource.h"

namespace hd::ui
{
namespace
{
constexpr ImU32 kTransparent = IM_COL32(0, 0, 0, 0);
constexpr ImU32 kNearBlack = IM_COL32(7, 8, 9, 255);
constexpr ImU32 kPanelBlack = IM_COL32(14, 15, 16, 255);
constexpr ImU32 kCardBlack = IM_COL32(22, 23, 24, 255);
constexpr ImU32 kCardBlackLight = IM_COL32(31, 32, 34, 255);
constexpr ImU32 kGold = IM_COL32(240, 197, 63, 255);
constexpr ImU32 kGoldBright = IM_COL32(255, 218, 83, 255);
constexpr ImU32 kGoldDark = IM_COL32(132, 94, 12, 255);
constexpr ImU32 kText = IM_COL32(247, 247, 244, 255);
constexpr ImU32 kMuted = IM_COL32(151, 153, 157, 255);
constexpr ImU32 kBorder = IM_COL32(58, 59, 61, 255);
constexpr ImU32 kGreen = IM_COL32(70, 209, 107, 255);
constexpr ImU32 kOrange = IM_COL32(244, 177, 53, 255);
constexpr ImU32 kRed = IM_COL32(241, 78, 72, 255);

IDirect3DTexture9* g_logo_texture = nullptr;
IDirect3DTexture9* g_banner_texture = nullptr;
IDirect3DDevice9* g_device = nullptr;
ImFont* g_body_font = nullptr;
ImFont* g_bold_font = nullptr;
ImFont* g_heading_font = nullptr;
ImFont* g_title_font = nullptr;
ImVector<ImWchar> g_font_ranges;
float g_scale = 1.0f;
float g_dpi_scale = 1.0f;

std::string g_modal_title;
std::string g_modal_body;
std::string g_modal_status;
bool g_modal_requested = false;

float S(float value)
{
    return value * g_scale;
}

ImTextureID TextureId(IDirect3DTexture9* texture)
{
    return static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(texture));
}

ImFont* FontOrDefault(ImFont* font)
{
    return font ? font : ImGui::GetFont();
}

ImU32 WithAlpha(ImU32 color, int alpha)
{
    return (color & 0x00FFFFFFU) |
        (static_cast<ImU32>((std::clamp)(alpha, 0, 255)) << 24U);
}

std::array<ImVec2, 8> BeveledPoints(
    const ImVec2& minimum,
    const ImVec2& maximum,
    float cut)
{
    cut = (std::min)(cut, (std::min)(
        (maximum.x - minimum.x) * 0.25f,
        (maximum.y - minimum.y) * 0.25f));
    return {{
        ImVec2(minimum.x + cut, minimum.y),
        ImVec2(maximum.x - cut, minimum.y),
        ImVec2(maximum.x, minimum.y + cut),
        ImVec2(maximum.x, maximum.y - cut),
        ImVec2(maximum.x - cut, maximum.y),
        ImVec2(minimum.x + cut, maximum.y),
        ImVec2(minimum.x, maximum.y - cut),
        ImVec2(minimum.x, minimum.y + cut)
    }};
}

void DrawBeveledPanel(
    ImDrawList* draw_list,
    const ImVec2& minimum,
    const ImVec2& maximum,
    float cut,
    ImU32 fill,
    ImU32 border,
    float thickness = 1.0f)
{
    const auto points = BeveledPoints(minimum, maximum, cut);
    draw_list->AddConvexPolyFilled(points.data(),
        static_cast<int>(points.size()), fill);
    if ((border & IM_COL32_A_MASK) != 0)
    {
        draw_list->AddPolyline(points.data(),
            static_cast<int>(points.size()), border,
            ImDrawFlags_Closed, thickness);
    }
}

void DrawCheckMark(
    ImDrawList* draw_list,
    const ImVec2& minimum,
    const ImVec2& maximum,
    ImU32 color)
{
    const float width = maximum.x - minimum.x;
    const float height = maximum.y - minimum.y;
    const ImVec2 a(minimum.x + width * 0.21f, minimum.y + height * 0.53f);
    const ImVec2 b(minimum.x + width * 0.43f, minimum.y + height * 0.74f);
    const ImVec2 c(minimum.x + width * 0.81f, minimum.y + height * 0.27f);
    draw_list->AddLine(a, b, color, S(3.0f));
    draw_list->AddLine(b, c, color, S(3.0f));
}

void DrawInfoGlyph(ImDrawList* draw_list, const ImVec2& center, bool hovered)
{
    draw_list->AddCircleFilled(
        center, S(18.0f), hovered ? WithAlpha(kGold, 38) : kTransparent, 32);
    draw_list->AddCircle(center, S(17.0f), kGold, 32, S(2.0f));
    ImFont* font = FontOrDefault(g_bold_font);
    const char* glyph = "!";
    const ImVec2 text_size = font->CalcTextSizeA(
        font->FontSize, FLT_MAX, 0.0f, glyph);
    draw_list->AddText(
        font,
        font->FontSize,
        ImVec2(center.x - text_size.x * 0.5f,
            center.y - text_size.y * 0.53f),
        kGold,
        glyph);
}

void DrawGearGlyph(ImDrawList* draw_list, const ImVec2& center)
{
    const float outer = S(15.0f);
    const float inner = S(7.0f);
    for (int index = 0; index < 8; ++index)
    {
        const float angle = static_cast<float>(index) * 3.14159265f / 4.0f;
        const ImVec2 direction(std::cos(angle), std::sin(angle));
        const ImVec2 tangent(-direction.y, direction.x);
        const ImVec2 tooth_center(
            center.x + direction.x * outer,
            center.y + direction.y * outer);
        const ImVec2 a(
            tooth_center.x - tangent.x * S(3.0f) - direction.x * S(3.5f),
            tooth_center.y - tangent.y * S(3.0f) - direction.y * S(3.5f));
        const ImVec2 b(
            tooth_center.x + tangent.x * S(3.0f) - direction.x * S(3.5f),
            tooth_center.y + tangent.y * S(3.0f) - direction.y * S(3.5f));
        const ImVec2 c(
            tooth_center.x + tangent.x * S(3.0f) + direction.x * S(3.5f),
            tooth_center.y + tangent.y * S(3.0f) + direction.y * S(3.5f));
        const ImVec2 d(
            tooth_center.x - tangent.x * S(3.0f) + direction.x * S(3.5f),
            tooth_center.y - tangent.y * S(3.0f) + direction.y * S(3.5f));
        const ImVec2 points[]{a, b, c, d};
        draw_list->AddConvexPolyFilled(points, 4, kGold);
    }
    draw_list->AddCircleFilled(center, S(12.0f), kGold, 32);
    draw_list->AddCircleFilled(center, inner, kPanelBlack, 32);
}

void DrawPlayGlyph(ImDrawList* draw_list, const ImVec2& center)
{
    const ImVec2 points[]{
        ImVec2(center.x - S(9.0f), center.y - S(13.0f)),
        ImVec2(center.x + S(13.0f), center.y),
        ImVec2(center.x - S(9.0f), center.y + S(13.0f))
    };
    draw_list->AddConvexPolyFilled(points, 3, IM_COL32(20, 18, 13, 255));
}

void DrawTextClipped(
    ImDrawList* draw_list,
    ImFont* font,
    const ImVec2& position,
    ImU32 color,
    const char* text,
    float maximum_x,
    float wrap_width = 0.0f)
{
    if (!text || !*text)
        return;
    font = FontOrDefault(font);
    const ImVec4 clip(
        position.x,
        position.y,
        (std::max)(position.x, maximum_x),
        position.y + S(80.0f));
    draw_list->AddText(
        font,
        font->FontSize,
        position,
        color,
        text,
        nullptr,
        wrap_width,
        &clip);
}

void RequestModal(
    const char* title,
    const char* body,
    const char* status)
{
    g_modal_title = title ? title : "Détails";
    g_modal_body = body ? body : "";
    g_modal_status = status ? status : "";
    g_modal_requested = true;
}

bool LoadTextureFromResource(
    IDirect3DDevice9* device,
    int resource_id,
    IDirect3DTexture9** output_texture)
{
    if (!device || !output_texture)
        return false;

    *output_texture = nullptr;
    const HMODULE module = GetModuleHandleW(nullptr);
    const HRSRC resource = FindResourceW(
        module, MAKEINTRESOURCEW(resource_id), RT_RCDATA);
    if (!resource)
        return false;

    const DWORD resource_size = SizeofResource(module, resource);
    const HGLOBAL loaded_resource = LoadResource(module, resource);
    const void* resource_data = loaded_resource
        ? LockResource(loaded_resource)
        : nullptr;
    if (!resource_data || resource_size == 0)
        return false;

    const HRESULT com_result = CoInitializeEx(
        nullptr, COINIT_APARTMENTTHREADED);
    const bool uninitialize_com = SUCCEEDED(com_result);

    IWICImagingFactory* factory = nullptr;
    IWICStream* stream = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    IDirect3DTexture9* texture = nullptr;
    bool succeeded = false;

    HRESULT result = CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory));
    if (SUCCEEDED(result))
        result = factory->CreateStream(&stream);
    if (SUCCEEDED(result))
    {
        result = stream->InitializeFromMemory(
            static_cast<BYTE*>(const_cast<void*>(resource_data)),
            resource_size);
    }
    if (SUCCEEDED(result))
    {
        result = factory->CreateDecoderFromStream(
            stream,
            nullptr,
            WICDecodeMetadataCacheOnLoad,
            &decoder);
    }
    if (SUCCEEDED(result))
        result = decoder->GetFrame(0, &frame);
    if (SUCCEEDED(result))
        result = factory->CreateFormatConverter(&converter);
    if (SUCCEEDED(result))
    {
        result = converter->Initialize(
            frame,
            GUID_WICPixelFormat32bppBGRA,
            WICBitmapDitherTypeNone,
            nullptr,
            0.0,
            WICBitmapPaletteTypeCustom);
    }

    UINT width = 0;
    UINT height = 0;
    if (SUCCEEDED(result))
        result = converter->GetSize(&width, &height);
    if (SUCCEEDED(result) && width > 0 && height > 0)
    {
        result = device->CreateTexture(
            width,
            height,
            1,
            D3DUSAGE_DYNAMIC,
            D3DFMT_A8R8G8B8,
            D3DPOOL_DEFAULT,
            &texture,
            nullptr);
    }

    D3DLOCKED_RECT locked{};
    if (SUCCEEDED(result))
        result = texture->LockRect(0, &locked, nullptr, D3DLOCK_DISCARD);
    if (SUCCEEDED(result))
    {
        result = converter->CopyPixels(
            nullptr,
            static_cast<UINT>(locked.Pitch),
            static_cast<UINT>(locked.Pitch) * height,
            static_cast<BYTE*>(locked.pBits));
        texture->UnlockRect(0);
    }

    if (SUCCEEDED(result))
    {
        *output_texture = texture;
        texture = nullptr;
        succeeded = true;
    }

    if (texture)
        texture->Release();
    if (converter)
        converter->Release();
    if (frame)
        frame->Release();
    if (decoder)
        decoder->Release();
    if (stream)
        stream->Release();
    if (factory)
        factory->Release();
    if (uninitialize_com)
        CoUninitialize();
    return succeeded;
}

void ReleaseTextures()
{
    if (g_logo_texture)
    {
        g_logo_texture->Release();
        g_logo_texture = nullptr;
    }
    if (g_banner_texture)
    {
        g_banner_texture->Release();
        g_banner_texture = nullptr;
    }
}

void ConfigureFonts(float dpi_scale)
{
    ImGuiIO& io = ImGui::GetIO();
    g_font_ranges.clear();
    ImFontGlyphRangesBuilder ranges_builder;
    ranges_builder.AddRanges(io.Fonts->GetGlyphRangesDefault());
    ranges_builder.AddText(
        u8"ÀÂÇÉÈÊËÎÏÔÙÛÜŸàâçéèêëîïôùûüÿœŒ—–×");
    ranges_builder.BuildRanges(&g_font_ranges);

    char windows_directory[MAX_PATH]{};
    const UINT directory_length = GetWindowsDirectoryA(
        windows_directory, MAX_PATH);
    const std::string fonts_directory = directory_length > 0
        ? std::string(windows_directory) + "\\Fonts\\"
        : std::string("C:\\Windows\\Fonts\\");

    ImFontConfig body_config{};
    body_config.OversampleH = 3;
    body_config.OversampleV = 2;
    body_config.RasterizerMultiply = 1.08f;

    g_body_font = io.Fonts->AddFontFromFileTTF(
        (fonts_directory + "segoeui.ttf").c_str(),
        12.5f * dpi_scale,
        &body_config,
        g_font_ranges.Data);

    ImFontConfig bold_config = body_config;
    bold_config.RasterizerMultiply = 1.12f;
    g_bold_font = io.Fonts->AddFontFromFileTTF(
        (fonts_directory + "segoeuib.ttf").c_str(),
        13.5f * dpi_scale,
        &bold_config,
        g_font_ranges.Data);

    ImFontConfig heading_config = body_config;
    heading_config.RasterizerMultiply = 1.14f;
    g_heading_font = io.Fonts->AddFontFromFileTTF(
        (fonts_directory + "bahnschrift.ttf").c_str(),
        17.0f * dpi_scale,
        &heading_config,
        g_font_ranges.Data);
    g_title_font = io.Fonts->AddFontFromFileTTF(
        (fonts_directory + "bahnschrift.ttf").c_str(),
        24.0f * dpi_scale,
        &heading_config,
        g_font_ranges.Data);

    if (!g_body_font)
        g_body_font = io.Fonts->AddFontDefault();
    if (!g_bold_font)
        g_bold_font = g_body_font;
    if (!g_heading_font)
        g_heading_font = g_bold_font;
    if (!g_title_font)
        g_title_font = g_heading_font;
    io.FontDefault = g_body_font;
}

void ConfigureStyle()
{
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(0.0f, 0.0f);
    style.FramePadding = ImVec2(10.0f, 7.0f);
    style.ItemSpacing = ImVec2(8.0f, 8.0f);
    style.ItemInnerSpacing = ImVec2(8.0f, 6.0f);
    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 0.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.WindowRounding = 0.0f;
    style.ChildRounding = 0.0f;
    style.PopupRounding = 10.0f;
    style.FrameRounding = 7.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding = 6.0f;
    style.ScrollbarSize = 12.0f;
    style.Colors[ImGuiCol_Text] = ImVec4(0.96f, 0.96f, 0.94f, 1.0f);
    style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.56f, 0.57f, 0.59f, 1.0f);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.04f, 0.04f, 0.045f, 1.0f);
    style.Colors[ImGuiCol_ChildBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    style.Colors[ImGuiCol_PopupBg] = ImVec4(0.055f, 0.058f, 0.062f, 0.99f);
    style.Colors[ImGuiCol_Border] = ImVec4(0.83f, 0.65f, 0.20f, 0.72f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.06f, 0.065f, 0.07f, 1.0f);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.16f, 0.06f, 1.0f);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.30f, 0.23f, 0.07f, 1.0f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.15f, 0.13f, 0.08f, 1.0f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.31f, 0.24f, 0.07f, 1.0f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.43f, 0.32f, 0.08f, 1.0f);
    style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.94f, 0.77f, 0.25f, 1.0f);
    style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(1.0f, 0.87f, 0.33f, 1.0f);
    style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.03f, 0.03f, 0.035f, 0.65f);
    style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.43f, 0.33f, 0.10f, 0.95f);
    style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.67f, 0.51f, 0.14f, 1.0f);
    style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.88f, 0.68f, 0.18f, 1.0f);
    style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.01f, 0.01f, 0.012f, 0.78f);
}

void DrawWindowControl(
    HWND window,
    const char* id,
    const ImVec2& minimum,
    int control)
{
    const float size = S(30.0f);
    ImGui::SetCursorScreenPos(minimum);
    ImGui::PushID(id);
    ImGui::InvisibleButton("##window_control", ImVec2(size, size));
    const bool hovered = ImGui::IsItemHovered();
    const bool clicked = ImGui::IsItemClicked();
    ImGui::PopID();

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    if (hovered)
    {
        draw_list->AddRectFilled(
            minimum,
            ImVec2(minimum.x + size, minimum.y + size),
            control == 2 ? WithAlpha(kRed, 64) : WithAlpha(kGold, 40),
            S(5.0f));
    }

    const ImVec2 center(minimum.x + size * 0.5f, minimum.y + size * 0.5f);
    const ImU32 color = hovered && control == 2 ? kRed : kGoldBright;
    if (control == 0)
    {
        draw_list->AddLine(
            ImVec2(center.x - S(7.0f), center.y + S(4.0f)),
            ImVec2(center.x + S(7.0f), center.y + S(4.0f)),
            color,
            S(2.0f));
    }
    else if (control == 1)
    {
        draw_list->AddRect(
            ImVec2(center.x - S(6.5f), center.y - S(6.5f)),
            ImVec2(center.x + S(6.5f), center.y + S(6.5f)),
            color,
            0.0f,
            0,
            S(2.0f));
    }
    else
    {
        draw_list->AddLine(
            ImVec2(center.x - S(7.0f), center.y - S(7.0f)),
            ImVec2(center.x + S(7.0f), center.y + S(7.0f)),
            color,
            S(2.0f));
        draw_list->AddLine(
            ImVec2(center.x + S(7.0f), center.y - S(7.0f)),
            ImVec2(center.x - S(7.0f), center.y + S(7.0f)),
            color,
            S(2.0f));
    }

    if (clicked)
    {
        if (control == 0)
            ShowWindow(window, SW_MINIMIZE);
        else if (control == 1)
            ShowWindow(window, IsZoomed(window) ? SW_RESTORE : SW_MAXIMIZE);
        else
            PostMessageW(window, WM_CLOSE, 0, 0);
    }
}

void DrawHeader(
    HWND window,
    const TrainerProcess& game_process,
    const ImVec2& minimum,
    const ImVec2& maximum)
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    DrawBeveledPanel(
        draw_list,
        minimum,
        maximum,
        S(22.0f),
        IM_COL32(15, 16, 17, 255),
        WithAlpha(kGold, 160),
        S(1.5f));

    if (g_banner_texture)
    {
        draw_list->AddImageRounded(
            TextureId(g_banner_texture),
            ImVec2(minimum.x + S(1.0f), minimum.y + S(1.0f)),
            ImVec2(maximum.x - S(1.0f), maximum.y - S(1.0f)),
            ImVec2(0.0f, 0.0f),
            ImVec2(1.0f, 1.0f),
            IM_COL32_WHITE,
            S(21.0f));
    }
    draw_list->AddRectFilledMultiColor(
        minimum,
        maximum,
        IM_COL32(7, 8, 9, 252),
        IM_COL32(7, 8, 9, 36),
        IM_COL32(7, 8, 9, 36),
        IM_COL32(7, 8, 9, 252));
    draw_list->AddRectFilledMultiColor(
        ImVec2(minimum.x, maximum.y - S(44.0f)),
        maximum,
        IM_COL32(7, 8, 9, 0),
        IM_COL32(7, 8, 9, 0),
        IM_COL32(7, 8, 9, 220),
        IM_COL32(7, 8, 9, 220));

    const float icon_size = S(98.0f);
    const ImVec2 icon_min(
        minimum.x + S(20.0f),
        minimum.y + (maximum.y - minimum.y - icon_size) * 0.5f);
    const ImVec2 icon_max(icon_min.x + icon_size, icon_min.y + icon_size);
    draw_list->AddRectFilled(
        ImVec2(icon_min.x - S(4.0f), icon_min.y - S(4.0f)),
        ImVec2(icon_max.x + S(4.0f), icon_max.y + S(4.0f)),
        WithAlpha(kGold, 32),
        S(18.0f));
    draw_list->AddRect(
        ImVec2(icon_min.x - S(2.0f), icon_min.y - S(2.0f)),
        ImVec2(icon_max.x + S(2.0f), icon_max.y + S(2.0f)),
        kGold,
        S(16.0f),
        0,
        S(2.0f));
    if (g_logo_texture)
    {
        draw_list->AddImageRounded(
            TextureId(g_logo_texture),
            icon_min,
            icon_max,
            ImVec2(0.0f, 0.0f),
            ImVec2(1.0f, 1.0f),
            IM_COL32_WHITE,
            S(14.0f));
    }

    const float controls_width = S(126.0f);
    const float text_x = icon_max.x + S(24.0f);
    const float text_max_x = maximum.x - controls_width - S(12.0f);
    DrawTextClipped(
        draw_list,
        g_title_font,
        ImVec2(text_x, minimum.y + S(31.0f)),
        IM_COL32(245, 210, 114, 255),
        "HD RADAR TRAINER",
        text_max_x);
    DrawTextClipped(
        draw_list,
        g_heading_font,
        ImVec2(text_x + S(2.0f), minimum.y + S(70.0f)),
        kText,
        "PHASE 2",
        text_max_x);

    const bool connected = game_process.IsConnected();
    const ImU32 status_color = connected ? kGreen : kOrange;
    const ImVec2 status_center(text_x + S(7.0f), maximum.y - S(27.0f));
    draw_list->AddCircleFilled(status_center, S(5.5f), status_color, 20);
    draw_list->AddCircle(
        status_center, S(9.0f), WithAlpha(status_color, 46), 20, S(4.0f));

    char status_text[192]{};
    if (connected)
    {
        std::snprintf(
            status_text,
            sizeof(status_text),
            "[OK] Connecté à hde.exe  •  PID %lu",
            static_cast<unsigned long>(game_process.ProcessId()));
    }
    else if (game_process.LastError() != ERROR_SUCCESS)
    {
        std::snprintf(
            status_text,
            sizeof(status_text),
            "En attente de hde.exe  •  erreur Windows %lu",
            static_cast<unsigned long>(game_process.LastError()));
    }
    else
    {
        std::snprintf(
            status_text,
            sizeof(status_text),
            "En attente du lancement de hde.exe...");
    }
    DrawTextClipped(
        draw_list,
        g_bold_font,
        ImVec2(status_center.x + S(15.0f), maximum.y - S(38.0f)),
        status_color,
        status_text,
        text_max_x);

    const float control_y = minimum.y + S(12.0f);
    const float control_right = maximum.x - S(14.0f);
    DrawWindowControl(
        window,
        "minimize",
        ImVec2(control_right - S(106.0f), control_y),
        0);
    DrawWindowControl(
        window,
        "maximize",
        ImVec2(control_right - S(66.0f), control_y),
        1);
    DrawWindowControl(
        window,
        "close",
        ImVec2(control_right - S(26.0f), control_y),
        2);
}

void DrawSectionHeader(const char* title, const char* trailing, bool gear)
{
    const ImVec2 minimum = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = S(34.0f);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    float text_x = minimum.x;
    if (gear)
    {
        DrawGearGlyph(
            draw_list,
            ImVec2(minimum.x + S(16.0f), minimum.y + height * 0.5f));
        text_x += S(40.0f);
    }

    ImFont* heading = FontOrDefault(g_heading_font);
    const ImVec2 title_size = heading->CalcTextSizeA(
        heading->FontSize, FLT_MAX, 0.0f, title);
    draw_list->AddText(
        heading,
        heading->FontSize,
        ImVec2(text_x, minimum.y + (height - heading->FontSize) * 0.42f),
        kGold,
        title);

    float trailing_width = 0.0f;
    if (trailing && *trailing)
    {
        ImFont* body = FontOrDefault(g_body_font);
        trailing_width = body->CalcTextSizeA(
            body->FontSize, FLT_MAX, 0.0f, trailing).x;
        const float trailing_x = minimum.x + width - trailing_width;
        draw_list->AddText(
            body,
            body->FontSize,
            ImVec2(trailing_x, minimum.y + (height - body->FontSize) * 0.45f),
            kMuted,
            trailing);
    }

    const float line_start = text_x + title_size.x + S(14.0f);
    const float line_end = minimum.x + width -
        (trailing_width > 0.0f ? trailing_width + S(16.0f) : 0.0f);
    if (line_end > line_start)
    {
        draw_list->AddRectFilledMultiColor(
            ImVec2(line_start, minimum.y + height * 0.55f),
            ImVec2(line_end, minimum.y + height * 0.55f + S(2.0f)),
            WithAlpha(kGold, 215),
            WithAlpha(kGold, 0),
            WithAlpha(kGold, 0),
            WithAlpha(kGold, 215));
    }

    ImGui::Dummy(ImVec2(width, height));
}

bool DrawCheatButton(
    const char* id,
    const char* hotkey,
    const char* label,
    const ImVec2& minimum,
    const ImVec2& size)
{
    ImGui::SetCursorScreenPos(minimum);
    ImGui::PushID(id);
    ImGui::InvisibleButton("##cheat", size);
    const bool hovered = ImGui::IsItemHovered();
    const bool clicked = ImGui::IsItemClicked();
    ImGui::PopID();

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImVec2 maximum(minimum.x + size.x, minimum.y + size.y);
    if (hovered)
    {
        DrawBeveledPanel(
            draw_list,
            ImVec2(minimum.x - S(2.0f), minimum.y - S(2.0f)),
            ImVec2(maximum.x + S(2.0f), maximum.y + S(2.0f)),
            S(13.0f),
            WithAlpha(kGold, 24),
            WithAlpha(kGoldBright, 92),
            S(2.0f));
    }
    DrawBeveledPanel(
        draw_list,
        minimum,
        maximum,
        S(12.0f),
        hovered ? kCardBlackLight : kCardBlack,
        hovered ? kGoldBright : WithAlpha(kGold, 170),
        S(1.5f));

    const ImVec2 key_min(
        minimum.x + S(16.0f),
        minimum.y + (size.y - S(34.0f)) * 0.5f);
    const ImVec2 key_max(
        key_min.x + S(57.0f),
        key_min.y + S(34.0f));
    draw_list->AddRectFilled(key_min, key_max, WithAlpha(kGold, 24), S(5.0f));
    draw_list->AddRect(key_min, key_max, kGoldDark, S(5.0f), 0, S(1.5f));
    ImFont* bold = FontOrDefault(g_bold_font);
    const ImVec2 key_size = bold->CalcTextSizeA(
        bold->FontSize, FLT_MAX, 0.0f, hotkey);
    draw_list->AddText(
        bold,
        bold->FontSize,
        ImVec2(
            key_min.x + (key_max.x - key_min.x - key_size.x) * 0.5f,
            key_min.y + (key_max.y - key_min.y - key_size.y) * 0.42f),
        kGold,
        hotkey);

    DrawTextClipped(
        draw_list,
        g_bold_font,
        ImVec2(key_max.x + S(15.0f), minimum.y + S(18.0f)),
        kText,
        label,
        maximum.x - S(10.0f));
    return clicked;
}

void DrawCheatGrid(CheatId& clicked_cheat)
{
    const ImVec2 grid_min = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float gap = S(12.0f);
    const float card_width = (width - gap) * 0.5f;
    const float card_height = S(60.0f);
    const ImVec2 size(card_width, card_height);

    // V97 : « Life Unlimited » et « Big Heads » ont ete retires. Santé Max et
    // Fullhands sont devenus natifs, et F3 sert desormais a sortir d'un
    // vehicule. Seul « Passer la mission » est encore tape au clavier, parce
    // que c'est le seul que le moteur honore vraiment en partie reseau.
    if (DrawCheatButton(
            "ironman", "F4", "SANTÉ MAX", grid_min, size))
    {
        clicked_cheat = CheatId::Ironman;
    }
    if (DrawCheatButton(
            "fullhands",
            "F5",
            "FULLHANDS",
            ImVec2(grid_min.x + card_width + gap, grid_min.y),
            size))
    {
        clicked_cheat = CheatId::Fullhands;
    }
    if (DrawCheatButton(
            "skipmission",
            "F7",
            "PASSER LA MISSION",
            ImVec2(grid_min.x, grid_min.y + card_height + gap),
            size))
    {
        clicked_cheat = CheatId::Skipmission;
    }
    if (DrawCheatButton(
            "vehicle_exit",
            "F3",
            "SORTIR DU VÉHICULE",
            ImVec2(
                grid_min.x + card_width + gap,
                grid_min.y + card_height + gap),
            size))
    {
        clicked_cheat = CheatId::VehicleExit;
    }
    if (DrawCheatButton(
            "revive_current_player",
            "F10",
            "RANIMER LE JOUEUR",
            ImVec2(grid_min.x, grid_min.y + (card_height + gap) * 2.0f),
            size))
    {
        clicked_cheat = CheatId::ReviveCurrentPlayer;
    }
    if (DrawCheatButton(
            "vehicle_menu",
            "F6",
            "VÉHICULES DE LA MISSION",
            ImVec2(grid_min.x, grid_min.y + (card_height + gap) * 3.0f),
            size))
    {
        clicked_cheat = CheatId::VehicleMenu;
    }
    if (DrawCheatButton(
            "vehicle_repair",
            "G",
            "RÉPARER LE VÉHICULE",
            ImVec2(
                grid_min.x + card_width + gap,
                grid_min.y + (card_height + gap) * 3.0f),
            size))
    {
        clicked_cheat = CheatId::VehicleRepair;
    }
    if (DrawCheatButton(
            "repair_current_player",
            "F12",
            "RESTAURER L'IMAGE",
            ImVec2(
                grid_min.x + card_width + gap,
                grid_min.y + (card_height + gap) * 2.0f),
            size))
    {
        clicked_cheat = CheatId::RepairCurrentPlayer;
    }

    ImGui::SetCursorScreenPos(
        ImVec2(grid_min.x, grid_min.y + card_height * 4.0f + gap * 3.0f));
    ImGui::Dummy(ImVec2(width, S(1.0f)));
}

struct CardInteraction
{
    bool toggled = false;
    bool info_clicked = false;
};

CardInteraction BeginCardInteraction(
    const char* id,
    const ImVec2& minimum,
    const ImVec2& size,
    float info_width,
    bool enabled)
{
    CardInteraction interaction{};
    ImGui::PushID(id);
    ImGui::SetCursorScreenPos(minimum);
    ImGui::BeginDisabled(!enabled);
    ImGui::InvisibleButton(
        "##toggle",
        ImVec2(size.x - info_width, size.y));
    interaction.toggled = enabled && ImGui::IsItemClicked();
    ImGui::EndDisabled();

    const ImVec2 info_min(
        minimum.x + size.x - info_width,
        minimum.y + (size.y - S(40.0f)) * 0.5f);
    ImGui::SetCursorScreenPos(info_min);
    ImGui::InvisibleButton("##info", ImVec2(info_width, S(40.0f)));
    interaction.info_clicked = ImGui::IsItemClicked();
    ImGui::PopID();
    return interaction;
}

void DrawCardBackground(
    const ImVec2& minimum,
    const ImVec2& maximum,
    bool enabled,
    bool hovered)
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    if (enabled)
    {
        draw_list->AddRectFilled(
            ImVec2(minimum.x - S(3.0f), minimum.y - S(3.0f)),
            ImVec2(maximum.x + S(3.0f), maximum.y + S(3.0f)),
            WithAlpha(kGold, hovered ? 38 : 24),
            S(12.0f));
    }
    draw_list->AddRectFilled(
        minimum,
        maximum,
        enabled ? IM_COL32(35, 31, 20, 255) :
            (hovered ? kCardBlackLight : kCardBlack),
        S(10.0f));
    if (enabled)
    {
        draw_list->AddRectFilledMultiColor(
            minimum,
            maximum,
            WithAlpha(kGold, 36),
            WithAlpha(kGold, 4),
            WithAlpha(kGold, 4),
            WithAlpha(kGold, 26));
    }
    draw_list->AddRect(
        minimum,
        maximum,
        enabled ? kGold : (hovered ? IM_COL32(95, 96, 98, 255) : kBorder),
        S(10.0f),
        0,
        S(enabled ? 1.8f : 1.2f));
}

void DrawToggleBox(
    ImDrawList* draw_list,
    const ImVec2& minimum,
    bool enabled)
{
    const ImVec2 maximum(
        minimum.x + S(30.0f),
        minimum.y + S(30.0f));
    if (enabled)
    {
        draw_list->AddRectFilled(
            ImVec2(minimum.x - S(5.0f), minimum.y - S(5.0f)),
            ImVec2(maximum.x + S(5.0f), maximum.y + S(5.0f)),
            WithAlpha(kGold, 24),
            S(8.0f));
    }
    draw_list->AddRectFilled(minimum, maximum, kNearBlack, S(6.0f));
    draw_list->AddRect(
        minimum,
        maximum,
        enabled ? kGoldBright : IM_COL32(109, 111, 114, 255),
        S(6.0f),
        0,
        S(2.2f));
    if (enabled)
        DrawCheckMark(draw_list, minimum, maximum, kGoldBright);
}

bool DrawToggleCard(
    const char* id,
    const char* title,
    bool& value,
    const char* detail,
    const char* status,
    const char* badge = nullptr)
{
    const ImVec2 minimum = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = S(62.0f);
    const ImVec2 size(width, height);
    const ImVec2 maximum(minimum.x + width, minimum.y + height);
    const float info_width = S(55.0f);
    const CardInteraction interaction = BeginCardInteraction(
        id, minimum, size, info_width, true);
    if (interaction.toggled)
        value = !value;

    const bool hovered = ImGui::IsMouseHoveringRect(minimum, maximum);
    DrawCardBackground(minimum, maximum, value, hovered);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    DrawToggleBox(
        draw_list,
        ImVec2(minimum.x + S(18.0f), minimum.y + S(16.0f)),
        value);

    float title_max_x = maximum.x - info_width - S(8.0f);
    if (badge && *badge)
    {
        ImFont* bold = FontOrDefault(g_bold_font);
        const ImVec2 badge_size = bold->CalcTextSizeA(
            bold->FontSize, FLT_MAX, 0.0f, badge);
        const float badge_width = badge_size.x + S(18.0f);
        const ImVec2 badge_min(
            title_max_x - badge_width,
            minimum.y + S(17.0f));
        const ImVec2 badge_max(
            title_max_x,
            minimum.y + S(45.0f));
        draw_list->AddRectFilled(
            badge_min, badge_max, WithAlpha(kGold, value ? 35 : 18), S(5.0f));
        draw_list->AddRect(
            badge_min, badge_max, value ? kGold : kGoldDark, S(5.0f));
        draw_list->AddText(
            bold,
            bold->FontSize,
            ImVec2(
                badge_min.x + (badge_width - badge_size.x) * 0.5f,
                badge_min.y + S(3.0f)),
            value ? kGoldBright : kMuted,
            badge);
        title_max_x = badge_min.x - S(12.0f);
    }

    DrawTextClipped(
        draw_list,
        g_bold_font,
        ImVec2(minimum.x + S(64.0f), minimum.y + S(20.0f)),
        value ? kText : IM_COL32(232, 232, 230, 255),
        title,
        title_max_x);

    const ImVec2 info_center(
        maximum.x - info_width * 0.5f,
        minimum.y + height * 0.5f);
    DrawInfoGlyph(
        draw_list,
        info_center,
        ImGui::IsMouseHoveringRect(
            ImVec2(maximum.x - info_width, minimum.y), maximum));

    if (interaction.info_clicked)
        RequestModal(title, detail, status);

    ImGui::SetCursorScreenPos(ImVec2(minimum.x, maximum.y + S(9.0f)));
    ImGui::Dummy(ImVec2(width, S(1.0f)));
    return interaction.toggled;
}

void DrawScopePill(
    const char* id,
    const char* label,
    bool selected,
    const ImVec2& minimum,
    const ImVec2& size,
    bool& clicked)
{
    ImGui::SetCursorScreenPos(minimum);
    ImGui::PushID(id);
    ImGui::InvisibleButton("##scope", size);
    clicked = ImGui::IsItemClicked();
    const bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImVec2 maximum(minimum.x + size.x, minimum.y + size.y);
    draw_list->AddRectFilled(
        minimum,
        maximum,
        selected ? WithAlpha(kGold, 36) : IM_COL32(12, 13, 14, 230),
        S(6.0f));
    draw_list->AddRect(
        minimum,
        maximum,
        selected ? kGold : (hovered ? IM_COL32(116, 117, 120, 255) : kBorder),
        S(6.0f));
    ImFont* body = FontOrDefault(selected ? g_bold_font : g_body_font);
    const ImVec2 text_size = body->CalcTextSizeA(
        body->FontSize, FLT_MAX, 0.0f, label);
    draw_list->AddText(
        body,
        body->FontSize,
        ImVec2(
            minimum.x + (size.x - text_size.x) * 0.5f,
            minimum.y + (size.y - text_size.y) * 0.42f),
        selected ? kGoldBright : kMuted,
        label);
}

void DrawNetworkPositionMaskCard(
    GameplaySettings& settings,
    const GameplayStatus& status)
{
    DrawToggleCard(
        "network_position_mask",
        "Masquer ma position reseau (W: basculer)",
        settings.network_position_mask_enabled,
        "La case arme seulement la fonction : votre position reste normale chez l'autre PC jusqu'au premier W. Ce W fige l'ancre, cache votre modele et vous rend invisible pour les ennemis - vous seul, et seulement tant que vous etes cache; les W suivants alternent montrer/cacher. La case Invisible pour les ennemis n'est jamais modifiee.",
        NetworkPositionMaskStatusText(status));

    const ImVec2 minimum = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float gap = S(8.0f);
    const float pill_width = (width - gap) * 0.5f;
    bool current_clicked = false;
    bool squad_clicked = false;
    DrawScopePill(
        "network_mask_current",
        "Joueur actuel (defaut)",
        settings.network_position_mask_scope ==
            NetworkPositionMaskScope::ControlledPlayer,
        minimum,
        ImVec2(pill_width, S(30.0f)),
        current_clicked);
    DrawScopePill(
        "network_mask_squad",
        "Escouade entiere",
        settings.network_position_mask_scope ==
            NetworkPositionMaskScope::WholeSquad,
        ImVec2(minimum.x + pill_width + gap, minimum.y),
        ImVec2(pill_width, S(30.0f)),
        squad_clicked);
    if (current_clicked)
        settings.network_position_mask_scope =
            NetworkPositionMaskScope::ControlledPlayer;
    if (squad_clicked)
        settings.network_position_mask_scope =
            NetworkPositionMaskScope::WholeSquad;

    ImGui::SetCursorScreenPos(ImVec2(minimum.x, minimum.y + S(39.0f)));
    ImGui::Dummy(ImVec2(width, S(1.0f)));
}

void DrawInvisibilityCard(
    GameplaySettings& settings,
    const GameplayStatus& status)
{
    const ImVec2 minimum = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = S(settings.enemy_invisibility_enabled ? 100.0f : 62.0f);
    const ImVec2 size(width, height);
    const ImVec2 maximum(minimum.x + width, minimum.y + height);
    const float info_width = S(55.0f);
    const CardInteraction interaction = BeginCardInteraction(
        "enemy_invisibility", minimum,
        ImVec2(width, S(62.0f)), info_width, true);
    if (interaction.toggled)
        settings.enemy_invisibility_enabled =
            !settings.enemy_invisibility_enabled;

    const bool hovered = ImGui::IsMouseHoveringRect(minimum, maximum);
    DrawCardBackground(
        minimum, maximum, settings.enemy_invisibility_enabled, hovered);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    DrawToggleBox(
        draw_list,
        ImVec2(minimum.x + S(18.0f), minimum.y + S(16.0f)),
        settings.enemy_invisibility_enabled);
    DrawTextClipped(
        draw_list,
        g_bold_font,
        ImVec2(minimum.x + S(64.0f), minimum.y + S(20.0f)),
        kText,
        "Invisible pour les ennemis",
        maximum.x - info_width - S(8.0f));
    DrawInfoGlyph(
        draw_list,
        ImVec2(maximum.x - info_width * 0.5f, minimum.y + S(31.0f)),
        ImGui::IsMouseHoveringRect(
            ImVec2(maximum.x - info_width, minimum.y),
            ImVec2(maximum.x, minimum.y + S(62.0f))));

    if (interaction.info_clicked)
    {
        RequestModal(
            "Invisible pour les ennemis",
            "Perception seulement : filtre la vue et l’ouïe des ennemis, sans jamais protéger des dégâts — c’est le rôle de Protection réseau totale. Portée limitée au joueur contrôlé ou étendue à toute l’escouade, joueurs connectés compris. Le changement de portée est appliqué immédiatement.",
            EnemyInvisibilityStatusText(status));
    }

    if (settings.enemy_invisibility_enabled)
    {
        const float pills_y = minimum.y + S(60.0f);
        const float pill_gap = S(8.0f);
        const float pill_width = (width - S(78.0f) - pill_gap) * 0.5f;
        bool controlled_clicked = false;
        bool squad_clicked = false;
        DrawScopePill(
            "controlled",
            "Joueur contrôlé",
            settings.enemy_invisibility_scope ==
                EnemyInvisibilityScope::ControlledPlayer,
            ImVec2(minimum.x + S(64.0f), pills_y),
            ImVec2(pill_width, S(30.0f)),
            controlled_clicked);
        DrawScopePill(
            "squad",
            "Escouade entière",
            settings.enemy_invisibility_scope ==
                EnemyInvisibilityScope::WholeSquad,
            ImVec2(minimum.x + S(64.0f) + pill_width + pill_gap, pills_y),
            ImVec2(pill_width, S(30.0f)),
            squad_clicked);
        if (controlled_clicked)
        {
            settings.enemy_invisibility_scope =
                EnemyInvisibilityScope::ControlledPlayer;
        }
        if (squad_clicked)
        {
            settings.enemy_invisibility_scope =
                EnemyInvisibilityScope::WholeSquad;
        }
    }

    ImGui::SetCursorScreenPos(ImVec2(minimum.x, maximum.y + S(9.0f)));
    ImGui::Dummy(ImVec2(width, S(1.0f)));
}

void DrawAimbotCard(
    GameplaySettings& settings,
    const GameplayStatus& status)
{
    const ImVec2 minimum = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const bool compact = width < S(700.0f);
    // V97 : une rangee de plus pour la case « Illimité ».
    const float height = S(compact ? 128.0f : 96.0f);
    const ImVec2 maximum(minimum.x + width, minimum.y + height);
    const float info_width = S(55.0f);

    const bool hovered = ImGui::IsMouseHoveringRect(minimum, maximum);
    DrawCardBackground(minimum, maximum, settings.aimbot_enabled, hovered);

    ImGui::PushID("aimbot");
    ImGui::SetCursorScreenPos(minimum);
    ImGui::InvisibleButton(
        "##toggle",
        ImVec2(compact ? width - info_width : S(320.0f), S(64.0f)));
    if (ImGui::IsItemClicked())
        settings.aimbot_enabled = !settings.aimbot_enabled;

    const ImVec2 info_min(
        maximum.x - info_width,
        minimum.y + S(11.0f));
    ImGui::SetCursorScreenPos(info_min);
    ImGui::InvisibleButton("##info", ImVec2(info_width, S(40.0f)));
    const bool info_clicked = ImGui::IsItemClicked();

    const float slider_width = compact ? width - S(96.0f) : S(255.0f);
    const ImVec2 slider_min(
        compact ? minimum.x + S(64.0f) : maximum.x - info_width - slider_width - S(16.0f),
        compact ? minimum.y + S(61.0f) : minimum.y + S(20.0f));
    ImGui::SetCursorScreenPos(slider_min);
    ImGui::SetNextItemWidth(slider_width);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.045f, 0.048f, 0.052f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.18f, 0.14f, 0.045f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.25f, 0.19f, 0.05f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.95f, 0.78f, 0.25f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(1.0f, 0.88f, 0.38f, 1.0f));
    if (settings.aimbot_unlimited_distance)
        ImGui::BeginDisabled();
    ImGui::SliderFloat(
        "##distance",
        &settings.aimbot_max_distance_m,
        40.0f,
        1000.0f,
        "%.0f m");
    if (settings.aimbot_unlimited_distance)
        ImGui::EndDisabled();
    ImGui::PopStyleColor(5);
    // Cochee, la case rend le curseur sans effet : le selecteur de cible
    // recoit alors 0, qui signifie « aucune limite de distance ».
    ImGui::SetCursorScreenPos(
        ImVec2(slider_min.x, slider_min.y + S(26.0f)));
    ImGui::Checkbox("Illimité", &settings.aimbot_unlimited_distance);
    ImGui::PopID();

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    DrawToggleBox(
        draw_list,
        ImVec2(minimum.x + S(18.0f), minimum.y + S(18.0f)),
        settings.aimbot_enabled);
    const float title_max = compact
        ? maximum.x - info_width - S(8.0f)
        : slider_min.x - S(18.0f);
    DrawTextClipped(
        draw_list,
        g_bold_font,
        ImVec2(minimum.x + S(64.0f), minimum.y + S(22.0f)),
        kText,
        "Aimbot tête (sans tir automatique)",
        title_max);
    DrawInfoGlyph(
        draw_list,
        ImVec2(maximum.x - info_width * 0.5f, minimum.y + S(35.0f)),
        ImGui::IsMouseHoveringRect(
            ImVec2(maximum.x - info_width, minimum.y),
            ImVec2(maximum.x, minimum.y + S(62.0f))));

    if (info_clicked)
    {
        RequestModal(
            "Aimbot tête",
            "Verrouille la visée sur la tête sans déclencher le tir. Le curseur de distance reste réglable de 40 à 220 mètres.",
            AimbotStatusText(status));
    }

    ImGui::SetCursorScreenPos(ImVec2(minimum.x, maximum.y + S(9.0f)));
    ImGui::Dummy(ImVec2(width, S(1.0f)));
}

void DrawSeriesButton(
    GameplaySettings& settings,
    const GameplayStatus& status)
{
    const ImVec2 minimum = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float info_width = S(58.0f);
    const float height = S(62.0f);
    const ImVec2 button_size(width - info_width - S(8.0f), height);

    ImGui::PushID("inventory_series");
    ImGui::SetCursorScreenPos(minimum);
    ImGui::BeginDisabled(settings.grant_all_items_consumed);
    ImGui::InvisibleButton("##series", button_size);
    const bool clicked = ImGui::IsItemClicked();
    const bool hovered = ImGui::IsItemHovered();
    ImGui::EndDisabled();
    ImGui::SetCursorScreenPos(
        ImVec2(minimum.x + width - info_width, minimum.y + S(11.0f)));
    ImGui::InvisibleButton("##info", ImVec2(info_width, S(40.0f)));
    const bool info_clicked = ImGui::IsItemClicked();
    const bool info_hovered = ImGui::IsItemHovered();
    ImGui::PopID();

    if (clicked && !settings.grant_all_items_consumed)
        settings.grant_all_items_requested = true;

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImVec2 button_max(
        minimum.x + button_size.x,
        minimum.y + button_size.y);
    DrawBeveledPanel(
        draw_list,
        minimum,
        button_max,
        S(13.0f),
        settings.grant_all_items_consumed
            ? IM_COL32(85, 72, 34, 170)
            : (hovered ? IM_COL32(255, 211, 64, 255) : IM_COL32(232, 177, 22, 255)),
        settings.grant_all_items_consumed ? kGoldDark : kGoldBright,
        S(2.0f));
    draw_list->AddRectFilledMultiColor(
        ImVec2(minimum.x + S(12.0f), minimum.y + S(3.0f)),
        ImVec2(button_max.x - S(12.0f), minimum.y + height * 0.5f),
        WithAlpha(IM_COL32_WHITE, 35),
        WithAlpha(IM_COL32_WHITE, 3),
        WithAlpha(IM_COL32_WHITE, 0),
        WithAlpha(IM_COL32_WHITE, 20));

    const char* label = "ARMES ET ÉQUIPEMENT — SÉRIE SUIVANTE  [M]";
    ImFont* heading = FontOrDefault(g_heading_font);
    const ImVec2 label_size = heading->CalcTextSizeA(
        heading->FontSize, FLT_MAX, 0.0f, label);
    draw_list->AddText(
        heading,
        heading->FontSize,
        ImVec2(
            minimum.x + (button_size.x - label_size.x) * 0.5f,
            minimum.y + (height - label_size.y) * 0.43f),
        settings.grant_all_items_consumed
            ? IM_COL32(151, 132, 83, 255)
            : IM_COL32(20, 18, 13, 255),
        label);
    DrawInfoGlyph(
        draw_list,
        ImVec2(
            minimum.x + width - info_width * 0.5f,
            minimum.y + height * 0.5f),
        info_hovered);

    if (info_clicked)
    {
        RequestModal(
            "Armes et équipement — série suivante",
            "Quatorze séries fixes. Chaque appui sur M remplace complètement la série précédente sans ouvrir l’inventaire, puis avance de 1/14 à 14/14.",
            GrantAllItemsStatusText(status));
    }

    ImGui::SetCursorScreenPos(ImVec2(minimum.x, minimum.y + height + S(2.0f)));
    ImGui::Dummy(ImVec2(width, S(1.0f)));
}

void DrawFooterButton(
    HWND window,
    const ImVec2& minimum,
    const ImVec2& maximum)
{
    const ImVec2 size(maximum.x - minimum.x, maximum.y - minimum.y);
    ImGui::SetCursorScreenPos(minimum);
    ImGui::PushID("launch");
    ImGui::InvisibleButton("##launch", size);
    const bool hovered = ImGui::IsItemHovered();
    const bool clicked = ImGui::IsItemClicked();
    ImGui::PopID();

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    if (hovered)
    {
        DrawBeveledPanel(
            draw_list,
            ImVec2(minimum.x - S(3.0f), minimum.y - S(3.0f)),
            ImVec2(maximum.x + S(3.0f), maximum.y + S(3.0f)),
            S(18.0f),
            WithAlpha(kGold, 32),
            WithAlpha(kGoldBright, 100),
            S(2.0f));
    }
    DrawBeveledPanel(
        draw_list,
        minimum,
        maximum,
        S(17.0f),
        hovered ? IM_COL32(255, 211, 61, 255) : IM_COL32(236, 181, 26, 255),
        kGoldBright,
        S(2.0f));
    draw_list->AddRectFilledMultiColor(
        ImVec2(minimum.x + S(15.0f), minimum.y + S(3.0f)),
        ImVec2(maximum.x - S(15.0f), minimum.y + size.y * 0.50f),
        WithAlpha(IM_COL32_WHITE, 45),
        WithAlpha(IM_COL32_WHITE, 9),
        WithAlpha(IM_COL32_WHITE, 0),
        WithAlpha(IM_COL32_WHITE, 24));

    ImFont* title = FontOrDefault(g_title_font);
    const char* label = "LANCER";
    const ImVec2 label_size = title->CalcTextSizeA(
        title->FontSize, FLT_MAX, 0.0f, label);
    const float total_width = S(38.0f) + label_size.x;
    const ImVec2 center(
        minimum.x + size.x * 0.5f,
        minimum.y + size.y * 0.5f);
    DrawPlayGlyph(
        draw_list,
        ImVec2(center.x - total_width * 0.5f + S(10.0f), center.y));
    draw_list->AddText(
        title,
        title->FontSize,
        ImVec2(
            center.x - total_width * 0.5f + S(38.0f),
            center.y - label_size.y * 0.52f),
        IM_COL32(20, 18, 13, 255),
        label);

    // The trainer is already running. This CTA simply clears the panel from
    // the way so the player can return to H&D without the trainer stealing
    // foreground focus from the game.
    if (clicked)
        ShowWindow(window, SW_MINIMIZE);
}

ImU32 SequenceColor(CheatSequenceResult result)
{
    switch (result)
    {
    case CheatSequenceResult::Success:
        return kGreen;
    case CheatSequenceResult::NeverRun:
    case CheatSequenceResult::GameNotActive:
        return kMuted;
    default:
        return kRed;
    }
}

const char* SequenceSummary(CheatSequenceResult result)
{
    switch (result)
    {
    case CheatSequenceResult::NeverRun:
        return "Prêt à appliquer la séquence.";
    case CheatSequenceResult::Success:
        return "Séquence appliquée.";
    case CheatSequenceResult::GameNotActive:
        return "Demande en attente du jeu.";
    default:
        return "Échec de la séquence.";
    }
}

void DrawFeatureModal()
{
    if (g_modal_requested)
    {
        ImGui::OpenPopup("##feature_details");
        g_modal_requested = false;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(S(420.0f), S(250.0f)),
        ImVec2(S(720.0f), S(520.0f)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(28.0f), S(24.0f)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, S(12.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, S(2.0f));
    if (ImGui::BeginPopupModal(
            "##feature_details",
            nullptr,
            ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoSavedSettings))
    {
        ImGui::PushFont(FontOrDefault(g_heading_font));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.94f, 0.77f, 0.25f, 1.0f));
        ImGui::TextWrapped("%s", g_modal_title.c_str());
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + S(620.0f));
        ImGui::TextWrapped("%s", g_modal_body.c_str());
        if (!g_modal_status.empty())
        {
            ImGui::Spacing();
            ImGui::PushStyleColor(
                ImGuiCol_Text,
                ImVec4(0.72f, 0.73f, 0.75f, 1.0f));
            ImGui::TextWrapped("État : %s", g_modal_status.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        const float button_width = S(150.0f);
        ImGui::SetCursorPosX(
            ImGui::GetWindowContentRegionMax().x - button_width);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.72f, 0.53f, 0.08f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.92f, 0.71f, 0.14f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.62f, 0.44f, 0.05f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.07f, 0.065f, 0.05f, 1.0f));
        if (ImGui::Button("FERMER", ImVec2(button_width, S(42.0f))))
            ImGui::CloseCurrentPopup();
        ImGui::PopStyleColor(4);
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(3);
}
}

void Initialize(IDirect3DDevice9* device, float dpi_scale)
{
    g_device = device;
    g_dpi_scale = (std::clamp)(dpi_scale, 0.75f, 3.0f);
    ConfigureFonts(g_dpi_scale);
    ConfigureStyle();
    CreateDeviceObjects(device);
}

void InvalidateDeviceObjects()
{
    ReleaseTextures();
}

void CreateDeviceObjects(IDirect3DDevice9* device)
{
    g_device = device;
    ReleaseTextures();
    LoadTextureFromResource(device, IDR_HD_LOGO, &g_logo_texture);
    LoadTextureFromResource(device, IDR_HD_BANNER, &g_banner_texture);
}

void Shutdown()
{
    ReleaseTextures();
    g_device = nullptr;
    g_body_font = nullptr;
    g_bold_font = nullptr;
    g_heading_font = nullptr;
    g_title_font = nullptr;
    g_modal_title.clear();
    g_modal_body.clear();
    g_modal_status.clear();
    g_modal_requested = false;
}

CheatId DrawTrainerWindow(
    HWND window,
    const TrainerProcess& game_process,
    CheatSequenceResult sequence_result,
    RadarRenderSettings& radar_render_settings,
    WeaponSettings& weapon_settings,
    GameplaySettings& gameplay_settings,
    GameplayStatus& gameplay_status)
{
    const ImGuiIO& io = ImGui::GetIO();
    const float logical_width = io.DisplaySize.x /
        (std::max)(g_dpi_scale, 0.75f);
    g_scale = (std::clamp)(logical_width / 1500.0f, 0.50f, 0.82f) *
        g_dpi_scale;

    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
    constexpr ImGuiWindowFlags window_flags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.015f, 0.016f, 0.018f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("##HDTrainerMainWindow", nullptr, window_flags);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImVec2 viewport_min = ImGui::GetWindowPos();
    const ImVec2 viewport_max(
        viewport_min.x + io.DisplaySize.x,
        viewport_min.y + io.DisplaySize.y);
    draw_list->AddRectFilled(viewport_min, viewport_max, kNearBlack);

    const ImVec2 outer_min(
        viewport_min.x + S(9.0f), viewport_min.y + S(9.0f));
    const ImVec2 outer_max(
        viewport_max.x - S(9.0f), viewport_max.y - S(9.0f));
    DrawBeveledPanel(
        draw_list,
        ImVec2(outer_min.x - S(3.0f), outer_min.y - S(3.0f)),
        ImVec2(outer_max.x + S(3.0f), outer_max.y + S(3.0f)),
        S(27.0f),
        WithAlpha(kGold, 18),
        WithAlpha(kGold, 42),
        S(3.0f));
    DrawBeveledPanel(
        draw_list,
        outer_min,
        outer_max,
        S(25.0f),
        IM_COL32(11, 12, 13, 255),
        kGold,
        S(2.0f));
    const ImVec2 inner_min(
        outer_min.x + S(9.0f), outer_min.y + S(9.0f));
    const ImVec2 inner_max(
        outer_max.x - S(9.0f), outer_max.y - S(9.0f));
    DrawBeveledPanel(
        draw_list,
        inner_min,
        inner_max,
        S(19.0f),
        kPanelBlack,
        WithAlpha(kGold, 108),
        S(1.0f));

    const float padding = S(12.0f);
    const float header_height =
        (std::clamp)(S(142.0f), S(112.0f), S(172.0f));
    const float footer_height = S(68.0f);
    const float gap = S(11.0f);
    const ImVec2 header_min(
        inner_min.x + padding,
        inner_min.y + padding);
    const ImVec2 header_max(
        inner_max.x - padding,
        header_min.y + header_height);
    DrawHeader(window, game_process, header_min, header_max);

    const ImVec2 footer_min(
        inner_min.x + padding + S(4.0f),
        inner_max.y - padding - footer_height);
    const ImVec2 footer_max(
        inner_max.x - padding - S(4.0f),
        inner_max.y - padding);

    const ImVec2 panel_min(
        inner_min.x + padding,
        header_max.y + gap);
    const ImVec2 panel_max(
        inner_max.x - padding,
        footer_min.y - gap);
    DrawBeveledPanel(
        draw_list,
        panel_min,
        panel_max,
        S(21.0f),
        IM_COL32(16, 17, 18, 255),
        WithAlpha(kGold, 125),
        S(1.2f));
    draw_list->AddRectFilledMultiColor(
        ImVec2(panel_min.x + S(2.0f), panel_min.y + S(2.0f)),
        ImVec2(panel_max.x - S(2.0f), panel_min.y + S(95.0f)),
        IM_COL32(35, 33, 25, 115),
        IM_COL32(16, 17, 18, 0),
        IM_COL32(16, 17, 18, 0),
        IM_COL32(35, 33, 25, 115));

    const ImVec2 content_min(
        panel_min.x + S(22.0f), panel_min.y + S(19.0f));
    const ImVec2 content_size(
        panel_max.x - panel_min.x - S(38.0f),
        panel_max.y - panel_min.y - S(36.0f));
    ImGui::SetCursorScreenPos(content_min);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::BeginChild(
        "ControlsScroll",
        content_size,
        false,
        ImGuiWindowFlags_AlwaysVerticalScrollbar |
        ImGuiWindowFlags_NoBackground);

    CheatId clicked_cheat = CheatId::Count;
    const char* sequence_text = SequenceSummary(sequence_result);
    DrawSectionHeader("CHEATS", sequence_text, true);
    DrawCheatGrid(clicked_cheat);
    ImGui::TextDisabled("F4 : %s", ExtendedHealthStatusText(gameplay_status));
    ImGui::TextDisabled("F3 : %s", VehicleExitStatusText(gameplay_status));
    ImGui::TextDisabled("G / F6 : %s", VehicleRepairStatusText(gameplay_status));
    ImGui::Dummy(ImVec2(1.0f, S(8.0f)));

    // V106 : la liste complete des touches, dans le panneau. Elles etaient
    // jusqu'ici dispersees entre les libelles des cartes et les LISEZ_MOI.
    DrawSectionHeader("TOUCHES", "toutes les commandes", false);
    const auto key_line = [](const char* key, const char* what)
    {
        ImGui::TextDisabled("%-12s %s", key, what);
    };
    key_line("F3", "sortir du vehicule, meme en vol ou coince");
    key_line("F4", "Sante Max : un appui etend et remplit, un autre annule");
    key_line("F5", "Fullhands : serie d'armes suivante");
    key_line("F6", "liste des vehicules de la mission (dans le jeu)");
    key_line("F7", "passer la mission (synchronise sur les deux PC)");
    key_line("F10", "ranimer le soldat controle");
    key_line("F12", "restaurer l'image du soldat (squelette apres explosion)");
    key_line("G", "au volant : reparer le vehicule");
    key_line("", "a pied  : ouvrir la fenetre des soldats");
    key_line("J", "soldats crees : figer ou resuivre votre arme");
    key_line("K", "carte native : destination d'un ordre, sinon teleportation");
    key_line("W", "masquer / montrer ma position reseau");
    key_line("M", "Fullhands : serie suivante, sans ouvrir l'inventaire");
    key_line("V", "noclip : activer ou couper le vol (si la case est armee)");
    key_line("Z S A E", "noclip : avancer, reculer, gauche, droite");
    key_line("H B", "noclip : monter, descendre");
    key_line("F8 F9", "vitesse du joueur : augmenter, reduire");
    key_line("N B", "vitesse du vehicule : augmenter, reduire");
    key_line("I U", "sensibilite de direction : augmenter, reduire");
    key_line("9 8", "vitesse du jeu : accelerer, ralentir (1.0x a 100x)");
    ImGui::Dummy(ImVec2(1.0f, S(4.0f)));
    ImGui::TextDisabled("Dans les fenetres affichees dans le jeu :");
    key_line("Haut Bas", "changer de ligne");
    key_line("0 a 9", "fenetre des soldats : taper le nombre de la ligne choisie");
    key_line("Retour arr.", "fenetre des soldats : effacer un chiffre");
    key_line("Gauche Droite", "meme nombre, un cran a la fois");
    key_line("Entree", "valider : creer, armer un ordre, ou prendre le controle");
    key_line("C", "liste des vehicules : recreer une copie conduisible");
    key_line("Echap", "fermer la fenetre");
    ImGui::Dummy(ImVec2(1.0f, S(8.0f)));

    DrawSectionHeader("VISUALS / ESP", nullptr, false);
    DrawToggleCard(
        "enemy_esp",
        "Afficher ESP Ennemis (Rouge / Vert)",
        radar_render_settings.show_enemy_esp,
        "Affiche les ennemis à travers le décor. Le rouge indique une cible masquée, le vert une zone atteignable.",
        radar_render_settings.show_enemy_esp ? "Activé" : "Désactivé");
    DrawToggleCard(
        "ally_esp",
        "Afficher ESP Alliés (Bleu)",
        radar_render_settings.show_ally_esp,
        "Affiche les alliés à travers le décor en bleu.",
        radar_render_settings.show_ally_esp ? "Activé" : "Désactivé");
    ImGui::Dummy(ImVec2(1.0f, S(6.0f)));

    DrawSectionHeader("WEAPONS / ARMES", nullptr, false);
    DrawToggleCard(
        "stable_precision",
        "Stabilité et précision 100 % (sans recul ni dispersion)",
        weapon_settings.stable_precision,
        "Supprime le recul et la dispersion uniquement pour l’arme du joueur local. Les valeurs originales sont restaurées à la désactivation.",
        weapon_settings.stable_precision ? "Activé" : "Désactivé");
    DrawToggleCard(
        "rapid_fire",
        "Tir ultra-rapide et munitions illimitées (sans recharge)",
        weapon_settings.rapid_fire_unlimited,
        "Accélère le tir et maintient les munitions uniquement pour l’arme du joueur local. Les valeurs originales restent restaurables.",
        weapon_settings.rapid_fire_unlimited ? "Activé" : "Désactivé");
    DrawToggleCard(
        "through_walls",
        "Permettre aux balles de traverser les murs",
        gameplay_settings.bullets_through_walls,
        "Nécessite Bullet Track. Une cible rouge derrière un mur ou une maison peut recevoir les dégâts forcés.",
        gameplay_settings.bullets_through_walls ? "Activé" : "Désactivé");
    ImGui::Dummy(ImVec2(1.0f, S(6.0f)));

    DrawSectionHeader("GAMEPLAY / FONCTIONS AVANCÉES", nullptr, false);
    char badge[64]{};
    std::snprintf(
        badge, sizeof(badge), "%.1f m/s", gameplay_settings.noclip_speed_mps);
    if (DrawToggleCard(
            "noclip",
            "Noclip spatial (touche V)",
            gameplay_settings.noclip_enabled,
            "V active ou coupe le vol sans désarmer la fonction. Z/S avancent et reculent, A/E déplacent latéralement, H/B montent et descendent, F8/F9 règlent la vitesse.",
            NoclipStatusText(gameplay_status),
            badge))
    {
        gameplay_settings.noclip_hotkey_enabled =
            gameplay_settings.noclip_enabled;
    }

    DrawInvisibilityCard(gameplay_settings, gameplay_status);
    if (DrawToggleCard(
        "absolute_player_protection",
        "Protection reseau totale",
        gameplay_settings.absolute_player_protection_enabled,
        "Joueur actuel : uniquement le soldat piloté. Escouade entière : tous vos soldats, et aussi les joueurs connectés pour les tirs et les explosions calculés par ce PC. Morts réseau, chutes et immortalité native restent limitées à vos soldats locaux.",
        AbsolutePlayerProtectionStatusText(gameplay_status)))
    {
        gameplay_settings.absolute_player_protection_toggle_requested = true;
        if (gameplay_settings.absolute_player_protection_enabled)
            gameplay_settings.remote_life_mirror_enabled = false;
    }
    {
        const ImVec2 minimum = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x;
        const float scope_gap = S(8.0f);
        const float pill_width = (width - scope_gap) * 0.5f;
        bool current_clicked = false;
        bool squad_clicked = false;
        DrawScopePill(
            "absolute_protection_current", "Joueur actuel (defaut)",
            gameplay_settings.absolute_player_protection_scope ==
                AbsolutePlayerProtectionScope::ControlledPlayer,
            minimum, ImVec2(pill_width, S(30.0f)), current_clicked);
        DrawScopePill(
            "absolute_protection_squad", "Escouade entiere",
            gameplay_settings.absolute_player_protection_scope ==
                AbsolutePlayerProtectionScope::WholeSquad,
            ImVec2(minimum.x + pill_width + scope_gap, minimum.y),
            ImVec2(pill_width, S(30.0f)), squad_clicked);
        if (current_clicked)
            gameplay_settings.absolute_player_protection_scope =
                AbsolutePlayerProtectionScope::ControlledPlayer;
        if (squad_clicked)
            gameplay_settings.absolute_player_protection_scope =
                AbsolutePlayerProtectionScope::WholeSquad;
        ImGui::SetCursorScreenPos(ImVec2(minimum.x, minimum.y + S(39.0f)));
        ImGui::Dummy(ImVec2(width, S(1.0f)));
    }
    if (DrawToggleCard(
        "remote_life_mirror",
        "Retour a la vie local (F12)",
        gameplay_settings.remote_life_mirror_enabled,
        "Mode distinct : les degats et la mort restent visibles normalement sur les deux PC. F12 relance ensuite la reanimation native du meme soldat, avec son modele et sa vie.",
        gameplay_settings.remote_life_mirror_enabled
            ? "[OK] Mort visible normale; F12 restaure le soldat sur les deux PC."
            : "Desactive : aucune reanimation F12 reseau demandee."))
    {
        if (gameplay_settings.remote_life_mirror_enabled)
        {
            gameplay_settings.absolute_player_protection_enabled = false;
            gameplay_settings.absolute_player_protection_toggle_requested = true;
        }
    }
    ImGui::TextDisabled("F10 : %s", ReviveCurrentPlayerStatusText(gameplay_status));
    DrawNetworkPositionMaskCard(gameplay_settings, gameplay_status);

    std::snprintf(
        badge,
        sizeof(badge),
        "%.1fx",
        gameplay_settings.player_speed_multiplier);
    DrawToggleCard(
        "super_run",
        "Super Run joueur (F8 augmente / F9 réduit)",
        gameplay_settings.player_speed_enabled,
        "Augmente ou réduit le multiplicateur de déplacement du joueur avec F8 et F9.",
        PlayerSpeedStatusText(gameplay_status),
        badge);
    DrawToggleCard(
        "teleport",
        "Téléportation via la carte native (K)",
        gameplay_settings.teleport_map_enabled,
        "K ouvre et arme la carte native. Cliquez ensuite la destination pour téléporter le joueur ou le véhicule conduit.",
        TeleportStatusText(gameplay_status));
    DrawAimbotCard(gameplay_settings, gameplay_status);
    DrawToggleCard(
        "bullet_track",
        "Bullet Track tête (sans tir automatique)",
        gameplay_settings.bullet_track_enabled,
        "Acquisition globale : le curseur et le cercle ne limitent plus la cible. La tête valide la plus proche du viseur est suivie.",
        BulletTrackStatusText(gameplay_status));
    DrawToggleCard(
        "bullet_track_instant_wall_impact_v2",
        "Bullet Track V2 : impact instantané derrière les murs",
        gameplay_settings.bullet_track_instant_wall_impact_v2,
        "Test séparé : agit seulement sur une cible rouge, avec traversée des murs activée. L'option Bullet Track originale reste inchangée.",
        gameplay_settings.bullet_track_instant_wall_impact_v2
            ? "V2 active pour les cibles rouges"
            : "V2 désactivée : comportement Bullet Track normal");

    std::snprintf(
        badge,
        sizeof(badge),
        "%.1fx",
        gameplay_settings.vehicle_speed_multiplier);
    DrawToggleCard(
        "vehicle_speed",
        "Super vitesse véhicule ([N] accélère, [B] réduit)",
        gameplay_settings.vehicle_speed_enabled,
        "N augmente le multiplicateur, B le réduit. I rend la direction plus vive et U la ramène vers le braquage d’origine.",
        VehicleSpeedStatusText(gameplay_status),
        badge);
    std::snprintf(
        badge,
        sizeof(badge),
        "%.1fx",
        gameplay_settings.game_speed_multiplier);
    DrawToggleCard(
        "game_speed",
        "Vitesse du jeu ([9] accélère, [8] réduit)",
        gameplay_settings.game_speed_enabled,
        "Accélère toute la simulation du jeu, pour ne plus attendre pendant les phases scriptées d’une mission. [9] monte jusqu’à 100.0x, [8] redescend jusqu’à 1.0x ; le pas s’élargit avec la vitesse. Au-delà d’une dizaine de fois, la physique devient franchement instable — [8] rattrape instantanément. À éviter en partie réseau : les deux machines ne suivraient plus le même temps.",
        GameSpeedStatusText(gameplay_status),
        badge);
    DrawToggleCard(
        "vehicle_invulnerability",
        "Véhicule indestructible (jamais d’explosion)",
        gameplay_settings.vehicle_invulnerable_enabled,
        "Les tirs ennemis, collisions et chutes de grande hauteur n’endommagent plus le véhicule conduit.",
        VehicleInvulnerabilityStatusText(gameplay_status));
    DrawSeriesButton(gameplay_settings, gameplay_status);
    ImGui::Dummy(ImVec2(1.0f, S(12.0f)));

    ImGui::EndChild();
    ImGui::PopStyleVar();

    DrawFooterButton(window, footer_min, footer_max);
    DrawFeatureModal();

    // Preserve the exact outcome color in a discreet top-edge indicator.
    draw_list->AddRectFilled(
        ImVec2(panel_min.x + S(22.0f), panel_min.y),
        ImVec2(panel_min.x + S(105.0f), panel_min.y + S(2.0f)),
        SequenceColor(sequence_result));

    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    return clicked_cheat;
}
}
