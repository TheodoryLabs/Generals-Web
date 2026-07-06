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

std::list<std::string> BigVFS::inactive_lru;
unsigned int BigVFS::inactive_cache_bytes = 0;

// ============================================================================
// Fetch_Range_JS — Native HTTP Range Request via Synchronous XHR
// ============================================================================
EM_JS(
    int, Fetch_Range_JS,
    (const char *url, unsigned int start, unsigned int end, void *buffer), {
      try {
        const urlStr = UTF8ToString(url);

        // JS-side range memo: the engine re-requests identical ranges heavily
        // (archive directories per lookup, evicted files re-read on demand);
        // measured ~120 duplicate fetches of one archive per boot. Each sync
        // XHR blocks the main thread, so repeats are pure freeze time. Cache
        // entries up to 4 MB with a ~192 MB LRU cap.
        // GeneralsX @feature WebPort 2026-07-07 (issue #2)
        if (!Module.gxRC) Module.gxRC = { map: new Map(), bytes: 0 };
        const rc = Module.gxRC;
        const key = urlStr + '|' + start + '|' + end;
        const hit = rc.map.get(key);
        if (hit !== undefined) {
          rc.map.delete(key); // LRU: move to back
          rc.map.set(key, hit);
          HEAPU8.set(hit, buffer);
          return hit.length;
        }

        const xhr = new XMLHttpRequest();
        xhr.open('GET', urlStr, false); // false makes it synchronous
        xhr.setRequestHeader('Range', 'bytes=' + start + '-' + end);
        xhr.overrideMimeType('text/plain; charset=x-user-defined');
        xhr.send(null);

        if (xhr.status !== 200 && xhr.status !== 206) {
          console.error("Fetch range sync failed with status", xhr.status);
          return -xhr.status;
        }

        const responseText = xhr.responseText || "";
        const byteLen = responseText.length;
        const u8 = new Uint8Array(byteLen);
        for (let i = 0; i < byteLen; ++i) {
          u8[i] = responseText.charCodeAt(i) & 0xff;
        }

        if (byteLen <= 4194304) {
          rc.map.set(key, u8);
          rc.bytes += byteLen;
          while (rc.bytes > 201326592 && rc.map.size > 0) {
            const oldest = rc.map.keys().next().value;
            rc.bytes -= rc.map.get(oldest).length;
            rc.map.delete(oldest);
          }
        }

        HEAPU8.set(u8, buffer);
        return byteLen;
      } catch (e) {
        if (typeof Module !== 'undefined' && Module.printErr) {
          Module.printErr("Fetch_Range_JS sync exception: " + e.stack);
        }
        console.error("Fetch_Range_JS sync exception:", e);
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
  inactive_lru.clear();
  inactive_cache_bytes = 0;

  fprintf(stderr, "INFO: BigVFS initialized with base URL: %s\n",
          base_url.c_str());
  return true;
}

// ============================================================================
// Shutdown
// ============================================================================
void BigVFS::Shutdown() {
  // Free preloaded archive buffers
  for (auto &archive : archives) {
    if (archive.preloaded_buffer) {
      free(archive.preloaded_buffer);
      archive.preloaded_buffer = nullptr;
    }
  }

  // Free dynamically loaded cached files
  for (auto &pair : cache) {
    if (pair.second.data) {
      free(pair.second.data);
    }
  }
  cache.clear();
  inactive_lru.clear();
  inactive_cache_bytes = 0;
  archives.clear();
  file_lookup.clear();
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
static bool Should_Preload(const std::string& filename) {
  std::string name = filename;
  for (auto &c : name) c = tolower(c);
  return name == "ini.big" || name == "inizh.big" ||
         name == "window.big" || name == "windowzh.big" ||
         name == "shaders.big" || name == "shaderszh.big" ||
         name == "maps.big" || name == "mapszh.big" ||
         name == "terrainzh.big";
}

bool BigVFS::Mount_Archive(const char *archive_name) {
  std::string url = base_url + archive_name;

  // Step 1: Fetch the first 16 bytes containing the header metadata
  unsigned char initial_header[16];
  int bytes_read = Fetch_Range_JS(url.c_str(), 0, 15, initial_header);
  if (bytes_read < 16) {
    fprintf(stderr, "ERROR: Failed to fetch first 16 bytes of archive: %s (status %d)\n",
            archive_name, bytes_read);
    return false;
  }

  // Check magic "BIGF"
  if (initial_header[0] != 'B' || initial_header[1] != 'I' || initial_header[2] != 'G' || initial_header[3] != 'F') {
    fprintf(stderr, "ERROR: Not a valid .big file magic for %s\n", archive_name);
    return false;
  }

  // Archive total size (little-endian at offset 4)
  unsigned int total_size = *(const unsigned int *)(initial_header + 4);

  // Header directory size (big-endian at offset 12 / 0x0C)
  unsigned int raw_header_size;
  memcpy(&raw_header_size, initial_header + 12, 4);
  unsigned int header_size = BE_To_Host(raw_header_size);

  BigArchive archive;
  archive.filename = archive_name;
  archive.url = url;
  archive.total_size = total_size;
  archive.preloaded_buffer = nullptr;

  bool ok = false;
  if (Should_Preload(archive_name)) {
    // Preload path: allocate memory for the entire archive and download it
    void* preload_buf = malloc(total_size);
    if (!preload_buf) {
      fprintf(stderr, "ERROR: Out of memory allocating %u bytes for preloading %s\n", total_size, archive_name);
      return false;
    }

    int total_read = Fetch_Range_JS(url.c_str(), 0, total_size - 1, preload_buf);
    if (total_read <= 0 || (unsigned int)total_read < total_size) {
      fprintf(stderr, "ERROR: Failed to preload entire archive %s (read %d/%u bytes)\n",
              archive_name, total_read, total_size);
      free(preload_buf);
      return false;
    }

    archive.preloaded_buffer = preload_buf;
    ok = Parse_Header(preload_buf, total_size, &archive);
  } else {
    // Lazy path: only download the exact remaining header bytes
    void* header_buf = malloc(header_size);
    if (!header_buf) {
      return false;
    }

    // Copy the first 16 bytes we already downloaded
    memcpy(header_buf, initial_header, 16);

    if (header_size > 16) {
      int remaining_read = Fetch_Range_JS(url.c_str(), 16, header_size - 1, (char*)header_buf + 16);
      if (remaining_read <= 0 || (unsigned int)remaining_read < (header_size - 16)) {
        fprintf(stderr, "ERROR: Failed to fetch remaining header bytes for %s (read %d)\n",
                archive_name, remaining_read);
        free(header_buf);
        return false;
      }
    }

    ok = Parse_Header(header_buf, header_size, &archive);
    free(header_buf);
  }

  if (!ok) {
    if (archive.preloaded_buffer) {
      free(archive.preloaded_buffer);
      archive.preloaded_buffer = nullptr;
    }
    return false;
  }

  // Set archive index on entries and build lookup table
  unsigned int archive_idx = (unsigned int)archives.size();
  for (unsigned int i = 0; i < archive.entries.size(); i++) {
    archive.entries[i].archive_index = archive_idx;
    std::string norm_path = Normalize_Path(archive.entries[i].path.c_str());
    // Later archives override earlier ones (matching game behavior)
    file_lookup[norm_path] = (archive_idx << 16) | i;
  }

  archives.push_back(std::move(archive));
  fprintf(stderr, "INFO: Mounted archive '%s' (%u files, preloaded=%d)\n", archive_name,
          archives.back().file_count, Should_Preload(archive_name));
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
    // If it was in the inactive LRU queue, rescue it
    if (cache_it->second.is_inactive) {
      inactive_lru.erase(cache_it->second.lru_it);
      inactive_cache_bytes -= cache_it->second.size;
      cache_it->second.is_inactive = false;
    }
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

  const BigArchive &archive = archives[entry->archive_index];
  if (archive.preloaded_buffer) {
    *out_data = (char*)archive.preloaded_buffer + entry->offset;
    *out_size = entry->size;
    return true;
  }

  // GeneralsX @feature WebPort 2026-05-05 — short-circuit zero-byte entries.
  // BIG archives can legitimately store empty files (e.g. ShellMapMD\map.ini).
  // Issuing a Range request like 'bytes=N-(N-1)' is invalid HTTP and returns
  // 416 Range Not Satisfiable; without this guard the engine keeps retrying
  // forever and floods the console with -416 errors.
  if (entry->size == 0) {
    void *empty = malloc(1);  // non-null sentinel
    if (!empty) return false;
    CachedData cd;
    cd.data = empty;
    cd.size = 0;
    cd.is_inactive = false;
    cache[norm] = cd;
    *out_data = cd.data;
    *out_size = 0;
    return true;
  }


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
  cd.is_inactive = false;
  cache[norm] = cd;

  *out_data = cd.data;
  *out_size = cd.size;
  return true;
}

// ============================================================================
// Evict_File — Free dynamic download cache for a file
// ============================================================================
void BigVFS::Evict_File(const char *path) {
  if (!path) return;
  std::string norm = Normalize_Path(path);

  // Make sure it is in cache
  auto it = cache.find(norm);
  if (it == cache.end()) return;

  // If it is already inactive, do nothing
  if (it->second.is_inactive) return;

  // Don't queue preloaded buffers (they are never evicted anyway)
  const BigFileEntry *entry = Find_File(path);
  if (entry && entry->size > 0) {
    const BigArchive &archive = archives[entry->archive_index];
    if (archive.preloaded_buffer) {
      return;
    }
  }

  // Add to LRU queue as inactive
  inactive_lru.push_front(norm);
  it->second.lru_it = inactive_lru.begin();
  it->second.is_inactive = true;
  inactive_cache_bytes += it->second.size;

  // If cache exceeds limit (128 MB), evict the oldest files (from the back of the queue)
  const unsigned int INACTIVE_CACHE_LIMIT_BYTES = 128 * 1024 * 1024;
  while (inactive_cache_bytes > INACTIVE_CACHE_LIMIT_BYTES && !inactive_lru.empty()) {
    std::string oldest = inactive_lru.back();
    inactive_lru.pop_back();

    auto cache_it = cache.find(oldest);
    if (cache_it != cache.end()) {
      const BigFileEntry *oldest_entry = Find_File(oldest.c_str());
      if (oldest_entry && oldest_entry->size > 0) {
        const BigArchive &archive = archives[oldest_entry->archive_index];
        if (!archive.preloaded_buffer) {
          free(cache_it->second.data);
        }
      } else if (!oldest_entry) {
        free(cache_it->second.data);
      }
      inactive_cache_bytes -= cache_it->second.size;
      cache.erase(cache_it);
    }
  }
}

// ============================================================================
// C-Linkage wrapper for GameEngine to call without headers
// ============================================================================
extern "C" bool Web_VFS_Read_File_Sync(const char *path, void **out_data,
                                       unsigned int *out_size) {
  return BigVFS::Read_File_Sync(path, out_data, out_size);
}

extern "C" void Web_VFS_Evict_File(const char *path) {
  BigVFS::Evict_File(path);
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
