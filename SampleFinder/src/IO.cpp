#include "SampleFinder.h"

namespace finder
{
	Loader::Loader(const std::string& path):
		m_file(path, std::ios::in | std::ios::binary)
	{
	}

	int Loader::NextInt()
	{
		int i = 0;
		m_file.read((char*) &i, 4);

		return i;
	}

	float Loader::NextFloat()
	{
		float f = 0;
		m_file.read((char*)&f, 4);

		return f;
	}

	std::string Loader::NextString()
	{
		int size;
		m_file.read((char*) &size, 4);
		auto data = std::make_unique<finder::byte[]>(size);
		char* buf = reinterpret_cast<char*>(data.get());
		m_file.read(buf, size);

		return std::string(buf, size);
	}

	/****************************************************************/

	Saver::Saver(const std::string& path):
		m_file(path, std::ios::out | std::ios::binary)
	{
	}

	void Saver::PutInt(int v)
	{
		m_file.write(reinterpret_cast<const char*>(&v), sizeof(v));
	}

	void Saver::PutFloat(float v)
	{
		m_file.write(reinterpret_cast<const char*>(&v), sizeof(v));
	}

	void Saver::PutString(const std::string& string, bool fixed_size)
	{
		if (!fixed_size)
			PutInt(string.size());
		m_file.write(string.c_str(), string.size());
	}

	/****************************************************************/

	ErrCode LoadTextFile(const std::string& path, std::string& out)
	{
		std::ifstream file(path);
		if (!file)
		{
			std::cerr << "Failed to load text file " << path << std::endl;
			return FAILURE;
		}

		out = std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

		return SUCCESS;
	}

	ErrCode SaveTextFile(const std::string& path, const std::string& in)
	{
		std::ofstream file(path);
		if (!file)
		{
			std::cerr << "Failed to save text file " << path << std::endl;
			return FAILURE;
		}

		file << in << std::endl;

		return SUCCESS;
	}
}
