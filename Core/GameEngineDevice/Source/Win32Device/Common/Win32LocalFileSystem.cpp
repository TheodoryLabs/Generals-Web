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
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

///////// Win32LocalFileSystem.cpp /////////////////////////
// Bryan Cleveland, August 2002
////////////////////////////////////////////////////////////

#include <windows.h>
#include "Common/AsciiString.h"
#include "Common/GameMemory.h"
#include "Common/PerfTimer.h"
#include "Win32Device/Common/Win32LocalFileSystem.h"
#include "Win32Device/Common/Win32LocalFile.h"
#include <io.h>

#if defined(__EMSCRIPTEN__)
#include <dirent.h>
#include <sys/stat.h>
#endif

Win32LocalFileSystem::Win32LocalFileSystem() : LocalFileSystem()
{
}

Win32LocalFileSystem::~Win32LocalFileSystem() {
}

//DECLARE_PERF_TIMER(Win32LocalFileSystem_openFile)
File * Win32LocalFileSystem::openFile(const Char *filename, Int access, size_t bufferSize)
{
	//USE_PERF_TIMER(Win32LocalFileSystem_openFile)

	// sanity check
	if (strlen(filename) <= 0) {
		return nullptr;
	}

	if (access & File::WRITE) {
		// if opening the file for writing, we need to make sure the directory is there
		// before we try to create the file.
		AsciiString string;
		string = filename;
		AsciiString token;
		AsciiString dirName;
		string.nextToken(&token, "\\/");
		dirName = token;
		while ((token.find('.') == nullptr) || (string.find('.') != nullptr)) {
			createDirectory(dirName);
			string.nextToken(&token, "\\/");
			dirName.concat('\\');
			dirName.concat(token);
		}
	}

	// TheSuperHackers @fix Mauller 21/04/2025 Create new file handle when necessary to prevent memory leak
	Win32LocalFile *file = newInstance( Win32LocalFile );

	if (file->open(filename, access, bufferSize) == FALSE) {
		deleteInstance(file);
		file = nullptr;
	} else {
		file->deleteOnClose();
	}

// this will also need to play nice with the STREAMING type that I added, if we ever enable this

// srj sez: this speeds up INI loading, but makes BIG files unusable.
// don't enable it without further tweaking.
//
// unless you like running really slowly.
//	if (!(access&File::WRITE)) {
//		// Return a ramfile.
//		RAMFile *ramFile = newInstance( RAMFile );
//		if (ramFile->open(file)) {
//			file->close(); // is deleteonclose, so should delete.
//			ramFile->deleteOnClose();
//			return ramFile;
//		}	else {
//			ramFile->close();
//			deleteInstance(ramFile);
//		}
//	}

	return file;
}

void Win32LocalFileSystem::update()
{
}

void Win32LocalFileSystem::init()
{
}

void Win32LocalFileSystem::reset()
{
}

//DECLARE_PERF_TIMER(Win32LocalFileSystem_doesFileExist)
Bool Win32LocalFileSystem::doesFileExist(const Char *filename) const
{
	//USE_PERF_TIMER(Win32LocalFileSystem_doesFileExist)
#if defined(__EMSCRIPTEN__)
	// Emscripten MEMFS is case-sensitive and BigVFS mounts all placeholders lowercase
	char normPath[1024];
	strncpy(normPath, filename, sizeof(normPath) - 1);
	normPath[sizeof(normPath) - 1] = '\0';
	for (int i = 0; normPath[i] != '\0'; ++i) {
		if (normPath[i] == '\\') normPath[i] = '/';
		normPath[i] = tolower(normPath[i]);
	}
	if (_access(normPath, 0) == 0) {
		return TRUE;
	}
#endif
	if (_access(filename, 0) == 0) {
		return TRUE;
	}
	return FALSE;
}

void Win32LocalFileSystem::getFileListInDirectory(const AsciiString& currentDirectory, const AsciiString& originalDirectory, const AsciiString& searchName, FilenameList & filenameList, Bool searchSubdirectories) const
{
#if defined(__EMSCRIPTEN__)
	
	AsciiString fullDir = originalDirectory;
	fullDir.concat(currentDirectory);
	
	char normDir[1024];
	strncpy(normDir, fullDir.str(), sizeof(normDir) - 1);
	normDir[sizeof(normDir) - 1] = '\0';
	for (int i = 0; normDir[i] != '\0'; ++i) {
		if (normDir[i] == '\\') normDir[i] = '/';
		normDir[i] = tolower(normDir[i]);
	}
	
	// Strip trailing slash for opendir
	int len = strlen(normDir);
	if (len > 0 && normDir[len-1] == '/') normDir[len-1] = '\0';
	if (strlen(normDir) == 0) strcpy(normDir, ".");
	
	DIR *dir = opendir(normDir);
	if (dir) {
		struct dirent *ent;
		while ((ent = readdir(dir)) != nullptr) {
			if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
			
			AsciiString newFilename = originalDirectory;
			newFilename.concat(currentDirectory);
			newFilename.concat(ent->d_name);
			
			// In Emscripten MEMFS, d_type is usually reliable, but fallback to stat if needed
			bool is_dir = (ent->d_type == DT_DIR);
			if (ent->d_type == DT_UNKNOWN) {
				struct stat st;
				char fullPath[1024];
				snprintf(fullPath, sizeof(fullPath), "%s/%s", normDir, ent->d_name);
				if (stat(fullPath, &st) == 0) {
					is_dir = S_ISDIR(st.st_mode);
				}
			}
			
			if (is_dir) {
				if (searchSubdirectories) {
					AsciiString tempsearchstr = currentDirectory;
					tempsearchstr.concat(ent->d_name);
					tempsearchstr.concat('\\');
					getFileListInDirectory(tempsearchstr, originalDirectory, searchName, filenameList, searchSubdirectories);
				}
			} else {
				// Wildcard match check
				bool match = false;
				const char *searchStr = searchName.str();
				
				if (strcmp(searchStr, "*") == 0 || strcmp(searchStr, "*.*") == 0) {
					match = true;
				} else if (searchStr[0] == '*') {
					const char *ext = &searchStr[1];
					if (AsciiString(ent->d_name).endsWithNoCase(ext)) {
						match = true;
					}
				} else {
					if (AsciiString(ent->d_name).compareNoCase(searchName) == 0) {
						match = true;
					}
				}
				
				if (match) {
					if (filenameList.find(newFilename) == filenameList.end()) {
						filenameList.insert(newFilename);
					}
				}
			}
		}
		closedir(dir);
	}
	return;
#endif

	HANDLE fileHandle = nullptr;
	WIN32_FIND_DATA findData;

	AsciiString asciisearch;
	asciisearch = originalDirectory;
	asciisearch.concat(currentDirectory);
	asciisearch.concat(searchName);

	Bool done = FALSE;

	fileHandle = FindFirstFile(asciisearch.str(), &findData);
	done = (fileHandle == INVALID_HANDLE_VALUE);

	while (!done)	{
		if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
				(strcmp(findData.cFileName, ".") != 0 && strcmp(findData.cFileName, "..") != 0)) {
			// if we haven't already, add this filename to the list.
				// a stl set should only allow one copy of each filename
				AsciiString newFilename;
				newFilename = originalDirectory;
				newFilename.concat(currentDirectory);
				newFilename.concat(findData.cFileName);
				if (filenameList.find(newFilename) == filenameList.end()) {
					filenameList.insert(newFilename);
				}
		}

		done = (FindNextFile(fileHandle, &findData) == 0);
	}
	FindClose(fileHandle);

	if (searchSubdirectories) {
		AsciiString subdirsearch;
		subdirsearch = originalDirectory;
		subdirsearch.concat(currentDirectory);
		subdirsearch.concat("*.");
		fileHandle = FindFirstFile(subdirsearch.str(), &findData);
		done = fileHandle == INVALID_HANDLE_VALUE;

		while (!done) {
			if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
					(strcmp(findData.cFileName, ".") != 0 && strcmp(findData.cFileName, "..") != 0)) {

					AsciiString tempsearchstr;
					tempsearchstr.concat(currentDirectory);
					tempsearchstr.concat(findData.cFileName);
					tempsearchstr.concat('\\');

					// recursively add files in subdirectories if required.
					getFileListInDirectory(tempsearchstr, originalDirectory, searchName, filenameList, searchSubdirectories);
			}

			done = (FindNextFile(fileHandle, &findData) == 0);
		}

		FindClose(fileHandle);
	}

}

Bool Win32LocalFileSystem::getFileInfo(const AsciiString& filename, FileInfo *fileInfo) const
{
	WIN32_FIND_DATA findData;
	HANDLE findHandle = nullptr;
	findHandle = FindFirstFile(filename.str(), &findData);

	if (findHandle == INVALID_HANDLE_VALUE) {
		return FALSE;
	}

	fileInfo->timestampHigh = findData.ftLastWriteTime.dwHighDateTime;
	fileInfo->timestampLow = findData.ftLastWriteTime.dwLowDateTime;
	fileInfo->sizeHigh = findData.nFileSizeHigh;
	fileInfo->sizeLow = findData.nFileSizeLow;

	FindClose(findHandle);

	return TRUE;
}

Bool Win32LocalFileSystem::createDirectory(AsciiString directory)
{
	if ((!directory.isEmpty()) && (directory.getLength() < _MAX_DIR)) {
		return (CreateDirectory(directory.str(), nullptr) != 0);
	}
	return FALSE;
}

AsciiString Win32LocalFileSystem::normalizePath(const AsciiString& filePath) const
{
#ifdef __EMSCRIPTEN__
	// GeneralsX @feature WebPort 2026-05-05 — normalise without Win32 API.
	//
	// GetFullPathNameA always fails on Emscripten which empties the string,
	// breaks everywhere paths are passed through normalize (notably the
	// "Save" directory probe and map-INI loading), and asserts in
	// release builds. Provide a POSIX-friendly canonicaliser that:
	//   * converts backslashes to forward slashes
	//   * lowercases ASCII (BigVFS keys are lowercase)
	//   * collapses repeated slashes
	//
	// We deliberately do NOT realpath() against the local FS — Emscripten's
	// MEMFS doesn't carry the original Windows working directory and the
	// engine only needs a stable, canonical key for its own bookkeeping.
	if (filePath.isEmpty()) {
		return AsciiString::TheEmptyString;
	}
	const Char *src = filePath.str();
	size_t len = strlen(src);
	AsciiString out;
	Char *dst = out.getBufferForRead(len);
	size_t w = 0;
	Char prev = '\0';
	for (size_t i = 0; i < len; ++i) {
		Char c = src[i];
		if (c == '\\') c = '/';
		else if (c >= 'A' && c <= 'Z') c = (Char)(c - 'A' + 'a');
		if (c == '/' && prev == '/') continue;  // collapse //
		dst[w++] = c;
		prev = c;
	}
	dst[w] = '\0';
	return out;
#else
	DWORD retval = GetFullPathNameA(filePath.str(), 0, nullptr, nullptr);
	if (retval == 0)
	{
		DEBUG_LOG(("Unable to determine buffer size for normalized file path. Error=(%u).", GetLastError()));
		return AsciiString::TheEmptyString;
	}

	AsciiString normalizedFilePath;
	retval = GetFullPathNameA(filePath.str(), retval, normalizedFilePath.getBufferForRead(retval - 1), nullptr);
	if (retval == 0)
	{
		DEBUG_LOG(("Unable to normalize file path '%s'. Error=(%u).", filePath.str(), GetLastError()));
		return AsciiString::TheEmptyString;
	}

	return normalizedFilePath;
#endif
}
