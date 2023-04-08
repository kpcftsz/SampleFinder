#include "SampleFinder.h"

#include <iostream>

#define STBI_FAILURE_USERMSG
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace finder
{
	Bitmap::Bitmap():
		width(0),
		height(0),
		px(nullptr)
	{
	}

	Bitmap::Bitmap(int width, int height):
		width(width),
		height(height)
	{
		px = new int[width * height]();
	}

	Bitmap::~Bitmap()
	{
		if (px)
			delete[] px;
	}

	void Bitmap::Clear(int col)
	{
		for (int i = 0; i < width * height; i++)
			px[i] = col;
	}

	void Bitmap::DrawPoint(int x, int y, int col)
	{
		if (x < 0 || y < 0 || x >= width || y >= height)
			return;
		px[x + y * width] = col;
	}

	void Bitmap::DrawLine(int x0, int y0, int x1, int y1, int col)
	{
		int x_start_i = (int) x0, x_end_i = (int) x1;
		int y_start_i = (int) y0, y_end_i = (int) y1;

		int x_dist = abs(x_end_i - x_start_i), x_dir = x_start_i < x_end_i ? 1 : -1;
		int y_dist = abs(y_end_i - y_start_i), y_dir = y_start_i < y_end_i ? 1 : -1;

		int err = (x_dist > y_dist ? x_dist : -y_dist) / 2, e = 0;

		for (;;)
		{
			int* dest_data = &px[x_start_i + y_start_i * width];

			if (x_start_i >= 0 && x_start_i < width && y_start_i >= 0 && y_start_i < height)
				*dest_data = AlphaBlend(*dest_data, col);

			if (x_start_i == x_end_i && y_start_i == y_end_i)
				break;

			e = err;
			if (e > -x_dist) err -= y_dist, x_start_i += x_dir;
			if (e < y_dist) err += x_dist, y_start_i += y_dir;
		}
	}

	void Bitmap::DrawImage(const Bitmap& other, int x, int y)
	{
		for (int i = 0; i < width; i++)
		{
			int xp = i + x;
			if (xp < 0 || i < 0 || xp >= width || i >= other.width)
				continue;

			for (int j = 0; j < height; j++)
			{
				int yp = j + y;
				if (yp < 0 || j < 0 || yp >= height || j >= other.height)
					continue;

				int src_data = other.px[i + j * other.width];
				int& dest_data = px[xp + yp * width];
				dest_data = AlphaBlend(dest_data, src_data);
			}
		}
	}

	int Bitmap::AlphaBlend(int col, int other)
	{
		float ba = ((col >> 24) & 0xFF) / 255.0f;
		float br = ((col >> 16) & 0xFF) / 255.0f;
		float bg = ((col >> 8) & 0xFF) / 255.0f;
		float bb = ((col) & 0xFF) / 255.0f;

		float fa = ((other >> 24) & 0xFF) / 255.0f;
		float fr = ((other >> 16) & 0xFF) / 255.0f;
		float fg = ((other >> 8) & 0xFF) / 255.0f;
		float fb = ((other) & 0xFF) / 255.0f;

		float a = (ba * (1.0 - fa) + fa) * 0xFF;
		float r = (fr * fa + (br * (1.0 - fa))) * 0xFF;
		float g = (fg * fa + (bg * (1.0 - fa))) * 0xFF;
		float b = (fb * fa + (bb * (1.0 - fa))) * 0xFF;

		return (int) a << 24 | (int) r << 16 | (int) g << 8 | (int) b;
	}

	ErrCode Bitmap::Load(const std::string& path)
	{
		stbi_uc* data = stbi_load(path.c_str(), &width, &height, nullptr, STBI_rgb_alpha);

		if (!data)
		{
			std::cerr << "Failed to load image from " << path << std::endl;
			std::cerr << "STBI says: " << stbi_failure_reason() << std::endl;

			// Worst case we'll have a placeholder
			width = height = 2;
			px = new int[width * height];

			for (uint i = 0; i < width * height; i++)
				px[i] = i % 3 == 0 ? 0xFFFF00FF : 0xFF000000;

			return FAILURE;
		}

		px = new int[width * height];

		// RGBA in bytes -> ARGB int
		for (uint i = 0; i < width * height; i++)
		{
			byte c[4];
			for (uint j = 0; j < 4; j++)
				c[j] = data[i * 4 + j];

			px[i] = c[3] << 24 | c[0] << 16 | c[1] << 8 | c[2];
		}

		stbi_image_free(data);

		return SUCCESS;
	}

	ErrCode Bitmap::Save(const std::string& path)
	{
		int* abgr = new int[width * height];

		// ARGB -> ABGR conversion to please STBIW
		for (uint i = 0; i < width * height; i++)
		{
			int p = px[i];
			abgr[i] = (p & 0xFF000000) | ((p & 0xFF0000) >> 16) | (p & 0x00FF00) | ((p & 0x0000FF) << 16);
		}

		if (!stbi_write_png(path.c_str(), width, height, STBI_rgb_alpha, abgr, width * STBI_rgb_alpha))
		{
			std::cerr << "Failed to save image to " << path << std::endl;
			return FAILURE;
		}

		delete[] abgr;

		return SUCCESS;
	}

	/****************************************************************/

	Texture::Texture():
		m_gl_id(0),
		m_width(0),
		m_height(0)
	{
		glGenTextures(1, &m_gl_id);
	}

	Texture::~Texture()
	{
		glDeleteTextures(1, &m_gl_id);
	}

	ErrCode Texture::Load(const Bitmap& bmp)
	{
		byte* buffer = new byte[bmp.width * bmp.height * 4];
		for (int i = 0; i < bmp.width * bmp.height; i++)
		{
			int src = bmp.px[i];
			buffer[i * 4 + 0] = (src >> 16) & 0xFF;
			buffer[i * 4 + 1] = (src >> 8) & 0xFF;
			buffer[i * 4 + 2] = (src) & 0xFF;
			buffer[i * 4 + 3] = (src >> 24) & 0xFF;
		}
		ErrCode status = Load(buffer, bmp.width, bmp.height);
		delete[] buffer;

		return status;
	}

	ErrCode Texture::Load(const std::string& path)
	{
		byte* data = stbi_load(path.c_str(), &m_width, &m_height, nullptr, STBI_rgb_alpha);
		ErrCode status = Load(data, m_width, m_height);

		if (data)
			stbi_image_free(data);

		return status;
	}

	ErrCode Texture::Load(const byte* data, int width, int height, bool for_fbo)
	{
		if (!data && !for_fbo)
		{
			std::cerr << "Failed to create texture object: Missing data" << std::endl;
			return FAILURE;
		}

		m_width = width;
		m_height = height;

		glBindTexture(GL_TEXTURE_2D, m_gl_id);

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

		glTexImage2D(
			GL_TEXTURE_2D,
			0,
			for_fbo ? GL_RGB : GL_RGBA,
			width,
			height,
			0,
			for_fbo ? GL_RGB : GL_RGBA,
			GL_UNSIGNED_BYTE,
			data
		);

		glBindTexture(GL_TEXTURE_2D, 0);

		return SUCCESS;
	}

	void Texture::Bind() const
	{
		glBindTexture(GL_TEXTURE_2D, m_gl_id);
	}

	GLuint Texture::GetGLID() const
	{
		return m_gl_id;
	}

	int Texture::GetWidth() const
	{
		return m_width;
	}

	int Texture::GetHeight() const
	{
		return m_height;
	}
}
