#pragma once

#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <mutex>
#include <atomic>
#include <fstream>
#include <iostream>

#include <GL/glew.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>

#include <boost/unordered_map.hpp>

#if defined(__clang__)
	#define FINDER_COMPILER_CLANG
#elif defined(__GNUC__) || defined(__GNUG__)
	#define FINDER_COMPILER_GCC
#elif defined(_MSC_VER)
	#define FINDER_COMPILER_MSVC
	#pragma warning(disable: 4018) // Signed/unsigned mismatch
	#pragma warning(disable: 4996) // Secure function warnings
#endif

namespace finder
{
	using uint8  = unsigned char;
	using uint16 = unsigned short;
	using uint32 = unsigned int;
	using uint64 = unsigned long long;

	using int8  = signed char;
	using int16 = signed short;
	using int32 = signed int;
	using int64 = signed long long;

	using uint = uint32;
	using byte = uint8;

	enum ErrCode
	{
		SUCCESS,
		FAILURE
	};

	/****************************************************************/
	/* Graphics utilities                                           */
	/****************************************************************/
	class Bitmap
	{
	public:
		Bitmap();
		Bitmap(int width, int height);
		~Bitmap();

		void Clear(int col);
		void DrawPoint(int x, int y, int col);
		void DrawLine(int x0, int y0, int x1, int y1, int col);
		void DrawImage(const Bitmap& other, int x, int y);

		int AlphaBlend(int col, int other);

		ErrCode Load(const std::string& path);
		ErrCode Save(const std::string& path);

	public:
		int width, height, *px;

	};

	class Texture
	{
	public:
		Texture();
		~Texture();

		ErrCode Load(const Bitmap& bmp);
		ErrCode Load(const std::string& path);
		ErrCode Load(const byte* data, int width, int height, bool for_fbo = false);

		void Bind() const;
		GLuint GetGLID() const;

		int GetWidth() const;
		int GetHeight() const;

		Texture(const Texture&) = delete;
		Texture& operator=(const Texture&) = delete;

	private:
		GLuint m_gl_id;

		int m_width;
		int m_height;

	};

	/****************************************************************/
	/* Audio processing                                             */
	/****************************************************************/
	class AudioFile;

	using SID = AudioFile*; // Don't want this to always be the case

	struct Fingerprint
	{
		AudioFile* source;
		boost::unordered_map<std::string, int> hashes;
	};
	
	struct Results
	{
		std::vector<std::pair<SID, int>> matches;
		std::unordered_map<SID, int> dedups;
	};

	struct FoundMatch
	{
		std::string hash;
		SID sid;
		int offset;
	};

	struct FoundSong
	{
		SID sid;
		int input_hashes;
		int fingerprinted_hashes;
		int hashes_matched;
		float input_confidence;
		float fingerprinted_confidence;
		float overall_confidence;
		float offset;
		float offset_secs;
	};

	class AudioFile
	{
	public:
		AudioFile();
		~AudioFile();

		ErrCode Load(const std::string& path, bool load_sdl = false);
		void Play(bool loop = false);
		void Pause();
		void Stop();
		void AdjustVolume(float volume);
		void Reload();
		void Reset(bool reset_sdl = true, bool reset_samples = true);

		std::unique_ptr<Bitmap> RenderWaveform();

		void Process(Bitmap* hd_spectrogram = nullptr);

	public:
		std::string path;
		std::vector<float> sample_data;
		std::vector<std::pair<int, int>> peaks;
		Mix_Music* sdl_object;
		float volume;
		float length;
		bool loaded;
		bool processed;
		int dims[2];
		Fingerprint fingerprint;

	};

	class AudioLibrary
	{
	public:
		AudioLibrary();
		~AudioLibrary();

		ErrCode Load(const std::string& path);
		ErrCode Save();
		void Process(bool force = false);
		void TestSong(AudioFile& missing);
		
	private:
		void FindMatches(Fingerprint& missing_fp, Results& results);
		void AlignMatches(const Results& results, int queried_hashes, int topn, std::vector<FoundSong>& songs_result);
		void RetrieveCachedMusic();

	public:
		std::mutex mutex;
		std::vector<AudioFile> files;
		std::vector<Fingerprint*> fingerprints;
		std::vector<FoundSong> matches;
		std::string library_path;
		std::string cache_path;
		std::vector<std::string> exclude;
		float highest_match_percent;
		float avg_length;
		bool cached_fps_present;
		bool loading;
		int load_min;
		int load_max;

	};
	
	/****************************************************************/
	/* Program UI/UX                                                */
	/****************************************************************/
	class UI
	{
	public:
		UI();
		~UI();

		void Update();
		void Render();

	private:
		void OpenSampleDialog();
		void OpenLibraryDialog();
		void ReplaceSample(const std::string& path);

		void RenderLibrary();
		void RenderMissingSample();
		void RenderMatches();
		void RenderAboutScreen();
		void RenderLibraryStats();
		void RenderSettings();
		void RenderLoadingScreen();

	private:
		AudioFile m_missing;
		AudioLibrary m_library;
		Texture m_missing_waveform;
		Texture m_missing_spectral;
		Texture m_peak;
		Texture m_logo;
		std::string m_library_path;
		bool m_di_init;
		bool m_show_about_window;
		bool m_show_settings_window;
		bool m_show_library_stats;
		bool m_show_waveform;
		bool m_show_spectrogram;

	};

	/****************************************************************/
	/* Algorithm config                                           */
	/****************************************************************/
	struct Settings
	{
		// Fingerprint algorithm settings
		int default_fan_value;
		int min_hash_time_delta;
		int max_hash_time_delta;
		int fingerprint_reduction;
		int peak_neighborhood_size;
		int default_window_size;
		float default_amp_min;
		float default_overlap_ratio;
		float fs;

		// Ranking algorithm settings
		bool demote_songs;
		float demotion_factor;
	};

	extern Settings settings;

	extern void LoadDefaults(Settings& settings);

	extern ErrCode LoadSettings(const std::string& path, Settings& settings);
	extern ErrCode SaveSettings(const std::string& path, const Settings& settings);

	/****************************************************************/
	/* Misc. I/O utilities                                          */
	/****************************************************************/
	class Loader
	{
	public:
		Loader(const std::string& path);

		int NextInt();
		float NextFloat();
		std::string NextString();

		template <size_t Size>
		std::string NextBufString()
		{
			std::string ret;
			ret.resize(Size);
			m_file.read(&ret[0], Size);
			return ret;
		}

	private:
		std::ifstream m_file;

	};

	class Saver
	{
	public:
		Saver(const std::string& path);

		void PutInt(int v);
		void PutFloat(float v);
		void PutString(const std::string& string, bool fixed_size = false);

	private:
		std::ofstream m_file;

	};

	extern ErrCode LoadTextFile(const std::string& path, std::string& out);
	extern ErrCode SaveTextFile(const std::string& path, const std::string& in);

}
