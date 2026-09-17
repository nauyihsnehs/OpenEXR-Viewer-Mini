#pragma once

#include <OpenEXR/ImfMultiPartInputFile.h>
#include <memory>
#include <mutex>
#include <map>

struct DeepSamples;

// A file and its frame-buffer binding lock live as long as any decoder uses them.
struct ExrInput {
    std::shared_ptr<Imf::MultiPartInputFile> file;
    std::mutex mutex;
    std::timed_mutex deepMutex;
    std::map<int, std::weak_ptr<const DeepSamples>> deepParts;
};
