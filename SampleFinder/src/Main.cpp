#include <stdlib.h>

#include <iostream>
#include <string>
#include <filesystem>
#include <fstream>
#include <streambuf>
#include <exception>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define VC_EXTRALEAN
#include <Windows.h>
#endif

#include <spdlog/fmt/fmt.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>

#define GLEW_STATIC
#include <GL/glew.h>

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_sdl.h>
#include <imgui_impl_opengl3.h>
#include <misc/cpp/imgui_stdlib.h>

#include <sndfile.h>

#include <nlohmann/json.hpp>

#include "SampleFinder.h"

namespace
{
	int ups = 0;
	int fps = 0;

	template <typename T>
	void InitErr(const std::string& msg, T lib_err)
	{
		std::string msgbox_title = "SampleFinder initialization error";
		std::string msgbox_error = fmt::format("{}:\n{}", msg, lib_err);

		std::cerr << msg << ": " << lib_err << std::endl;

#ifdef _WIN32
		// SDL's message box doesn't look the best on Win32 so we'll use their own
		MessageBoxA(NULL, msgbox_error.c_str(), msgbox_title.c_str(), MB_OK | MB_ICONERROR);
#else
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, msgbox_title.c_str(), msgbox_error.c_str(), nullptr);
#endif

		exit(EXIT_FAILURE);
	}
	
	bool UnsavedDialog()
	{
		const SDL_MessageBoxButtonData buttons[] = {
			{SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 0, "Continue"},
			{SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 1, "Cancel"}
		};
		const SDL_MessageBoxData data = {
			SDL_MESSAGEBOX_WARNING,
			NULL,
			"Exit SampleFinder?",
			"Changes you made may not be saved.",
			SDL_arraysize(buttons),
			buttons,
			NULL
		};

		int button_id;
		SDL_ShowMessageBox(&data, &button_id);

		return button_id != 0;
	}

	void ApplyImGuiTheme()
	{
		// The "KP3D" theme is actually based off of the dark ImGui theme. So let's set that.
		ImGui::StyleColorsDark();
		ImGui::GetStyle().Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.5f);

		// Now fix it up
		ImGuiStyle& style = ImGui::GetStyle();
		style.WindowRounding = 0.0f;
		style.ChildRounding = 0.0f;
		style.FrameRounding = 0.0f;
		style.GrabRounding = 0.0f;
		style.PopupRounding = 0.0f;
		style.ScrollbarRounding = 0.0f;
		style.TabRounding = 0.0f;
		style.AntiAliasedFill = false;
		style.AntiAliasedLines = false;
		style.WindowTitleAlign.x = 0.5f;
		style.WindowPadding = { 10, 10 };
		style.FramePadding = { 4, 3 };
		style.ItemSpacing = { 8, 4 };
		style.ItemInnerSpacing = { 4, 4 };
		style.TouchExtraPadding = { 0, 0 };
		style.IndentSpacing = 21.0f;
		style.ScrollbarSize = 14.0f;
		style.GrabMinSize = 10.0f;
		style.WindowBorderSize = 1.0f;
		style.ChildBorderSize = 1.0f;
		style.PopupBorderSize = 1.0f;
		style.FrameBorderSize = 1.0f;
		style.TabBorderSize = 1.0f;

		ImVec4* colors = style.Colors;
		colors[ImGuiCol_FrameBg] = ImVec4(0.17f, 0.17f, 0.17f, 0.54f);
		colors[ImGuiCol_FrameBgHovered] = ImVec4(0.98f, 0.26f, 0.26f, 0.40f);
		colors[ImGuiCol_FrameBgActive] = ImVec4(0.98f, 0.26f, 0.26f, 0.67f);
		colors[ImGuiCol_TitleBgActive] = ImVec4(0.48f, 0.16f, 0.16f, 1.00f);
		colors[ImGuiCol_CheckMark] = ImVec4(0.57f, 0.57f, 0.57f, 1.00f);
		colors[ImGuiCol_SliderGrab] = ImVec4(0.36f, 0.36f, 0.36f, 1.00f);
		colors[ImGuiCol_SliderGrabActive] = ImVec4(0.57f, 0.57f, 0.57f, 1.00f);
		colors[ImGuiCol_Button] = ImVec4(0.98f, 0.26f, 0.26f, 0.40f);
		colors[ImGuiCol_ButtonHovered] = ImVec4(0.98f, 0.26f, 0.26f, 1.00f);
		colors[ImGuiCol_ButtonActive] = ImVec4(0.80f, 0.14f, 0.18f, 1.00f);
		colors[ImGuiCol_Header] = ImVec4(0.98f, 0.26f, 0.26f, 0.31f);
		colors[ImGuiCol_HeaderHovered] = ImVec4(0.98f, 0.26f, 0.26f, 0.80f);
		colors[ImGuiCol_HeaderActive] = ImVec4(0.98f, 0.26f, 0.26f, 1.00f);
		colors[ImGuiCol_Separator] = ImVec4(1.00f, 1.00f, 1.00f, 0.25f);
		colors[ImGuiCol_SeparatorHovered] = ImVec4(0.88f, 0.36f, 1.00f, 1.00f);
		colors[ImGuiCol_SeparatorActive] = ImVec4(0.92f, 0.58f, 1.00f, 1.00f);
		colors[ImGuiCol_ResizeGrip] = ImVec4(0.49f, 0.49f, 0.49f, 0.00f);
		colors[ImGuiCol_ResizeGripHovered] = ImVec4(1.00f, 1.00f, 1.00f, 0.10f);
		colors[ImGuiCol_ResizeGripActive] = ImVec4(0.88f, 0.36f, 1.00f, 1.00f);
		colors[ImGuiCol_Tab] = ImVec4(0.58f, 0.18f, 0.18f, 0.86f);
		colors[ImGuiCol_TabHovered] = ImVec4(0.98f, 0.26f, 0.26f, 0.80f);
		colors[ImGuiCol_TabActive] = ImVec4(0.73f, 0.25f, 0.25f, 1.00f);
		colors[ImGuiCol_TabUnfocused] = ImVec4(0.15f, 0.07f, 0.07f, 0.97f);
		colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.42f, 0.14f, 0.14f, 1.00f);
		colors[ImGuiCol_DockingPreview] = ImVec4(0.98f, 0.26f, 0.26f, 0.70f);
		colors[ImGuiCol_TextSelectedBg] = ImVec4(0.88f / 2.0f, 0.36f / 2.0f, 1.00f / 2.0f, 0.35f);
		colors[ImGuiCol_NavHighlight] = ImVec4(0.98f, 0.26f, 0.26f, 1.00f);
		colors[ImGuiCol_PlotHistogram] = colors[ImGuiCol_ResizeGripActive]; // Progress bar
	}

	void Update(finder::UI& ui)
	{
		ui.Update();
	}

	void Render(finder::UI& ui)
	{
		glClearColor(0.3f, 0.3f, 0.3f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);

		ui.Render();
	}
}

namespace finder
{
	Settings settings;

	void LoadDefaults(Settings& settings)
	{
		settings.default_fan_value = 15;
		settings.min_hash_time_delta = 0;
		settings.max_hash_time_delta = 200;
		settings.fingerprint_reduction = 20;
		settings.peak_neighborhood_size = 20;
		settings.default_amp_min = -48.0f;
		settings.default_window_size = 4096;
		settings.default_overlap_ratio = 0.5f;
		settings.fs = 22050.0f;
		settings.demote_songs = true;
		settings.demotion_factor = 2.0f;
	}

	ErrCode LoadSettings(const std::string& path, Settings& settings)
	{
		std::string json_str;

		if (LoadTextFile(path, json_str) == FAILURE)
		{
			std::cerr << "Reverting to default settings..." << std::endl;
			LoadDefaults(settings);
			return FAILURE;
		}

		nlohmann::json json = nlohmann::json::parse(json_str);
		settings.default_fan_value = json["default_fan_value"];
		settings.min_hash_time_delta = json["min_hash_time_delta"];
		settings.max_hash_time_delta = json["max_hash_time_delta"];
		settings.fingerprint_reduction = json["fingerprint_reduction"];
		settings.peak_neighborhood_size = json["peak_neighborhood_size"];
		settings.default_window_size = json["default_window_size"];
		settings.default_amp_min = json["default_amp_min"];
		settings.default_overlap_ratio = json["default_overlap_ratio"];
		settings.fs = json["fs"];
		settings.demote_songs = json["demote_songs"];
		settings.demotion_factor = json["demotion_factor"];

		return SUCCESS;
	}

	ErrCode SaveSettings(const std::string& path, const Settings& settings)
	{
		std::string json_str;

		nlohmann::json json;
		json["default_fan_value"] = settings.default_fan_value;
		json["min_hash_time_delta"] = settings.min_hash_time_delta;
		json["max_hash_time_delta"] = settings.max_hash_time_delta;
		json["fingerprint_reduction"] = settings.fingerprint_reduction;
		json["peak_neighborhood_size"] = settings.peak_neighborhood_size;
		json["default_window_size"] = settings.default_window_size;
		json["default_amp_min"] = settings.default_amp_min;
		json["default_overlap_ratio"] = settings.default_overlap_ratio;
		json["fs"] = settings.fs;
		json["demote_songs"] = settings.demote_songs;
		json["demotion_factor"] = settings.demotion_factor;

		json_str = json.dump();

		if (SaveTextFile(path, json_str) == FAILURE)
			return FAILURE;

		return SUCCESS;
	}
}

int main(int argc, char* argv[])
{
	// Set up SDL
	std::cout << "Initializing SDL..." << std::endl;
	if (SDL_Init(SDL_INIT_EVERYTHING))
		InitErr("Failed to initialize SDL", SDL_GetError());

	int flags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL;
	SDL_Window* window = SDL_CreateWindow("SampleFinder", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, flags);
	SDL_SetWindowMinimumSize(window, 800, 600);

	// Custom window icon (important for swag points)
	finder::Bitmap icon;
	icon.Load("assets/icon.png");
	SDL_Surface* surface = SDL_CreateRGBSurfaceFrom(
		icon.px,
		icon.width, icon.height,
		32,
		icon.width * 4,
		0xFF << 16, 0xFF << 8, 0xFF, 0xFF << 24
	);
	SDL_SetWindowIcon(window, surface);
	SDL_FreeSurface(surface);

	// Set up SDL_mixer
	std::cout << "Initializing SDL_mixer..." << std::endl;
	int mix_flags = MIX_INIT_OGG | MIX_INIT_MP3;
	int truc = 0;
	if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 1024))
		InitErr("Failed to set up SDL_mixer", Mix_GetError());
	if (mix_flags != (truc = Mix_Init(mix_flags)))
		InitErr("Failed to initialize SDL_mixer", Mix_GetError());
	Mix_AllocateChannels(32);

	// Set up OpenGL
	SDL_GLContext gl_context = SDL_GL_CreateContext(window);
	SDL_GL_MakeCurrent(window, gl_context);
	SDL_GL_SetSwapInterval(1);

	// Set up GLEW (OpenGL extensions)
	std::cout << "Initializing GLEW..." << std::endl;
	glewExperimental = GL_TRUE;
	if (GLenum status = glewInit(); status != GLEW_OK)
		InitErr("Failed to initialize GLEW", glewGetErrorString(status));

	// Set up Dear ImGui
	std::cout << "Initializing Dear ImGui..." << std::endl;
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::GetIO().IniFilename = NULL;
	ImGui::GetIO().Fonts->AddFontDefault();
	ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
	ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	ImGui::StyleColorsDark();
	ImGui::GetStyle().Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.5f);
	ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
	ImGui_ImplOpenGL3_Init("#version 330 core");
	ApplyImGuiTheme();

	// Load our settings and run SampleFinder
	finder::LoadSettings("./settings.json", finder::settings);

	{
		finder::UI ui;

		bool running = true;

		int frames = 0;
		int updates = 0;

		double previous = -1.0;
		double last_stats_time = 0.0;
		double lag = 0.0;

		float frame_time = 1.0f / 60.0f;

		while (running)
		{
			for (SDL_Event e; SDL_PollEvent(&e); )
			{
				ImGui_ImplSDL2_ProcessEvent(&e);

				switch (e.type)
				{
				case SDL_QUIT:
					if (!UnsavedDialog())
						running = false;
					break;
				}
			}

			ImGui_ImplOpenGL3_NewFrame();
			ImGui_ImplSDL2_NewFrame(window);
			ImGui::NewFrame();

			// Begin frame
			double now = SDL_GetTicks() / 1000.0;
			if (previous == -1)
				previous = now;

			double delta = now - previous;
			if (delta >= 1.0)
				delta = frame_time;

			lag += delta;

			// Update
			while (lag >= frame_time)
			{
				updates++;
				Update(ui);
				lag -= frame_time;
			}

			// Render
			frames++;
			Render(ui);
			ImGui::Render();
			ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

			SDL_GL_SwapWindow(window);

			if (now - last_stats_time >= 1.0)
			{
				ups = updates;
				fps = frames;

				updates = frames = 0;
				last_stats_time = now;
			}

			previous = now;
		}
	}

	finder::SaveSettings("./settings.json", finder::settings);

	Mix_CloseAudio();
	SDL_DestroyWindow(window);
	SDL_Quit();

	return 0;
}
