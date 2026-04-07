/*
** GeneralsX Web Port — .BIG Archive Virtual Filesystem Adapter
**
** Mounts C&C Generals .big archive files into Emscripten's virtual filesystem,
** allowing the game engine to access archived assets as if they were regular files.
**
** .BIG FILE FORMAT (EA / Westwood):
**   Offset  Size  Endian  Field
**   0x00    4     -       Magic: "BIGF" (4 ASCII bytes)
**   0x04    4     LE      Archive total size in bytes
**   0x08    4     BE      Number of file entries
**   0x0C    4     -       (padding/reserved)
**   0x10    ...   -       File entry table
**
**   Each file entry:
**     4 bytes (BE): Absolute offset of file data within the archive
**     4 bytes (BE): File size in bytes
**     N bytes:      Null-terminated file path string (uses \ or / separators)
**
** WEB ASSET DELIVERY STRATEGY:
**   .big archives are NOT bundled into the WASM binary (they're ~500MB total).
**   Instead, they're served via HTTP and accessed on-demand:
**
**   1. On startup: Download just the .big HEADERS (first ~1MB of each archive)
**      This gives us the file directory — which files exist and where they are.
**
**   2. On file access: Fetch the specific byte range from the server via
**      HTTP Range requests. The browser Cache API stores fetched chunks.
**
**   3. Emscripten VFS: Files are mounted into /assets/ in the virtual FS.
**      The game's file I/O code opens "/assets/data/ini/weapon.ini" and our
**      VFS adapter transparently fetches it from the server.
**
** LICENSE: GPL-3.0
*/

#pragma once

#if defined(__EMSCRIPTEN__)

#include <emscripten.h>
#include <string>
#include <vector>
#include <unordered_map>

// ============================================================================
// BIG Archive Entry — one file within a .big archive
// ============================================================================
struct BigFileEntry {
    std::string path;           // Full path within archive (e.g., "data/ini/weapon.ini")
    unsigned int offset;        // Byte offset of data within the .big file
    unsigned int size;          // Uncompressed file size
    unsigned int archive_index; // Which .big archive contains this file
    bool is_cached;             // True if already downloaded and in memory
};

// ============================================================================
// BIG Archive — represents one .big file's directory
// ============================================================================
struct BigArchive {
    std::string filename;       // e.g., "INIZH.big"
    std::string url;            // HTTP URL to fetch from (e.g., "/assets/INIZH.big")
    unsigned int total_size;    // Total archive size
    unsigned int file_count;    // Number of entries
    std::vector<BigFileEntry> entries;
};

// ============================================================================
// BigVFS — Virtual Filesystem bridging .big archives to Emscripten VFS
// ============================================================================
class BigVFS {
public:
    // Initialize the VFS system
    // base_url: HTTP URL prefix where .big files are served
    //           e.g., "https://cdn.example.com/generals/assets/"
    static bool Init(const char* base_url);

    // Shutdown and free all cached data
    static void Shutdown();

    // Register a .big archive by loading its header/directory
    // This fetches just the first few KB to parse the file table.
    // archive_name: e.g., "INIZH.big"
    // Returns true if the header was parsed successfully.
    static bool Mount_Archive(const char* archive_name);

    // Parse a .big file header from raw data
    // header_data: pointer to at least the first N bytes of the .big file
    //              (enough to contain the full directory)
    // header_size: number of bytes available
    // out_archive: output archive structure to fill
    static bool Parse_Header(const void* header_data, unsigned int header_size,
                              BigArchive* out_archive);

    // Look up a file by its path (case-insensitive, handles \ and /)
    // Returns pointer to entry, or nullptr if not found
    static const BigFileEntry* Find_File(const char* path);

    // Read a file's data (may trigger async HTTP fetch on first access)
    // path: virtual path (e.g., "data/ini/weapon.ini")
    // out_data: receives pointer to file data (valid until Shutdown)
    // out_size: receives file size
    // Returns true if file was loaded successfully
    static bool Read_File(const char* path, void** out_data,
                           unsigned int* out_size);

    // Synchronous file read (blocks until data is available)
    // Uses emscripten_wget_data internally for HTTP fetch
    static bool Read_File_Sync(const char* path, void** out_data,
                                unsigned int* out_size);

    // Pre-cache a list of files (e.g., during loading screen)
    // Starts async downloads for all listed files
    static void Prefetch_Files(const char** paths, unsigned int count);

    // Get the number of mounted archives
    static unsigned int Get_Archive_Count();

    // Get total number of known files across all archives
    static unsigned int Get_Total_File_Count();

    // Get stats about cache usage
    static void Get_Cache_Stats(unsigned int* cached_count,
                                 unsigned int* cached_bytes,
                                 unsigned int* total_count,
                                 unsigned int* total_bytes);

    // Mount all files into Emscripten's MEMFS at /assets/
    // This creates the directory structure but with lazy file content loading
    static bool Mount_To_Emscripten_FS();

private:
    static std::string base_url;
    static std::vector<BigArchive> archives;
    static std::unordered_map<std::string, unsigned int> file_lookup;
    // flat index: archive_index * MAX_FILES + entry_index

    // Cached file data (downloaded byte ranges)
    struct CachedData {
        void* data;
        unsigned int size;
    };
    static std::unordered_map<std::string, CachedData> cache;

    // Normalize a file path (lowercase, forward slashes)
    static std::string Normalize_Path(const char* path);

    // Big-endian to host byte order conversion
    static unsigned int BE_To_Host(unsigned int be_value);
};

#endif // __EMSCRIPTEN__
