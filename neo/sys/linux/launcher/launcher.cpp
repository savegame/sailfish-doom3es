#include "launcher.h"

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"
#include "framework/Session_local.h"
#include "framework/FileSystem.h"
#include "imfilebrowser.h"
#include "renderer/Image.h"
// #include "../../../../SDL2/src/SDL_internal.h"
#include <QCoreApplication>
#include <QDBusContext>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <SDL.h>
#include <vector>
#include <list>
#include <map>

// #define HAVE_DBUS_DBUS_H
// #define SDL_USE_LIBDBUS
// #include "../../../../libsdl/src/core/linux/SDL_dbus.h"
// #include "../../../../libsdl/src/core/linux/SDL_dbus.c"

#define Intent_Service "ru.omp.RuntimeManager"
#define Intent_Path "/ru/omp/RuntimeManager/Intents1"
#define Intent_IFace "ru.omp.RuntimeManager.Intents1"
#define Intent_Method "InvokeIntent"
#define Intent_Hints ""
#define Intent_IntentMethod "OpenURI"

class idStr_ : public idStr {
public:

    char *c_str() const {
        return data;
    }
};

struct _LImage {
    int w = 0;
    int h = 0;
    GLuint texture = 0;
};
typedef _LImage LImage;

struct SettingsValue {
public:
    enum Type {
        None,
        Option,
        Boolean,
        Integer,
        String
    };

    enum OptionValue : int {
        Enable = 1,
        Disable = 0
    };

    SettingsValue() 
        : uid(++SettingsValue::last_uid)
    {}

    void operator=(const SettingsValue &other) 
    {
        int_value = other.int_value;
        type = other.type;
        bool_value = other.bool_value;
        name = other.name;
    }

    int int_value = -1;
    bool bool_value = false;
    idStr *str_value = nullptr;
    Type type = Type::None;
    idStr name;
    idStr description;
    const int uid = -1;
    static int last_uid;
    bool save = true; // should it save to settings
};

int SettingsValue::last_uid = -1;

class LauncherPrivate {
public:
    int nativeWidth = 0;
    int nativeHeight = 0;

    ImGui::FileBrowser fileDialog {ImGuiFileBrowserFlags_SelectDirectory | ImGuiFileBrowserFlags_NoTitleBar | ImGuiFileBrowserFlags_NoStatusBar};
    idStr sys_path;
    std::list<idStr*> args;
    std::vector<const char*> argv;

    std::list<SettingsValue*> settings;

    SDL_Window *window = nullptr;
    SDL_GLContext gl_context = nullptr;
    // SDL_DBusContext *dbus_context = nullptr;
    bool no_basepath = true;
    bool settings_changed = false;
    bool native_landscape = false;
    // imgui ui variables
    int button_height = 160;
    int tabIndex = 0;
    int graphicsShowDescription = -1; // id to show description
    float dpmm = 1.0f;

    LImage logo512;
    LImage avatar;
    ImFont *fntPixy = nullptr;

    void openUrl(idStr url) {
        auto request = QDBusMessage::createMethodCall(Intent_Service, Intent_Path, Intent_IFace, Intent_Method);
        request << Intent_IntentMethod;
        request << QVariantMap ();
        request << QVariantMap { {QString("uri"),url.c_str()}} ;
        auto async = QDBusConnection::sessionBus().asyncCall(request);
        // TODO Fallback to SDL@ OpenURL
    }

    char *get_num(int num) {
        idStr *str = new idStr(num);
        args.push_back(str);
        return ((idStr_*)str)->c_str();
    }

    char *get_str(const char *cstr) {
        idStr *str = new idStr(cstr);
        args.push_back(str);
        return ((idStr_*)str)->c_str();
    }

    void set_setting_option(idStr name, bool value, idStr description = idStr("")) {
        auto search = std::find_if(settings.begin(), settings.end(), [name](SettingsValue* current){
            return current->name == name;
        });
        SettingsValue *newVal;
        if (search == settings.end()) {
            newVal = new SettingsValue;
            settings.push_back(newVal);
        } else {
            newVal = *search;
        }

        newVal->type = SettingsValue::Type::Option;
        newVal->name = name;
        newVal->bool_value = value;
        if (!description.IsEmpty())
            newVal->description = description;
    }

    void set_setting_bool(idStr name, bool value, idStr description = idStr("")) {
        auto search = std::find_if(settings.begin(), settings.end(), [name](SettingsValue* current){
            return current->name == name;
        });
        SettingsValue *newVal;
        if (search == settings.end()) {
            newVal = new SettingsValue;
            settings.push_back(newVal);
        } else {
            newVal = *search;
        }

        newVal->type = SettingsValue::Type::Boolean;
        newVal->name = name;
        newVal->bool_value = value;
        if (!description.IsEmpty())
            newVal->description = description;
    }

    void set_setting_int(idStr name, int value, idStr description = idStr("")) {
        auto search = std::find_if(settings.begin(), settings.end(), [name](SettingsValue* current){
            return current->name == name;
        });
        SettingsValue *newVal;
        if (search == settings.end()) {
            newVal = new SettingsValue;
            settings.push_back(newVal);
        } else {
            newVal = *search;
        }

        newVal->type = SettingsValue::Type::Integer;
        newVal->name = name;
        newVal->int_value = value;
        if (!description.IsEmpty())
            newVal->description = description;
    }

    void set_setting_string(idStr name, idStr value, idStr description = idStr("")) {
        auto search = std::find_if(settings.begin(), settings.end(), [name](SettingsValue* current){
            return current->name == name;
        });
        SettingsValue *newVal;
        if (search == settings.end()) {
            newVal = new SettingsValue;
            settings.push_back(newVal);
        } else {
            newVal = *search;
        }
        if (!newVal->str_value)
            newVal->str_value = new idStr;

        newVal->type = SettingsValue::Type::String;
        newVal->name = name;
        newVal->str_value->operator=(value);
        if (!description.IsEmpty())
            newVal->description = description;
    }

    const SettingsValue* get_setting(idStr name) {
        auto search = std::find_if(settings.begin(), settings.end(), [name](SettingsValue* current){
            return current->name == name;
        });
        if (search == settings.end()) {
            return nullptr;
        }
        return *search;
    }

    SettingsValue* get_setting_edit(idStr name) {
        auto search = std::find_if(settings.begin(), settings.end(), [name](SettingsValue* current){
            return current->name == name;
        });
        if (search == settings.end()) {
            return nullptr;
        }
        return *search;
    }

    LImage load_image(idStr filename) {
        LImage result;
        idStr config_path;
#ifdef SAILFISH_APPNAME
        Sys_GetPath(PATH_BASE, config_path);
#else
        Sys_GetPath(PATH_CONFIG, config_path);
#endif
        if (filename.Find('/') != 0) 
            config_path += "/";
        config_path += filename;
        if (!R_LoadImageFile(config_path.c_str(), result.texture, result.w, result.h))
            printf("Image not found: %s\n", config_path.c_str());
        return result;
    }

    ImFont* load_font(idStr filename) {
        ImFont *result = nullptr;
        idStr config_path;
#ifdef SAILFISH_APPNAME
        Sys_GetPath(PATH_BASE, config_path);
#else
        Sys_GetPath(PATH_CONFIG, config_path);
#endif
        if (filename.Find('/') != 0) 
            config_path += "/";
        config_path += filename;
        result = ImGui::GetIO().Fonts->AddFontFromFileTTF(config_path.c_str(),
        38, nullptr, ImGui::GetIO().Fonts->GetGlyphRangesCyrillic());
        if (!result)
            printf("Font not found: %s\n", config_path.c_str());
        return result;
    }
    

    bool CenterButton(const char* label, float alignment = 0.5f)
    {
        ImGuiStyle& style = ImGui::GetStyle();

        float size = ImGui::CalcTextSize(label).x + style.FramePadding.x * 4.0f;
        float avail = ImGui::GetContentRegionAvail().x;

        float off = (avail - size) * alignment;
        if (off > 0.0f)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + off);

        return ImGui::Button(label);
    }

    void CenterText(const char* label, float alignment = 0.5f)
    {
        ImGuiStyle& style = ImGui::GetStyle();

        float size = ImGui::CalcTextSize(label).x + style.FramePadding.x * 2.0f;
        float avail = ImGui::GetContentRegionAvail().x;

        float off = (avail - size) * alignment;
        if (off > 0.0f)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + off);

        ImGui::Text(label);
    }

    void save_settings();
    void load_settings();
    void default_settings();

    int render_tab0(bool &done);
    int render_tab1(bool &done);
    int render_tab2_about(bool &done);
    int render_tabBar(bool &done);
    int render_resolutions();
};

Launcher::Launcher(int argc, char** argv)
    : d_ptr(new LauncherPrivate)
{
    // here we need init IMGUI ui
    // Setup SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0)
    {
        printf("Error: %s\n", SDL_GetError());
        return;
    }

    // Decide GL+GLSL versions
#if defined(IMGUI_IMPL_OPENGL_ES2)
    // GL ES 2.0 + GLSL 100
    const char* glsl_version =  "#version 100";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#elif defined(__APPLE__)
    // GL 3.2 Core + GLSL 150
    const char* glsl_version = "#version 150";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG); // Always required on Mac
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#else
    // GL 3.0 + GLSL 130
    const char* glsl_version = "#version 130";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif
    // From 2.0.18: Enable native IME.
#ifdef SDL_HINT_IME_SHOW_UI
    SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
#endif
    SDL_SetHint(SDL_HINT_QTWAYLAND_CONTENT_ORIENTATION, "primary");

    // Create window with graphics context
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_DisplayMode dm;
    SDL_GetCurrentDisplayMode(0,&dm);
    d_ptr->nativeWidth = dm.w;
    d_ptr->nativeHeight = dm.h;
    fprintf(stderr, "Native resolution is %i x %i\n", dm.w, dm.h);
    #ifndef SAILFISHOS
        d_ptr->nativeWidth = 1280;
        d_ptr->nativeHeight = 720;
    #endif
    d_ptr->native_landscape = d_ptr->nativeWidth > d_ptr->nativeHeight;
    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    d_ptr->window = SDL_CreateWindow("ru.sashikknox.doom3es", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, d_ptr->nativeWidth, d_ptr->nativeHeight, window_flags);
    d_ptr->gl_context = SDL_GL_CreateContext(d_ptr->window);
    SDL_GL_MakeCurrent(d_ptr->window, d_ptr->gl_context);
    SDL_GL_SetSwapInterval(1); // Enable vsync

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    // io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsLight();

    // Setup Platform/Renderer backends
    ImGui_ImplSDL2_InitForOpenGL(d_ptr->window, d_ptr->gl_context);
    ImGui_ImplOpenGL3_Init(glsl_version);

    ImGuiStyle * style = &ImGui::GetStyle();
    #ifdef SAILFISHOS
    float imgui_scale_factor = d_ptr->nativeWidth / 640.0f;
    #else
    float imgui_scale_factor = 1.0; //640 / 320.0f;
    #endif
    style->ScaleAllSizes(imgui_scale_factor);
    io.FontGlobalScale = imgui_scale_factor;
    idStr resourcesPath;
#ifdef SAILFISH_APPNAME
    d_ptr->fntPixy = d_ptr->load_font("res/PIXY.ttf");
#else
    Sys_GetPath(PATH_CONFIG, resourcesPath);
    resourcesPath = "/home/sashikknox/Projects/doom3es/res/PIXY.ttf";
    d_ptr->fntPixy = io.Fonts->AddFontFromFileTTF(resourcesPath.c_str(),
        38, nullptr, io.Fonts->GetGlyphRangesCyrillic());
#endif

    // Main loop
    // create a file browser instance
    idStr arg_string;
    std::vector<int> clean_index;
    char *buf;
    int fs_basepath_index = -1;
    d_ptr->argv.reserve(20);
    // check args 
    for (int i = 1; i < argc; i++) {
        if (strcmp("fs_basepath", argv[i]) == 0) {
            d_ptr->no_basepath = false;
            i++;
            fs_basepath_index = i;
            break;
        }
    }

    
    //TODO setup d_ptr->settings from args 
    for (int i = 0; i < argc; i++) {
        if (i <= fs_basepath_index && i >= fs_basepath_index - 2) {
            continue;
        }
        d_ptr->argv.push_back(argv[i]);
    }
    // for now, just setup basepath in settins
    if (fs_basepath_index != -1)
        d_ptr->set_setting_string("fs_basepath", idStr(argv[fs_basepath_index]));

    // calculate pixels per mm
    SDL_GetDisplayDPI(0,nullptr, &d_ptr->dpmm, nullptr);
    d_ptr->dpmm /= 25.4f;

    // default settungs
    d_ptr->default_settings();
    // load settings from file
    d_ptr->load_settings();
    // load resources
    d_ptr->logo512 = d_ptr->load_image("res/512.png");
    d_ptr->avatar = d_ptr->load_image("res/sashikknox.png");
}

Launcher::~Launcher() 
{
    delete d_ptr;
}

int Launcher::argc() const 
{
    return d_ptr->argv.size();
}

const char** Launcher::argv()
{
    return &(d_ptr->argv[0]);
}

int LauncherPrivate::render_tab0(bool &done)
{
    ImGuiIO& io = ImGui::GetIO();
    const SettingsValue *basePath = get_setting("fs_basepath");

    ImVec2 logoSize;
    logoSize.x = logoSize.y = ImGui::GetContentRegionAvail().y - ImGui::GetTextLineHeight() - ImGui::GetStyle().ItemSpacing.y * 5 - button_height * 3;
    if (logoSize.x > ImGui::GetContentRegionAvail().x) {
        logoSize.x = logoSize.y = ImGui::GetContentRegionAvail().x;
    }
    float off = (ImGui::GetContentRegionAvail().x - logoSize.x) * 0.5;
    if (off > 0.0f)
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + off);

    ImGui::Image((void*)(intptr_t)logo512.texture, logoSize);//, ImVec2(0.0, 1.0), ImVec2(1.0, 0.0), ImVec4(1.0, 1.0, 1.0, 1.0));

    idStr message;
    if (fntPixy)
        ImGui::PushFont(fntPixy);
    if (!basePath || !basePath->str_value || basePath->str_value->IsEmpty()) {
        // message = "Choose directory with original Doom 3 files. You can buy it on Steam";
        message = "Выбирите папку в которой расположена копия оригинальной игры Doom 3. Игру можно купить на Steam.";
        ImGui::TextWrapped(message.c_str());
        if (CenterButton("Ссылка на Steam")) {
            // SDL_OpenURL("https://store.steampowered.com/app/208200/DOOM_3/");
            // SDL_SetClipboardText("https://store.steampowered.com/app/208200/DOOM_3/");
            openUrl("https://store.steampowered.com/app/208200/DOOM_3/");
        }
    } else {
        message = "Путо до игры Doom 3: ";
        message += *(basePath->str_value);
        ImGui::Text(message.c_str());
    }


    if (ImGui::Button("Выбрать папку", ImVec2(io.DisplaySize.x - ImGui::GetStyle().WindowPadding.x*2, button_height))) {
        fileDialog.Open();
    }


    if (basePath && ImGui::Button("Играть в Doom 3", ImVec2(io.DisplaySize.x - ImGui::GetStyle().WindowPadding.x*2, button_height))) {
        done = true;
        set_setting_string("fs_game", "");
    }

    if (basePath && ImGui::Button("Играть в D3XP", ImVec2(io.DisplaySize.x - ImGui::GetStyle().WindowPadding.x*2, button_height))) {
        done = true;
        set_setting_string("fs_game", "d3xp");
    }
    
    ImGui::PopFont();
    ImGui::End();

    fileDialog.SetWindowPos(0,0);
    fileDialog.SetWindowSize(io.DisplaySize);

    if (no_basepath) {
        fileDialog.Display();
    
        if (fileDialog.HasSelected() && fileDialog.GetSelected().string().size() > 0) {
            set_setting_string("fs_basepath", fileDialog.GetSelected().string().c_str());
            fileDialog.ClearSelected();
            no_basepath = false;
            settings_changed = true;
        }
    }

    return Launcher::Status::Ok;
}

int LauncherPrivate::render_tab1(bool &done)
{ 
    bool show = true;
    auto io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0,ImGui::GetFontSize() * 4));
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, io.DisplaySize.y - ImGui::GetFontSize() * 4));
    ImGui::Begin("tab1", &show, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysVerticalScrollbar);
    render_resolutions();
    for (auto setting : settings) {
        if (setting->type == SettingsValue::Option || setting->type == SettingsValue::Boolean) {
            if (ImGui::Checkbox(setting->name.c_str(), &setting->bool_value)) {
                printf("%s %s\n", setting->name.c_str(), setting->bool_value ? "enabled" : "disabled");
                settings_changed = true;
            }
            ImGui::SameLine();
            ImGui::PushID("btn");
            if (ImGui::ArrowButton(setting->name.c_str(), graphicsShowDescription == setting->uid ? ImGuiDir_Up : ImGuiDir_Down)) {
                if (graphicsShowDescription == setting->uid)
                    graphicsShowDescription = -1;
                else
                    graphicsShowDescription = setting->uid;
            }
            ImGui::PopID();
            if (setting->uid == graphicsShowDescription) {
                ImGui::TextWrapped(setting->description.c_str());
            }
        }
    }
    ImGui::End();

    ImGui::End();
    return Launcher::Status::Ok;
}

int LauncherPrivate::render_tab2_about(bool &done) {
    ImVec2 logoSize = {ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().x};

    logoSize.x = logoSize.y = dpmm * 20.0; // 20 mm size

    ImGui::PushFont(fntPixy);
    // float off = (logoSize.x - size) * alignment;
    // if (off > 0.0f)
    //     ImGui::SetCursorPosX(ImGui::GetCursorPosX() + off);
    ImGui::Image((void*)(intptr_t)avatar.texture, logoSize);//, ImVec2(0.0, 1.0), ImVec2(1.0, 0.0), ImVec4(1.0, 1.0, 1.0, 1.0));
    ImGui::SameLine();
    ImGui::TextWrapped("Порт игры базируется на проекте \"dhewm3\". Игра портирована разработчиком sashikknox");
    ImGui::LabelText("","");
    CenterText("Исходные коды:");
    if (CenterButton("GitHub.com")) {
        openUrl("https://github.com/savegame/sailfish-doom3es");
    }
    ImGui::LabelText("","");
    CenterText("Поддержать монетой:");
    if (CenterButton("Boosty.to/sashikknox")) {
        openUrl("https://boosty.to/sashikknox");
    }

    ImGui::PopFont();
    ImGui::End();
    return Launcher::Status::Ok;
}

int LauncherPrivate::render_tabBar(bool &done) 
{
    ImGui::LabelText("","");
    ImGui::PushFont(fntPixy);
    if (ImGui::Button("Главная"))
        tabIndex = 0;
    ImGui::SameLine();
    if (ImGui::Button("Графика"))
        tabIndex = 1;
    ImGui::SameLine();
    if (ImGui::Button("О программе"))
        tabIndex = 2;
    ImGui::PopFont();
    return Launcher::Status::Ok;
}

int LauncherPrivate::render_resolutions() 
{
    #ifdef SAILFISHOS // do not use framebuffer on desktop
    const SettingsValue *fbw = get_setting("r_framebufferWidth");
    const SettingsValue *fbh = get_setting("r_framebufferHeight");
    #else
    const SettingsValue *fbw = get_setting("r_customWidth");
    const SettingsValue *fbh = get_setting("r_customHeight");
    #endif
    if (!fbw || !fbh) {
        return Launcher::Status::Error;
    }
    typedef struct { idStr name; int w; int h; } resolution;
    static int current_item = 0, i = 0;
    static std::list<resolution> items;
    static idStr preview_value;

    if (items.empty()) {
        int w = nativeWidth > nativeHeight ? nativeWidth : nativeHeight;
        int h = nativeWidth > nativeHeight ? nativeHeight : nativeWidth;
        int minWidth = 320;
        int minHeight = 180;
        #ifdef SAILFISHOS // do not use framebuffer on desktop
        float coef = 1.0;
        #else
        float coef = 1.5;
        #endif
        int currentW = (int)((float)w * coef);
        int currentH = (int)((float)h * coef);
        
        while (currentW >= minWidth && currentH >= minHeight) {
            idStr current = va("%ix%i", currentW, currentH);
            items.push_back({current, currentW, currentH});
            if (fbw->int_value == currentW) {
                current_item = i;
                preview_value = current;
            }
            i++;
            coef -= 0.25;
            currentW = (int)((float)w * coef);
            currentH = (int)((float)h * coef);
        }

        for (auto it = items.begin(); it != items.end(); it++, i++) {
            printf("Add %s with %i x %i\n", (*it).name.c_str(), (*it).w, (*it).h);
        }
    }

    // ImGui::Combo("Render resolution", &current_item, items->c_str());
    if (ImGui::BeginCombo("Resolution", preview_value.c_str())) {
        i = 0;
        for (auto it = items.begin(); it != items.end(); it++, i++) {
            bool selected = current_item == i;
            if (ImGui::Selectable((*it).name.c_str(), selected)) {
                current_item = i;
                #ifdef SAILFISHOS 
                set_setting_int("r_framebufferWidth", (*it).w);
                set_setting_int("r_framebufferHeight", (*it).h);
                #else // do not use framebuffer on desktop
                set_setting_int("r_customWidth", (*it).w);
                set_setting_int("r_customHeight", (*it).h);
                #endif
                preview_value = (*it).name;
                settings_changed = true;
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    return Launcher::Status::Ok;
}

void LauncherPrivate::default_settings()
{
    set_setting_string("fs_game", "");
    get_setting_edit("fs_game")->save = false;
    set_setting_int("r_mode", -1);
    set_setting_int("r_customWidth", nativeWidth);
    set_setting_int("r_customHeight", nativeHeight);
    #ifdef SAILFISHOS // do not use framebuffer on desktop
    if (native_landscape) {
        set_setting_int("r_framebufferWidth", (const int)((float)nativeWidth * 0.5f));
        set_setting_int("r_framebufferHeight", (const int)((float)nativeHeight * 0.5f));
    } else {
        set_setting_int("r_framebufferWidth", (const int)((float)nativeHeight * 0.5f));
        set_setting_int("r_framebufferHeight", (const int)((float)nativeWidth * 0.5f));
    }
    #endif
    set_setting_option("+disconnect", true, "skip video on game start");
    set_setting_int("r_multiSamples", 0);
    set_setting_bool("r_shadows", true, "use stencil shadows");
    set_setting_bool("r_skipMegaTexture", false);

    set_setting_bool("r_skipBlendLights", false, "skip all blend lights");
    set_setting_bool("r_skipFogLights", false, "skip all fog lights");
    set_setting_bool("r_skipDeforms", false, "leave all deform materials in their original state");
    // set_setting_bool("r_skipFrontEnd", false, "bypasses all front end work, but 2D gui rendering still draws");
    set_setting_bool("r_skipCopyTexture", false, "do all rendering, but don't actually copyTexSubImage2D");
    // set_setting_bool("r_skipUpdates", false, "1 = don't accept any entity or light updates, making everything static");
    // set_setting_bool("r_skipBackEnd", false, "don't draw anything");
    set_setting_bool("r_skipOverlays", false, "skip overlay surfaces");
    // set_setting_bool("r_skipRender", false, "skip 3D rendering, but pass 2D");
    set_setting_bool("r_skipSpecular", false, "use black for specular1");
    set_setting_bool("r_skipTranslucent", false, "skip the translucent interaction rendering");
    set_setting_bool("r_skipBump", false, "uses a flat surface instead of the bump map");
    // set_setting_bool("r_skipAmbient", false, "bypasses all non-interaction drawing. All screens in game will be off, even on weapons.");
    // set_setting_bool("r_skipDiffuse", false, "use black for diffuse");
    set_setting_bool("r_skipROQ", false, "skip ROQ decoding");
    set_setting_bool("r_skipPostProcess", false, "skip all post-process renderings");
    set_setting_bool("r_skipSubviews", false, "1 = don't render any gui elements on surfaces (mirrors)");
    set_setting_bool("r_skipNewAmbient", false, "bypasses all vertex/fragment program ambient drawing");
    // set_setting_int("r_skipGuiShaders", 0, "1 = skip all gui elements on surfaces, 2 = skip drawing but still handle events, 3 = draw but skip events");
    set_setting_bool("r_skipParticles", false, "1 = skip all particle systems. Fire, blows and other particles effects will be invisible.");
    set_setting_bool("r_skipDynamicTextures", false, "don't dynamically create textures");
    // set_setting_bool("r_skipInteractions", false, "skip all light/surface interaction drawing");
    // set_setting_bool("r_skipSuppress", false, "ignore the per-view suppressions");

    set_setting_bool("r_multithread", true, "Multithread backend");
    set_setting_bool("r_useETC1", false, "Compress texture data with ETC, saves GPU memory but can be very slow to load");
}

void LauncherPrivate::save_settings() 
{
    if (idFile *f = fileSystem->OpenExplicitFileWrite(sys_path.c_str())) {
        f->WriteInt(settings.size());
        for (auto setting : settings) {
            f->WriteString(setting->name);
            f->WriteInt(setting->type);
            switch(setting->type) {
                case SettingsValue::Type::Option:
                case SettingsValue::Type::Boolean:
                    f->WriteBool(setting->bool_value);
                    printf("Write setting %s [%i] : %s\n", setting->name.c_str(), setting->type, setting->bool_value ? "true" : "false");
                    break;
                case SettingsValue::Type::String:
                    f->WriteString(*setting->str_value);
                    printf("Write setting %s [%i] : %s\n", setting->name.c_str(), setting->type, setting->str_value->c_str());
                    break;
                case SettingsValue::Type::Integer:
                    f->WriteInt(setting->int_value);
                    printf("Write setting %s [%i] : %i\n", setting->name.c_str(), setting->type, setting->int_value);
                    break;
            }
        }
        delete f; // close file
    }
}

void LauncherPrivate::load_settings() 
{
    Sys_GetPath(PATH_CONFIG,sys_path);
    sys_path += "/imgui_settings.cfg";
    // Try load settings file 
    if (idFile *f = fileSystem->OpenExplicitFileRead(sys_path.c_str() )) {
        int count = 0;
        bool value_bool;
        int value_int;
        idStr value_str;

        f->ReadInt(count);
        if (count > 50) {
            printf("Wrong settings file, looks like from older version.\n");
            return;
        }
        for (int i = 0; i < count; i++) {
            idStr name;
            int type = SettingsValue::Type::None;
            f->ReadString(name);
            f->ReadInt(type);
            switch(type) {
                case SettingsValue::Type::Option:
                    f->ReadBool(value_bool);
                    set_setting_option(name, value_bool);
                    printf("Read %s : %s\n", name.c_str(), value_bool ? "true" : "false" );
                    break;
                case SettingsValue::Type::Boolean:
                    f->ReadBool(value_bool);
                    set_setting_bool(name, value_bool);
                    printf("Read %s : %s\n", name.c_str(), value_bool ? "true" : "false" );
                    break;
                case SettingsValue::Type::String:
                    f->ReadString(value_str);
                    if (auto s = get_setting(name)) {
                        if (!s->save) {
                            printf("Skip %s : %s\n", name.c_str(), value_str.c_str() );
                            continue;
                        }
                    }
                    set_setting_string(name, value_str);
                    printf("Read %s : %s\n", name.c_str(), value_str.c_str() );
                    break;
                case SettingsValue::Type::Integer:
                    f->ReadInt(value_int);
                    set_setting_int(name, value_int);
                    printf("Read %s : %i\n", name.c_str(), value_int );
                    break;
            }
        }
        delete f; // close file 
    }
}

int Launcher::render() 
{
    int result = Launcher::Status::Ok;
    bool show_demo_window = true;
    bool done = false;
    int finger_id = -1; // use just first finger for ImGui events
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
    ImGuiIO& io = ImGui::GetIO();
    int argc = 0;
    QCoreApplication app(argc, nullptr);

    #ifdef __EMSCRIPTEN__
    // For an Emscripten build we are disabling file-system access, so let's not attempt to do a fopen() of the imgui.ini file.
    // You may manually call LoadIniSettingsFromMemory() to load settings from your own storage.
    io.IniFilename = NULL;
    EMSCRIPTEN_MAINLOOP_BEGIN
#else
    ImVec2 mouse_pos;
    while (!done)
#endif
    {
        app.processEvents(QEventLoop::AllEvents, 1);
        // Poll and handle events (inputs, window resize, etc.)
        // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
        // - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
        // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
        // Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) {
                result = Launcher::Status::Quit;
                done = true;
            } else if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE && event.window.windowID == SDL_GetWindowID(d_ptr->window))
                done = true;
            else if (event.type == SDL_FINGERMOTION) {
                if(finger_id != event.tfinger.fingerId)
                    continue;
                mouse_pos = ImVec2((float)event.tfinger.x * io.DisplaySize.x, (float)event.tfinger.y * io.DisplaySize.y);
                io.AddMousePosEvent(mouse_pos.x, mouse_pos.y);
            } else if (event.type == SDL_FINGERDOWN || event.type == SDL_FINGERUP) {
                if (finger_id == -1 && event.type == SDL_FINGERDOWN) {
                    finger_id = event.tfinger.fingerId;
                } else if(finger_id == event.tfinger.fingerId) {
                    finger_id = -1;
                } else {
                    continue;
                }
                // mouse_pos = ImVec2((float)event.tfinger.x * io.DisplaySize.x, (float)event.tfinger.y * io.DisplaySize.y);
                // io.AddMousePosEvent(mouse_pos.x, mouse_pos.y);
                // io.AddMouseButtonEvent(0, (event.type == SDL_FINGERDOWN));
            }
        }

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        // Render UI
        {
            ImGui::SetNextWindowPos(ImVec2(0,0));
            ImGui::SetNextWindowSize(io.DisplaySize);
            ImGui::Begin("Doom3 Startup settings", &show_demo_window, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_HorizontalScrollbar);
            d_ptr->render_tabBar(done);
            switch(d_ptr->tabIndex) {
            case 0:
                d_ptr->render_tab0(done);
                break;
            case 1:
                d_ptr->render_tab1(done);
                break;
            case 2:
                d_ptr->render_tab2_about(done);
                break;
            default: 
                ImGui::End();
            }
        }

        // Rendering
        ImGui::Render();
        glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
        glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(d_ptr->window);
    }
#ifdef __EMSCRIPTEN__
    EMSCRIPTEN_MAINLOOP_END;
#endif

    ImGui::GetStyle().ScaleAllSizes(1.0 / imgui_scale_factor);
    io.FontGlobalScale = 1.0f;

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DeleteContext(d_ptr->gl_context);
    SDL_DestroyWindow(d_ptr->window);

    // savesettings 
    if (d_ptr->settings_changed) {
        d_ptr->save_settings();
    }
    // create args for settings
    for (auto setting : d_ptr->settings) 
    {
        if (setting->type == SettingsValue::Type::Option) {
            if (setting->bool_value)
                d_ptr->argv.push_back(d_ptr->get_str(setting->name.c_str()));
            else
                continue;
        } else if (setting->type == SettingsValue::Type::Integer) {
            d_ptr->argv.push_back("+set");
            d_ptr->argv.push_back(setting->name.c_str());
            d_ptr->argv.push_back(d_ptr->get_num(setting->int_value));
        } else if (setting->type == SettingsValue::Type::Boolean) {
            d_ptr->argv.push_back("+set");
            d_ptr->argv.push_back(setting->name.c_str());
            d_ptr->argv.push_back(d_ptr->get_num(setting->bool_value ? SettingsValue::Enable : SettingsValue::Disable));
        } else if (setting->type == SettingsValue::Type::String) {
            d_ptr->argv.push_back("+set");
            d_ptr->argv.push_back(setting->name.c_str());
            d_ptr->argv.push_back(setting->str_value->c_str());
        }
    }
    return result;
}
