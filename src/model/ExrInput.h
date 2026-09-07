#pragma once

#include <OpenEXR/ImfMultiPartInputFile.h>
#include <memory>
#include <mutex>

// A file and its frame-buffer binding lock live as long as any decoder uses them.
struct ExrInput {
    std::shared_ptr<Imf::MultiPartInputFile> file;
    std::mutex mutex;
};
