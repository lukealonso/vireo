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

#include "tiff.h"
#include "imagecore/image/rgba.h"
#include "imagecore/formats/writer.h"
#include "imagecore/utils/mathutils.h"
#include "imagecore/utils/securemath.h"

namespace imagecore {

REGISTER_IMAGE_READER(ImageReaderTIFF);
REGISTER_IMAGE_WRITER(ImageWriterTIFF);

static void TIFFSilentWarningHandler( const char*, const char*, va_list ) {}

tsize_t ImageReaderTiffRead(thandle_t handle, tdata_t buffer, tsize_t size)
{
	ImageReader::Storage* storage = (ImageReader::Storage*)handle;
	return (tsize_t)storage->read(buffer, size);
}

tsize_t ImageReaderTiffWrite(thandle_t handle, tdata_t buffer, tsize_t size)
{
	ASSERT(0);
	return 0;
}

int ImageReaderTiffClose(thandle_t handle)
{
	return 0;
}

toff_t ImageReaderTiffSeek(thandle_t handle, toff_t pos, int whence)
{
	if( pos == 0xFFFFFFFF ) {
		return 0xFFFFFFFF;
	}
	ImageReader::Storage* storage = (ImageReader::Storage*)handle;
	if( whence == SEEK_CUR ) {
		storage->seek(pos, ImageReader::Storage::kSeek_Current);
	} else if( whence == SEEK_END ) {
		storage->seek(pos, ImageReader::Storage::kSeek_End);
	} else {
		storage->seek(pos, ImageReader::Storage::kSeek_Set);
	}
	return (toff_t)storage->tell();
}

toff_t ImageReaderTiffSize(thandle_t handle)
{
	ImageReader::Storage* storage = (ImageReader::Storage*)handle;
	uint64_t pos = storage->tell();
	storage->seek(0, ImageReader::Storage::kSeek_End);
	uint64_t size = storage->tell();
	storage->seek(pos, ImageReader::Storage::kSeek_Set);
	return (toff_t)size;
}

int ImageReaderTiffMap(thandle_t, tdata_t*, toff_t*)
{
	return 0;
}

void ImageReaderTiffUnmap(thandle_t, tdata_t, toff_t)
{
	return;
}

tsize_t ImageWriterTiffRead(thandle_t handle, tdata_t buffer, tsize_t size)
{
	ImageWriterTIFF::SeekableMemoryStorage* seekableStorage = (ImageWriterTIFF::SeekableMemoryStorage*)handle;
	tsize_t read = (tsize_t)seekableStorage->read(buffer, size);
	return read;
}

tsize_t ImageWriterTiffWrite(thandle_t handle, tdata_t buffer, tsize_t size)
{
	ImageWriterTIFF::SeekableMemoryStorage* seekableStorage = (ImageWriterTIFF::SeekableMemoryStorage*)handle;
	tsize_t written = (tsize_t)seekableStorage->write(buffer, size);
	return written;
}

int ImageWriterTiffClose(thandle_t handle)
{
	// We let the d'tor handle closing TIFF handle
	return 0;
}

toff_t ImageWriterTiffSeek(thandle_t handle, toff_t pos, int whence)
{
	if( pos == 0xFFFFFFFF ) {
		return 0xFFFFFFFF;
	}

	ImageWriterTIFF::SeekableMemoryStorage* seekableStorage = (ImageWriterTIFF::SeekableMemoryStorage*)handle;
	if(whence == SEEK_CUR) {
		seekableStorage->seek(pos, ImageWriterTIFF::SeekMode::kSeek_Current);
	} else if(whence == SEEK_END) {
		seekableStorage->seek(pos, ImageWriterTIFF::SeekMode::kSeek_End);
	} else {
		seekableStorage->seek(pos, ImageWriterTIFF::SeekMode::kSeek_Set);
	}
	return (toff_t)seekableStorage->tell();
}

toff_t ImageWriterTiffSize(thandle_t handle)
{
	ImageWriterTIFF::SeekableMemoryStorage* seekableStorage = (ImageWriterTIFF::SeekableMemoryStorage*)handle;
	return seekableStorage->totalBytesWritten();
}

int ImageWriterTiffMap(thandle_t, tdata_t*, toff_t*)
{
	return 0;
}

void ImageWriterTiffUnmap(thandle_t, tdata_t, toff_t)
{
	return;
}

bool ImageReaderTIFF::Factory::matchesSignature(const uint8_t* sig, unsigned int sigLen)
{
	if( sigLen >= 2 && ((sig[0] == 'I' && sig[1] == 'I') || (sig[0] == 'M' && sig[1] == 'M')) ) {
		return true;
	}
	return false;
}

ImageReaderTIFF::ImageReaderTIFF()
:	m_Source(NULL)
,	m_TempSource(NULL)
,	m_TempStorage(NULL)
,	m_Width(0)
,	m_Height(0)
,	m_HasAlpha(false)
{
}

ImageReaderTIFF::~ImageReaderTIFF()
{
	if( m_Tiff != NULL ) {
		TIFFClose(m_Tiff);
		m_Tiff = NULL;
	}
	delete m_TempSource;
	m_TempSource = NULL;
	delete m_TempStorage;
	m_TempStorage = NULL;
}

bool ImageReaderTIFF::initWithStorage(ImageReader::Storage* source)
{
	m_Source = source;
	// libtiff requires random access, so if we have a non-seekable source, it needs to be fully read and copied.
	if( !m_Source->canSeek() ) {
		m_TempStorage = new ImageWriter::MemoryStorage();
		uint8_t buffer[1024];
		long totalBytesRead = 0;
		long bytesRead = 0;
		do {
			bytesRead = (long)source->read(buffer, 1024);
			if( bytesRead > 0 ) {
				m_TempStorage->write(buffer, bytesRead);
			}
			totalBytesRead += bytesRead;
		} while ( bytesRead > 0 );

		uint8_t* storageBuffer;
		uint64_t storageLength;
		if( m_TempStorage->asBuffer(storageBuffer, storageLength) ) {
			m_TempSource = new MemoryStorage(storageBuffer, totalBytesRead);
			m_Source = m_TempSource;
		}
	}
	return true;
}

bool ImageReaderTIFF::readHeader()
{
	TIFFSetErrorHandler(TIFFSilentWarningHandler);
	TIFFSetWarningHandler(TIFFSilentWarningHandler);
	// Don't allow memory mapping, read only.
	m_Tiff = TIFFClientOpen("None", "rm", m_Source, ImageReaderTiffRead, ImageReaderTiffWrite, ImageReaderTiffSeek, ImageReaderTiffClose,
		ImageReaderTiffSize, ImageReaderTiffMap, ImageReaderTiffUnmap);
	int width = 0;
	int height = 0;
	if( m_Tiff == NULL ) {
		return false;
	}
	if( TIFFGetField(m_Tiff, TIFFTAG_IMAGEWIDTH, &width) == 0 || TIFFGetField(m_Tiff, TIFFTAG_IMAGELENGTH, &height) == 0 ) {
		return false;
	}
	m_Width = width > 0 ? width : 0;
	m_Height = height > 0 ? height : 0;
	return true;
}

bool ImageReaderTIFF::readImage(Image* dest)
{
	if( !supportsOutputColorModel(dest->getColorModel()) ) {
		return false;
	}

	ImageRGBA* destImage = dest->asRGBA();

	char err[1024];
	if( TIFFRGBAImageOK(m_Tiff, err) == 0 ) {
		fprintf(stderr, "error reading TIFF: '%s'\n", err);
	}

	TIFFRGBAImage tiffImage;
	memset(&tiffImage, 0, sizeof(TIFFRGBAImage));
	if( TIFFRGBAImageBegin(&tiffImage, m_Tiff, 1, err) == 0 ) {
		fprintf(stderr, "error reading TIFF: '%s'\n", err);
	}

	m_HasAlpha = tiffImage.alpha > 0;

	bool success = false;
	ImageRGBA* tempImage = ImageRGBA::create(m_Width, m_Height);
	if( tempImage != NULL ) {
		unsigned int pitch = 0;
		uint8_t* buffer = tempImage->lockRect(m_Width, m_Height, pitch);
		TIFFReadRGBAImageOriented(m_Tiff, m_Width, m_Height, (uint32_t*)buffer, ORIENTATION_TOPLEFT, 1);
		tempImage->unlockRect();
		tempImage->copy(destImage);
		delete tempImage;
		success = true;
	}
	TIFFRGBAImageEnd(&tiffImage);
	return success;
}

ImageWriterTIFF::ImageWriterTIFF()
:	m_Tiff(NULL),
	m_TempStorage(NULL),
	m_OutputStorage(NULL),
	m_EncodedDataBuffer(NULL)
{
}

ImageWriterTIFF::~ImageWriterTIFF()
{
	if( m_Tiff != NULL ) {
		TIFFClose(m_Tiff);
		m_Tiff = NULL;
	}

	if (m_TempStorage != NULL) {
		delete m_TempStorage;
		m_TempStorage = NULL;
	}

	if (m_EncodedDataBuffer != NULL) {
		delete [] m_EncodedDataBuffer;
		m_EncodedDataBuffer = NULL;
	}
}

EImageFormat ImageReaderTIFF::getFormat()
{
	return kImageFormat_TIFF;
}

const char* ImageReaderTIFF::getFormatName()
{
	return "TIFF";
}

unsigned int ImageReaderTIFF::getWidth()
{
	return m_Width;
}

unsigned int ImageReaderTIFF::getHeight()
{
	return m_Height;
}

EImageColorModel ImageReaderTIFF::getNativeColorModel()
{
	return m_HasAlpha ? kColorModel_RGBA : kColorModel_RGBX;
}

// Note: Tags are not copied with this current implementation.
//       Only default basic tags applied as if from scratch.
bool ImageWriterTIFF::copyLossless(ImageReader* reader)
{
	if (reader->getFormat() != kImageFormat_TIFF) {
		return false;
	}

	// Reader and writer both only support RGB with and w/o alpha
	if (reader->getNativeColorModel() != kColorModel_RGBA && reader->getNativeColorModel() != kColorModel_RGBX) {
		return false;
	}

	return ImageWriter::copyLossless(reader);
}

bool determineTileSize(unsigned int &ts, unsigned int imageWidth, unsigned int imageHeight)
{
	if (ts) {
		if (imageWidth % ts != 0 || imageHeight % ts != 0) {
			fprintf(stderr, "Cannot use requested tile size of %u with %ux%u image, partially filled tiles "
				"currently not supported\n", ts, imageWidth, imageHeight);
			return false;
		}
		return true;
	}

	for (ts = 256; ts >= 16; ts--) {
		if (imageWidth % ts == 0 && imageHeight % ts == 0) {
			return true;
		}
	}

	fprintf(stderr, "Failed to auto-calculate tile size, image doesn't seem 16px aligned, valid tile size is [16..256], unaligned tiles not currently supported.");
	return false;
}

bool ImageWriterTIFF::writeImage(Image* sourceImage)
{
	TIFFSetErrorHandler(TIFFSilentWarningHandler);
	TIFFSetWarningHandler(TIFFSilentWarningHandler);

	m_Tiff = TIFFClientOpen("Memory", "wb", m_TempStorage, ImageWriterTiffRead, ImageWriterTiffWrite, ImageWriterTiffSeek, ImageWriterTiffClose,
		ImageWriterTiffSize, ImageWriterTiffMap, ImageWriterTiffUnmap);
	if (!m_Tiff) {
		fprintf(stderr, "Failed to open TIFF client handle\n");
		return false;
	}

	ImageRGBA *srcImageRGBA = sourceImage->asRGBA();
	if (!srcImageRGBA) {
		fprintf(stderr, "Failed to get RGBA/RGBX image buffer\n");
		return false;
	}

	if (sourceImage->getColorModel() != kColorModel_RGBA && sourceImage->getColorModel() != kColorModel_RGBX) {
		fprintf(stderr, "Source image color model is %d, only RGBA/RGBX is supported\n", sourceImage->getColorModel());
		return false;
	}

	bool hasAlpha = sourceImage->getColorModel() == kColorModel_RGBA;
	bool doTiling = !(m_WriteOptions & kSupportedWriteOptions_Progressive);

	TIFFSetField(m_Tiff, TIFFTAG_IMAGEWIDTH, srcImageRGBA->getWidth());
	TIFFSetField(m_Tiff, TIFFTAG_IMAGELENGTH, srcImageRGBA->getHeight());
	TIFFSetField(m_Tiff, TIFFTAG_SAMPLESPERPIXEL, hasAlpha ? 4 : 3);
	TIFFSetField(m_Tiff, TIFFTAG_BITSPERSAMPLE, 8);
	TIFFSetField(m_Tiff, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
	TIFFSetField(m_Tiff, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
	TIFFSetField(m_Tiff, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);

	if (hasAlpha) {
		// Is there a way to indicate from srcImage that alpha channel is associated/pre-multiplied?
		uint16_t alphaChannel = EXTRASAMPLE_UNASSALPHA;
		TIFFSetField(m_Tiff, TIFFTAG_EXTRASAMPLES, 1, &alphaChannel);
	}

	unsigned int pitch = 0;
	uint8_t* rawBuffer = srcImageRGBA->lockRect(srcImageRGBA->getWidth(), srcImageRGBA->getHeight(), pitch);

	if (doTiling) {
		if (!determineTileSize(m_TileSize, srcImageRGBA->getWidth(), srcImageRGBA->getHeight())) {
			return false;
		}

		// Right now we're only supporting square tiles but we set both explicitly here and proceed as if
		// not square so that we only need to update here if this changes.
		uint32_t tileWidth = m_TileSize;
		uint32_t tileHeight = m_TileSize;

		TIFFSetField(m_Tiff, TIFFTAG_TILELENGTH, tileHeight);
		TIFFSetField(m_Tiff, TIFFTAG_TILEWIDTH, tileWidth);

		ImageRGBA* tile = ImageRGBA::create(tileWidth, tileHeight, hasAlpha);
		pitch = 0;
		uint8_t *tilePtr = tile->lockRect(tileWidth, tileHeight, pitch);
		uint8_t *tripleChannelBuffer = NULL;
		if (!hasAlpha) {
			tripleChannelBuffer = new uint8_t[pitch * tileHeight];
		}

		for (unsigned int y = 0 ; y < srcImageRGBA->getHeight() ; y += tileHeight) {
			for (unsigned int x = 0 ; x < srcImageRGBA->getWidth() ; x += tileWidth) {
				srcImageRGBA->copyRect(tile, x, y, 0, 0, tileWidth, tileHeight);

				tilePtr = tile->lockRect(tileWidth, tileHeight, pitch);

				// If there is no alpha channel we compress this down to a 3 channel buffer in order to make a smaller TIFF output at the expense of time now.
				if (!hasAlpha) {
					for (uint8_t *src = tilePtr, *dst = tripleChannelBuffer ; src < tilePtr + (tileHeight * pitch) && dst < tripleChannelBuffer + (tileHeight * pitch) ; src += 4, dst += 3) {
						dst[0] = src[0];
						dst[1] = src[1];
						dst[2] = src[2];
					}
					tilePtr = tripleChannelBuffer;
				}

				int write_ret = TIFFWriteTile(m_Tiff,  tilePtr,  x, y, 0, 0);
				int expected_tile_size = TIFFTileSize(m_Tiff);
				if (write_ret != expected_tile_size) {
					fprintf(stderr, "Failed to write tile to %u, %u (wrote %u bytes)\n", x, y, write_ret);
				}
			}
		}

		delete tile;
		if (tripleChannelBuffer) {
			delete [] tripleChannelBuffer;
		}
	} else {
		// If there's no alpha channel we can compress the buffer 25% and write only the 3 valid channels,
		// this requires an extra copy - more time consuming for the benefit of smaller TIFF output.
		uint8_t *newBuffer = NULL;
		if (!hasAlpha) {
			// pitch is in bytes, not pixels
			newBuffer = new uint8_t[pitch * srcImageRGBA->getHeight()];
			uint8_t *srcPtr = rawBuffer;
			uint8_t *dstPtr = newBuffer;
			for (unsigned int i = 0 ; i < srcImageRGBA->getHeight() * srcImageRGBA->getWidth() ; ++i) {
				*dstPtr = *srcPtr; dstPtr++; srcPtr++;
				*dstPtr = *srcPtr; dstPtr++; srcPtr++;
				*dstPtr = *srcPtr; dstPtr++; srcPtr++;
				srcPtr++;
			}
			rawBuffer = newBuffer;
		}

		for (unsigned int i = 0 ; i < srcImageRGBA->getHeight() ; ++i) {
			if (TIFFWriteScanline(m_Tiff, &rawBuffer[srcImageRGBA->getWidth() * i * (hasAlpha ? 4 : 3)], i, 0) != 1) {
				fprintf(stderr, "Failed to write scanline: %u\n", i);
			}
		}

		if (newBuffer != NULL) {
			delete [] newBuffer;
		}
	}

	srcImageRGBA->unlockRect();

	TIFFFlush(m_Tiff);

	uint8_t *tmpBuffer;
	uint64_t tmpLength;
	if (!m_TempStorage->asBuffer(tmpBuffer, tmpLength)) {
		return false;
	}

	// Is there any way to prevent this copy?  We could get the pointer from m_OutputStorage and wrap that with
	// SeekableMemoryStorage, but then m_OutputStorage's m_UsedBytes would not be valid as it will if we call write().
	// A setter for m_UsedBytes would impact all Storage classes in IC for this niche use-case.
	uint64_t bytes_written = m_OutputStorage->write(tmpBuffer, m_TempStorage->totalBytesWritten());
	if (bytes_written != m_TempStorage->totalBytesWritten()) {
		fprintf(stderr, "Failed to copy encoded temp output to output storage\n");
		return false;
	}

	return true;
}

unsigned int ImageWriterTIFF::writeRows(Image* sourceImage, unsigned int sourceRow, unsigned int numRows)
{
	return 0;
}

bool ImageWriterTIFF::beginWrite(unsigned int width, unsigned int height, EImageColorModel colorModel)
{
	return false;
}

bool ImageWriterTIFF::endWrite()
{
	return false;
}
bool ImageWriterTIFF::initWithStorage(Storage* output)
{

	uint8_t *buffer;
	uint64_t length;
	if (!output) {
		fprintf(stderr, "Output storage pointer is NULL\n");
		return false;
	}

	// Ignore buffer ptr, only want length
	if (!output->asBuffer(buffer, length)) {
		fprintf(stderr, "Failed to get pointer/length of output storage buffer\n");
		return false;
	}
	m_OutputStorage = output;

	// Allocate separately to prevent ownership and overgrowing destination buffer
	m_EncodedDataBuffer = new uint8_t[length];
	m_TempStorage = new SeekableMemoryStorage(m_EncodedDataBuffer, length);
	if (!m_TempStorage || !m_EncodedDataBuffer) {
		fprintf(stderr, "Failed to create temporary storage for encoded output.\n");
		return false;
	}

	return true;
}

void ImageWriterTIFF::setWriteOptions(unsigned int writeOptions)
{
	// Warn of invalid flags according to:
	// 31:25: Reserved
	// 24:16: Tile size ([16..256])
	// 15:0:  Flags
	int bitWidth = (sizeof(int) * 8);
	for (int i = 0 ; i < bitWidth ; ++i) {
		// Skip the place where the tile size may be, that's not a flag.
		if (i >= 16 && i <= 24) {
			continue;
		}

		unsigned int optionSet = writeOptions & (1 << i);
		switch (optionSet) {
			case 0:
			case kSupportedWriteOptions_Progressive:
				break;
			default:
				fprintf(stderr, "ImageWriterTIFF option 0x%x not supported\n", optionSet);
		}
	}

	unsigned int tile_size = (writeOptions & kSupportedWriteOptions_TIFFTileSizeMask) >> 16;
	if (tile_size != 0 && (tile_size < 16 || tile_size > 256)) {
		fprintf(stderr, "ImageWriterTIFF tile size parameter is outside [16..256] boundary, ignoring, using auto (0)\n");
		tile_size = 0;
		writeOptions = (writeOptions & (~kSupportedWriteOptions_TIFFTileSizeMask));
	}

	m_TileSize = tile_size;
	m_WriteOptions = writeOptions;
}

EImageFormat ImageWriterTIFF::Factory::getFormat()
{
	return kImageFormat_TIFF;
}

bool ImageWriterTIFF::Factory::appropriateForInputFormat(EImageFormat inputFormat)
{
	return inputFormat == kImageFormat_TIFF;
}

bool ImageWriterTIFF::Factory::supportsInputColorModel(EImageColorModel colorModel)
{
	return Image::colorModelIsRGBA(colorModel);
}

bool ImageWriterTIFF::Factory::matchesExtension(const char *extension)
{
	return strcasecmp(extension, "tif") == 0 || strcasecmp(extension, "tiff") == 0;
}

uint64_t ImageWriterTIFF::SeekableMemoryStorage::write(const void* dataBuffer, uint64_t numBytes)
{
	uint64_t bytesWritten = MemoryStorage::write(dataBuffer, numBytes);
	if (m_UsedBytes > m_WrittenSize) {
		m_WrittenSize = m_UsedBytes;
	}
	return bytesWritten;
}

uint64_t ImageWriterTIFF::SeekableMemoryStorage::read(void* destBuffer, uint64_t numBytes)
{
	uint64_t bytesToRead = numBytes;
	if(SafeUAdd(m_UsedBytes, numBytes) > m_TotalBytes) {
		bytesToRead = SafeUSub(m_TotalBytes, m_UsedBytes);
	}
	if(bytesToRead > 0) {
		memcpy(destBuffer, m_Buffer + m_UsedBytes, (size_t)bytesToRead);
		m_UsedBytes = SafeUAdd(m_UsedBytes, bytesToRead);
	}
	return bytesToRead;
}

uint64_t ImageWriterTIFF::SeekableMemoryStorage::totalBytesWritten()
{
	return m_WrittenSize;
}

bool ImageWriterTIFF::SeekableMemoryStorage::seek(int64_t pos, SeekMode mode)
{
	if (mode == SeekMode::kSeek_Current) {
		if (m_UsedBytes + pos > m_TotalBytes) {
			fprintf(stderr, "ImageWriterTIFF::SeekableMemoryStorage::seek Seek_Current exceeded buffer size\n");
			return false;
		}
		m_UsedBytes += pos;
	} else if (mode == SeekMode::kSeek_End) {
		if (m_WrittenSize + pos > m_TotalBytes) {
			fprintf(stderr, "ImageWriterTIFF::SeekableMemoryStorage::seek Seek_End exceeded buffer size\n");
			return false;
		}
		m_WrittenSize += pos;
	} else { // SeekMode::kSeek_Set
		if (pos > static_cast<int64_t>(m_TotalBytes)) {
			fprintf(stderr, "ImageWriterTIFF::SeekableMemoryStorage::seek Seek_Set exceeded buffer size\n");
			return false;
		}
		m_UsedBytes = pos;
	}

	return true;
}

uint64_t ImageWriterTIFF::SeekableMemoryStorage::tell()
{
	return m_UsedBytes;
}

} // namespace
