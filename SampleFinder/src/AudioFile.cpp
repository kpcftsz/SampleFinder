#include "SampleFinder.h"

#include <stdio.h>
#include <math.h>

#include <iostream>
#include <algorithm>
#include <vector>
#include <limits>
#include <iterator>
#include <iostream>
#include <typeinfo>
#include <chrono>
#include <fstream>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/uuid/detail/sha1.hpp>

#include <opencv2/opencv.hpp>

#include <sndfile.h>

using boost::property_tree::ptree;

namespace
{
	void Normalize(std::vector<float>& data)
	{
		float absmax = 0.0f;
		for (float& v: data)
			absmax = std::max(absmax, fabsf(v));
		for (float& v: data)
			v /= absmax;
	}

	std::vector<std::vector<float>> StrideWindows(const std::vector<float>& data, size_t blocksize, size_t overlap)
	{
		// https://stackoverflow.com/questions/21344296/striding-windows/21345055
		std::vector<std::vector<float>> res;
		size_t minlen = (data.size() - overlap) / (blocksize - overlap);
		size_t start_idx = 0;
		for (size_t i = 0; i < blocksize; ++i)
		{
			res.emplace_back(std::vector<float>());
			std::vector<float>& block = res.back();
			size_t idx = start_idx++;
			for (size_t j = 0; j < minlen; ++j)
			{
				block.push_back(data[idx]);
				idx += blocksize - overlap;
			}
		}
		return res;
	}

	int Detrend(std::vector<std::vector<float>>& data)
	{
		size_t nocols = data[0].size();
		size_t norows = data.size();
		float mean = 0;
		for (size_t i = 0; i < nocols; i++)
		{
			for (size_t j = 0; j < norows; j++)
				mean += data[j][i];
		}
		mean /= (norows * nocols);
		for (size_t i = 0; i < nocols; i++)
		{
			for (size_t j = 0; j < norows; j++)
				data[j][i] -= - mean;
		}
		return 0;
	}

	std::vector<float> CreateWindow(int wsize)
	{
		std::vector<float> res;
		for (int i = 0; i < wsize; i++)
			res.emplace_back(0.5f * (1 - cos(2 * M_PI * i / ((wsize - 1)))));

		return res;
	}

	void ApplyWindow(std::vector<float>& hann_window, std::vector<std::vector<float>>& data)
	{
		size_t nocols = data[0].size();
		size_t norows = data.size();
		for (size_t i = 0; i < nocols; i++)
		{
			for (size_t j = 0; j < norows; j++)
				data[j][i] = data[j][i] * hann_window[j];
		}
	}

	std::string GetSHA1(const std::string& p_arg)
	{
		boost::uuids::detail::sha1 sha1;
		sha1.process_bytes(p_arg.data(), p_arg.size());
		unsigned hash[5] = { 0 };
		sha1.get_digest(hash);
		// Back to string
		char buf[41] = { 0 };
		for (int i = 0; i < 5; i++)
			sprintf(buf + (i << 3), "%08x", hash[i]);

		return std::string(buf);
	}

	void GenerateHashes(std::vector<std::pair<int, int>>& v_in, boost::unordered_map<std::string, int>& out)
	{
		// Sorting
		// https://stackoverflow.com/questions/279854/how-do-i-sort-a-vector-of-pairs-based-on-the-second-element-of-the-pair
		std::sort(v_in.begin(), v_in.end(), [](auto& left, auto& right)
		{
			if (left.second == right.second)
				return left.first < right.first;
			return left.second < right.second;
		});
		for (int i = 0; i < v_in.size(); i++)
		{
			for (int j = 1; j < finder::settings.default_fan_value; j++)
			{
				if (i + j >= v_in.size())
					continue;

				int freq1 = v_in[i].first;
				int freq2 = v_in[i + j].first;
				int time1 = v_in[i].second;
				int time2 = v_in[i + j].second;
				int t_delta = time2 - time1;
				if ((t_delta >= finder::settings.min_hash_time_delta) && (t_delta <= finder::settings.max_hash_time_delta))
				{
					char buffer[100];
					snprintf(buffer, sizeof(buffer), "%d|%d|%d", freq1, freq2, t_delta);
					std::string to_be_hashed = buffer;
					std::string hash_result = GetSHA1(to_be_hashed).erase(finder::settings.fingerprint_reduction, 40);

					out.emplace(hash_result, time1);
				}
			}
		}
	}

	void Get2DPeaks(cv::Mat data, std::vector<std::pair<int, int>>& out)
	{
		// Generate binary structure and apply maximum filter
		cv::Mat tmpkernel = cv::getStructuringElement(cv::MORPH_CROSS, cv::Size(3, 3), cv::Point(-1, -1));
		cv::Mat kernel = cv::Mat(finder::settings.peak_neighborhood_size * 2 + 1, finder::settings.peak_neighborhood_size * 2 + 1, CV_8U, uint8_t(0));
		kernel.at<uint8_t>(finder::settings.peak_neighborhood_size, finder::settings.peak_neighborhood_size) = uint8_t(1);
		cv::dilate(kernel, kernel, tmpkernel, cv::Point(-1, -1), finder::settings.peak_neighborhood_size, 1, 1);
		cv::Mat d1;
		cv::dilate(data, d1, kernel); // d1 now contains m1 with max filter applied
		// Generate eroded background
		cv::Mat background = (data == 0); // 255 if element == 0, 0 otherwise
		cv::Mat local_max = (data == d1); // 255 if true, 0 otherwise
		cv::Mat eroded_background;
		cv::erode(background, eroded_background, kernel);
		cv::Mat detected_peaks = local_max - eroded_background;
		// Now detected peaks.size == m1.size .. iterate through m1. get amp where peak == 255 (true), get indices i,j as well.
		for (int i = 0; i < data.rows; i++)
		{
			for (int j = 0; j < data.cols; j++)
			{
				if ((detected_peaks.at<uint8_t>(i, j) == 255) && (data.at<float>(i, j) > finder::settings.default_amp_min))
					out.push_back(std::make_pair(i, j));
			}
		}
	}
}

namespace finder
{
	AudioFile::AudioFile():
		volume(1.0f),
		loaded(false),
		sdl_object(nullptr),
		processed(false)
	{
		dims[0] = 0;
		dims[1] = 0;
	}

	AudioFile::~AudioFile()
	{
		if (sdl_object)
			Mix_FreeMusic(sdl_object);
	}

	ErrCode AudioFile::Load(const std::string& path, bool load_sdl)
	{
		this->path = path;

		// Clear out any old data
		Reset();

		// Loading the file twice is crude, but SDL doesn't have a better way to play audio that's already in memory (e.g. by sndfile)
		// I guess in theory we could ditch sndfile and just use SDL, but I prefer sndfile's API :^)
		if (load_sdl)
			sdl_object = Mix_LoadMUS(path.c_str());

		// Anyways, let's load it using sndfile now
		SF_INFO sfinfo;
		memset(&sfinfo, 0, sizeof(sfinfo));
		SNDFILE* sf_in = sf_open(path.c_str(), SFM_READ, &sfinfo);
		if (!sf_in)
		{
			std::cerr << "Failed to open audio file: " << sf_strerror(sf_in) << std::endl;
			return FAILURE;
		}

		double fs = sfinfo.samplerate;
		sf_count_t n_frames = sfinfo.frames;
		sf_count_t n_samples = sfinfo.frames * sfinfo.channels;

		short* stereo_datai = new short[n_samples];
		sample_data.resize(n_frames);

		// sf_count_t n_read = sf_readf_float(sf_in, stereo_data, n_frames);
		sf_count_t n_read = sf_readf_short(sf_in, stereo_datai, n_frames);
		if (n_read == 0) // != n_frames)
		{
			std::cerr << "Error reading data from audio file: " << sf_strerror(sf_in) << std::endl;
			return FAILURE;
		}
		std::cout << "Loaded audio with " << n_samples << " samples into memory" << std::endl;

		sf_close(sf_in);

		length = n_frames / fs;

		loaded = true;

		// Convert data to mono
		for (int i = 0; i < n_frames; i++)
		{
			float v = 0;
			// Uncomment later to restore original behavior; opting to do this instead of averaging to better replicate DejaVu.
			//for (int c = 0; c < sfinfo.channels; c++)
			v += (float) stereo_datai[i * sfinfo.channels + 0 /* c */];
			sample_data[i] = v;// * (1.0f / 65536.0f);
		}
		delete[] stereo_datai;

		return SUCCESS;
	}

	void AudioFile::Play(bool loop)
	{
		if (!Mix_PlayingMusic())
		{
			AdjustVolume(volume);
			Mix_PlayMusic(sdl_object, -((int) loop));
		}
		else
		{
			Mix_ResumeMusic();
		}
	}

	void AudioFile::Pause()
	{
		Mix_PauseMusic();
	}

	void AudioFile::Stop()
	{
		Mix_SetMusicPosition(0);
		Mix_HaltMusic();
	}

	void AudioFile::AdjustVolume(float volume)
	{
		this->volume = volume;
		Mix_VolumeMusic((int) (volume * 128.0f));
	}

	void AudioFile::Reload()
	{
		Stop();
		Load(path);
	}

	void AudioFile::Reset(bool reset_sdl, bool reset_samples)
	{
		if (sdl_object && reset_sdl)
			Mix_FreeMusic(sdl_object);
		if (!sample_data.empty() && reset_samples)
			sample_data.clear();
	}

	std::unique_ptr<Bitmap> AudioFile::RenderWaveform()
	{
		std::unique_ptr<Bitmap> bmp(new Bitmap(1024, 512));

		bmp->Clear(0xFF000000);

		float absmax = 0.0f;
		for (int i = 0; i < sample_data.size(); i++)
			absmax = std::max(absmax, fabsf(sample_data[i]));
		for (int x = 1; x < sample_data.size(); x++)
		{
			float p0 = (float)(x - 1) / (float) sample_data.size();
			float p1 = (float) x / (float) sample_data.size();

			float yc = (float) bmp->height / 2.0f;

			float sample0 = sample_data[x - 1] * (1.0f / absmax) * (bmp->height / 2.0f);
			float sample1 = sample_data[x] * (1.0f / absmax) * (bmp->height / 2.0f);

			int xx0 = p0 * bmp->width;
			int xx1 = p1 * bmp->width;

			bmp->DrawLine(xx0, yc - sample0, xx1, yc - sample1, 0xFFBA4040);
		}

		return bmp;
	}

	void AudioFile::Process(Bitmap* hd_spectrogram)
	{
		peaks.clear();
		fingerprint.hashes.clear();

		/*
		 * FFT the signal and extract frequency components
		 */
		int max_freq = 0; // One-sided; reference mlab.py
		if (finder::settings.default_window_size % 2)
			max_freq = int(std::floor((finder::settings.default_window_size + 1) / 2));
		else
			max_freq = int(std::floor(finder::settings.default_window_size / 2)) + 1;

		// Apply hanning windows
		std::vector<std::vector<float>> blocks = StrideWindows(sample_data, finder::settings.default_window_size, finder::settings.default_window_size * finder::settings.default_overlap_ratio);
		std::vector<float> hann_window = CreateWindow(finder::settings.default_window_size);
		ApplyWindow(hann_window, blocks);
		Detrend(blocks);
		// Reshape
		cv::Mat result(blocks[0].size(), blocks.size(), CV_32F);
		for (int i = 0; i < result.rows; i++)
		{
			for (int j = 0; j < result.cols; j++)
				result.at<float>(i, j) = blocks[j][i];
		}
		// He looooves fourier transforms!
		cv::dft(result, result, cv::DftFlags::DFT_COMPLEX_OUTPUT | cv::DftFlags::DFT_ROWS, 0);
		cv::mulSpectrums(result, result, result, 0, true); // i.e. result *= conj(result)

		// Compute the DFT sample frequencies
		cv::Mat freqs(max_freq, blocks[0].size(), CV_32F);
		for (int i = 0; i < max_freq; i++)
		{
			for (int j = 0; j < freqs.cols; j++)
				freqs.at<float>(i, j) = result.ptr<float>(j)[2 * i];
		}
		for (int i = 1; i < freqs.rows - 1; i++)
		{
			for (int j = 0; j < freqs.cols; j++)
				freqs.at<float>(i, j) *= 2;
		}

		// Divide by sampling frequency so that density function has units of dB/Hz and can be integrated by the plotted frequency values.
		freqs /= finder::settings.fs;
		float sum = 0.0f;
		for (float window: hann_window)
			sum += fabsf(window) * fabsf(window);
		// Scale the spectrum by the norm of the window to compensate for windowing loss;
		// See Bendat & Piersol Sec 11.5.2.
		freqs /= sum;

		/*
		 * Apply log transform since specgram function returns linear array. 0s are excluded to avoid np warning.
		 */
		for (int i = 0; i < freqs.rows; i++)
		{
			for (int j = 0; j < freqs.cols; j++)
			{
				if (freqs.at<float>(i, j) < std::numeric_limits<float>::epsilon())
					freqs.at<float>(i, j) = std::numeric_limits<float>::epsilon();
				freqs.at<float>(i, j) = 10 * log10(freqs.at<float>(i, j));
				// See https://github.com/worldveil/dejavu/issues/118
				// if (freqs.at<float>(i, j) == -INFINITY)
				//     freqs.at<float>(i, j) = 0;
			}
		}

		// Build the fingerprint!
		std::cout << "Getting peaks..." << std::endl;
		cv::Mat& final_specgram = freqs;
		Get2DPeaks(final_specgram, peaks);
		GenerateHashes(peaks, fingerprint.hashes);
		fingerprint.source = this;
		processed = true;
		std::cout << "# of hash/offset pairs after proc: " << fingerprint.hashes.size() << std::endl;

		// Optionally we can render out a spectrogram to look at what's happening
		if (hd_spectrogram)
		{
			int extent_x = final_specgram.cols;
			int extent_y = final_specgram.rows;

			dims[0] = extent_x;
			dims[1] = extent_y;

			hd_spectrogram->width = std::min(extent_x, 8192);
			hd_spectrogram->height = std::min(extent_y, 8192);
			delete[] hd_spectrogram->px;
			hd_spectrogram->px = new int[hd_spectrogram->width * hd_spectrogram->height];
			hd_spectrogram->Clear(0xFF000000);

			int lo_v = INFINITY;
			int hi_v = -INFINITY;

			for (int i = 1; i < hd_spectrogram->width - 1; i++)
			{
				int vi = (int)(((float) i / (float) hd_spectrogram->width) * extent_x);
				if (vi < 0) vi = 0;
				if (vi >= extent_x) vi = extent_x - 1;

				for (int j = 1; j < hd_spectrogram->height - 1; j++)
				{
					int vj = (int)(((float) j / (float) hd_spectrogram->height) * extent_y);
					if (vj < 0) vj = 0;
					if (vj >= extent_y) vj = extent_y - 1;
					vj = extent_y - vj;

					float v = final_specgram.at<float>(vj, vi);

					if (v < lo_v) lo_v = v;
					if (v > hi_v) hi_v = v;

					int r = 0x00, g = 0x00, b = 0x00;

					float rv = v * (255.0f / 80.0f);
					b += (int)(rv * 2.0f);
					r += (int)(rv);

					if (r < 0) r = 0; if (r > 255) r = 255;
					if (g < 0) g = 0; if (g > 255) g = 255;
					if (b < 0) b = 0; if (b > 255) b = 255;

					hd_spectrogram->px[i + j * hd_spectrogram->width] = 0xFF << 24 | r << 16 | g << 8 | b;
				}
			}

#if 0
			std::cout << lo_v << " => " << hi_v << std::endl;
			hd_spectrogram->Save("fuck.png");
#endif
		}
	}
}
