////////////////////////////////////////////////////////
//
// GEM - Graphics Environment for Multimedia
//
// zmoelnig@iem.at
//
// Implementation file
//
//    Copyright (c) 1997-1999 Mark Danks.
//    Copyright (c) Günther Geiger.
//    Copyright (c) 2001-2011 IOhannes m zmölnig. forum::für::umläute. IEM. zmoelnig@iem.at
//    For information on usage and redistribution, and for a DISCLAIMER OF ALL
//    WARRANTIES, see the file, "GEM.LICENSE.TERMS" in this distribution.
//
/////////////////////////////////////////////////////////
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#ifdef _WIN32

#include "filmAVI.h"
#include "plugins/PluginFactory.h"
#include "Utils/Functions.h"
#include "Gem/Properties.h"
#include "Gem/RTE.h"

using namespace gem::plugins;

REGISTER_FILMFACTORY("AVI", filmAVI);


/////////////////////////////////////////////////////////
//
// filmAVI
//
/////////////////////////////////////////////////////////
// Constructor
//
/////////////////////////////////////////////////////////

filmAVI :: filmAVI(void) :
  m_wantedFormat(GEM_RGBA),
  m_fps(-1.0),
  m_numFrames(-1),
  m_curFrame(-1),

  m_nRawBuffSize(0),
  m_RawBuffer(NULL),
  m_format(GEM_RGB), /* it's really GL_BGR_EXT */
  m_reqFrame(0),
  m_frame(NULL),
  m_pbmihRaw(NULL),
  m_pbmihDst(NULL),
  m_hic(NULL),
  m_getFrame(NULL),
  m_streamVid(NULL)
{
  AVIFileInit();
}

/////////////////////////////////////////////////////////
// Destructor
//
/////////////////////////////////////////////////////////
filmAVI :: ~filmAVI(void)
{
  close();
  AVIFileExit();
}

void filmAVI :: close(void)
{
  if (m_streamVid) {
    AVIStreamRelease(m_streamVid);
    m_streamVid = NULL;
  }

  if (m_pbmihRaw) {
    delete[] m_pbmihRaw;
    m_pbmihRaw = NULL;
  }

  if (m_pbmihDst) {
    delete[] m_pbmihDst;
    m_pbmihDst = NULL;
  }

  if (m_hic)    {
    ICDecompressEnd(m_hic);
    ICClose(m_hic);
    m_hic = NULL;
  }

  if (m_frame) {
    delete[]m_frame;
    m_frame = NULL;
  }
  if (m_RawBuffer) {
    delete[] m_RawBuffer;
    m_RawBuffer = NULL;
  }
  m_nRawBuffSize = 0;
}

/////////////////////////////////////////////////////////
// open the file
//
/////////////////////////////////////////////////////////
bool filmAVI :: open(const std::string&filename,
                     const gem::Properties&wantProps)
{
  AVISTREAMINFO streaminfo;
  long lSize = 0; // in bytes

  double d;
  if(wantProps.get("colorspace", d) && d>0) {
    m_wantedFormat=d;
  }

  if (AVIStreamOpenFromFile(&m_streamVid, filename.c_str(), streamtypeVIDEO,
                            0, OF_READ, NULL)) {
    logpost(0, 3+0, "[GEM:filmAVI] Unable to open file: %s", filename.c_str());
    goto unsupported;
  }

  if( AVIStreamInfo( m_streamVid, &streaminfo, sizeof(streaminfo)) ||
      AVIStreamReadFormat(m_streamVid, AVIStreamStart(m_streamVid), NULL,
                          &lSize))  {
    logpost(0, 3+0, "[GEM:filmAVI] Unable to read file format: %s",
            filename.c_str());
    goto unsupported;
  }

  m_pbmihRaw = (BITMAPINFOHEADER*) new char[lSize];

  if(AVIStreamReadFormat(m_streamVid, AVIStreamStart(m_streamVid),
                         m_pbmihRaw, &lSize)) {
    logpost(0, 3+0, "[GEM:filmAVI] Unable to read file format: %s",
            filename.c_str());
    goto unsupported;
  }
  if ((8 == m_pbmihRaw->biBitCount)
      || ((40 == m_pbmihRaw->biBitCount)
          && (mmioFOURCC('c','v','i','d') == m_pbmihRaw->biCompression))) {
    // HACK: attempt to decompress 8 bit films or BW cinepak films to greyscale
    m_pbmihDst = (BITMAPINFOHEADER*) new char[sizeof(BITMAPINFOHEADER) +
                                          256*3];
    logpost(0, 3+0, "[GEM:filmAVI] Loading as greyscale");

    *m_pbmihDst = *m_pbmihRaw;
    m_pbmihDst->biSize = sizeof(BITMAPINFOHEADER);

    m_format = GEM_GRAY;

    m_pbmihDst->biBitCount                      = 8;
    m_pbmihDst->biClrUsed                       = 256;
    m_pbmihDst->biClrImportant          = 256;

    char* pClrPtr = ((char*)m_pbmihDst) + sizeof(BITMAPINFOHEADER);
    for (int i = 0; i < 256; i++) {
      *pClrPtr++ = i;
      *pClrPtr++ = i;
      *pClrPtr++ = i;
    }
  } else {
    m_pbmihDst = (BITMAPINFOHEADER*) new char[sizeof(BITMAPINFOHEADER)];
    *m_pbmihDst = *m_pbmihRaw;

    m_format = GEM_RGB;

    m_pbmihDst->biBitCount      = 24;
    m_pbmihDst->biClrUsed       = 0;
    m_pbmihDst->biClrImportant  = 0;
  }

  m_pbmihDst->biCompression             = BI_RGB;
  m_pbmihDst->biSizeImage                       = 0;

  // Get the length of the movie
  m_numFrames = streaminfo.dwLength - 1;

  m_fps = (double)streaminfo.dwRate / streaminfo.dwScale;

  m_image.image.xsize = streaminfo.rcFrame.right - streaminfo.rcFrame.left;
  m_image.image.ysize = streaminfo.rcFrame.bottom - streaminfo.rcFrame.top;
  m_image.image.setFormat(m_wantedFormat);
  m_image.image.reallocate();

  if (!(m_hic = ICLocate(ICTYPE_VIDEO, 0, m_pbmihRaw, m_pbmihDst,
                         ICMODE_DECOMPRESS))) {
    logpost(0, 3+0, "[GEM:filmAVI] Could not find decompressor: %s",
            filename.c_str());
    goto unsupported;
  }
  if (m_format==GEM_GRAY) {
    if (ICERR_OK != ICDecompressSetPalette(m_hic, m_pbmihDst)) {
      logpost(0, 3+0, "[GEM:filmAVI] Could not set palette: %s", filename.c_str());
    }
  }

  if (ICERR_OK != ICDecompressBegin(m_hic, m_pbmihRaw, m_pbmihDst)) {
    logpost(0, 3+0, "[GEM:filmAVI] Could not begin decompression: %s",
            filename.c_str());
    goto unsupported;
  }
  //if (!m_pbmihRaw->biSizeImage)
  //    m_pbmihRaw->biSizeImage = m_xsize * m_ysize * m_csize;
  //m_nRawBuffSize = MIN(streaminfo.dwSuggestedBufferSize, m_pbmihRaw->biSizeImage);
  m_nRawBuffSize = MAX(static_cast<int>(streaminfo.dwSuggestedBufferSize),
                       static_cast<int>(m_pbmihRaw->biSizeImage));
  if(!m_nRawBuffSize) {
    m_nRawBuffSize = m_image.image.xsize * m_image.image.ysize * 3;
  }

  m_RawBuffer = new unsigned char[m_nRawBuffSize];
  m_frame     = new unsigned char[m_nRawBuffSize];
  m_reqFrame = 0;
  m_curFrame = -1;
  return true;

unsupported:
  close();
  return false;
}

/////////////////////////////////////////////////////////
// render
//
/////////////////////////////////////////////////////////
pixBlock* filmAVI :: getFrame(void)
{
  BITMAPINFOHEADER* pbmih;
  long lBytesWritten;
  unsigned char*data=NULL;
  if (m_reqFrame > m_numFrames) {
    m_reqFrame=m_numFrames;
  }

  m_image.newimage=1;
  m_image.image.upsidedown=false;
  m_image.image.setFormat(m_wantedFormat);
  m_image.image.reallocate();

  if (!AVIStreamRead(m_streamVid,
                     m_reqFrame, 1,
                     m_RawBuffer, m_nRawBuffSize,
                     &lBytesWritten, 0)) {
    m_pbmihRaw->biSize = lBytesWritten;
    ICDecompress(m_hic, 0, m_pbmihRaw, m_RawBuffer, m_pbmihDst, m_frame);
    data=m_frame;
  } else {
    data=(unsigned char *)AVIStreamGetFrame(m_getFrame, m_curFrame)+40;
  }
  if(!data) {
    return 0;
  }

  switch(m_format) {
  case GEM_GRAY:
    m_image.image.fromGray(data);
    break;
  default:
    m_image.image.fromBGR(data);
  }
  return &m_image;
}

film::errCode filmAVI :: changeImage(int imgNum, int trackNum)
{
  if (imgNum<0) {
    return film::FAILURE;
  }
  if (m_numFrames<0) {
    m_reqFrame = imgNum;
    return film::SUCCESS;
  }
  if (imgNum>m_numFrames) {
    m_reqFrame=m_numFrames;
    return film::FAILURE;
  }

  m_reqFrame = imgNum;
  return film::SUCCESS;
}


///////////////////////////////
// Properties
bool filmAVI::enumProperties(gem::Properties&readable,
                             gem::Properties&writeable)
{
  readable.clear();
  writeable.clear();

  gem::any value;
  value=0.;
  readable.set("fps", value);
  readable.set("frames", value);
  readable.set("width", value);
  readable.set("height", value);

  writeable.set("colorspace", value);

  return false;
}

void filmAVI::setProperties(gem::Properties&props)
{
  double d;
  if(props.get("colorspace", d)) {
    m_wantedFormat=d;
  }
}

void filmAVI::getProperties(gem::Properties&props)
{
  std::vector<std::string> keys=props.keys();
  gem::any value;
  double d;
  for(unsigned int i=0; i<keys.size(); i++) {
    std::string key=keys[i];
    props.erase(key);
    if("fps"==key) {
      d=m_fps;
      value=d;
      props.set(key, value);
    }
    if("frames"==key) {
      d=m_numFrames;
      value=d;
      props.set(key, value);
    }
    if("width"==key) {
      d=m_image.image.xsize;
      value=d;
      props.set(key, value);
    }
    if("height"==key) {
      d=m_image.image.ysize;
      value=d;
      props.set(key, value);
    }
  }
}

#endif
