////////////////////////////////////////////////////////
//
// GEM - Graphics Environment for Multimedia
//
// zmoelnig@iem.at
//
// Implementation file
//
//    Copyright (c) 1997-2000 Mark Danks.
//    Copyright (c) 2001-2002 IOhannes m zmoelnig. forum::für::umläute. IEM
//    Copyright (c) 2002 James Tittle & Chris Clepper
//    For information on usage and redistribution, and for a DISCLAIMER OF ALL
//    WARRANTIES, see the file, "GEM.LICENSE.TERMS" in this distribution.
//
// a wrapper for calling Pd's sys_register_loader()
//
/////////////////////////////////////////////////////////
#ifdef _MSC_VER
# pragma warning( disable: 4091)
#endif /* _MSC_VER */

#include "Dylib.h"
#include "Base/CPPExtern.h"

#include <string>
#include <stdio.h>

#if defined __linux__ || defined __APPLE__
#include <unistd.h>
# define DL_OPEN
#endif

#ifdef DL_OPEN
# include <dlfcn.h>
#endif

#if defined _WIN32
# include <io.h>
# include <windows.h>
#endif

#include <iostream>

class GemDylibHandle {
public:
  int dummy1;
#ifdef DL_OPEN
  void * dlhandle;
#endif
#ifdef _WIN32
  HINSTANCE w32handle;
#endif
  int dummy2;
 

  GemDylibHandle(void) : 
    dummy1(0),
#ifdef DL_OPEN
    dlhandle(NULL),
#endif
#ifdef _WIN32
    w32handle(NULL),
#endif
    dummy2(0)
  {;}
};


GemDylib::GemDylib(const CPPExtern*obj, const std::string filename, const std::string extension) 
  throw (GemException) : 
  m_handle(0) {
    m_handle=open(obj, filename, extension);
    if(NULL==m_handle) {
      std::string err="unable to open '";
      err+=filename;
      if(!extension.empty()) {
	err+=".";
	err+=extension;
      }
      err+="'";
      throw GemException(err);
    }
  }

GemDylib::GemDylib(const std::string filename, const std::string extension) throw (GemException) : m_handle(0) {
  m_handle=open(0,   filename, extension);
  if(NULL==m_handle) {
    std::string err="unable to open '";
    err+=filename;
    if(!extension.empty()) {
      err+=".";
      err+=extension;
    }
    err+="'";
    throw GemException(err);
  }
}


GemDylib::~GemDylib(void) {
#ifdef DL_OPEN
  if(m_handle->dlhandle)
    dlclose(m_handle->dlhandle);
  m_handle->dlhandle=NULL;
#endif
#ifdef _WIN32
  if(m_handle->w32handle)
    FreeLibrary(m_handle->handle);
  m_handle->w32handle=NULL;
#endif
  delete m_handle;
}

GemDylib::function_t*GemDylib::proc(const std::string procname) {
  function_t*result=NULL;
  //  if(NULL==procname)return NULL;
#ifdef DL_OPEN
  dlerror();
  if(m_handle->dlhandle)
    result=reinterpret_cast<function_t*>(dlsym(m_handle->dlhandle, procname.c_str()));
  if(NULL!=result)return result;
#endif
#ifdef _WIN32
  if(m_handle->w32handle)
    result=reinterpret_cast<function_t*>(GetProcAddress(m_handle->w32handle, procname.c_str()));
  if(NULL!=result)return result;
#endif

  return result;
}

bool GemDylib::run(const std::string procname) {
  function_t*runproc=proc(procname);
  if(runproc) {
    (*runproc)();
    return true;
  }
  return false;
}

static std::string defaultExtension = 
#ifdef _WIN32
  std::string(".dll")
#elif defined DL_OPEN
  std::string(".so")
#else
  std::string("")
#endif
  ;

static std::string getFullfilename(const t_canvas*canvas, const char*filename, const char*ext) {
  std::string fullname;

  char buf[MAXPDSTRING];
  char*bufptr;
  int fd=0;
  if ((fd=canvas_open(const_cast<t_canvas*>(canvas), filename, ext, buf, &bufptr, MAXPDSTRING, 1))>=0){
    close(fd);
    fullname=buf;
    fullname+="/";
    fullname+=bufptr;
  } else {
    if(canvas) {
      canvas_makefilename(const_cast<t_canvas*>(canvas), const_cast<char*>(filename), buf, MAXPDSTRING);
      fullname=buf;
    } else {
      return std::string("");
    }
  }
  return fullname;
}

GemDylibHandle* GemDylib::open(const CPPExtern*obj, const std::string filename, const std::string extension) {
  GemDylibHandle*handle=new GemDylibHandle();
  char buf[MAXPDSTRING];

  std::string fullname = "";

  const t_canvas*canvas=(obj)?(canvas=const_cast<CPPExtern*>(obj)->getCanvas()):0;
  const char*ext=extension.c_str();

  fullname=getFullfilename(canvas, filename.c_str(), ext);
  if(fullname.empty()) {
    fullname=getFullfilename(canvas, filename.c_str(), defaultExtension.c_str());
  }

  if(fullname.empty()) {
    std::string error="couldn't find '";
    error+=filename;
    error+="'.'";
    error+=ext;
    error+="'";
    throw(GemException(error));
  }

  sys_bashfilename(fullname.c_str(), buf);
  
#ifdef DL_OPEN
  handle->dlhandle=dlopen(fullname.c_str(), RTLD_NOW);
  if(handle->dlhandle)
    return handle;
#endif
#ifdef _WIN32
  UINT errorboxflags=SetErrorMode(SEM_FAILCRITICALERRORS);
  handle->w32handle=LoadLibrary(buf);
  errorboxflags=SetErrorMode(errorboxflags);
  if(handle->w32handle)
    return handle;
#endif

  delete handle;
  handle=NULL;

  std::string errormsg;
#ifdef DL_OPEN
  errormsg=dlerror();
  if(!errormsg.empty()) {
    std::string error="dlerror '";
    error+=errormsg;
    error+="'";
    throw(GemException(error));
  }
#endif
#ifdef _WIN32
  DWORD errorNumber = GetLastError();
  LPVOID lpErrorMessage;
  FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | 
		FORMAT_MESSAGE_FROM_SYSTEM,
		NULL,
		errorNumber,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR) &lpErrorMessage,
		0, NULL );
  std::cerr << "GemDylib: "<<errorNumber<<std::endl;
  std::string error = "DLLerror: ";
  error+=(unsigned int)errorNumber;
  throw(GemException(error));
#endif
  
  return 0;
}


bool GemDylib::LoadLib(const std::string basefilename, const std::string extension, const std::string procname) {
  try {
    GemDylib*dylib=new GemDylib(basefilename, extension);
    if(NULL!=dylib) {
      dylib->run(procname);
      return true;
    }
  } catch (GemException&x) {
    std::cerr << "GemDylib::LoadLib: "<<x.what()<<std::endl;
  }

  return false;
}


const std::string GemDylib::getDefaultExtension(void) {
  return defaultExtension;
}