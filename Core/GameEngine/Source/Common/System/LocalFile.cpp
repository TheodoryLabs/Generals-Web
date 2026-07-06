/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.
//  //
//																																						//
////////////////////////////////////////////////////////////////////////////////

//----------------------------------------------------------------------------
//
//                       Westwood Studios Pacific.
//
//                       Confidential Information
//                Copyright(C) 2001 - All Rights Reserved
//
//----------------------------------------------------------------------------
//
// Project:   WSYS Library
//
// Module:    IO_
//
// File name: IO_LocalFile.cpp
//
// Created:   4/23/01
//
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
//         Includes
//----------------------------------------------------------------------------

#include "PreRTS.h"

#include <fcntl.h>
#if !defined(__linux__) && !defined(__EMSCRIPTEN__)
#include <io.h>
#else
#include <unistd.h>
#endif
#include <ctype.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "Common/LocalFile.h"
#include "Common/PerfTimer.h"
#include "Common/RAMFile.h"
#include "Lib/BaseType.h"

#if defined(__EMSCRIPTEN__)
extern "C" bool Web_VFS_Read_File_Sync(const char *path, void **out_data,
                                       unsigned int *out_size);
extern "C" void Web_VFS_Evict_File(const char *path);
#endif

//----------------------------------------------------------------------------
//         Externals
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
//         Defines
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
//         Private Types
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
//         Private Data
//----------------------------------------------------------------------------

static Int s_totalOpen = 0;

//----------------------------------------------------------------------------
//         Public Data
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
//         Private Prototypes
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
//         Private Functions
//----------------------------------------------------------------------------

//=================================================================
// LocalFile::LocalFile
//=================================================================

LocalFile::LocalFile()
#if USE_BUFFERED_IO
    : m_file(nullptr)
#else
    : m_handle(-1)
#endif
{
}

//----------------------------------------------------------------------------
//         Public Functions
//----------------------------------------------------------------------------

//=================================================================
// LocalFile::~LocalFile
//=================================================================

LocalFile::~LocalFile() { closeFile(); }

//=================================================================
// LocalFile::open
//=================================================================
/**
 * This function opens a file using the standard C open() or
 * fopen() call. Access flags are mapped to the appropriate flags.
 * Returns true if file was opened successfully.
 */
//=================================================================

// DECLARE_PERF_TIMER(LocalFile)
Bool LocalFile::open(const Char *filename, Int access, size_t bufferSize) {
  // USE_PERF_TIMER(LocalFile)
  if (!File::open(filename, access)) {
    return FALSE;
  }

#if defined(__EMSCRIPTEN__)
  // Intercept read-only accesses and try fetching from the HTTP BigVFS first
  if (!(access & WRITE)) {
    // BigVFS caches requested files in memory

    void *vfs_data = nullptr;
    unsigned int vfs_size = 0;

    // GeneralsX @feature WebPort 2026-05-05 — normalise the requested
    // filename so BigVFS lookups succeed.
    //
    // The engine builds paths in Win32 form: mixed case + backslashes
    // ("Maps\\ShellMapMD\\map.ini"). The BigVFS layer keys files by the
    // POSIX/lowercase form delivered by the deploy ("maps/shellmapmd/map.ini").
    // Without this fixup the shell map's map.ini fails to load with
    //   ASSERTION FAILURE: INI::load, cannot open file 'Maps\\ShellMapMD\\map.ini'
    // and the menu's animated background never appears.
    char gx_vfs_path[1024];
    const Char *vfs_path = filename;
    if (filename) {
      size_t len = strlen(filename);
      if (len < sizeof(gx_vfs_path)) {
        for (size_t i = 0; i < len; ++i) {
          char c = filename[i];
          if (c == '\\') c = '/';
          else if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
          gx_vfs_path[i] = c;
        }
        gx_vfs_path[len] = '\0';
        vfs_path = gx_vfs_path;
      }
    }

    if (Web_VFS_Read_File_Sync(vfs_path, &vfs_data, &vfs_size)) {
      // Wrap the fetched memory buffer in a standard FILE* using fmemopen
      m_file = fmemopen(vfs_data, vfs_size, "rb");
      if (m_file) {
        ++s_totalOpen;
        return TRUE;
      }
    }
    // If not found in BigVFS, fall through to check local Emscripten MEMFS
  }

  // Path normalization for fopen on Emscripten. The engine builds paths with
  // backslash separators (it was a Win32 codebase); Emscripten's MEMFS uses
  // POSIX semantics where '\' is a valid filename character, NOT a separator.
  // Without this fixup, writing a save file produces a single weirdly-named
  // file at root rather than nested directories under /userdata.
  //
  // We allocate on the stack (paths are bounded; PATH_MAX worst case is ~4K).
  // Failure modes: if the path is longer than the buffer we just truncate —
  // the fopen will fail downstream, which is the same outcome as before.
  // GeneralsX @feature WebPort 2026-05-04 — IDBFS persistence
  // GeneralsX @feature WebPort 2026-05-05 — normalise BOTH slashes and case.
  // The deploy stores all assets under /maps/shellmapmd/map.ini etc. (lower
  // case, forward slashes); the engine builds Win32-form paths
  // ("Maps\\ShellMapMD\\map.ini"). MEMFS is POSIX case-sensitive, so without
  // the lowercase fixup INI::load fails with
  //   ASSERTION FAILURE: INI::load, cannot open file 'Maps\\ShellMapMD\\map.ini'
  // for the shellmap.
  //
  // Skip lowercasing for absolute paths under /home or /userdata so user
  // save-game directories (which are created with mixed case) keep working.
  char gx_normalized_path[1024];
  const Char *fopen_path = filename;
  if (filename) {
    size_t len = strlen(filename);
    if (len < sizeof(gx_normalized_path)) {
      const bool is_userdata =
          (len >= 5 && filename[0] == '/' &&
           (strncmp(filename, "/home", 5) == 0 ||
            strncmp(filename, "/user", 5) == 0 ||
            strncmp(filename, "/tmp",  4) == 0));
      bool needs_fix = false;
      for (size_t i = 0; i < len; ++i) {
        const Char c = filename[i];
        if (c == '\\') { needs_fix = true; break; }
        if (!is_userdata && c >= 'A' && c <= 'Z') { needs_fix = true; break; }
      }
      if (needs_fix) {
        for (size_t i = 0; i < len; ++i) {
          Char c = filename[i];
          if (c == '\\') c = '/';
          else if (!is_userdata && c >= 'A' && c <= 'Z')
            c = (Char)(c - 'A' + 'a');
          gx_normalized_path[i] = c;
        }
        gx_normalized_path[len] = '\0';
        fopen_path = gx_normalized_path;
      }
    }
  }
#  define GX_FOPEN_PATH fopen_path
#else
#  define GX_FOPEN_PATH filename
#endif

  /* here we translate WSYS file access to the std C equivalent */
#if USE_BUFFERED_IO

  // r    open for reading (The file must exist)
  // w    open for writing (creates file if it doesn't exist). Deletes content
  // and overwrites the file. a    open for appending (creates file if it
  // doesn't exist). r+   open for reading and writing (The file must exist). w+
  // open for reading and writing.
  //      If file exists deletes content and overwrites the file, otherwise
  //      creates an empty new file.
  // a+   open for reading and writing (append if file exists).

  const Bool write = (m_access & WRITE) != 0;
  const Bool readwrite = (m_access & READWRITE) == READWRITE;
  const Bool append = (m_access & APPEND) != 0;
  const Bool create = (m_access & CREATE) != 0;
  const Bool truncate = (m_access & TRUNCATE) != 0;
  const Bool binary = (m_access & BINARY) != 0;

  const Char *mode = nullptr;

  // Mode string selection (mimics _open flag combinations)
  // TEXT is implicit for fopen if 'b' is not present
  if (readwrite) {
    if (append)
      mode = binary ? "a+b" : "a+";
    else if (truncate || create)
      mode = binary ? "w+b" : "w+";
    else
      mode = binary ? "r+b" : "r+";
  } else if (write) {
    if (append)
      mode = binary ? "ab" : "a";
    else
      mode = binary ? "wb" : "w";
  } else // implicitly read-only
  {
    mode = binary ? "rb" : "r";
  }

  m_file = fopen(GX_FOPEN_PATH, mode);
#undef GX_FOPEN_PATH
  if (m_file == nullptr) {
    goto error;
  }

  {
    Int result = 0;

    if (bufferSize == 0) {
      result = setvbuf(m_file, nullptr, _IONBF, 0); // Uses no buffering
    } else {
      const Int bufferMode = (m_access & LINEBUF)
                                 ? _IOLBF  // Uses line buffering
                                 : _IOFBF; // Uses full buffering

      // Buffer is expected to lazy allocate on first read or write later
      result = setvbuf(m_file, nullptr, bufferMode, bufferSize);
    }

    DEBUG_ASSERTCRASH(result == 0, ("LocalFile::open - setvbuf failed"));
  }

#else

  int flags = 0;

  if (m_access & CREATE) {
    flags |= _O_CREAT;
  }
  if (m_access & TRUNCATE) {
    flags |= _O_TRUNC;
  }
  if (m_access & APPEND) {
    flags |= _O_APPEND;
  }
  if (m_access & TEXT) {
    flags |= _O_TEXT;
  }
  if (m_access & BINARY) {
    flags |= _O_BINARY;
  }

  if ((m_access & READWRITE) == READWRITE) {
    flags |= _O_RDWR;
  } else if (m_access & WRITE) {
    flags |= _O_WRONLY;
    flags |= _O_CREAT;
  } else // implicitly read-only
  {
    flags |= _O_RDONLY;
  }

  m_handle = _open(filename, flags, _S_IREAD | _S_IWRITE);

  if (m_handle == -1) {
    goto error;
  }

#endif

  ++s_totalOpen;
  ///	DEBUG_LOG(("LocalFile::open %s (total %d)",filename,s_totalOpen));
  if (m_access & APPEND) {
    if (seek(0, END) < 0) {
      goto error;
    }
  }

  return TRUE;

error:

  // TheSuperHackers @fix Instance must never delete itself in this function.
  closeWithoutDelete();

  return FALSE;
}

//=================================================================
// LocalFile::close
//=================================================================
/**
 * Closes the current file if it is open.
 * Must call LocalFile::close() for each successful LocalFile::open() call.
 */
//=================================================================

void LocalFile::close() {
  closeFile();
  File::close();
}

//=================================================================

void LocalFile::closeWithoutDelete() {
  closeFile();
  File::closeWithoutDelete();
}

//=================================================================

void LocalFile::closeFile() {
#if USE_BUFFERED_IO
  if (m_file) {
#if defined(__EMSCRIPTEN__)
    const char* filename = getName();
    if (filename) {
      size_t len = strlen(filename);
      if (len > 4) {
        const char* ext = filename + len - 4;
        if (strcasecmp(ext, ".w3d") == 0 || strcasecmp(ext, ".tga") == 0 || 
            strcasecmp(ext, ".dds") == 0 || strcasecmp(ext, ".wav") == 0) {
          Web_VFS_Evict_File(filename);
        }
      }
    }
#endif
    fclose(m_file);
    m_file = nullptr;
    --s_totalOpen;
  }
#else
  if (m_handle != -1) {
    _close(m_handle);
    m_handle = -1;
    --s_totalOpen;
  }
#endif
}

//=================================================================
// LocalFile::read
//=================================================================

Int LocalFile::read(void *buffer, Int bytes) {
  // USE_PERF_TIMER(LocalFile)
  if (!m_open) {
    return -1;
  }

  if (buffer == nullptr) {
#if USE_BUFFERED_IO
    fseek(m_file, bytes, SEEK_CUR);
#else
    _lseek(m_handle, bytes, SEEK_CUR);
#endif
    return bytes;
  }

#if USE_BUFFERED_IO
  Int ret = fread(buffer, 1, bytes, m_file);
#else
  Int ret = _read(m_handle, buffer, bytes);
#endif

  return ret;
}

//=================================================================
// LocalFile::readChar
//=================================================================

Int LocalFile::readChar() {
  Char character = '\0';

  Int ret = read(&character, sizeof(character));

  if (ret == sizeof(character))
    return (Int)character;

  return EOF;
}

//=================================================================
// LocalFile::readWideChar
//=================================================================

Int LocalFile::readWideChar() {
  WideChar character = L'\0';

  Int ret = read(&character, sizeof(character));

  if (ret == sizeof(character))
    return (Int)character;

  return WEOF;
}

//=================================================================
// LocalFile::write
//=================================================================

Int LocalFile::write(const void *buffer, Int bytes) {

  if (!m_open || !buffer) {
    return -1;
  }

#if USE_BUFFERED_IO
  Int ret = fwrite(buffer, 1, bytes, m_file);
#else
  Int ret = _write(m_handle, buffer, bytes);
#endif
  return ret;
}

//=================================================================
// LocalFile::writeFormat - Ascii
//=================================================================

Int LocalFile::writeFormat(const Char *format, ...) {
  char buffer[1024];

  va_list args;
  va_start(args, format);
  Int length = vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);

  return write(buffer, length);
}

//=================================================================
// LocalFile::writeFormat - Wide character
//=================================================================

Int LocalFile::writeFormat(const WideChar *format, ...) {
  WideChar buffer[1024];

  va_list args;
  va_start(args, format);
  Int length =
      vswprintf(buffer, sizeof(buffer) / sizeof(WideChar), format, args);
  va_end(args);

  return write(buffer, length * sizeof(WideChar));
}

//=================================================================
// LocalFile::writeChar - Ascii
//=================================================================

Int LocalFile::writeChar(const Char *character) {
  if (write(character, sizeof(Char)) == sizeof(Char)) {
    return (Int)character;
  }

  return EOF;
}

//=================================================================
// LocalFile::writeChar - Wide character
//=================================================================

Int LocalFile::writeChar(const WideChar *character) {
  if (write(character, sizeof(WideChar)) == sizeof(WideChar)) {
    return (Int)character;
  }

  return WEOF;
}

//=================================================================
// LocalFile::seek
//=================================================================

Int LocalFile::seek(Int pos, seekMode mode) {
  int lmode;

  switch (mode) {
  case START:
    DEBUG_ASSERTCRASH(pos >= 0, ("LocalFile::seek - pos must be >= 0 when "
                                 "seeking from the beginning of the file"));
    lmode = SEEK_SET;
    break;
  case CURRENT:
    lmode = SEEK_CUR;
    break;
  case END:
    lmode = SEEK_END;
    break;
  default:
    DEBUG_CRASH(("LocalFile::seek - bad seek mode"));
    return -1;
  }

#if USE_BUFFERED_IO
  Int ret = fseek(m_file, pos, lmode);
  if (ret == 0)
    return ftell(m_file);
  else
    return -1;
#else
  Int ret = _lseek(m_handle, pos, lmode);
#endif
  return ret;
}

//=================================================================
// LocalFile::flush
//=================================================================

Bool LocalFile::flush() { return fflush(m_file) != EOF; }

//=================================================================
// LocalFile::scanInt
//=================================================================
// skips preceding whitespace and stops at the first non-number
// or at EOF
Bool LocalFile::scanInt(Int &newInt) {
  newInt = 0;
  AsciiString tempstr;
  Char c;
  Int val;

  // skip preceding non-numeric characters
  do {
#if USE_BUFFERED_IO
    val = fread(&c, 1, 1, m_file);
#else
    val = _read(m_handle, &c, 1);
#endif
  } while ((val != 0) && (((c < '0') || (c > '9')) && (c != '-')));

  if (val == 0) {
    return FALSE;
  }

  do {
    tempstr.concat(c);
#if USE_BUFFERED_IO
    val = fread(&c, 1, 1, m_file);
#else
    val = _read(m_handle, &c, 1);
#endif
  } while ((val != 0) && ((c >= '0') && (c <= '9')));

  // put the last read char back, since we didn't use it.
  if (val != 0) {
#if USE_BUFFERED_IO
    fseek(m_file, -1, SEEK_CUR);
#else
    _lseek(m_handle, -1, SEEK_CUR);
#endif
  }

  newInt = atoi(tempstr.str());
  return TRUE;
}

//=================================================================
// LocalFile::scanReal
//=================================================================
// skips preceding whitespace and stops at the first non-number
// or at EOF
Bool LocalFile::scanReal(Real &newReal) {
  newReal = 0.0;
  AsciiString tempstr;
  Char c;
  Int val;
  Bool sawDec = FALSE;

  // skip the preceding white space
  do {
#if USE_BUFFERED_IO
    val = fread(&c, 1, 1, m_file);
#else
    val = _read(m_handle, &c, 1);
#endif
  } while ((val != 0) &&
           (((c < '0') || (c > '9')) && (c != '-') && (c != '.')));

  if (val == 0) {
    return FALSE;
  }

  do {
    tempstr.concat(c);
    if (c == '.') {
      sawDec = TRUE;
    }
#if USE_BUFFERED_IO
    val = fread(&c, 1, 1, m_file);
#else
    val = _read(m_handle, &c, 1);
#endif
  } while ((val != 0) &&
           (((c >= '0') && (c <= '9')) || ((c == '.') && !sawDec)));

  if (val != 0) {
#if USE_BUFFERED_IO
    fseek(m_file, -1, SEEK_CUR);
#else
    _lseek(m_handle, -1, SEEK_CUR);
#endif
  }

  newReal = atof(tempstr.str());
  return TRUE;
}

//=================================================================
// LocalFile::scanString
//=================================================================
// skips preceding whitespace and stops at the first whitespace
// or at EOF
Bool LocalFile::scanString(AsciiString &newString) {
  Char c;
  Int val;

  newString.clear();

  // skip the preceding whitespace
  do {
#if USE_BUFFERED_IO
    val = fread(&c, 1, 1, m_file);
#else
    val = _read(m_handle, &c, 1);
#endif
  } while ((val != 0) && (isspace(c)));

  if (val == 0) {
    return FALSE;
  }

  do {
    newString.concat(c);
#if USE_BUFFERED_IO
    val = fread(&c, 1, 1, m_file);
#else
    val = _read(m_handle, &c, 1);
#endif
  } while ((val != 0) && (!isspace(c)));

  if (val != 0) {
#if USE_BUFFERED_IO
    fseek(m_file, -1, SEEK_CUR);
#else
    _lseek(m_handle, -1, SEEK_CUR);
#endif
  }

  return TRUE;
}

//=================================================================
// LocalFile::nextLine
//=================================================================
// scans to the first character after a new-line or at EOF
void LocalFile::nextLine(Char *buf, Int bufSize) {
  Char c = 0;
  Int val;
  Int i = 0;

  // seek to the next new-line.
  do {
    if ((buf == nullptr) || (i >= (bufSize - 1))) {
#if USE_BUFFERED_IO
      val = fread(&c, 1, 1, m_file);
#else
      val = _read(m_handle, &c, 1);
#endif
    } else {
#if USE_BUFFERED_IO
      val = fread(buf + i, 1, 1, m_file);
#else
      val = _read(m_handle, buf + i, 1);
#endif
      c = buf[i];
    }
    ++i;
  } while ((val != 0) && (c != '\n'));

  if (buf != nullptr) {
    if (i < bufSize) {
      buf[i] = 0;
    } else {
      buf[bufSize] = 0;
    }
  }
}

//=================================================================
//=================================================================
File *LocalFile::convertToRAMFile() {
  RAMFile *ramFile = newInstance(RAMFile);
  if (ramFile->open(this)) {
    if (this->m_deleteOnClose) {
      ramFile->deleteOnClose();
    }
    deleteInstance(this);
    return ramFile;
  } else {
    deleteInstance(ramFile);
    return this;
  }
}

//=================================================================
// LocalFile::readEntireAndClose
//=================================================================
/**
        Allocate a buffer large enough to hold entire file, read
        the entire file into the buffer, then close the file.
        the buffer is owned by the caller, who is responsible
        for freeing is (via delete[]). This is a Good Thing to
        use because it minimizes memory copies for BIG files.
*/
char *LocalFile::readEntireAndClose() {
  UnsignedInt fileSize = size();
  char *buffer = NEW char[fileSize];

  read(buffer, fileSize);

  close();

  return buffer;
}
