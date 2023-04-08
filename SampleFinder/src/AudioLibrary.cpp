#include "SampleFinder.h"

#include <fstream>
#include <filesystem>
#include <algorithm>
#include <iostream>
#include <string>
#include <unordered_map>
#include <map>
#include <execution>
#include <chrono>

namespace
{
	bool EndsWith(const std::string& str, const std::string& suffix)
	{
		return str.size() >= suffix.size() && 0 == str.compare(str.size() - suffix.size(), suffix.size(), suffix);
	}
}

namespace finder
{
	AudioLibrary::AudioLibrary():
		cached_fps_present(false),
		loading(false),
		load_min(0.0f),
		load_max(1.0f)
	{
	}

	AudioLibrary::~AudioLibrary()
	{
	}

	ErrCode AudioLibrary::Load(const std::string& path)
	{
		loading = true;

		// Check and see if there's a library file available. If there is we'll load cached fingerprints from it and skip them
		// during the processing phase.
		library_path = path;
		cache_path = path + "/library.kpsf";
		exclude.clear();
		
		// kk, now do the rest of the loading
		load_min = load_max = 0;
		for (const auto& file : std::filesystem::recursive_directory_iterator(path))
			load_max++;

		avg_length = 0;

		std::thread loading_thread([&]()
		{
			if (std::filesystem::exists(cache_path))
				RetrieveCachedMusic();

			for (const auto& file: std::filesystem::recursive_directory_iterator(path))
			{
				{
					std::unique_lock<std::mutex> lck(mutex);
					load_min++;
				}

				if (file.is_directory())
					continue;

				std::string file_path = file.path().string();

				// Check if it's in the cache before loading
				bool should_process = true;
				for (int i = 0; i < exclude.size(); i++)
				{
					if (file_path.find(exclude[i]) != std::string::npos)
						should_process = false;
				}
				if (!should_process)
					continue;

				if (!EndsWith(file_path, ".wav") && !EndsWith(file_path, ".mp3"))
					continue;

				files.push_back({});
				if (files.back().Load(file_path) == FAILURE)
				{
					files.pop_back();
					continue;
				}
				avg_length += files.back().length;
			}

			loading = false;
			avg_length /= files.size();
			std::cout << "Average track length is " << avg_length << " seconds." << std::endl;
		});
		loading_thread.detach();

		return SUCCESS;
	}

	ErrCode AudioLibrary::Save()
	{
		if (cache_path.empty() || library_path.empty())
			return FAILURE;

		Saver svr(cache_path);
		// Encode header info
		svr.PutInt(avg_length * files.size());
		svr.PutInt(fingerprints.size());
		// Encode fingerprints
		for (const Fingerprint* fp: fingerprints)
		{
			if (!fp->source)
				continue;
			std::string path = std::filesystem::proximate(fp->source->path, library_path).string();
			svr.PutString(path);
			svr.PutFloat(fp->source->length);
			svr.PutInt(fp->hashes.size());
			for (const auto& [k, v]: fp->hashes)
			{
				svr.PutString(k, true);
				svr.PutInt(v);
			}
		}
	}

	void AudioLibrary::Process(bool force)
	{
		loading = true;

		load_min = 0;
		load_max = files.size();

		std::thread processing_thread([&]()
		{
			std::for_each(std::execution::par, files.begin(), files.end(), [&](AudioFile& file)
			{
				if (!file.processed || force)
					file.Process();
				std::unique_lock<std::mutex> lck(mutex);
				load_min++;
				if (load_min == files.size())
					loading = false;
				fingerprints.push_back(&file.fingerprint);
			});
		});
		processing_thread.detach();
	}

	void AudioLibrary::TestSong(AudioFile& missing)
	{
		Results results;
		FindMatches(missing.fingerprint, results);
		AlignMatches(results, missing.fingerprint.hashes.size(), 10, matches);
	}

	//

	/*
	 * Return a list of (song_id, offset_difference) pairs and a map with the amount of hashes matched (not considering
	 * duplicated hashes) in each song.
	 */
	void AudioLibrary::FindMatches(Fingerprint& missing_fp, Results& results)
	{
		matches.clear();

		// Create a map of hash => offset pairs for later lookups
		boost::unordered_map<std::string, std::vector<int>> mapper;
		for (const auto& [hsh, offset]: missing_fp.hashes)
		{
			if (mapper.count(hsh) != 0)
				mapper[hsh].push_back(offset);
			else
				mapper.emplace(hsh, std::vector<int>(1, offset));
		}

		std::vector<std::string> values;
		for (const auto& [hash, offsets]: mapper)
			values.push_back(hash);

		// In order to count each hash only once per db offset we use the map below
		for (int i = 0; i < values.size(); i++)
		{
			std::string* it = &values[i];

			// Pull the hash, SID, and offset for each fingerprint containing hashes from the missing sample
			std::vector<FoundMatch> cur;
			for (Fingerprint* fp: fingerprints)
			{
				// Skip the one we're trying to find
				std::string fp_path = std::filesystem::path(fp->source->path).filename().string();
				std::string in_path = std::filesystem::path(missing_fp.source->path).filename().string();
				if (fp->source && fp_path == in_path)
					continue;

				// Add the matching hash!
				if (fp->hashes.count(*it))
				{
					cur.push_back({
						*it,            // hsh
						fp->source,     // sid
						fp->hashes[*it] // offset
					});
				}
			}
			for (const FoundMatch& c: cur)
			{
				if (results.dedups.count(c.sid) == 0)
					results.dedups.emplace(c.sid, 1);
				else
					results.dedups[c.sid]++;

				// We now evaluate all offsets for each hash matched
				for (int song_sampled_offset: mapper[c.hash])
					results.matches.push_back({c.sid, c.offset - song_sampled_offset});
			}
		}
	}

	/*
	 * Finds hash matches that align in time with other matches and finds consensus about which hashes are "true" signal from the
	 * audio. This is basically the final step of the ranking process. For our purposes we do a few things differently from the
	 * original DejaVu implementation.
	 */
	void AudioLibrary::AlignMatches(const Results& results, int queried_hashes, int topn, std::vector<FoundSong>& songs_result)
	{
		// Keep only the maximum offset occurrences.
		// Note: We don't count peak offsets like DejaVu, as (AFAIK) the original code doesn't use the count for anything.
		// Also note: If you want to retrieve multiple matches in one song, this code should be adjusted.
		std::map<SID, int> max_diff;
		for (const std::pair<SID, int>& p: results.matches)
		{
			SID sid = p.first;
			int offset_diff = p.second;

			if (max_diff.count(sid) == 0)
				max_diff.emplace(sid, offset_diff);
			else
				max_diff[sid] = std::max(max_diff[sid], offset_diff);
		}

		// Another quirk: we look at all matches *now* and filter them later
		for (const auto& [song, sample_offset]: max_diff)
		{
			float offset = (float) sample_offset;
			int   song_hashes = song->fingerprint.hashes.size();
			float nseconds = (offset / settings.fs * settings.default_window_size * settings.default_overlap_ratio) * 0.5f;
			int   hashes_matched = results.dedups.at(song);
			float input_confidence = (float) hashes_matched / (float) queried_hashes;
			float fingerprinted_confidence = (float) hashes_matched / (float) song_hashes;

			// DejaVu's ranking algorithm has a caveat where it'll favor longer tracks.
			// E.g, if I have track A and I'm comparing it against tracks B and C, where B is the correct one and C isn't,
			// but longer, track C will be ranked above B regardless. This has been reported to DejaVu devs but still not fixed.
			// 
			// In our case we'll just try to see how abnormally long this track is and adjust the ranking accordingly.
			// We can either do this based on an arbitrary track length we consider normal, or look at the average length in the
			// user library. I'm opting to do the latter for now. Just make sure not to mix sample snippets and full tracks.
			// 
			float adj_input_confidence = input_confidence;
			if (settings.demote_songs)
			{
				float length_adjust = (avg_length / song->length) * settings.demotion_factor;
				adj_input_confidence *= std::min(length_adjust, 1.0f);
			}
			float overall_confidence = fingerprinted_confidence + adj_input_confidence;

			// Aight, we have everything to construct the ranked result
			FoundSong found_song = {
				song,
				queried_hashes,
				song_hashes,
				hashes_matched,
				input_confidence,
				fingerprinted_confidence,
				overall_confidence,
				offset,
				nseconds
			};

			songs_result.push_back(found_song);
		}

		// Prioritize confidence over offsets
		std::sort(songs_result.begin(), songs_result.end(), [](const FoundSong& a, const FoundSong& b)
		{
			if (a.overall_confidence > b.overall_confidence) return true;
			if (b.overall_confidence > a.overall_confidence) return false;

			if (a.offset > b.offset) return true;
			if (b.offset > a.offset) return false;

			return false;
		});
	}

	/*
	 * Pull cached music from a .kpsf file
	 */
	void AudioLibrary::RetrieveCachedMusic()
	{
		Loader ldr(cache_path);

		// Decode our header info
		avg_length = (float) ldr.NextInt(); // Note we're actually pulling the total here and we average it later
		int num_fps = ldr.NextInt();

		exclude.reserve(num_fps);
		files.reserve(std::max(load_max, num_fps));
		fingerprints.reserve(fingerprints.size() + num_fps);

		// Process fingerprints
		for (int i = 0; i < num_fps; i++)
		{
			std::string path = ldr.NextString();
			float length = ldr.NextFloat();
			int num_hash_offset_pairs = ldr.NextInt();

			files.push_back({});
			exclude.push_back(path);

			AudioFile* sid = &files.back(); // FIXME: This pointer may be invalidated & cause crashes!
			sid->path = library_path + "/" + path;
			sid->length = length;
			sid->fingerprint.source = sid; // Gross coupling
			sid->processed = true;
			sid->fingerprint.hashes.reserve(num_hash_offset_pairs);
			for (int j = 0; j < num_hash_offset_pairs; j++)
			{
				std::string hash = ldr.NextBufString<20>();
				int offset = ldr.NextInt();
				sid->fingerprint.hashes.emplace(hash, offset);
			}

			fingerprints.push_back(&sid->fingerprint);
		}
	}
}
