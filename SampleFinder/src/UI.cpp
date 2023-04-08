#include "SampleFinder.h"

#include <iostream>
#include <filesystem>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define VC_EXTRALEAN
#include <Windows.h>
#endif

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_sdl.h>
#include <imgui_impl_opengl3.h>
#include <misc/cpp/imgui_stdlib.h>

#include <nfd.h>

namespace
{
	constexpr const char* WIN_ID_OPTIONS = "Options";
	constexpr const char* WIN_ID_ABOUT = "About SampleFinder";
	constexpr const char* WIN_ID_LIBRARY_INFO = "Library Info";
	constexpr const char* WIN_ID_LIBRARY = "Library";
	constexpr const char* WIN_ID_MISSING_SAMPLE = "Missing Sample";
	constexpr const char* WIN_ID_MATCHES = "Matches";

	void* TexID(const finder::Texture& texture)
	{
		return reinterpret_cast<void*>(texture.GetGLID());
	}

	void ErrMsg(const std::string& msg, bool fatal = false)
	{
		std::string msgbox_title = "SampleFinder error";

		std::cerr << msg << std::endl;

#ifdef _WIN32
		// SDL's message box doesn't look the best on Win32 so we'll use their own
		MessageBoxA(NULL, msg.c_str(), msgbox_title.c_str(), MB_OK | MB_ICONERROR);
#else
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, msgbox_title.c_str(), msg.c_str(), nullptr);
#endif

		if (fatal)
			exit(EXIT_FAILURE);
	}

	std::string FormatTime(int time)
	{
		int seconds = time % 60;
		int minutes = time / 60;
		int hours = minutes / 60;

		std::string out;
		if (hours > 0)
			out += std::to_string(hours) + "h ";
		if (minutes > 0)
			out += std::to_string(minutes) + "m ";
		out += std::to_string(seconds) + "s ";

		return out;
	}
}

namespace finder
{
	UI::UI():
		m_di_init(false),
		m_show_about_window(false),
		m_show_settings_window(true),
		m_show_waveform(true),
		m_show_spectrogram(false)
	{
		// Placeholder image for the waveform/spectrogram preview
		Bitmap bmp(255, 255);
		for (int i = 0; i < bmp.width * bmp.height; i++)
			bmp.px[i] = (bmp.height - i / bmp.height) << 24;
		m_missing_waveform.Load(bmp);
		m_missing_spectral.Load(bmp);

		// Misc. assets
		m_peak.Load("assets/peak.png");
		m_logo.Load("assets/logo.png");
	}

	UI::~UI()
	{
	}

	void UI::Update()
	{
	}

	/*
	 * Messy DearImGui code
	 */
	void UI::Render()
	{
		// Dockspace initialization
		ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_PassthruCentralNode;
		ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
		ImGuiViewport* viewport = ImGui::GetMainViewport();
		float offset = 0;
		auto viewpos = viewport->Pos;
		viewpos.x += offset;
		auto viewsize = viewport->Size;
		viewsize.x -= offset;
		ImGui::SetNextWindowPos(viewpos);
		ImGui::SetNextWindowSize(viewsize);
		ImGui::SetNextWindowViewport(viewport->ID);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
		window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::SetNextWindowBgAlpha(0.0f);
		ImGui::Begin("_imgui_dockspace_window", nullptr, window_flags);
		ImGui::PopStyleVar();
		ImGui::PopStyleVar(2);
		ImGuiIO& io = ImGui::GetIO();
		ImGuiID dockspace_id = ImGui::GetID("_imgui_dockspace");
		ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
		if (!m_di_init)
		{
			ImGuiID right_id = ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Right, 0.75f, NULL, &dockspace_id);
			ImGuiID left_id = ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Left, 0.25f, NULL, &dockspace_id);
			ImGuiID missing_sample_dock_id = ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Up, 0.25f, NULL, &dockspace_id);
			ImGuiID options_dock_id = ImGui::DockBuilderSplitNode(left_id, ImGuiDir_Up, 0.45f, NULL, &left_id);
			ImGuiID downloads_dock_id = ImGui::DockBuilderSplitNode(left_id, ImGuiDir_Down, 0.55f, NULL, &left_id);

			ImGui::DockBuilderDockWindow(WIN_ID_OPTIONS, options_dock_id);
			ImGui::DockBuilderDockWindow(WIN_ID_LIBRARY, downloads_dock_id);
			ImGui::DockBuilderDockWindow(WIN_ID_MATCHES, dockspace_id);
			ImGui::DockBuilderDockWindow(WIN_ID_MISSING_SAMPLE, missing_sample_dock_id);

			ImGuiDockNode* node = ImGui::DockBuilderGetNode(missing_sample_dock_id);
			node->LocalFlags |= ImGuiDockNodeFlags_AutoHideTabBar;

			m_di_init = true;
		}
		ImGui::End();

		// Menu bar
		float menu_bar_height = 0.0f;
		if (ImGui::BeginMainMenuBar())
		{
			menu_bar_height = ImGui::GetWindowHeight();

			if (ImGui::BeginMenu("File"))
			{
				if (ImGui::MenuItem("Open Sample...", "Ctrl+O"))
					OpenSampleDialog();
				if (ImGui::MenuItem("Open Library...", "Ctrl+L"))
					OpenLibraryDialog();
				if (ImGui::MenuItem("Save Library", "Ctrl+S"))
					m_library.Save();
				ImGui::Separator();
				if (ImGui::MenuItem("Exit", "Alt+F4"))
					exit(EXIT_SUCCESS);
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("View"))
			{
				if (ImGui::MenuItem("Options..."))
					m_show_settings_window = true;
				if (ImGui::MenuItem("Library Info..."))
					m_show_library_stats = true;
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Help"))
			{
				if (ImGui::MenuItem("About SampleFinder"))
					m_show_about_window ^= 1;
				ImGui::EndMenu();
			}

			ImGui::EndMainMenuBar();
		}

		// Windows
		if (!m_library.loading)
		{
			RenderLibrary();
			RenderMissingSample();
			RenderMatches();
			if (m_show_about_window)
				RenderAboutScreen();
			if (m_show_library_stats)
				RenderLibraryStats();
			if (m_show_settings_window)
				RenderSettings();
		}

		// Loading screen
		if (m_library.loading)
		{
			if (!ImGui::IsPopupOpen("Loading"))
				ImGui::OpenPopup("Loading");
			RenderLoadingScreen();
		}
		else
		{
			if (ImGui::IsPopupOpen("Loading"))
				ImGui::CloseCurrentPopup();
		}
	}

	//
	
	void UI::OpenSampleDialog()
	{
		nfdchar_t* out_path = nullptr;
		nfdresult_t result = NFD_OpenDialog("wav,mp3", nullptr, &out_path);
		if (result == NFD_OKAY)
		{
			ReplaceSample(out_path);

			delete out_path;
		}
		else if (result == NFD_CANCEL)
		{
			// ...
		}
		else
		{
			ErrMsg(NFD_GetError());
		}
	}

	void UI::OpenLibraryDialog()
	{
		nfdchar_t* out_path = nullptr;
		nfdresult_t result = NFD_PickFolder(nullptr, &out_path);
		if (result == NFD_OKAY)
		{
			m_library_path = out_path;
			m_library.Load(m_library_path);

			delete out_path;
		}
		else if (result == NFD_CANCEL)
		{
			// ...
		}
		else
		{
			ErrMsg(NFD_GetError());
		}
	}

	void UI::ReplaceSample(const std::string& path)
	{
		if (m_missing.Load(path, true) != finder::SUCCESS)
			ErrMsg("Failed to load audio file");

		std::unique_ptr<Bitmap> waveform_bmp = m_missing.RenderWaveform();
		m_missing_waveform.Load(*waveform_bmp);

		Bitmap bmp(1024, 512);
		m_missing.Process(&bmp);
		m_missing_spectral.Load(bmp);
	}

	void UI::RenderLibrary()
	{
		if (ImGui::Begin(WIN_ID_LIBRARY))
		{
			ImGui::InputText("Path##library", &m_library_path);
			ImGui::SameLine();
			if (ImGui::Button("Browse##library"))
				OpenLibraryDialog();
			if (ImGui::Button("Save##library"))
				m_library.Save();
			ImGui::SameLine();
			if (ImGui::Button("Process##library"))
				m_library.Process();
			if (m_missing.loaded)
			{
				ImGui::SameLine();
				if (ImGui::Button("Scan##library"))
					m_library.TestSong(m_missing);
			}
			if (ImGui::BeginChild("##library_children"))
			{
				for (AudioFile& file: m_library.files)
				{
					std::string filename = std::filesystem::proximate(file.path, m_library_path).string();

					int c = file.processed;
					if (c)
						ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 255, 0, 255));
					ImGui::Text(filename.c_str());
					ImGui::PopStyleColor(c);
					ImGui::Separator();
				}
				ImGui::EndChild();
			}

			ImGui::End();
		}
	}

	void UI::RenderMissingSample()
	{
		if (ImGui::Begin(WIN_ID_MISSING_SAMPLE))
		{
			float titlebarsize = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2.0f;

			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 0, 0 });
			ImVec2 win_pos = ImGui::GetWindowPos();
			ImVec2 size = ImGui::GetWindowSize();
			ImVec2 pos = ImGui::GetCursorScreenPos();
			win_pos.x += 5;
			win_pos.y += titlebarsize / 3.0f;
			ImVec2 sp = win_pos;
			sp.x += size.x - 10;
			sp.y += size.y - 10;

			if (m_show_spectrogram)
			{
				// Draw spectrogram
				ImGui::GetWindowDrawList()->AddImage(
					TexID(m_missing_spectral),
					win_pos,
					sp,
					ImVec2(0, 0),
					ImVec2(1, 1)
				);

				// Draw the peak plots
				for (const std::pair<int, int>& p: m_missing.peaks)
				{
					int i = p.second;
					int j = p.first;

					float vi = (((float)i / (float)m_missing.dims[0]) * (sp.x - win_pos.x));
					float vj = (((float)j / (float)m_missing.dims[1]) * (sp.y - win_pos.y));

					vj = (sp.y - win_pos.y) - vj;

					float xx = (win_pos.x + vi) - m_peak.GetWidth() / 2.0f;
					float yy = (win_pos.y + vj) - m_peak.GetHeight() / 2.0f;

					ImGui::GetWindowDrawList()->AddImage(
						TexID(m_peak),
						{ xx, yy },
						{ xx + m_peak.GetWidth(), yy + m_peak.GetHeight() }
					);
				}
			}
			else
			{
				// Draw waveform
				ImGui::GetWindowDrawList()->AddImage(
					TexID(m_missing_waveform),
					win_pos,
					sp,
					ImVec2(0, 0),
					ImVec2(1, 1)
				);
			}
			ImGui::PopStyleVar();

			if (m_missing.loaded)
			{
				// Playhead
				float playback_line_pos = 0.0f;
				double mus_pos = Mix_GetMusicPosition(m_missing.sdl_object);
				double mus_len = Mix_MusicDuration(m_missing.sdl_object);
				playback_line_pos = (float)(mus_pos / mus_len) * size.x;
				ImGui::GetWindowDrawList()->AddLine(
					{
						win_pos.x + playback_line_pos,
						win_pos.y
					},
					{
						win_pos.x + playback_line_pos,
						sp.y
					},
					0xFF333333,
					2.0f
				);

				// UI elements
				bool playing = Mix_PlayingMusic() && !Mix_PausedMusic();
				const char* text = playing ? "Pause" : "Play";
				if (ImGui::Button(text))
				{
					if (playing)
						m_missing.Pause();
					else
						m_missing.Play();
				}

				ImGui::SameLine();
				if (ImGui::Button("Stop"))
					m_missing.Stop();
				ImGui::SameLine();
				if (ImGui::Button("Reload"))
					m_missing.Reload();
				ImGui::SameLine();
				if (ImGui::Button("Reprocess"))
				{
					Bitmap bmp(1024, 512);
					m_missing.Process(&bmp);
					m_missing_spectral.Load(bmp);
				}

				if (m_missing.processed)
				{
					ImGui::SameLine();
					ImGui::Checkbox("Spectrogram", &m_show_spectrogram);
					ImGui::SameLine();
					ImGui::Text("| Peaks: %i | %s", m_missing.peaks.size(), m_missing.path.c_str());
				}
			}
			else
			{
				ImGui::Text("No sample loaded. Use File > Open Sample... to import audio.");
			}

			ImGui::End();
		}
	}

	void UI::RenderMatches()
	{	
		if (ImGui::Begin(WIN_ID_MATCHES))
		{
			if (!m_library.matches.empty())
			{
				ImGui::Text("Here are songs that sound similar to this one:");
				ImGui::Separator();
			}

			int i = 1;
			for (const FoundSong& match: m_library.matches)
			{
				if (i > 10) // Only want the top 10
					continue;
				std::string filename = std::filesystem::proximate(match.sid->path, m_library_path).string();
				if (!match.sid)
					continue;
				ImGui::Text(
					"#%d: "
					"%s, "
					"c: %.2f, ic: %.2f, fc: %.2f, "
					"offsec: %f"
					,
					i,
					filename.c_str(),
					match.overall_confidence * 100.0f,
					match.input_confidence * 100.0f,
					match.fingerprinted_confidence * 100.0f,
					match.offset_secs
				);
				// TODO: In the future, implement a little "compare" button for the #1 match that plays the audio at offset_secs
				if (i == 1)
					ImGui::Separator();
				i++;
			}

			ImGui::End();
		}
	}

	void UI::RenderAboutScreen()
	{
		ImGui::SetNextWindowSize({320, 400});
		if (ImGui::Begin(WIN_ID_ABOUT, &m_show_about_window, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoResize))
		{
			ImGui::Image(TexID(m_logo), {(float)m_logo.GetWidth(), (float)m_logo.GetHeight()});
			ImGui::Text("Version 0.2 (prototype/PoC)");
			ImGui::NewLine();
			ImGui::Text("Credits");
			ImGui::Separator();
			ImGui::Text("KP - https://kpworld.xyz/");
			ImGui::Text("worldveil - https://willdrevo.com/");
			ImGui::Text("salsowelim - https://en.suliman.ws/about/");
			ImGui::NewLine();
			ImGui::TextWrapped("This was made over a couple weekends. Don't expect amazing results!");
			ImGui::NewLine();
			ImGui::TextWrapped("See LICENSE.md for copyright info.");
			ImGui::End();
		}
	}

	void UI::RenderLibraryStats()
	{
		ImGui::SetNextWindowSize({ 320, 160 });
		if (ImGui::Begin(WIN_ID_LIBRARY_INFO, &m_show_library_stats))
		{
			ImGui::Text(
				"%d files found\n"
				"%d from cache\n"
				"%d new entries"
				,
				m_library.files.size(),
				m_library.exclude.size(),
				m_library.files.size() - m_library.exclude.size()
			);
			ImGui::Separator();
			ImGui::Text(
				"%s total\n"
				"%s avg."
				,
				FormatTime(m_library.avg_length * m_library.files.size()).c_str(),
				FormatTime(m_library.avg_length).c_str()
			);
			ImGui::End();
		}
	}

	void UI::RenderSettings()
	{
		if (ImGui::Begin(WIN_ID_OPTIONS, &m_show_settings_window))
		{
			ImGui::PushItemWidth(100.0f);
			ImGui::Text("Fingerprinting");
			ImGui::InputInt("Default fan value", &settings.default_fan_value);
			ImGui::InputInt("Min. hash time delta", &settings.min_hash_time_delta);
			ImGui::InputInt("Max. hash time delta", &settings.max_hash_time_delta);
			// Don't feel like controlling this is necessary
			// ImGui::InputInt("Fingerprint reduction", &settings.fingerprint_reduction);
			ImGui::InputInt("Peak neighborhood size", &settings.peak_neighborhood_size);
			ImGui::InputInt("Window size", &settings.default_window_size);
			ImGui::InputFloat("Min. amplitude", &settings.default_amp_min);
			ImGui::InputFloat("Overlap ratio", &settings.default_overlap_ratio);
			// this too
			// ImGui::InputFloat("Sample rate/Max. freq", &settings.fs);

			ImGui::Separator();

			ImGui::Text("Ranking");
			ImGui::Checkbox("Demote songs based on length", &settings.demote_songs);
			ImGui::InputFloat("Demotion factor", &settings.demotion_factor);

			ImGui::Separator();

			ImGui::PopItemWidth();

			if (ImGui::Button("Reset to defaults"))
				LoadDefaults(settings);

			ImGui::End();
		}
	}

	void UI::RenderLoadingScreen()
	{
		ImGui::SetNextWindowSize({320, 100});
		if (ImGui::BeginPopupModal("Loading", nullptr, ImGuiWindowFlags_NoResize))
		{
			ImGui::Text("Please wait, be patient, stop whining, etc.");

			{
				std::unique_lock<std::mutex> lck(m_library.mutex);
				ImGui::ProgressBar((float) m_library.load_min / (float) m_library.load_max, { -1, 0 });
			}

			ImGui::EndPopup();
		}
		
	}
}
