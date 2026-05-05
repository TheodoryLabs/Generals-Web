/*
** GeneralsX Web Port — .BIG Archive Virtual Filesystem Implementation
**
** Parses EA/Westwood .big archives and serves their contents to the game
** engine via Emscripten's virtual filesystem and HTTP range requests.
**
** LICENSE: GPL-3.0
*/

#if defined(__EMSCRIPTEN__)

#include "gles3_big_vfs.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <emscripten.h>
#include <sys/stat.h>

// Static members
std::string BigVFS::base_url;
std::vector<BigArchive> BigVFS::archives;
std::unordered_map<std::string, unsigned int> BigVFS::file_lookup;
std::unordered_map<std::string, BigVFS::CachedData> BigVFS::cache;

// ============================================================================
// Fetch_Range_JS — Native HTTP Range Request via JS Promises + Asyncify
// ============================================================================
EM_ASYNC_JS(
    int, Fetch_Range_JS,
    (const char *url, unsigned int start, unsigned int end, void *buffer), {
      try {
        const urlStr = UTF8ToString(url);
        const headers = new Headers();
        headers.append('Range', 'bytes=' + start + '-' + end);
        const response = await fetch(urlStr, {headers : headers});

        if (!response.ok && response.status != 206 && response.status != 200) {
          console.error("Fetch range failed with status", response.status);
          return -response.status; // return negative status on HTTP error
        }

        const arrayBuffer = await response.arrayBuffer();
        let u8;
        if (response.status === 200) {
            // Server didn't support Range requests and returned the whole file
            const byteLen = arrayBuffer.byteLength;
            if (start >= byteLen) { return 0; }
            let sliceEnd = end + 1;
            if (sliceEnd > byteLen) { sliceEnd = byteLen; }
            u8 = new Uint8Array(arrayBuffer, start, sliceEnd - start);
        } else {
            // Server returned 206 Partial Content
            let byteLen = arrayBuffer.byteLength;
            const expectedLen = end - start + 1;
            if (byteLen > expectedLen) {
                byteLen = expectedLen;
            }
            u8 = new Uint8Array(arrayBuffer, 0, byteLen);
        }
        HEAPU8.set(u8, buffer);

        return u8.length; // Return actual bytes read
      } catch (e) {
        if (typeof Module != 'undefined' && Module.printErr) {
          Module.printErr("Fetch_Range_JS exception: " + e.stack);
        }
        console.error("Fetch_Range_JS exception:", e);
        return 0; // 0 indicates network error or exception
      }
    });

// ============================================================================
// Byte order conversion (big-endian to host)
// ============================================================================
unsigned int BigVFS::BE_To_Host(unsigned int be_value) {
  const unsigned char *bytes = (const unsigned char *)&be_value;
  return ((unsigned int)bytes[0] << 24) | ((unsigned int)bytes[1] << 16) |
         ((unsigned int)bytes[2] << 8) | ((unsigned int)bytes[3]);
}

// ============================================================================
// Path normalization — lowercase, forward slashes
// ============================================================================
std::string BigVFS::Normalize_Path(const char *path) {
  std::string normalized(path);
  for (auto &c : normalized) {
    if (c == '\\')
      c = '/';
    c = tolower(c);
  }
  // Strip leading slash if present
  if (!normalized.empty() && normalized[0] == '/')
    normalized = normalized.substr(1);
  return normalized;
}

// ============================================================================
// Init
// ============================================================================
bool BigVFS::Init(const char *url) {
  if (!url)
    return false;
  base_url = url;
  // Ensure trailing slash
  if (!base_url.empty() && base_url.back() != '/')
    base_url += '/';

  archives.clear();
  file_lookup.clear();
  cache.clear();

  fprintf(stderr, "INFO: BigVFS initialized with base URL: %s\n",
          base_url.c_str());
  return true;
}

// ============================================================================
// Shutdown
// ============================================================================
void BigVFS::Shutdown() {
  for (auto &pair : cache) {
    if (pair.second.data) {
      free(pair.second.data);
    }
  }
  cache.clear();
  file_lookup.clear();
  archives.clear();
}

// ============================================================================
// Parse_Header — Parse a .big file header from raw bytes
//
// .BIG format:
//   [0x00] 4 bytes: "BIGF" magic
//   [0x04] 4 bytes: archive size (little-endian)
//   [0x08] 4 bytes: file count (big-endian)
//   [0x0C] 4 bytes: reserved
//   [0x10] entries: { offset(4B BE), size(4B BE), path(null-terminated) }
// ============================================================================
bool BigVFS::Parse_Header(const void *header_data, unsigned int header_size,
                          BigArchive *out_archive) {
  if (!header_data || header_size < 16 || !out_archive)
    return false;

  const unsigned char *data = (const unsigned char *)header_data;

  // Check magic "BIGF"
  if (data[0] != 'B' || data[1] != 'I' || data[2] != 'G' || data[3] != 'F') {
    fprintf(stderr, "ERROR: Not a valid .big file (magic mismatch)\n");
    return false;
  }

  // Archive size (little-endian at offset 4)
  out_archive->total_size = *(const unsigned int *)(data + 4);

  // File count (big-endian at offset 8)
  unsigned int raw_count;
  memcpy(&raw_count, data + 8, 4);
  out_archive->file_count = BE_To_Host(raw_count);

  fprintf(stderr, "INFO: Parsing .big archive: %u files, %u bytes total\n",
          out_archive->file_count, out_archive->total_size);

  // Parse file entries starting at offset 0x10
  unsigned int pos = 0x10;
  out_archive->entries.clear();
  out_archive->entries.reserve(out_archive->file_count);

  for (unsigned int i = 0; i < out_archive->file_count; i++) {
    if (pos + 8 >= header_size) {
      fprintf(stderr,
              "WARN: Header truncated at entry %u/%u (need more data)\n", i,
              out_archive->file_count);
      break;
    }

    BigFileEntry entry;

    // File offset (big-endian)
    unsigned int raw_offset;
    memcpy(&raw_offset, data + pos, 4);
    entry.offset = BE_To_Host(raw_offset);
    pos += 4;

    // File size (big-endian)
    unsigned int raw_size;
    memcpy(&raw_size, data + pos, 4);
    entry.size = BE_To_Host(raw_size);
    pos += 4;

    // Null-terminated file path
    entry.path.clear();
    while (pos < header_size && data[pos] != 0) {
      entry.path += (char)data[pos];
      pos++;
    }
    pos++; // skip null terminator

    entry.is_cached = false;
    out_archive->entries.push_back(std::move(entry));
  }

  fprintf(stderr, "INFO: Parsed %zu file entries from header\n",
          out_archive->entries.size());
  return true;
}

// ============================================================================
// Mount_Archive — Download header and register a .big archive
// ============================================================================
bool BigVFS::Mount_Archive(const char *archive_name) {
  std::string url = base_url + archive_name;

  // We fetch the first 512KB which should be enough for most .big directories
  unsigned int fetch_size = 524288;
  void *fetch_data = malloc(fetch_size);

  int bytes_read = Fetch_Range_JS(url.c_str(), 0, fetch_size - 1, fetch_data);

  if (bytes_read <= 0) {
    fprintf(stderr, "ERROR: Failed to fetch .big header: %s (status %d)\n",
            archive_name, bytes_read);
    free(fetch_data);
    return false;
  }

  BigArchive archive;
  archive.filename = archive_name;
  archive.url = url;

  bool ok = Parse_Header(fetch_data, bytes_read, &archive);
  free(fetch_data);

  if (!ok)
    return false;

  // Set archive index on entries and build lookup table
  unsigned int archive_idx = (unsigned int)archives.size();
  for (unsigned int i = 0; i < archive.entries.size(); i++) {
    archive.entries[i].archive_index = archive_idx;
    std::string norm_path = Normalize_Path(archive.entries[i].path.c_str());
    // Later archives override earlier ones (matching game behavior)
    file_lookup[norm_path] = (archive_idx << 16) | i;
  }

  archives.push_back(std::move(archive));
  fprintf(stderr, "INFO: Mounted archive '%s' (%u files)\n", archive_name,
          archives.back().file_count);
  return true;
}

// ============================================================================
// Find_File — Look up a file by path
// ============================================================================
const BigFileEntry *BigVFS::Find_File(const char *path) {
  std::string norm = Normalize_Path(path);
  auto it = file_lookup.find(norm);
  if (it == file_lookup.end())
    return nullptr;

  unsigned int archive_idx = it->second >> 16;
  unsigned int entry_idx = it->second & 0xFFFF;

  if (archive_idx >= archives.size())
    return nullptr;
  if (entry_idx >= archives[archive_idx].entries.size())
    return nullptr;

  return &archives[archive_idx].entries[entry_idx];
}

// ============================================================================
// Read_File_Sync — Synchronous file read via HTTP Range request
// ============================================================================
bool BigVFS::Read_File_Sync(const char *path, void **out_data,
                            unsigned int *out_size) {
  if (!path || !out_data || !out_size)
    return false;

  std::string norm = Normalize_Path(path);

  // Check cache first
  auto cache_it = cache.find(norm);
  if (cache_it != cache.end()) {
    *out_data = cache_it->second.data;
    *out_size = cache_it->second.size;
    return true;
  }

  // Find the file entry
  const BigFileEntry *entry = Find_File(path);
  if (!entry) {
    fprintf(stderr, "WARN: File not found in .big archives: %s\n", path);
    return false;
  }

  // Fetch the byte range from the server
  const BigArchive &archive = archives[entry->archive_index];

  // Copy data into cache
  void *data_copy = malloc(entry->size);
  if (!data_copy) {
    return false;
  }

  // GX-TRACE — record the fetch right before it suspends Asyncify so we
  // can identify which file is the last one before a freeze.
  {
    FILE *t = fopen("/gx_trace.log", "a");
    if (t) {
      fprintf(t, "BigVFS BEFORE-FETCH path='%s' offset=%u size=%u\n",
              path, entry->offset, entry->size);
      fclose(t);
    }
  }

  // Fetch the file data.  Retry once on transient failure (Asyncify state
  // can be transiently corrupted when two async chains call Fetch_Range_JS
  // concurrently; the second call may see garbage arguments and throw
  // "TypeError: Failed to fetch" without the request reaching the server).
  int bytes_read = -1;
  for (int attempt = 0; attempt < 3 && bytes_read <= 0; ++attempt) {
    if (attempt > 0) {
      fprintf(stderr,
              "WARN: Retrying fetch for %s (attempt %d)\n", path, attempt + 1);
    }
    bytes_read = Fetch_Range_JS(archive.url.c_str(), entry->offset,
                                entry->offset + entry->size - 1, data_copy);
  }

  {
    FILE *t = fopen("/gx_trace.log", "a");
    if (t) {
      fprintf(t, "BigVFS AFTER-FETCH path='%s' bytes_read=%d\n",
              path, bytes_read);
      fclose(t);
    }
  }

  if (bytes_read <= 0) {
    fprintf(stderr,
            "ERROR: Failed to fetch file data: %s from %s (status %d)\n", path,
            archive.filename.c_str(), bytes_read);
    free(data_copy);
    return false;
  }

  CachedData cd;
  cd.data = data_copy;
  cd.size = (unsigned int)bytes_read;
  cache[norm] = cd;

  *out_data = cd.data;
  *out_size = cd.size;
  return true;
}

// ============================================================================
// C-Linkage wrapper for GameEngine to call without headers
// ============================================================================
extern "C" bool Web_VFS_Read_File_Sync(const char *path, void **out_data,
                                       unsigned int *out_size) {
  return BigVFS::Read_File_Sync(path, out_data, out_size);
}

// ============================================================================
// Read_File — Wrapper that uses sync fetch
// ============================================================================
bool BigVFS::Read_File(const char *path, void **out_data,
                       unsigned int *out_size) {
  return Read_File_Sync(path, out_data, out_size);
}

// ============================================================================
// Prefetch_Files — Start async downloads for a batch of files
// ============================================================================
void BigVFS::Prefetch_Files(const char **paths, unsigned int count) {
  // For now, do synchronous prefetching
  // A future version could use async fetch for parallel downloads
  for (unsigned int i = 0; i < count; i++) {
    void *data;
    unsigned int size;
    Read_File_Sync(paths[i], &data, &size);
  }
}

// ============================================================================
// Stats
// ============================================================================
unsigned int BigVFS::Get_Archive_Count() {
  return (unsigned int)archives.size();
}

unsigned int BigVFS::Get_Total_File_Count() {
  return (unsigned int)file_lookup.size();
}

void BigVFS::Get_Cache_Stats(unsigned int *cached_count,
                             unsigned int *cached_bytes,
                             unsigned int *total_count,
                             unsigned int *total_bytes) {
  *cached_count = (unsigned int)cache.size();
  *cached_bytes = 0;
  for (const auto &pair : cache) {
    *cached_bytes += pair.second.size;
  }
  *total_count = (unsigned int)file_lookup.size();
  *total_bytes = 0;
  for (const auto &archive : archives) {
    *total_bytes += archive.total_size;
  }
}

// ============================================================================
// Mount_To_Emscripten_FS — Create directory structure in Emscripten's MEMFS
// This creates empty placeholder files that get lazily loaded on access.
// ============================================================================
bool BigVFS::Mount_To_Emscripten_FS() {
  // Create the base mount point
  // Create directory structure for all known files
  std::unordered_map<std::string, bool> created_dirs;

  for (const auto &archive : archives) {
    for (const auto &entry : archive.entries) {
      std::string norm = Normalize_Path(entry.path.c_str());
      std::string full_path = "/" + norm;

      // Create parent directories
      size_t pos = 1; // Start after leading slash
      while ((pos = full_path.find('/', pos)) != std::string::npos) {
        std::string dir = full_path.substr(0, pos);
        if (created_dirs.find(dir) == created_dirs.end()) {
          EM_ASM_(
              {
                try {
                  FS.mkdir(UTF8ToString($0));
                } catch (e) {
                  // Directory may already exist
                }
              },
              dir.c_str());
          created_dirs[dir] = true;
        }
        pos++;
      }

      // Create an empty placeholder file
      // When the game opens it, the VFS read callback will fetch the data
      EM_ASM_(
          {
            try {
              FS.writeFile(UTF8ToString($0), new Uint8Array(0));
            } catch (e) {
              // File may already exist
            }
          },
          full_path.c_str());
    }
  }

  fprintf(stderr, "INFO: Mounted %zu files to Emscripten FS under /assets/\n",
          file_lookup.size());
  return true;
}

#endif // __EMSCRIPTEN__
