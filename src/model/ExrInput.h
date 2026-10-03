#pragma once

#include <OpenEXR/ImfMultiPartInputFile.h>
#include <memory>
#include <mutex>
#include <map>

struct DeepSamples;
class RadianceInput;

// A source snapshot and its decoding locks live as long as any decoder uses them.
struct ExrInput {
    std::shared_ptr<Imf::MultiPartInputFile> file;
    std::shared_ptr<RadianceInput> radiance;

    int parts() const;
    const Imf::Header& header(int part) const;
    std::mutex mutex;
    std::timed_mutex deepMutex;
    std::map<int, std::weak_ptr<const DeepSamples>> deepParts;
};
