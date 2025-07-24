/**
 * File: imageBuffer.cpp
 *
 * Author:  Al Chaussee
 * Created: 09/19/2018
 *
 * Description: Wrapper around raw image data
 *
 * Copyright: Anki, Inc. 2018
 *
 **/

#include "coretech/vision/engine/imageBuffer/imageBuffer.h"

#include "coretech/vision/engine/imageBuffer/conversions/imageConversions.h"
#include "coretech/vision/engine/image_impl.h"
#include "coretech/vision/engine/imageCache.h"
#include "coretech/vision/engine/neonMacros.h"

#include "opencv2/core.hpp"
#include "opencv2/imgproc.hpp"

#include <unistd.h>

namespace Anki {
namespace Vision {

ImageBuffer::ImageBuffer(u8* data, s32 numRows, s32 numCols, ImageEncoding format, TimeStamp_t timestamp, s32 id)
: _rawData(data)
, _rawNumRows(numRows)
, _rawNumCols(numCols)
, _format(format)
, _imageId(id)
, _timestamp(timestamp)
{

}

void ImageBuffer::GetNumRowsCols(s32& rows, s32& cols) const
{
  switch(_format)
  {
    case ImageEncoding::BAYER:
      {
        rows = _rawNumRows;
        cols = _rawNumCols;
      }
      break;
    case ImageEncoding::RawRGB:
      {
        rows = _rawNumRows;
        cols = _rawNumCols;
      }
      break;
    case ImageEncoding::YUV420sp:
      {
        // YUV420sp data is has extra rows for UV
        rows = (_rawNumRows * (2.f/3.f));
        cols = _rawNumCols;
      }
      break;
    case ImageEncoding::RawGray:
      {
        rows = _rawNumRows;
        cols = _rawNumCols;
      }
      break;
    default:
      {
        rows = 0;
        cols = 0;

        ANKI_VERIFY(false,
                    "ImageBuffer.GetNumRowsCols.UnsupportedFormat",
                    "%s",
                    EnumToString(_format));
      }
      break;
  }
}
  
s32 ImageBuffer::GetNumCols() const
{
  s32 rows = 0;
  s32 cols = 0;
  GetNumRowsCols(rows, cols);
  return cols;
}

s32 ImageBuffer::GetNumRows() const
{
  s32 rows = 0;
  s32 cols = 0;
  GetNumRowsCols(rows, cols);
  return rows;
}
    
bool ImageBuffer::GetRGB(ImageRGB& rgb, ImageCacheSize size) const
{
  DEV_ASSERT(_rawData != nullptr, "ImageBuffer.GetRGB.NullData");

  bool res = false;
  
  switch(_format)
  {
    case ImageEncoding::BAYER:
      {
        res = GetRGBFromBAYER(rgb, size);
      }
      break;
    case ImageEncoding::RawRGB:
      {
        res = GetRGBFromRawRGB(rgb, size);
      }
      break;
    case ImageEncoding::YUV420sp:
      {
        res = GetRGBFromYUV420sp(rgb, size);
      }
      break;
    case ImageEncoding::RawGray:
      {
        res = GetRGBFromRawGray(rgb, size);
      }
      break;
    default:
      {
        DEV_ASSERT_MSG(false,
                       "ImageBuffer.GetRGB.UnsupportedFormat",
                       "%s", EnumToString(_format));
        return false;
      }
      break;
  }

  // Even if conversion failed doesn't matter that we set these
  rgb.SetTimestamp(_timestamp);
  rgb.SetImageId(_imageId);

  return res;
}
  
bool ImageBuffer::GetGray(Image& gray, ImageCacheSize size) const
{
  DEV_ASSERT(_rawData != nullptr, "ImageBuffer.GetRGB.NullData");

  bool res = false;
  
  switch(_format)
  {
    case ImageEncoding::BAYER:
      {
        res = GetGrayFromBAYER(gray, size);
      }
      break;
    case ImageEncoding::RawRGB:
      {
        res = GetGrayFromRawRGB(gray, size);
      }
      break;
    case ImageEncoding::YUV420sp:
      {
        res = GetGrayFromYUV420sp(gray, size);
      }
      break;
    case ImageEncoding::RawGray:
      {
        res = GetGrayFromRawGray(gray, size);
      }
      break;
    default:
      {
        DEV_ASSERT_MSG(false,
                       "ImageBuffer.GetGray.UnsupportedFormat",
                       "%s", EnumToString(_format));
        return false;
      }
      break;
  }

  gray.SetTimestamp(_timestamp);
  gray.SetImageId(_imageId);

  return res;
}

bool ImageBuffer::GetRGBFromBAYER(ImageRGB& rgb, ImageCacheSize size) const
{
  switch(size)
  {
    case ImageCacheSize::Full:
      {
        ImageConversions::DemosaicBGGR10ToRGB(_rawData,
                                              _rawNumRows,
                                              _rawNumCols,
                                              rgb);
      }
      return true;
      
    case ImageCacheSize::Half:
      {
        ImageConversions::HalveBGGR10ToRGB(_rawData,
                                           _rawNumRows,
                                           _rawNumCols,
                                           rgb);
      }
      return true;
      
    case ImageCacheSize::Quarter:
      {
        // ImageCacheSizes smaller than Full are defined relative to Full
        // So Half of Full ends up being quarter sized bayer
        ImageConversions::QuarterBGGR10ToRGB(_rawData,
                                             _rawNumRows,
                                             _rawNumCols,
                                             rgb);
      }
      return true;
      
    case ImageCacheSize::Eighth:
      {
        // Get closest conversion we know how to do and then
        // resize to the correct size
        ImageConversions::QuarterBGGR10ToRGB(_rawData,
                                             _rawNumRows,
                                             _rawNumCols,
                                             rgb);
        rgb.Resize(0.5f, _resizeMethod);
      }
      return true;
  }
}

bool ImageBuffer::GetRGBFromRawRGB(ImageRGB& rgb, ImageCacheSize size) const
{
  // Wrap ImageRGB around raw rgb data and we will resize it to the "size"
  ImageRGB orig(_rawNumRows, _rawNumCols, _rawData);

  f32 scaleFactor = ImageCacheSizeToScaleFactor(size);

  if(Util::IsNear(scaleFactor, 1.f))
  {
    rgb = orig;
    return true;
  }
  
  const s32 desiredNumRows = _rawNumRows * scaleFactor;
  const s32 desiredNumCols = _rawNumCols * scaleFactor;
  rgb.Allocate(desiredNumRows, desiredNumCols);
  
  orig.Resize(rgb, _resizeMethod);
  
  return true;
}

bool ImageBuffer::GetRGBFromYUV420sp(ImageRGB& rgb, ImageCacheSize size) const
{
  const f32 scaleFactor = ImageCacheSizeToScaleFactor(size);

  s32 rows = 0;
  s32 cols = 0;
  GetNumRowsCols(rows, cols);
  // TODO Add additional methods to convert and resize at the same time
  // instead of converting and then resizing
  ImageConversions::ConvertYUV420spToRGB(_rawData,
                                         rows,
                                         cols,
                                         rgb);
  
  rgb.Resize(scaleFactor, _resizeMethod);

  return true;
}

bool ImageBuffer::GetRGBFromRawGray(ImageRGB& rgb, ImageCacheSize size) const
{
  Image gray;

  bool res = GetGrayFromRawGray(gray, size);
  if(res)
  {
    rgb.SetFromGray(gray);
  }

  return res;
}

bool ImageBuffer::GetGrayFromBAYER(Image& gray, ImageCacheSize size) const
{
  // For now the only optimized conversion from Bayer to Gray is for halved gray
  // So do that and then resize image if needed
  ImageConversions::HalveBGGR10ToGray(_rawData,
                                      _rawNumRows,
                                      _rawNumCols,
                                      gray);

  switch(size)
  {
    // TODO VIC-9781 Add method to convert directly to Full sized Gray
    // instead of halving and resizing
    case ImageCacheSize::Full:
      gray.Resize(2.0f, _resizeMethod);
      return true;
      
    case ImageCacheSize::Half:
      return true;
      
    case ImageCacheSize::Quarter:
      gray.Resize(0.5f, _resizeMethod);
      return true;

    case ImageCacheSize::Eighth:
      gray.Resize(0.25f, _resizeMethod);
      return true;
  }
}

bool ImageBuffer::GetGrayFromRawRGB(Image& gray, ImageCacheSize size) const
{
  ImageRGB rgb;
  // For now there are no optimized conversions from RawRGB to Gray so convert to
  // RGB and then fill gray
  bool res = GetRGBFromRawRGB(rgb, size);
  if(res)
  {
    rgb.FillGray(gray);
  }
  
  return res;
}

bool ImageBuffer::GetGrayFromYUV420sp(Image& gray, ImageCacheSize size) const
{
  ImageRGB rgb;
  // For now there are no optimized conversions from YUV420sp to Gray so convert to
  // RGB and then fill gray
  bool res = GetRGBFromYUV420sp(rgb, size);
  if(res)
  {
    rgb.FillGray(gray);
  }
  
  return res;
}

bool ImageBuffer::GetGrayFromRawGray(Image& gray, ImageCacheSize size) const
{
  // Wrap Image around raw gray data and we will resize it to the "size"
  Image orig(_rawNumRows, _rawNumCols, _rawData);

  const f32 scaleFactor = ImageCacheSizeToScaleFactor(size);

  if(Util::IsNear(scaleFactor, 1.f))
  {
    gray = orig;
    return true;
  }

  const s32 desiredNumRows = _rawNumRows * scaleFactor;
  const s32 desiredNumCols = _rawNumCols * scaleFactor;
  gray.Allocate(desiredNumRows, desiredNumCols);
  
  orig.Resize(gray, _resizeMethod);
  
  return true;
}

}
}
