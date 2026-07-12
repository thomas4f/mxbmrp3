#include "atomic_file_writer.h"

#include "../diagnostics/logger.h"

#include <windows.h>

#include <cstdio>    // std::remove
#include <fstream>

bool AtomicFileWriter::writeFileAtomic(const std::string& path, const std::string& bytes) {
    const std::string temp = path + ".tmp";
    {
        std::ofstream f(temp, std::ios::binary | std::ios::trunc);
        if (!f.is_open()) {
            DEBUG_WARN_F("[AtomicFileWriter] cannot open temp for write: %s", temp.c_str());
            return false;
        }
        f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        f.close();
        if (f.fail()) {
            DEBUG_WARN_F("[AtomicFileWriter] failed writing temp: %s", temp.c_str());
            std::remove(temp.c_str());
            return false;
        }
    }
    // Atomic replace; MOVEFILE_WRITE_THROUGH flushes to disk. This consolidates the
    // identical pattern previously copied in settings/stats/tracked-riders/rumble.
    if (!MoveFileExA(temp.c_str(), path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DEBUG_WARN_F("[AtomicFileWriter] atomic replace failed (err %lu): %s",
                     static_cast<unsigned long>(GetLastError()), path.c_str());
        std::remove(temp.c_str());
        return false;
    }
    return true;
}
