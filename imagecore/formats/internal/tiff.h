/*
 * MIT License
 *
 * Copyright (c) 2017 Twitter
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#pragma once

#include "imagecore/formats/reader.h"
#include "imagecore/formats/writer.h"
#include "imagecore/utils/securemath.h"
#include "register.h"
#include "tiffio.h"

namespace imagecore {

class ImageReaderTIFF : public ImageReader
{
public:
	DECLARE_IMAGE_READER(ImageReaderTIFF);

	virtual bool initWithStorage(Storage* source);
	virtual bool readHeader();
	virtual bool readImage(Image* destImage);
	virtual EImageFormat getFormat();
	virtual const char* getFormatName();
	virtual unsigned int getWidth();
	virtual unsigned int getHeight();
	virtual EImageColorModel getNativeColorModel();

private:
	Storage* m_Source;
	Storage* m_TempSource;
	ImageWriter::Storage* m_TempStorage;
	unsigned int m_Width;
	unsigned int m_Height;
	bool m_HasAlpha;
	TIFF* m_Tiff;
};

class ImageWriterTIFF : public ImageWriter
{
public:
	DECLARE_IMAGE_WRITER(ImageWriterTIFF);

	virtual bool writeImage(Image* sourceImage);

	// Incremental writing not done, just implement to satisfy impl requirement.
	virtual bool beginWrite(unsigned int width, unsigned int height, EImageColorModel colorModel);
	virtual unsigned int writeRows(Image* sourceImage, unsigned int sourceRow, unsigned int numRows);
	virtual bool endWrite();

	virtual bool copyLossless(ImageReader* reader);

	enum SupportedWriteOptions
	{
		// Default to tiled mode, this option forces progressive (scanline) based encoding
		kSupportedWriteOptions_Progressive       = 0x200,

		// Tile size, bounded to [16..256] inclusive, 0 denotes auto choose based on resolution
		kSupportedWriteOptions_TIFFTileSizeMask  = 0x1FF0000,
	};
	static_assert(sizeof(SupportedWriteOptions) == sizeof(unsigned int), "Cannot mask unsigned int write options, incompatible sizes.");

	virtual void setWriteOptions(unsigned int writeOptions);

	enum SeekMode
	{
		kSeek_Set = 0,
		kSeek_Current = 1,
		kSeek_End = 2
	};

	class SeekableMemoryStorage : public MemoryStorage
	{
	public:
		SeekableMemoryStorage() : m_WrittenSize(0) {}
		SeekableMemoryStorage(uint64_t bufferLength) :
			MemoryStorage(bufferLength),
			m_WrittenSize(0) {}
		SeekableMemoryStorage(void* buffer, uint64_t length) :
			MemoryStorage(buffer, length),
			m_WrittenSize(0) {}
		virtual uint64_t write(const void* dataBuffer, uint64_t numBytes);
		virtual uint64_t read(void* destBuffer, uint64_t numBytes);
		virtual uint64_t totalBytesWritten();
		virtual bool seek(int64_t pos, SeekMode mode);
		virtual uint64_t tell();
		// The MemoryStorage::flush() will mess with seekable offsets, so override here to
		// prevent it being called virtually.
		virtual void flush() {}
	protected:
		// libtiff will seek past the end of written data, m_WrittenSize will therefore reflect max written
		// bytes, while m_UsedBytes reflects the current pointer.
		uint64_t m_WrittenSize;
	};

private:
	virtual bool initWithStorage(Storage* output);

	TIFF* m_Tiff;
	SeekableMemoryStorage *m_TempStorage;
	Storage *m_OutputStorage;
	uint8_t *m_EncodedDataBuffer;
	unsigned int m_WriteOptions;
	unsigned int m_TileSize;
};

}
