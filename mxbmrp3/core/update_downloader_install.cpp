// ============================================================================
// core/update_downloader_install.cpp
// Update installation — backup / restore / extract-and-install: the second half
// of the update flow that runs after the .zip has been downloaded (invoked by
// UpdateDownloader::workerThread via extractAndInstall). Extracted verbatim from
// update_downloader.cpp when that file grew past ~1.5k lines; the class,
// members, and public API are unchanged — only where these method bodies (and
// their file-local filesystem helpers) live moves. Same byte-identical-
// extraction pattern as the plugin_data / http_server splits.
// ============================================================================
#include "update_downloader.h"
#include "atomic_file_writer.h"
#include "plugin_constants.h"
#include "plugin_manager.h"
#include "update_checker.h"
#include "../game/game_config.h"
#include "../diagnostics/logger.h"

#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>

#include "../vendor/miniz/miniz.h"

#include <cstring>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <cctype>

// Forward declaration for recursive delete (defined later in this file, used by
// createBackupDirectory above its definition).
static bool deleteDirectoryRecursive(const std::string& dir);

bool UpdateDownloader::createBackupDirectory(const std::string& backupDir) {
    // Remove existing backup directory if present (recursively)
    deleteDirectoryRecursive(backupDir);

    // Create fresh backup directory
    if (!CreateDirectoryA(backupDir.c_str(), NULL)) {
        DWORD err = GetLastError();
        if (err != ERROR_ALREADY_EXISTS) {
            DEBUG_WARN_F("UpdateDownloader: Failed to create backup directory (error %lu)", err);
            return false;
        }
    }
    DEBUG_INFO_F("UpdateDownloader: Created backup directory: %s", backupDir.c_str());
    return true;
}

// Helper to create directories recursively (like mkdir -p)
static bool createDirectoriesRecursive(const std::string& path) {
    // Try to create the directory
    if (CreateDirectoryA(path.c_str(), NULL)) {
        return true;
    }

    DWORD err = GetLastError();
    if (err == ERROR_ALREADY_EXISTS) {
        return true;
    }

    if (err == ERROR_PATH_NOT_FOUND) {
        // Parent doesn't exist, create it first
        size_t lastSlash = path.find_last_of('\\');
        if (lastSlash != std::string::npos && lastSlash > 0) {
            std::string parent = path.substr(0, lastSlash);
            if (createDirectoriesRecursive(parent)) {
                // Now try again
                return CreateDirectoryA(path.c_str(), NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
            }
        }
    }

    return false;
}

// Recursively copy a directory
static bool copyDirectoryRecursive(const std::string& srcDir, const std::string& dstDir) {
    // Create destination directory
    if (!createDirectoriesRecursive(dstDir)) {
        return false;
    }

    WIN32_FIND_DATAA findData;
    std::string searchPath = srcDir + "*";
    HANDLE hFind = FindFirstFileA(searchPath.c_str(), &findData);

    if (hFind == INVALID_HANDLE_VALUE) {
        return true;  // Empty or non-existent directory is ok
    }

    bool success = true;
    do {
        if (strcmp(findData.cFileName, ".") == 0 || strcmp(findData.cFileName, "..") == 0) {
            continue;
        }

        std::string srcPath = srcDir + findData.cFileName;
        std::string dstPath = dstDir + findData.cFileName;

        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            // Recurse into subdirectory
            if (!copyDirectoryRecursive(srcPath + "\\", dstPath + "\\")) {
                success = false;
                break;
            }
        } else {
            // Copy file
            if (!CopyFileA(srcPath.c_str(), dstPath.c_str(), FALSE)) {
                DEBUG_WARN_F("UpdateDownloader: Failed to copy %s", srcPath.c_str());
                success = false;
                break;
            }
        }
    } while (FindNextFileA(hFind, &findData));

    FindClose(hFind);
    return success;
}

// Recursively delete a directory and its contents
static bool deleteDirectoryRecursive(const std::string& dir) {
    WIN32_FIND_DATAA findData;
    std::string searchPath = dir + "*";
    HANDLE hFind = FindFirstFileA(searchPath.c_str(), &findData);

    if (hFind == INVALID_HANDLE_VALUE) {
        return true;  // Already gone
    }

    do {
        if (strcmp(findData.cFileName, ".") == 0 || strcmp(findData.cFileName, "..") == 0) {
            continue;
        }

        std::string path = dir + findData.cFileName;

        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            deleteDirectoryRecursive(path + "\\");
        } else {
            DeleteFileA(path.c_str());
        }
    } while (FindNextFileA(hFind, &findData));

    FindClose(hFind);
    RemoveDirectoryA(dir.c_str());
    return true;
}

namespace {
// MoveFileA with bounded retry + escalating backoff. A loaded DLL can itself be
// renamed on Windows, so a move failure here is a TRANSIENT external lock — an
// AV scanner mid-scan or a second handle — surfacing as ERROR_SHARING_VIOLATION /
// ERROR_ACCESS_DENIED / ERROR_LOCK_VIOLATION. The original code aborted the whole
// update on the first such failure; retry a few times (~1.5s total) so a brief
// lock doesn't lose the user their install. Non-transient errors fail fast.
bool moveFileWithRetry(const std::string& src, const std::string& dst) {
    constexpr int kMaxAttempts = 6;
    DWORD delayMs = 50;
    for (int attempt = 1; ; ++attempt) {
        if (MoveFileA(src.c_str(), dst.c_str())) return true;
        const DWORD err = GetLastError();
        const bool transient = (err == ERROR_SHARING_VIOLATION ||
                                err == ERROR_ACCESS_DENIED ||
                                err == ERROR_LOCK_VIOLATION);
        if (attempt >= kMaxAttempts || !transient) {
            DEBUG_WARN_F("UpdateDownloader: MoveFile '%s' -> '%s' failed (error %lu, attempt %d/%d)",
                         src.c_str(), dst.c_str(), err, attempt, kMaxAttempts);
            return false;
        }
        Sleep(delayMs);
        delayMs *= 2;
    }
}
}  // namespace

bool UpdateDownloader::backupExistingFiles(const std::string& pluginDir, const std::string& backupDir) {
    // Move the entire plugin installation to backup:
    // Windows allows moving loaded DLLs, so we can move even the running plugin!
    // This is faster and more atomic than copying.

    // Move the .dlo file (works even while loaded!)
    std::string dloSrc = pluginDir + GAME_DLO_NAME;
    std::string dloDst = backupDir + GAME_DLO_NAME;
    DWORD attrs = GetFileAttributesA(dloSrc.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES) {
        if (!moveFileWithRetry(dloSrc, dloDst)) {
            DEBUG_WARN_F("UpdateDownloader: Failed to move %s to backup (error %lu)", GAME_DLO_NAME, GetLastError());
            return false;
        }
        DEBUG_INFO_F("UpdateDownloader: Moved %s to backup", GAME_DLO_NAME);
    }

    // Move the data directory
    std::string dataSrc = pluginDir + "mxbmrp3_data";  // No trailing backslash for MoveFile
    std::string dataDst = backupDir + "mxbmrp3_data";
    attrs = GetFileAttributesA(dataSrc.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        if (!moveFileWithRetry(dataSrc, dataDst)) {
            DEBUG_WARN_F("UpdateDownloader: Failed to move mxbmrp3_data/ to backup (error %lu)", GetLastError());
            // Try to restore the .dlo we already moved
            if (moveFileWithRetry(dloDst, dloSrc)) {
                DEBUG_INFO_F("UpdateDownloader: Restored %s after failed data backup", GAME_DLO_NAME);
            } else {
                DEBUG_WARN_F("UpdateDownloader: CRITICAL - Failed to restore %s (error %lu) - DO NOT delete backup!",
                    GAME_DLO_NAME, GetLastError());
            }
            return false;
        }
        DEBUG_INFO("UpdateDownloader: Moved mxbmrp3_data/ to backup");
    }

    return true;
}

bool UpdateDownloader::restoreFromBackup(const std::string& pluginDir, const std::string& backupDir,
                                         const std::vector<std::string>& extractedFiles) {
    DEBUG_WARN("UpdateDownloader: Restoring from backup...");

    // First, delete any files we extracted
    for (const auto& relativePath : extractedFiles) {
        std::string filePath = pluginDir + relativePath;
        DeleteFileA(filePath.c_str());
    }

    // Also remove any new mxbmrp3_data directory that might have been created
    std::string dataDir = pluginDir + "mxbmrp3_data";
    deleteDirectoryRecursive(dataDir + "\\");

    // Move the .dlo file back from backup. This is the critical one — if it
    // fails the install is left without a plugin, so its result drives the return
    // value (callers/logs can tell a clean rollback from a broken one).
    bool restoredOk = true;
    std::string dloSrc = backupDir + GAME_DLO_NAME;
    std::string dloDst = pluginDir + GAME_DLO_NAME;
    DWORD attrs = GetFileAttributesA(dloSrc.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES) {
        // Delete any partial .dlo that might have been extracted
        DeleteFileA(dloDst.c_str());
        if (moveFileWithRetry(dloSrc, dloDst)) {
            DEBUG_INFO_F("UpdateDownloader: Restored %s", GAME_DLO_NAME);
        } else {
            DEBUG_WARN_F("UpdateDownloader: CRITICAL - Failed to restore %s (error %lu) - backup kept",
                         GAME_DLO_NAME, GetLastError());
            restoredOk = false;
        }
    }

    // Move the data directory back from backup
    std::string dataSrc = backupDir + "mxbmrp3_data";
    std::string dataDst = pluginDir + "mxbmrp3_data";
    attrs = GetFileAttributesA(dataSrc.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        if (moveFileWithRetry(dataSrc, dataDst)) {
            DEBUG_INFO("UpdateDownloader: Restored mxbmrp3_data/ directory");
        } else {
            DEBUG_WARN_F("UpdateDownloader: Failed to restore mxbmrp3_data/ (error %lu)", GetLastError());
            restoredOk = false;
        }
    }

    DEBUG_INFO("UpdateDownloader: Restore complete");
    return restoredOk;
}

void UpdateDownloader::cleanupBackup(const std::string& backupDir) {
    deleteDirectoryRecursive(backupDir);
    DEBUG_INFO("UpdateDownloader: Cleaned up backup directory");
}

void UpdateDownloader::cleanupExtractedFiles(const std::string& pluginDir, const std::vector<std::string>& files) {
    for (const auto& relativePath : files) {
        std::string filePath = pluginDir + relativePath;
        DeleteFileA(filePath.c_str());
    }
}

bool UpdateDownloader::verifyExtractedFiles(const std::string& pluginDir,
                                            const std::vector<std::pair<std::string, size_t>>& expectedFiles) {
    for (const auto& [relativePath, expectedSize] : expectedFiles) {
        std::string filePath = pluginDir + relativePath;

        // Check file exists
        HANDLE hFile = CreateFileA(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                   NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) {
            DEBUG_WARN_F("UpdateDownloader: Verify failed - file missing: %s", relativePath.c_str());
            return false;
        }

        // Check file size
        LARGE_INTEGER fileSize;
        if (!GetFileSizeEx(hFile, &fileSize)) {
            CloseHandle(hFile);
            DEBUG_WARN_F("UpdateDownloader: Verify failed - can't get size: %s", relativePath.c_str());
            return false;
        }
        CloseHandle(hFile);

        if (static_cast<size_t>(fileSize.QuadPart) != expectedSize) {
            DEBUG_WARN_F("UpdateDownloader: Verify failed - size mismatch for %s: expected %zu, got %lld",
                        relativePath.c_str(), expectedSize, fileSize.QuadPart);
            return false;
        }
    }

    DEBUG_INFO_F("UpdateDownloader: Verified %zu files successfully", expectedFiles.size());
    return true;
}

// Check if a file should be skipped during extraction
static bool shouldSkipFile(const std::string& filename) {
    // Skip documentation files - not needed for runtime
    if (filename == "LICENSE" ||
        filename == "README.md" ||
        filename == "README.txt" ||
        filename == "THIRD_PARTY_LICENSES.md") {
        return true;
    }

    // Skip DLO files that don't match the current game
    // The ZIP may contain multiple game DLOs (mxbmrp3.dlo, mxbmrp3_gpb.dlo, etc.)
    // We only extract the one that matches GAME_DLO_NAME
    if (filename.size() >= 4 &&
        filename.substr(filename.size() - 4) == ".dlo") {
        if (filename != GAME_DLO_NAME) {
            DEBUG_INFO_F("UpdateDownloader: Skipping %s (not for this game)", filename.c_str());
            return true;
        }
    }

    return false;
}

std::string UpdateDownloader::mapToInstallPath(const std::string& zipFilename) const {
    // .dlo files go directly to plugin directory
    // Everything else goes under mxbmrp3_data/ subdirectory
    if (zipFilename.size() >= 4 &&
        zipFilename.substr(zipFilename.size() - 4) == ".dlo") {
        return zipFilename;
    }

    // All other files go under mxbmrp3_data/
    return "mxbmrp3_data\\" + zipFilename;
}

bool UpdateDownloader::extractAndInstall(const std::vector<char>& zipData, std::string& outError) {
    std::string pluginDir;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        pluginDir = m_pluginPath;
    }

    if (pluginDir.empty()) {
        outError = "Cannot determine plugin directory";
        return false;
    }

    DEBUG_INFO_F("UpdateDownloader: Target directory: %s", pluginDir.c_str());
    DEBUG_INFO("UpdateDownloader: Scanning release...");

    // Initialize ZIP reader
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));

    if (!mz_zip_reader_init_mem(&zip, zipData.data(), zipData.size(), 0)) {
        outError = "Failed to open ZIP";
        return false;
    }

    int numFiles = static_cast<int>(mz_zip_reader_get_num_files(&zip));
    DEBUG_INFO_F("UpdateDownloader: ZIP contains %d files", numFiles);

    // First pass: collect all files we'll extract and their expected sizes
    std::vector<std::string> filesToBackup;
    std::vector<std::pair<std::string, size_t>> expectedFiles;  // relative path -> expected size

    for (int i = 0; i < numFiles; i++) {
        mz_zip_archive_file_stat fileStat;
        if (!mz_zip_reader_file_stat(&zip, i, &fileStat)) {
            continue;
        }

        if (mz_zip_reader_is_file_a_directory(&zip, i)) {
            continue;
        }

        std::string filename = fileStat.m_filename;

        if (filename.find("..") != std::string::npos ||
            filename.find(':') != std::string::npos) {
            // ".." escapes the install dir; ":" would name an NTFS alternate data
            // stream (or a drive) once the entry is mapped under it. No legitimate
            // release asset contains either.
            continue;
        }

        // Strip top-level directory if present
        size_t firstSlash = filename.find('/');
        if (firstSlash != std::string::npos && firstSlash < filename.size() - 1) {
            std::string topDir = filename.substr(0, firstSlash);
            if (topDir.find("mxbmrp3") != std::string::npos) {
                filename = filename.substr(firstSlash + 1);
            }
        }

        if (filename.empty()) {
            continue;
        }

        // Skip documentation files (not needed at runtime)
        if (shouldSkipFile(filename)) {
            continue;
        }

        // Convert to Windows path separators
        std::replace(filename.begin(), filename.end(), '/', '\\');

        // Map to actual install path (.dlo to root, others to mxbmrp3_data/)
        std::string installPath = mapToInstallPath(filename);

        filesToBackup.push_back(installPath);
        expectedFiles.push_back({installPath, static_cast<size_t>(fileStat.m_uncomp_size)});
    }

    if (filesToBackup.empty()) {
        mz_zip_reader_end(&zip);
        outError = "ZIP contains no valid files";
        return false;
    }

    DEBUG_INFO_F("UpdateDownloader: Will extract %zu files", filesToBackup.size());

    // Early check: verify ZIP contains this game's DLO before doing any backup/extraction
    // This prevents unnecessary backup operations if the ZIP is for a different game
    bool hasGameDlo = false;
    for (const auto& file : filesToBackup) {
        if (file == GAME_DLO_NAME) {
            hasGameDlo = true;
            break;
        }
    }
    if (!hasGameDlo) {
        mz_zip_reader_end(&zip);
        DEBUG_WARN_F("UpdateDownloader: ZIP does not contain %s - invalid release for this game!", GAME_DLO_NAME);
        outError = "Release not for " GAME_NAME;
        return false;
    }
    DEBUG_INFO_F("UpdateDownloader: Found %s in ZIP", GAME_DLO_NAME);

    // In debug mode, skip backup since we're extracting to an empty test directory
    std::string backupDir;
    if (!m_debugMode) {
        // Backup step
        setStepStatus(Step::BACKUP, StepStatus::IN_PROGRESS);

        // Create backup directory
        backupDir = pluginDir + "mxbmrp3_update_backup\\";
        if (!createBackupDirectory(backupDir)) {
            mz_zip_reader_end(&zip);
            outError = "Failed to create backup directory";
            return false;
        }

        // Backup existing files (moves the game's .dlo and mxbmrp3_data/ to backup)
        if (!backupExistingFiles(pluginDir, backupDir)) {
            mz_zip_reader_end(&zip);
            // DO NOT cleanupBackup here - the DLO might still be in backup if restore failed!
            // The backup dir will be cleaned up on next successful update attempt.
            outError = "Backup failed - try manual update";
            return false;
        }

        setStepStatus(Step::BACKUP, StepStatus::COMPLETE);
    } else {
        DEBUG_INFO("UpdateDownloader: DEBUG MODE - Skipping backup (test directory)");
        setStepStatus(Step::BACKUP, StepStatus::SKIPPED);
    }

    // Extract step
    setStepStatus(Step::EXTRACT, StepStatus::IN_PROGRESS);
    DEBUG_INFO("UpdateDownloader: Extracting files...");

    // Track what we've extracted for potential rollback
    std::vector<std::string> extractedFiles;
    bool extractionFailed = false;
    std::string extractError;

    // Second pass: extract files
    for (int i = 0; i < numFiles && !extractionFailed; i++) {
        if (m_cancelRequested || m_shutdownRequested) {
            extractionFailed = true;
            extractError = "Cancelled";
            break;
        }

        mz_zip_archive_file_stat fileStat;
        if (!mz_zip_reader_file_stat(&zip, i, &fileStat)) {
            continue;
        }

        if (mz_zip_reader_is_file_a_directory(&zip, i)) {
            continue;
        }

        std::string filename = fileStat.m_filename;

        if (filename.find("..") != std::string::npos ||
            filename.find(':') != std::string::npos) {
            // ".." escapes the install dir; ":" would name an NTFS alternate data
            // stream (or a drive) once the entry is mapped under it. No legitimate
            // release asset contains either.
            continue;
        }

        size_t firstSlash = filename.find('/');
        if (firstSlash != std::string::npos && firstSlash < filename.size() - 1) {
            std::string topDir = filename.substr(0, firstSlash);
            if (topDir.find("mxbmrp3") != std::string::npos) {
                filename = filename.substr(firstSlash + 1);
            }
        }

        if (filename.empty()) {
            continue;
        }

        // Skip documentation files (not needed at runtime)
        if (shouldSkipFile(filename)) {
            continue;
        }

        // Convert to Windows path separators
        std::replace(filename.begin(), filename.end(), '/', '\\');

        // Map to actual install path (.dlo to root, others to mxbmrp3_data/)
        std::string installPath = mapToInstallPath(filename);
        std::string outputPath = pluginDir + installPath;

        // Create subdirectories if needed (recursive)
        size_t lastSlash = outputPath.find_last_of('\\');
        if (lastSlash != std::string::npos) {
            std::string dir = outputPath.substr(0, lastSlash);
            createDirectoriesRecursive(dir);
        }

        // Extract file (existing files have been moved to backup, so path should be clear)
        if (!mz_zip_reader_extract_to_file(&zip, i, outputPath.c_str(), 0)) {
            extractionFailed = true;
            extractError = "Failed to extract: " + installPath;
            break;
        }

        extractedFiles.push_back(installPath);
        DEBUG_INFO_F("UpdateDownloader: Extracted %s", installPath.c_str());
    }

    mz_zip_reader_end(&zip);

    // Handle extraction failure
    if (extractionFailed) {
        DEBUG_WARN_F("UpdateDownloader: Extraction failed: %s", extractError.c_str());
        if (!m_debugMode && !restoreFromBackup(pluginDir, backupDir, extractedFiles)) {
            // Rollback couldn't put the original .dlo back — the install is broken
            // and the backup is the only copy. Surface it so the user knows to
            // recover from mxbmrp3_update_backup\ rather than assuming a clean revert.
            DEBUG_WARN("UpdateDownloader: CRITICAL - rollback incomplete; backup retained for manual recovery");
            outError = extractError + " (rollback incomplete - recover from mxbmrp3_update_backup)";
            return false;
        }
        // In debug mode, leave files in test dir - cleanup causes crashes
        outError = extractError;
        return false;
    }

    // Extract complete (DLO presence was verified before backup started)
    setStepStatus(Step::EXTRACT, StepStatus::COMPLETE);

    // Install step (verification)
    setStepStatus(Step::INSTALL, StepStatus::IN_PROGRESS);

    // Verify all extracted files (skip in debug mode)
    if (!m_debugMode && !verifyExtractedFiles(pluginDir, expectedFiles)) {
        DEBUG_WARN("UpdateDownloader: Verification failed, restoring backup");
        if (!restoreFromBackup(pluginDir, backupDir, extractedFiles)) {
            DEBUG_WARN("UpdateDownloader: CRITICAL - rollback incomplete; backup retained for manual recovery");
            outError = "File verification failed (rollback incomplete - recover from mxbmrp3_update_backup)";
            return false;
        }
        outError = "File verification failed";
        return false;
    }

    // Install complete!
    setStepStatus(Step::INSTALL, StepStatus::COMPLETE);

    // Success!
    if (!m_debugMode) {
        // Intentionally keep backup until the next update for manual recovery if needed.
        // The backup directory is cleaned up by createBackupDirectory() when a new update starts,
        // allowing users to manually recover files even after multiple game restarts.
        DEBUG_INFO_F("UpdateDownloader: Extraction complete. Backup kept at: %s", backupDir.c_str());
    } else {
        DEBUG_INFO_F("UpdateDownloader: DEBUG MODE - Extraction complete at: %s", pluginDir.c_str());
    }
    return true;
}

std::string UpdateDownloader::calculateSHA256(const std::vector<char>& data) {
    BCRYPT_ALG_HANDLE hAlg = NULL;
    BCRYPT_HASH_HANDLE hHash = NULL;
    std::string result;

    // Open algorithm provider
    NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, NULL, 0);
    if (!BCRYPT_SUCCESS(status)) {
        return result;
    }

    // Get hash object size
    DWORD hashObjSize = 0;
    DWORD cbData = 0;
    status = BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&hashObjSize, sizeof(DWORD), &cbData, 0);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return result;
    }

    // Get hash size (should be 32 for SHA256)
    DWORD hashSize = 0;
    status = BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH, (PUCHAR)&hashSize, sizeof(DWORD), &cbData, 0);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return result;
    }

    // Allocate hash object
    std::vector<UCHAR> hashObj(hashObjSize);
    std::vector<UCHAR> hash(hashSize);

    // Create hash object
    status = BCryptCreateHash(hAlg, &hHash, hashObj.data(), hashObjSize, NULL, 0, 0);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return result;
    }

    // Hash the data
    status = BCryptHashData(hHash, (PUCHAR)data.data(), static_cast<ULONG>(data.size()), 0);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptDestroyHash(hHash);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return result;
    }

    // Finish hash
    status = BCryptFinishHash(hHash, hash.data(), hashSize, 0);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptDestroyHash(hHash);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return result;
    }

    // Convert to hex string (lowercase)
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (DWORD i = 0; i < hashSize; i++) {
        oss << std::setw(2) << static_cast<int>(hash[i]);
    }
    result = oss.str();

    // Cleanup
    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);

    return result;
}
