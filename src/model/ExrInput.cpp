#include "ExrInput.h"
#include "RadianceInput.h"
#include <stdexcept>

int ExrInput::parts() const
{
    if (file) return file->parts();
    return radiance ? 1 : 0;
}

const Imf::Header& ExrInput::header(int part) const
{
    if (part < 0 || part >= parts()) throw std::runtime_error("Invalid image part.");
    return file ? file->header(part) : radiance->header();
}
