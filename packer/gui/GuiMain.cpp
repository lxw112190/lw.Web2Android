#include "gui/GuiProjectModel.h"

#include "core/BuildPipeline.h"
#include "core/Logging.h"
#include "core/ProcessRunner.h"
#include "core/Toolchain.h"

#include <Windows.h>
#include <CommCtrl.h>
#include <Dwmapi.h>
#include <ShObjIdl.h>
#include <Shellapi.h>
#include <Uxtheme.h>

#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace lw::web2android::gui {
namespace {

constexpr wchar_t kClassName[] = L"lw.Web2Android.Packer";
constexpr COLORREF kBackground = RGB(244, 247, 252);
constexpr COLORREF kCard = RGB(255, 255, 255);
constexpr COLORREF kPrimary = RGB(39, 94, 246);
constexpr COLORREF kPrimaryPressed = RGB(30, 78, 220);
constexpr COLORREF kText = RGB(28, 39, 60);
constexpr COLORREF kMuted = RGB(101, 116, 139);
constexpr COLORREF kBorder = RGB(218, 226, 238);
constexpr COLORREF kHeader = RGB(234, 241, 255);
constexpr COLORREF kSuccess = RGB(17, 142, 86);
constexpr COLORREF kFailure = RGB(194, 48, 48);

enum ControlId {
    kModeLocal = 100,
    kModeRemote,
    kSource,
    kBrowseSource,
    kName,
    kPackage,
    kVersionName,
    kVersionCode,
    kOrientation,
    kFullscreen,
    kOutput,
    kBrowseOutput,
    kBuild,
    kToolchainStatus,
    kInstallToolchain,
    kSourceLabel,
    kModeHint,
};

enum class FontRole { Title, Section, Body, Small };

struct ControlLayout {
    HWND control = nullptr;
    RECT logical{};
    FontRole font = FontRole::Body;
};

struct State {
    HWND window = nullptr;
    UINT dpi = 96;
    HFONT titleFont = nullptr;
    HFONT sectionFont = nullptr;
    HFONT bodyFont = nullptr;
    HFONT smallFont = nullptr;
    HBRUSH backgroundBrush = nullptr;
    HBRUSH cardBrush = nullptr;
    std::vector<ControlLayout> controls;
    GuiEnvironment environment;
    std::wstring status = L"准备就绪 · 请选择网页来源和 APK 输出目录";
    COLORREF statusColor = kSuccess;
    bool busy = false;
};

struct BuildCompletion {
    bool success = false;
    BuildResult result;
    std::wstring error;
};

constexpr RECT kStatusRect{48, 828, 712, 856};
constexpr UINT kBuildProgressMessage = WM_APP + 1;
constexpr UINT kBuildFinishedMessage = WM_APP + 2;
constexpr UINT kToolchainFinishedMessage = WM_APP + 3;

int Scale(int value, UINT dpi) { return MulDiv(value, static_cast<int>(dpi), 96); }

RECT ScaleRect(RECT value, UINT dpi) {
    return {Scale(value.left, dpi), Scale(value.top, dpi), Scale(value.right, dpi), Scale(value.bottom, dpi)};
}

HFONT MakeFont(int height, int weight, UINT dpi) {
    return CreateFontW(-Scale(height, dpi), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
}

HFONT FontForRole(const State& state, FontRole role) {
    switch (role) {
        case FontRole::Title: return state.titleFont;
        case FontRole::Section: return state.sectionFont;
        case FontRole::Small: return state.smallFont;
        default: return state.bodyFont;
    }
}

void RecreateFonts(State& state) {
    const auto oldTitle = state.titleFont;
    const auto oldSection = state.sectionFont;
    const auto oldBody = state.bodyFont;
    const auto oldSmall = state.smallFont;
    state.titleFont = MakeFont(30, FW_SEMIBOLD, state.dpi);
    state.sectionFont = MakeFont(18, FW_SEMIBOLD, state.dpi);
    state.bodyFont = MakeFont(16, FW_NORMAL, state.dpi);
    state.smallFont = MakeFont(14, FW_NORMAL, state.dpi);
    for (const auto& item : state.controls) {
        SendMessageW(item.control, WM_SETFONT, reinterpret_cast<WPARAM>(FontForRole(state, item.font)), TRUE);
    }
    if (oldTitle) DeleteObject(oldTitle);
    if (oldSection) DeleteObject(oldSection);
    if (oldBody) DeleteObject(oldBody);
    if (oldSmall) DeleteObject(oldSmall);
}

void LayoutControls(const State& state) {
    for (const auto& item : state.controls) {
        const auto rect = ScaleRect(item.logical, state.dpi);
        MoveWindow(item.control, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, TRUE);
    }
}

HWND AddControl(State& state, const wchar_t* kind, const wchar_t* text, DWORD style,
                int x, int y, int width, int height, int id = 0,
                FontRole font = FontRole::Body, DWORD extendedStyle = 0) {
    const auto control = CreateWindowExW(
        extendedStyle, kind, text, WS_CHILD | WS_VISIBLE | style,
        Scale(x, state.dpi), Scale(y, state.dpi), Scale(width, state.dpi), Scale(height, state.dpi),
        state.window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    if (!control) throw std::runtime_error("Unable to create a GUI control");
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(FontForRole(state, font)), TRUE);
    SetWindowTheme(control, L"Explorer", nullptr);
    state.controls.push_back({control, RECT{x, y, x + width, y + height}, font});
    return control;
}

HWND AddLabel(State& state, const wchar_t* text, int x, int y, int width, int height,
              int id = 0, FontRole font = FontRole::Body) {
    return AddControl(state, L"STATIC", text, SS_LEFT, x, y, width, height, id, font);
}

HWND AddEdit(State& state, const wchar_t* text, int x, int y, int width, int id,
             const wchar_t* cue = nullptr, DWORD extraStyle = 0) {
    const auto edit = AddControl(state, L"EDIT", text, WS_TABSTOP | ES_AUTOHSCROLL | extraStyle,
                                 x, y, width, 34, id, FontRole::Body, WS_EX_CLIENTEDGE);
    if (cue) SendMessageW(edit, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(cue));
    return edit;
}

HWND AddButton(State& state, const wchar_t* text, int x, int y, int width, int height, int id) {
    return AddControl(state, L"BUTTON", text, WS_TABSTOP | BS_OWNERDRAW, x, y, width, height, id);
}

HWND AddCombo(State& state, int x, int y, int width, int id) {
    return AddControl(state, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_VSCROLL,
                      x, y, width, 180, id, FontRole::Body, WS_EX_CLIENTEDGE);
}

std::wstring Text(HWND window, int id) {
    const auto control = GetDlgItem(window, id);
    const auto length = GetWindowTextLengthW(control);
    std::wstring result(static_cast<std::size_t>(length) + 1U, L'\0');
    GetWindowTextW(control, result.data(), length + 1);
    result.resize(static_cast<std::size_t>(length));
    return result;
}

std::filesystem::path PickFolder(HWND owner, const wchar_t* title) {
    IFileDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        return {};
    }
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    dialog->SetTitle(title);
    std::filesystem::path result;
    if (SUCCEEDED(dialog->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                result = path;
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dialog->Release();
    return result;
}

void DrawRoundedPanel(HDC dc, RECT rect, COLORREF fill, COLORREF border, int radius) {
    const auto fillBrush = CreateSolidBrush(fill);
    const auto borderPen = CreatePen(PS_SOLID, 1, border);
    const auto oldBrush = SelectObject(dc, fillBrush);
    const auto oldPen = SelectObject(dc, borderPen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(borderPen);
    DeleteObject(fillBrush);
}

void DrawOwnerButton(const DRAWITEMSTRUCT& item) {
    const bool primary = item.CtlID == kBuild;
    const bool disabled = (item.itemState & ODS_DISABLED) != 0;
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    COLORREF fill = kCard;
    COLORREF border = kBorder;
    COLORREF text = kText;
    if (primary) {
        fill = disabled ? RGB(151, 176, 242) : (pressed ? kPrimaryPressed : kPrimary);
        border = fill;
        text = RGB(255, 255, 255);
    } else if (pressed) {
        fill = RGB(237, 242, 250);
    }
    const auto dpi = GetDpiForWindow(item.hwndItem);
    DrawRoundedPanel(item.hDC, item.rcItem, fill, border, Scale(primary ? 12 : 9, dpi));
    wchar_t caption[128]{};
    GetWindowTextW(item.hwndItem, caption, static_cast<int>(std::size(caption)));
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, text);
    SelectObject(item.hDC, reinterpret_cast<HFONT>(SendMessageW(item.hwndItem, WM_GETFONT, 0, 0)));
    auto textRect = item.rcItem;
    DrawTextW(item.hDC, caption, -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void SetStatus(HWND window, const std::wstring& text, COLORREF color = kSuccess) {
    auto* state = reinterpret_cast<State*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (!state) return;
    state->status = text;
    state->statusColor = color;
    const auto rect = ScaleRect(kStatusRect, state->dpi);
    RedrawWindow(window, &rect, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_NOCHILDREN);
}

void UpdateMode(HWND window) {
    const bool local = IsDlgButtonChecked(window, kModeLocal) == BST_CHECKED;
    SetWindowTextW(GetDlgItem(window, kSourceLabel), local ? L"网页目录" : L"在线网址");
    SetWindowTextW(GetDlgItem(window, kModeHint),
                   local ? L"选择普通 HTML、Vue、React 或 Vite 的构建产物目录（入口为 index.html）"
                         : L"输入以 https:// 开头的在线网页地址");
    SendMessageW(GetDlgItem(window, kSource), EM_SETCUEBANNER, TRUE,
                 reinterpret_cast<LPARAM>(local ? L"例如：D:\\project\\dist" : L"例如：https://example.com"));
    EnableWindow(GetDlgItem(window, kBrowseSource), local);
    InvalidateRect(window, nullptr, TRUE);
}

std::wstring ProgressText(int step, int total, const std::string& name) {
    std::wstring action;
    if (name == "Validate project") action = L"正在校验项目";
    else if (name == "Resolve locked Android toolchain") action = L"正在检查锁定工具链";
    else if (name == "Prepare isolated workspace") action = L"正在准备隔离工作目录";
    else if (name == "Copy web assets and generate Runtime config") action = L"正在复制网页资源";
    else if (name == "Generate Android manifest") action = L"正在生成 Android Manifest";
    else if (name == "Generate Android resources") action = L"正在生成 Android 资源";
    else if (name == "Compile resources with AAPT2") action = L"正在使用 AAPT2 编译资源";
    else if (name == "Link resource APK with AAPT2") action = L"正在链接资源 APK";
    else if (name == "Normalize assets and inject precompiled Runtime DEX") action = L"正在注入 Runtime DEX";
    else if (name == "Align unsigned APK") action = L"正在执行 APK 对齐";
    else if (name == "Resolve package signing identity") action = L"正在加载 Package 独立签名";
    else if (name == "Sign APK") action = L"正在签名 APK";
    else if (name == "Verify APK signature") action = L"正在验证 APK 签名";
    else if (name == "Publish APK and release metadata") action = L"正在发布 APK 与发行元数据";
    else action = L"构建完成";
    return L"[" + std::to_wstring(step) + L"/" + std::to_wstring(total) + L"] " + action + L"…";
}

std::filesystem::path CurrentExecutable() {
    std::wstring buffer(32768, L'\0');
    const auto length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) throw std::runtime_error("Unable to locate the GUI executable");
    buffer.resize(length);
    return std::filesystem::path(buffer);
}

void StartBuild(HWND window) {
    auto* state = reinterpret_cast<State*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (!state || state->busy) return;
    try {
        GuiProjectInput input;
        input.remote = IsDlgButtonChecked(window, kModeRemote) == BST_CHECKED;
        if (input.remote) input.remoteUrl = WideToUtf8(Text(window, kSource));
        else input.sourceDirectory = std::filesystem::path(Text(window, kSource));
        input.name = WideToUtf8(Text(window, kName));
        input.packageName = WideToUtf8(Text(window, kPackage));
        input.versionName = WideToUtf8(Text(window, kVersionName));
        const auto versionCodeText = Text(window, kVersionCode);
        std::size_t consumed = 0;
        input.versionCode = std::stoi(versionCodeText, &consumed);
        if (consumed != versionCodeText.size()) throw std::runtime_error("Version Code must be an integer");
        const auto orientationIndex = SendMessageW(GetDlgItem(window, kOrientation), CB_GETCURSEL, 0, 0);
        input.orientation = orientationIndex == 1 ? "portrait" : orientationIndex == 2 ? "landscape" : "auto";
        input.fullscreen = IsDlgButtonChecked(window, kFullscreen) == BST_CHECKED;
        input.outputDirectory = std::filesystem::path(Text(window, kOutput));
        auto config = CreateProjectConfig(input, state->environment);
        PackerLogger().Info("GUI build requested for package " + config.packageName);
        BuildOptions options;
        options.runtimeDirectory = state->environment.runtimeDirectory;
        if (!state->environment.toolchainDirectory.empty()) {
            options.androidSdk = state->environment.toolchainDirectory;
        }
        options.progress = [window](int step, int total, const std::string& name) {
            auto update = std::make_unique<std::wstring>(ProgressText(step, total, name));
            if (PostMessageW(window, kBuildProgressMessage, 0, reinterpret_cast<LPARAM>(update.get()))) {
                update.release();
            }
        };
        state->busy = true;
        EnableWindow(GetDlgItem(window, kBuild), FALSE);
        SetWindowTextW(GetDlgItem(window, kBuild), L"正在生成 APK…");
        SetStatus(window, L"正在准备 Android APK 构建任务…");
        std::thread([window, config = std::move(config), options = std::move(options)]() mutable {
            auto completion = std::make_unique<BuildCompletion>();
            try {
                completion->result = BuildPipeline::Build(config, options);
                completion->success = true;
            } catch (const std::exception& error) {
                completion->error = Utf8ToWide(error.what());
            } catch (...) {
                completion->error = L"发生未知构建错误";
            }
            if (PostMessageW(window, kBuildFinishedMessage, 0,
                             reinterpret_cast<LPARAM>(completion.get()))) {
                completion.release();
            }
        }).detach();
    } catch (const std::exception& error) {
        PackerLogger().Error(std::string("Unable to start GUI build: ") + error.what());
        const auto message = Utf8ToWide(error.what());
        SetStatus(window, L"生成失败：" + message, kFailure);
        MessageBoxW(window, message.c_str(), L"生成失败", MB_OK | MB_ICONERROR);
    }
}

void UpdateToolchainDisplay(State& state) {
    const bool ready = IsMinimalToolchainDirectory(state.environment.toolchainDirectory);
    SetWindowTextW(GetDlgItem(state.window, kToolchainStatus),
                   ready ? L"工具链：最小工具链已就绪，可直接生成 APK"
                         : L"工具链：尚未初始化（也可使用系统开发环境）");
    SetWindowTextW(GetDlgItem(state.window, kInstallToolchain), ready ? L"已初始化" : L"初始化工具链");
    EnableWindow(GetDlgItem(state.window, kInstallToolchain), !ready && !state.busy);
}

void StartToolchainInstall(HWND window) {
    auto* state = reinterpret_cast<State*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (!state || state->busy) return;
    const auto script = state->environment.applicationRoot / "tools" / "install-minimal-toolchain.ps1";
    if (!std::filesystem::is_regular_file(script)) {
        MessageBoxW(window, L"工具链初始化脚本不存在，请重新下载完整发行包。", L"无法初始化",
                    MB_OK | MB_ICONERROR);
        return;
    }
    const auto consent = MessageBoxW(
        window,
        L"将从 Android 和 Eclipse Adoptium 官方源下载锁定版本的 Android 构建组件与 Java 17 JRE。\n\n"
        L"继续前请阅读 Android SDK License：\nhttps://developer.android.com/studio/terms\n\n"
        L"点击“是”表示你已阅读并接受该许可，是否继续？",
        L"初始化最小工具链", MB_YESNO | MB_ICONINFORMATION | MB_DEFBUTTON2);
    if (consent != IDYES) return;

    PackerLogger().Info("GUI toolchain initialization accepted by the local user");

    const auto destination = state->environment.applicationRoot / "toolchain";
    auto parameters = std::wstring(L"-NoProfile -ExecutionPolicy Bypass -File \"") + script.wstring() +
                      L"\" -Destination \"" + destination.wstring() +
                      L"\" -AcceptAndroidSdkLicense -Force";
    SHELLEXECUTEINFOW execute{};
    execute.cbSize = sizeof(execute);
    execute.fMask = SEE_MASK_NOCLOSEPROCESS;
    execute.hwnd = window;
    execute.lpVerb = L"open";
    execute.lpFile = L"powershell.exe";
    execute.lpParameters = parameters.c_str();
    execute.lpDirectory = state->environment.applicationRoot.c_str();
    execute.nShow = SW_SHOW;
    if (!ShellExecuteExW(&execute) || execute.hProcess == nullptr) {
        MessageBoxW(window, L"无法启动工具链初始化程序。", L"无法初始化", MB_OK | MB_ICONERROR);
        return;
    }

    state->busy = true;
    EnableWindow(GetDlgItem(window, kBuild), FALSE);
    EnableWindow(GetDlgItem(window, kInstallToolchain), FALSE);
    SetStatus(window, L"正在初始化最小工具链，请查看下载窗口…");
    std::thread([window, process = execute.hProcess]() {
        WaitForSingleObject(process, INFINITE);
        DWORD exitCode = 1;
        GetExitCodeProcess(process, &exitCode);
        CloseHandle(process);
        PostMessageW(window, kToolchainFinishedMessage, static_cast<WPARAM>(exitCode), 0);
    }).detach();
}

void BuildInterface(State& state) {
    AddLabel(state, L"lw.Web2Android", 34, 22, 360, 38, 0, FontRole::Title);
    AddLabel(state, L"把网页项目快速转换为可安装的 Android APK", 35, 63, 520, 24, 0, FontRole::Small);
    const auto productVersion = std::wstring(L"v") + Utf8ToWide(LW_WEB2ANDROID_VERSION);
    AddControl(state, L"STATIC", productVersion.c_str(), SS_RIGHT, 620, 39, 92, 22, 0, FontRole::Small);

    AddLabel(state, L"01  网页来源", 48, 126, 200, 28, 0, FontRole::Section);
    AddControl(state, L"BUTTON", L"本地静态目录", WS_TABSTOP | BS_AUTORADIOBUTTON | WS_GROUP,
               48, 162, 145, 26, kModeLocal);
    AddControl(state, L"BUTTON", L"在线网址", WS_TABSTOP | BS_AUTORADIOBUTTON,
               208, 162, 120, 26, kModeRemote);
    CheckRadioButton(state.window, kModeLocal, kModeRemote, kModeLocal);
    AddLabel(state, L"网页目录", 48, 202, 90, 22, kSourceLabel, FontRole::Small);
    AddLabel(state, L"选择普通 HTML、Vue、React 或 Vite 的构建产物目录（入口为 index.html）",
             141, 203, 565, 22, kModeHint, FontRole::Small);
    AddEdit(state, L"", 48, 228, 548, kSource, L"例如：D:\\project\\dist");
    AddButton(state, L"选择目录", 610, 228, 102, 34, kBrowseSource);

    AddLabel(state, L"02  应用设置", 48, 327, 200, 28, 0, FontRole::Section);
    AddLabel(state, L"应用名称", 48, 369, 120, 22, 0, FontRole::Small);
    AddEdit(state, L"我的网页应用", 48, 393, 664, kName, L"显示在 Android 桌面上的名称");
    AddLabel(state, L"Package Name", 48, 441, 160, 22, 0, FontRole::Small);
    AddEdit(state, L"com.example.myapp", 48, 465, 664, kPackage, L"例如：com.company.app");

    AddLabel(state, L"Version Name", 48, 513, 150, 22, 0, FontRole::Small);
    AddEdit(state, L"1.0.0", 48, 537, 170, kVersionName);
    AddLabel(state, L"Version Code", 242, 513, 150, 22, 0, FontRole::Small);
    AddEdit(state, L"1", 242, 537, 110, kVersionCode, nullptr, ES_NUMBER);
    AddLabel(state, L"屏幕方向", 376, 513, 120, 22, 0, FontRole::Small);
    const auto orientation = AddCombo(state, 376, 537, 170, kOrientation);
    SendMessageW(orientation, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"自动"));
    SendMessageW(orientation, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"竖屏"));
    SendMessageW(orientation, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"横屏"));
    SendMessageW(orientation, CB_SETCURSEL, 0, 0);
    AddControl(state, L"BUTTON", L"全屏显示", WS_TABSTOP | BS_AUTOCHECKBOX,
               584, 541, 120, 26, kFullscreen);

    AddLabel(state, L"输出目录", 48, 597, 120, 22, 0, FontRole::Small);
    AddEdit(state, L"", 48, 621, 548, kOutput, L"APK、校验和与发行元数据的输出目录");
    AddButton(state, L"选择位置", 610, 621, 102, 34, kBrowseOutput);
    AddLabel(state, L"签名：按 Package Name 自动创建或复用独立身份，可在 CLI 中导出 PFX 备份",
             48, 674, 650, 24, 0, FontRole::Small);
    AddLabel(state, L"", 48, 702, 470, 24, kToolchainStatus, FontRole::Small);
    AddButton(state, L"初始化工具链", 560, 694, 152, 34, kInstallToolchain);

    AddButton(state, L"生成 Android APK", 48, 755, 664, 48, kBuild);

    const auto output = std::filesystem::current_path() / "output";
    SetWindowTextW(GetDlgItem(state.window, kOutput), output.c_str());
    UpdateToolchainDisplay(state);
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* state = reinterpret_cast<State*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    switch (message) {
        case WM_CREATE: {
            const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
            state = new State{};
            state->window = window;
            state->dpi = GetDpiForWindow(window);
            state->environment = *reinterpret_cast<const GuiEnvironment*>(create->lpCreateParams);
            RecreateFonts(*state);
            state->backgroundBrush = CreateSolidBrush(kBackground);
            state->cardBrush = CreateSolidBrush(kCard);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
            BuildInterface(*state);
            return 0;
        }
        case WM_DPICHANGED:
            if (state) {
                state->dpi = HIWORD(wparam);
                RecreateFonts(*state);
                LayoutControls(*state);
                const auto* suggested = reinterpret_cast<RECT*>(lparam);
                SetWindowPos(window, nullptr, suggested->left, suggested->top,
                             suggested->right - suggested->left, suggested->bottom - suggested->top,
                             SWP_NOACTIVATE | SWP_NOZORDER);
                RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
            }
            return 0;
        case kBuildProgressMessage: {
            std::unique_ptr<std::wstring> update(reinterpret_cast<std::wstring*>(lparam));
            if (update) SetStatus(window, *update);
            return 0;
        }
        case kBuildFinishedMessage: {
            std::unique_ptr<BuildCompletion> completion(reinterpret_cast<BuildCompletion*>(lparam));
            if (!state || !completion) return 0;
            state->busy = false;
            EnableWindow(GetDlgItem(window, kBuild), TRUE);
            SetWindowTextW(GetDlgItem(window, kBuild), L"生成 Android APK");
            InvalidateRect(GetDlgItem(window, kBuild), nullptr, TRUE);
            if (completion->success) {
                PackerLogger().Info("GUI build completed: " + completion->result.apk.u8string());
                SetStatus(window, L"生成完成 · APK 签名、验证与 SHA-256 均已通过");
                const auto detail = L"APK 已成功生成：\n\n" + completion->result.apk.wstring() +
                                    L"\n\nAPK SHA-256：\n" + Utf8ToWide(completion->result.apkSha256) +
                                    L"\n\n证书 SHA-256：\n" + Utf8ToWide(completion->result.certificateSha256);
                MessageBoxW(window, detail.c_str(), L"lw.Web2Android", MB_OK | MB_ICONINFORMATION);
            } else {
                PackerLogger().Error("GUI build failed: " + WideToUtf8(completion->error));
                SetStatus(window, L"生成失败：" + completion->error, kFailure);
                const auto& logger = PackerLogger();
                const auto logPath = logger.Enabled() ? logger.File().wstring() : L"日志文件不可用（请查看错误输出）";
                const auto detail = completion->error + L"\n\n打包日志：\n" + logPath;
                MessageBoxW(window, detail.c_str(), L"生成失败", MB_OK | MB_ICONERROR);
            }
            return 0;
        }
        case kToolchainFinishedMessage: {
            if (!state) return 0;
            state->busy = false;
            EnableWindow(GetDlgItem(window, kBuild), TRUE);
            const auto installed = state->environment.applicationRoot / "toolchain";
            if (wparam == 0 && IsMinimalToolchainDirectory(installed)) {
                PackerLogger().Info("Toolchain initialization completed");
                state->environment.toolchainDirectory = installed;
                const auto toolchainRuntime = installed / "runtime";
                if (std::filesystem::is_directory(toolchainRuntime)) {
                    state->environment.runtimeDirectory = toolchainRuntime;
                }
                SetStatus(window, L"最小工具链初始化完成 · 以后将自动复用");
                MessageBoxW(window, L"最小工具链已安装，可以直接生成 APK。", L"初始化完成",
                            MB_OK | MB_ICONINFORMATION);
            } else {
                const auto initializationLog = state->environment.applicationRoot / "logs" / "toolchain-init.log";
                PackerLogger().Error("Toolchain initialization failed with exit code " +
                                     std::to_string(static_cast<unsigned long long>(wparam)) +
                                     "; log: " + initializationLog.u8string());
                SetStatus(window, L"工具链初始化失败，请查看 logs/toolchain-init.log", kFailure);
                const auto errorMessage = std::wstring(L"工具链初始化未完成。\n\n请查看日志：\n") +
                                          initializationLog.wstring();
                MessageBoxW(window, errorMessage.c_str(),
                            L"初始化失败", MB_OK | MB_ICONERROR);
            }
            UpdateToolchainDisplay(*state);
            return 0;
        }
        case WM_COMMAND:
            if (!state) break;
            switch (LOWORD(wparam)) {
                case kModeLocal:
                case kModeRemote:
                    UpdateMode(window);
                    return 0;
                case kBrowseSource: {
                    const auto path = PickFolder(window, L"选择网页构建产物目录");
                    if (!path.empty()) SetWindowTextW(GetDlgItem(window, kSource), path.c_str());
                    return 0;
                }
                case kBrowseOutput: {
                    const auto path = PickFolder(window, L"选择 APK 输出目录");
                    if (!path.empty()) SetWindowTextW(GetDlgItem(window, kOutput), path.c_str());
                    return 0;
                }
                case kBuild:
                    StartBuild(window);
                    return 0;
                case kInstallToolchain:
                    StartToolchainInstall(window);
                    return 0;
            }
            break;
        case WM_DRAWITEM:
            DrawOwnerButton(*reinterpret_cast<DRAWITEMSTRUCT*>(lparam));
            return TRUE;
        case WM_CTLCOLORSTATIC: {
            const auto dc = reinterpret_cast<HDC>(wparam);
            const auto id = GetDlgCtrlID(reinterpret_cast<HWND>(lparam));
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, id == kModeHint ? kMuted : kText);
            return reinterpret_cast<INT_PTR>(GetStockObject(NULL_BRUSH));
        }
        case WM_CTLCOLORBTN: {
            const auto dc = reinterpret_cast<HDC>(wparam);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, kText);
            return reinterpret_cast<INT_PTR>(GetStockObject(NULL_BRUSH));
        }
        case WM_ERASEBKGND:
            return TRUE;
        case WM_CLOSE:
            if (state && state->busy) {
                MessageBoxW(window, L"后台任务正在进行，请等待完成后再关闭窗口。",
                            L"lw.Web2Android", MB_OK | MB_ICONINFORMATION);
                return 0;
            }
            break;
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            const auto dc = BeginPaint(window, &paint);
            RECT client{};
            GetClientRect(window, &client);
            FillRect(dc, &client, state ? state->backgroundBrush : reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
            RECT header{0, 0, client.right, state ? Scale(105, state->dpi) : 105};
            const auto headerBrush = CreateSolidBrush(kHeader);
            FillRect(dc, &header, headerBrush);
            DeleteObject(headerBrush);
            const auto dpi = state ? state->dpi : 96;
            DrawRoundedPanel(dc, ScaleRect(RECT{28, 109, 732, 285}, dpi), kCard, kBorder, Scale(18, dpi));
            DrawRoundedPanel(dc, ScaleRect(RECT{28, 300, 732, 738}, dpi), kCard, kBorder, Scale(18, dpi));
            if (state) {
                const auto statusRect = ScaleRect(kStatusRect, state->dpi);
                FillRect(dc, &statusRect, state->backgroundBrush);
                SetBkMode(dc, OPAQUE);
                SetBkColor(dc, kBackground);
                SetTextColor(dc, state->statusColor);
                SelectObject(dc, state->smallFont);
                auto textRect = statusRect;
                DrawTextW(dc, state->status.c_str(), -1, &textRect,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            }
            EndPaint(window, &paint);
            return 0;
        }
        case WM_DESTROY:
            PackerLogger().Info("GUI closed");
            PackerLogger().Flush();
            if (state) {
                DeleteObject(state->titleFont);
                DeleteObject(state->sectionFont);
                DeleteObject(state->bodyFont);
                DeleteObject(state->smallFont);
                DeleteObject(state->backgroundBrush);
                DeleteObject(state->cardBrush);
                delete state;
            }
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

int RunGui(HINSTANCE instance) {
    PackerLogger().Info(std::string("GUI started, version ") + LW_WEB2ANDROID_VERSION);
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    const auto environment = GuiEnvironment::Discover(CurrentExecutable(), std::filesystem::current_path());
    WNDCLASSEXW windowClass{sizeof(windowClass)};
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.hbrBackground = nullptr;
    windowClass.lpszClassName = kClassName;
    if (!RegisterClassExW(&windowClass)) throw std::runtime_error("Unable to register the GUI window");
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    const auto dpi = GetDpiForSystem();
    RECT bounds{0, 0, Scale(760, dpi), Scale(875, dpi)};
    AdjustWindowRectExForDpi(&bounds, style, FALSE, WS_EX_CONTROLPARENT, dpi);
    const auto window = CreateWindowExW(
        WS_EX_CONTROLPARENT, kClassName, L"lw.Web2Android · 网页转 Android APK",
        style, CW_USEDEFAULT, CW_USEDEFAULT, bounds.right - bounds.left, bounds.bottom - bounds.top,
        nullptr, nullptr, instance, const_cast<GuiEnvironment*>(&environment));
    if (!window) throw std::runtime_error("Unable to create the GUI window");
    const DWORD corner = 2;
    DwmSetWindowAttribute(window, 33, &corner, sizeof(corner));
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    return static_cast<int>(message.wParam);
}

int RunSmokeTest() {
    const auto environment = GuiEnvironment::Discover(CurrentExecutable(), std::filesystem::current_path());
    return std::filesystem::is_regular_file(environment.toolchainLock) &&
                   std::filesystem::is_directory(environment.runtimeDirectory)
               ? 0
               : 1;
}

}  // namespace
}  // namespace lw::web2android::gui

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    try {
        const auto result = std::wstring(commandLine) == L"--smoke-test"
                                ? lw::web2android::gui::RunSmokeTest()
                                : lw::web2android::gui::RunGui(instance);
        if (SUCCEEDED(initialized)) CoUninitialize();
        return result;
    } catch (const std::exception& error) {
        lw::web2android::PackerLogger().Error(std::string("GUI fatal error: ") + error.what());
        lw::web2android::PackerLogger().Flush();
        const auto message = lw::web2android::Utf8ToWide(error.what());
        MessageBoxW(nullptr, message.c_str(), L"lw.Web2Android", MB_OK | MB_ICONERROR);
        if (SUCCEEDED(initialized)) CoUninitialize();
        return 1;
    }
}
