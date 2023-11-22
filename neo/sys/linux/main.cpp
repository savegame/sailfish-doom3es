/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company.

This file is part of the Doom 3 GPL Source Code ("Doom 3 Source Code").

Doom 3 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>

#include "sys/platform.h"
#include "framework/Licensee.h"
#include "framework/FileSystem.h"
#include "sys/posix/posix_public.h"
#include "sys/sys_local.h"

#include <locale.h>

#ifdef IMGUI_TOUCHSCREEN
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"
#include "framework/Session_local.h"
#include "imfilebrowser.h"
#include <SDL.h>
#else
#include <SDL_main.h>
#endif

#undef snprintf // no, I don't want to use idStr::snPrintf() here.

// lots of code following to get the current executable dir, taken from Yamagi Quake II
// and actually based on DG_Snippets.h

#if defined(__linux) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
#include <unistd.h> // readlink(), amongst others
#endif

#if defined(__FreeBSD__) || defined(__NetBSD__) || defined(__DragonFly__)
#include <sys/sysctl.h> // for sysctl() to get path to executable
#endif

#ifdef _WIN32
#include <windows.h> // GetModuleFileNameA()
#endif

#ifdef __APPLE__
#include <mach-o/dyld.h> // _NSGetExecutablePath
#endif

#ifdef __HAIKU__
#include <FindDirectory.h>
#endif

#ifndef PATH_MAX
// this is mostly for windows. windows has a MAX_PATH = 260 #define, but allows
// longer paths anyway.. this might not be the maximum allowed length, but is
// hopefully good enough for realistic usecases
#define PATH_MAX 4096
#endif

static char path_argv[PATH_MAX];
static char path_exe[PATH_MAX];
static char save_path[PATH_MAX];

const char* Posix_GetSavePath()
{
	return save_path;
}

static void SetSavePath()
{
	const char* s = getenv("XDG_DATA_HOME");
#ifdef SAILFISHOS
	if (s)
		D3_snprintfC99(save_path, sizeof(save_path), "%s/ru.sashikknox/doom3es", s);
	else
		D3_snprintfC99(save_path, sizeof(save_path), "%s/.local/share/ru.sashikknox/doom3es", getenv("HOME"));
#else
	if (s)
		D3_snprintfC99(save_path, sizeof(save_path), "%s/dhewm3", s);
	else
		D3_snprintfC99(save_path, sizeof(save_path), "%s/.local/share/dhewm3", getenv("HOME"));
#endif
}

const char* Posix_GetExePath()
{
	return path_exe;
}

static void SetExecutablePath(char* exePath)
{
	// !!! this assumes that exePath can hold PATH_MAX chars !!!

#ifdef _WIN32
	WCHAR wexePath[PATH_MAX];
	DWORD len;

	GetModuleFileNameW(NULL, wexePath, PATH_MAX);
	len = WideCharToMultiByte(CP_UTF8, 0, wexePath, -1, exePath, PATH_MAX, NULL, NULL);

	if(len <= 0 || len == PATH_MAX)
	{
		// an error occured, clear exe path
		exePath[0] = '\0';
	}

#elif defined(__linux)

	// all the platforms that have /proc/$pid/exe or similar that symlink the
	// real executable - basiscally Linux and the BSDs except for FreeBSD which
	// doesn't enable proc by default and has a sysctl() for this. OpenBSD once
	// had /proc but removed it for security reasons.
	char buf[PATH_MAX] = {0};
	snprintf(buf, sizeof(buf), "/proc/%d/exe", getpid());
	// readlink() doesn't null-terminate!
	int len = readlink(buf, exePath, PATH_MAX-1);
	if (len <= 0)
	{
		// an error occured, clear exe path
		exePath[0] = '\0';
	}
	else
	{
		exePath[len] = '\0';
	}

#elif defined(__FreeBSD__) || defined(__NetBSD__) || defined(__DragonFly__)

	// the sysctl should also work when /proc/ is not mounted (which seems to
	// be common on FreeBSD), so use it..
#if defined(__FreeBSD__) || defined(__DragonFly__)
	int name[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1};
#else
	int name[4] = {CTL_KERN, KERN_PROC_ARGS, -1, KERN_PROC_PATHNAME};
#endif
	size_t len = PATH_MAX-1;
	int ret = sysctl(name, sizeof(name)/sizeof(name[0]), exePath, &len, NULL, 0);
	if(ret != 0)
	{
		// an error occured, clear exe path
		exePath[0] = '\0';
	}

#elif defined(__APPLE__)

	uint32_t bufSize = PATH_MAX;
	if(_NSGetExecutablePath(exePath, &bufSize) != 0)
	{
		// WTF, PATH_MAX is not enough to hold the path?
		// an error occured, clear exe path
		exePath[0] = '\0';
	}

	// TODO: realpath() ?
	// TODO: no idea what this is if the executable is in an app bundle
#elif defined(__HAIKU__)
	if (find_path(B_APP_IMAGE_SYMBOL, B_FIND_PATH_IMAGE_PATH, NULL, exePath, PATH_MAX) != B_OK)
	{
		exePath[0] = '\0';
	}

#else

	// Several platforms (for example OpenBSD) don't provide a
	// reliable way to determine the executable path. Just return
	// an empty string.
	exePath[0] = '\0';

// feel free to add implementation for your platform and send a pull request.
#warning "SetExecutablePath() is unimplemented on this platform"

#endif
}

bool Sys_GetPath(sysPath_t type, idStr &path) {
	const char *s;
	char buf[MAX_OSPATH];
	struct stat st;

	path.Clear();

	switch(type) {
	case PATH_BASE:
		if (stat(BUILD_DATADIR, &st) != -1 && S_ISDIR(st.st_mode)) {
			path = BUILD_DATADIR;
			return true;
		}

		common->Warning("base path '" BUILD_DATADIR "' does not exist");

		// try next to the executable..
		if (Sys_GetPath(PATH_EXE, path)) {
			path = path.StripFilename();
			// the path should have a base dir in it, otherwise it probably just contains the executable
			idStr testPath = path + "/" BASE_GAMEDIR;
			if (stat(testPath.c_str(), &st) != -1 && S_ISDIR(st.st_mode)) {
				common->Warning("using path of executable: %s", path.c_str());
				return true;
			} else {
				idStr testPath = path + "/demo/demo00.pk4";
				if(stat(testPath.c_str(), &st) != -1 && S_ISREG(st.st_mode)) {
					common->Warning("using path of executable (seems to contain demo game data): %s", path.c_str());
					return true;
				} else {
					path.Clear();
				}
			}
		}

		// fallback to vanilla doom3 install
		if (stat(LINUX_DEFAULT_PATH, &st) != -1 && S_ISDIR(st.st_mode)) {
			common->Warning("using hardcoded default base path: " LINUX_DEFAULT_PATH);

			path = LINUX_DEFAULT_PATH;
			return true;
		}

		return false;

	case PATH_CONFIG:
		s = getenv("XDG_CONFIG_HOME");
#ifdef SAILFISHOS
		if (s)
			idStr::snPrintf(buf, sizeof(buf), "%s/ru.sashikknox/doom3es", s);
		else // TODO: should be orgname plus appname ru.sashikknox/doom3es
			idStr::snPrintf(buf, sizeof(buf), "%s/.config/ru.sashikknox/doom3es", getenv("HOME"));
#else
		if (s)
			idStr::snPrintf(buf, sizeof(buf), "%s/dhewm3", s);
		else
			idStr::snPrintf(buf, sizeof(buf), "%s/.config/dhewm3", getenv("HOME"));
#endif

		path = buf;
		return true;

	case PATH_SAVE:
		if(save_path[0] != '\0') {
			path = save_path;
			return true;
		}
		return false;

	case PATH_EXE:
		if (path_exe[0] != '\0') {
			path = path_exe;
			return true;
		}

		return false;
	}

	return false;
}

/*
===============
Sys_Shutdown
===============
*/
void Sys_Shutdown( void ) {
	Posix_Shutdown();
}

/*
================
Sys_GetSystemRam
returns in megabytes
================
*/
int Sys_GetSystemRam( void ) {
	long	count, page_size;
	int		mb;

	count = sysconf( _SC_PHYS_PAGES );
	if ( count == -1 ) {
		common->Printf( "GetSystemRam: sysconf _SC_PHYS_PAGES failed\n" );
		return 512;
	}
	page_size = sysconf( _SC_PAGE_SIZE );
	if ( page_size == -1 ) {
		common->Printf( "GetSystemRam: sysconf _SC_PAGE_SIZE failed\n" );
		return 512;
	}
	mb= (int)( (double)count * (double)page_size / ( 1024 * 1024 ) );
	// round to the nearest 16Mb
	mb = ( mb + 8 ) & ~15;
	return mb;
}

/*
==================
Sys_DoStartProcess
if we don't fork, this function never returns
the no-fork lets you keep the terminal when you're about to spawn an installer

if the command contains spaces, system() is used. Otherwise the more straightforward execl ( system() blows though )
==================
*/
void Sys_DoStartProcess( const char *exeName, bool dofork ) {
	bool use_system = false;
	if ( strchr( exeName, ' ' ) ) {
		use_system = true;
	} else {
		// set exec rights when it's about a single file to execute
		struct stat buf;
		if ( stat( exeName, &buf ) == -1 ) {
			printf( "stat %s failed: %s\n", exeName, strerror( errno ) );
		} else {
			if ( chmod( exeName, buf.st_mode | S_IXUSR ) == -1 ) {
				printf( "cmod +x %s failed: %s\n", exeName, strerror( errno ) );
			}
		}
	}
	if ( dofork ) {
		switch ( fork() ) {
		case -1:
			printf( "fork failed: %s\n", strerror( errno ) );
			break;
		case 0:
			if ( use_system ) {
				printf( "system %s\n", exeName );
				if (system( exeName ) == -1)
					printf( "system failed: %s\n", strerror( errno ) );
				_exit( 0 );
			} else {
				printf( "execl %s\n", exeName );
				execl( exeName, exeName, NULL );
				printf( "execl failed: %s\n", strerror( errno ) );
				_exit( -1 );
			}
			break;
		default:
			break;
		}
	} else {
		if ( use_system ) {
			printf( "system %s\n", exeName );
			if (system( exeName ) == -1)
				printf( "system failed: %s\n", strerror( errno ) );
			else
				sleep( 1 );	// on some systems I've seen that starting the new process and exiting this one should not be too close
		} else {
			printf( "execl %s\n", exeName );
			execl( exeName, exeName, NULL );
			printf( "execl failed: %s\n", strerror( errno ) );
		}
		// terminate
		_exit( 0 );
	}
}

/*
=================
Sys_OpenURL
=================
*/
void idSysLocal::OpenURL( const char *url, bool quit ) {
	const char	*script_path;
	idFile		*script_file;
	char		cmdline[ 1024 ];

	static bool	quit_spamguard = false;

	if ( quit_spamguard ) {
		common->DPrintf( "Sys_OpenURL: already in a doexit sequence, ignoring %s\n", url );
		return;
	}

	common->Printf( "Open URL: %s\n", url );
	// opening an URL on *nix can mean a lot of things ..
	// just spawn a script instead of deciding for the user :-)

	// look in the savepath first, then in the basepath
	script_path = fileSystem->BuildOSPath( cvarSystem->GetCVarString( "fs_savepath" ), "", "openurl.sh" );
	script_file = fileSystem->OpenExplicitFileRead( script_path );
	if ( !script_file ) {
		script_path = fileSystem->BuildOSPath( cvarSystem->GetCVarString( "fs_basepath" ), "", "openurl.sh" );
		script_file = fileSystem->OpenExplicitFileRead( script_path );
	}
	if ( !script_file ) {
		common->Printf( "Can't find URL script 'openurl.sh' in either savepath or basepath\n" );
		common->Printf( "OpenURL '%s' failed\n", url );
		return;
	}
	fileSystem->CloseFile( script_file );

	// if we are going to quit, only accept a single URL before quitting and spawning the script
	if ( quit ) {
		quit_spamguard = true;
	}

	common->Printf( "URL script: %s\n", script_path );

	// StartProcess is going to execute a system() call with that - hence the &
	idStr::snPrintf( cmdline, 1024, "%s '%s' &",  script_path, url );
	sys->StartProcess( cmdline, quit );
}

/*
===============
main
===============
*/
int main(int argc, char **argv) {
	// Prevent running Doom 3 as root
	// Borrowed from Yamagi Quake II
	if (getuid() == 0) {
		printf("Doom 3 shouldn't be run as root! Backing out to save your ass. If\n");
		printf("you really know what you're doing, edit neo/sys/linux/main.cpp and remove\n");
		printf("this check. But don't complain if an imp kills your bunny afterwards!:)\n");

		return 1;
	}
	// fallback path to the binary for systems without /proc
	// while not 100% reliable, its good enough
	if (argc > 0) {
		if (!realpath(argv[0], path_argv))
			path_argv[0] = 0;
	} else {
		path_argv[0] = 0;
	}

	SetExecutablePath(path_exe);
	if (path_exe[0] == '\0') {
		memcpy(path_exe, path_argv, sizeof(path_exe));
	}

	SetSavePath();

	// some ladspa-plugins (that may be indirectly loaded by doom3 if they're
	// used by alsa) call setlocale(LC_ALL, ""); This sets LC_ALL to $LANG or
	// $LC_ALL which usually is not "C" and will fuck up scanf, strtod
	// etc when using a locale that uses ',' as a float radix.
	// so set $LC_ALL to "C".
	setenv("LC_ALL", "C", 1);

	Posix_InitSignalHandlers();

#ifdef IMGUI_TOUCHSCREEN
	{
		// here we need init IMGUI ui
		// Setup SDL
		if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0)
		{
			printf("Error: %s\n", SDL_GetError());
			return -1;
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

		// Create window with graphics context
		SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
		SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
		SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
		SDL_DisplayMode dm;
		SDL_GetCurrentDisplayMode(0,&dm);
		int w = dm.w < dm.h ? dm.w : dm.h;
		int h = dm.w > dm.h ? dm.w : dm.h;
		SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
		#ifdef SAILFISHOS
		SDL_Window* window = SDL_CreateWindow("Dear ImGui SDL2+OpenGL3 example", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, window_flags);
		#else
		SDL_Window* window = SDL_CreateWindow("Dear ImGui SDL2+OpenGL3 example", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 640, 480, window_flags);
		#endif
		SDL_GLContext gl_context = SDL_GL_CreateContext(window);
		SDL_GL_MakeCurrent(window, gl_context);
		SDL_GL_SetSwapInterval(1); // Enable vsync

		// Setup Dear ImGui context
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO(); (void)io;
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

		// Setup Dear ImGui style
		ImGui::StyleColorsDark();
		//ImGui::StyleColorsLight();

		// Setup Platform/Renderer backends
		ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
		ImGui_ImplOpenGL3_Init(glsl_version);

		ImGuiStyle * style = &ImGui::GetStyle();
		#ifdef SAILFISHOS
		float imgui_scale_factor = dm.w / 220.0f;
		#else
		float imgui_scale_factor = 640 / 320.0f;
		#endif
		style->ScaleAllSizes(imgui_scale_factor);
		io.FontGlobalScale = imgui_scale_factor;

		// Our state
		bool show_demo_window = true;
		ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

		// Main loop
		// create a file browser instance
		ImGui::FileBrowser fileDialog(ImGuiFileBrowserFlags_SelectDirectory | ImGuiFileBrowserFlags_NoModal);
		int finger_id = -1; // use just first finger for ImGui events
		bool done = false;
		bool no_basepath = true;
		int argc_copy;
		const int argc_add = 3;
		char** argv_copy = NULL;
		char *fs_basepath_ptr = NULL;
		// gui settings
		const int button_height = 160;
		bool settings_changed = false;
		// check args 
		for (int i = 1; i < argc; i++) {
			if (strcmp("fs_basepath", argv[i]) == 0) {
				no_basepath = false;
				i++;
				fs_basepath_ptr = argv[i];
				break;
			}
		}

		if (no_basepath) {
			argc_copy = argc + argc_add;
			argv_copy = (char**)malloc(argc_copy * sizeof(char*));
			memset(argv_copy, 0, argc_copy * sizeof(char*));
			for (int i = 0; i < argc; i++) {
				argv_copy[i] = argv[i];
			}
			argv_copy[argc] = "+set\0";
			argv_copy[argc + 1] = "fs_basepath\0";
			fs_basepath_ptr = argv_copy[argc + 2];
		}

		idStr sys_path; 
		Sys_GetPath(PATH_CONFIG,sys_path);
		sys_path += "/imgui_settings.cfg";
		// Try load settings file 
		if (idFile *f = fileSystem->OpenExplicitFileRead(sys_path.c_str() )) {
			idStr tmp_string;

			f->ReadString(tmp_string);
			
			if (!tmp_string.IsEmpty()) {
				argv_copy[argc + 2] = (char*)malloc(tmp_string.Length() + 1);
				// memccpy(argv_copy[argc + 2], tmp_string.c_str(), tmp_string.Length(), sizeof(char));
				strcpy(argv_copy[argc + 2], tmp_string.c_str());
				argv_copy[argc + 2][tmp_string.Length()] = '\0';
				fs_basepath_ptr = argv_copy[argc + 2];
			}

			delete f; // close file 
		}

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
			// Poll and handle events (inputs, window resize, etc.)
			// You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
			// - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
			// - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
			// Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
			SDL_Event event;
			while (SDL_PollEvent(&event))
			{
				ImGui_ImplSDL2_ProcessEvent(&event);
				if (event.type == SDL_QUIT)
					return 0;
				else if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE && event.window.windowID == SDL_GetWindowID(window))
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

			// 2. Show a simple window that we create ourselves. We use a Begin/End pair to create a named window.
			{
				static float f = 0.0f;
				static int counter = 0;

				ImGui::Begin("Doom3 Startup settings", &show_demo_window, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration);
				ImGui::SetWindowPos(ImVec2(0,0));
				ImGui::SetWindowSize(io.DisplaySize);
				ImGui::Text("Doom 3 path: %s", fs_basepath_ptr ? fs_basepath_ptr : "null");               // Display some text (you can use a format strings too)

				
				if (ImGui::Button("Open path", ImVec2(io.DisplaySize.x - ImGui::GetStyle().WindowPadding.x*2, button_height))) {
					fileDialog.Open();
				}


				if (fs_basepath_ptr && ImGui::Button("Continue to Doom 3", ImVec2(io.DisplaySize.x - ImGui::GetStyle().WindowPadding.x*2, button_height)))
					done = true;
				
				ImGui::End();

				fileDialog.SetWindowPos(0,0);
				fileDialog.SetWindowSize(io.DisplaySize);

				if (no_basepath) {
					fileDialog.Display();
				
					if (fileDialog.HasSelected() && fileDialog.GetSelected().string().size() > 0) {
						argv_copy[argc + 2] = (char*)malloc(fileDialog.GetSelected().string().size() + 1);
						fs_basepath_ptr = argv_copy[argc + 2];
						memcpy(argv_copy[argc + 2], fileDialog.GetSelected().string().c_str(), fileDialog.GetSelected().string().size());
						argv_copy[argc + 2][fileDialog.GetSelected().string().size()] = '\0';
						fileDialog.ClearSelected();
						no_basepath = false;
						settings_changed = true;
					}
				}
			}

			// Rendering
			ImGui::Render();
			glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
			glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w);
			glClear(GL_COLOR_BUFFER_BIT);
			ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
			SDL_GL_SwapWindow(window);
		}
	#ifdef __EMSCRIPTEN__
		EMSCRIPTEN_MAINLOOP_END;
	#endif

		style->ScaleAllSizes(1.0 / imgui_scale_factor);
		io.FontGlobalScale = 1.0f;

		// Cleanup
		ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplSDL2_Shutdown();
		ImGui::DestroyContext();

		SDL_GL_DeleteContext(gl_context);
		SDL_DestroyWindow(window);

		// savesettings 
		if (settings_changed) {
			if (idFile *f = fileSystem->OpenExplicitFileWrite(sys_path.c_str())) {
				f->WriteString(fs_basepath_ptr);
				delete f; // close file
			}
		}

		if ( argc > 1 ) {
			if (no_basepath) {
				common->Init( argc_copy-1, &argv_copy[1] );
				// TODO: free string allocated when choose fs_basepath folder
				if(argv_copy[argc + 2])
					free(argv_copy[argc + 2]);
				free(argv_copy);
			} else {
				common->Init( argc-1, &argv[1] );
			}
		} else {
			common->Init( 0, NULL );
		}
	}
#else
	if ( argc > 1 ) {
		common->Init( argc-1, &argv[1] );
	} else {
		common->Init( 0, NULL );
	}
#endif

	while (1) {
		common->Frame();
	}
	return 0;
}
