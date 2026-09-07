/**
 * Copyright (c) 2021 Alban Fichet <alban dot fichet at gmx dot fr>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following
 * disclaimer in the documentation and/or other materials provided
 * with the distribution.
 *  * Neither the name of the organization(s) nor the names of its
 * contributors may be used to endorse or promote products derived
 * from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "OpenEXRImage.h"
#include "StdIStream.h"

#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfHeader.h>

#include <Imath/ImathBox.h>

#include <QFile>

#include <memory>
#include <stdexcept>
#include <utility>

#ifdef _WIN32
#    define NOMINMAX
#    include <windows.h>
#    include <fcntl.h>
#    include <io.h>
#endif


class QFileIStream: public Imf::IStream
{
  public:
    QFileIStream(const QByteArray& filename, QFile& file)
      : Imf::IStream(filename.constData())
      , m_file(file)
    {}

    bool read(char c[], int n) override
    {
        if (n < 0 || m_file.read(c, n) != n)
            throw std::runtime_error(
              "Unexpected end of EXR file or read error.");
        return !m_file.atEnd();
    }

    uint64_t tellg() override
    {
        const qint64 pos = m_file.pos();
        if (pos < 0)
            throw std::runtime_error("Cannot query EXR file position.");
        return static_cast<uint64_t>(pos);
    }

    void seekg(uint64_t pos) override
    {
        if (
          pos > uint64_t(m_file.size())
          || !m_file.seek(static_cast<qint64>(pos)))
            throw std::runtime_error("Cannot seek in EXR file.");
    }

    bool isMemoryMapped() const override { return false; }

  private:
    QFile& m_file;
};


OpenEXRImage::OpenEXRImage(const QString& filename, QObject* parent)
  : QObject(parent)
  , m_filename(filename)
  , m_isStream(false)
{
    std::shared_ptr<QFile> file(new QFile(filename));

#ifdef _WIN32
    // Permit render tools to atomically replace a file while its old snapshot is open.
    const HANDLE handle = CreateFileW(
      reinterpret_cast<LPCWSTR>(filename.utf16()),
      GENERIC_READ,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
      nullptr,
      OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL,
      nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        throw std::runtime_error(
          QString("Cannot read image file \"%1\" (Windows error %2).")
            .arg(filename)
            .arg(GetLastError())
            .toStdString());
    const int descriptor = _open_osfhandle(
      reinterpret_cast<intptr_t>(handle),
      _O_RDONLY | _O_BINARY);
    if (descriptor == -1) {
        CloseHandle(handle);
        throw std::runtime_error("Cannot open EXR file handle.");
    }
    const bool opened
      = file->open(descriptor, QFile::ReadOnly, QFileDevice::AutoCloseHandle);
    if (!opened) _close(descriptor);
#else
    const bool opened = file->open(QFile::ReadOnly);
#endif
    if (!opened) {
        throw std::runtime_error(QString("Cannot read image file \"%1\". %2.")
                                   .arg(filename, file->errorString())
                                   .toStdString());
    }

    m_streamName = filename.toUtf8();

    std::shared_ptr<Imf::IStream> stream(new QFileIStream(m_streamName, *file));
    std::shared_ptr<Imf::MultiPartInputFile> exrIn(
      new Imf::MultiPartInputFile(*stream),
      [file, stream](Imf::MultiPartInputFile* input) {
          delete input;
      });
    std::unique_ptr<HeaderModel> headerModel(
      new HeaderModel(*exrIn, exrIn->parts(), nullptr));
    std::unique_ptr<LayerModel> layerModel(new LayerModel(*exrIn, nullptr));

    headerModel->addFile(*exrIn, filename);

    m_input->file = std::move(exrIn);
    m_headerModel = std::move(headerModel);
    m_layerModel  = std::move(layerModel);
}


OpenEXRImage::OpenEXRImage(std::istream& stream, QObject* parent)
  : QObject(parent)
  , m_isStream(true)
{
    std::shared_ptr<Imf::IStream> inputStream(new StdIStream(stream));
    std::shared_ptr<Imf::MultiPartInputFile> exrIn(
      new Imf::MultiPartInputFile(*inputStream),
      [inputStream](Imf::MultiPartInputFile* input) {
          delete input;
      });
    std::unique_ptr<HeaderModel> headerModel(
      new HeaderModel(*exrIn, exrIn->parts(), nullptr));
    std::unique_ptr<LayerModel> layerModel(new LayerModel(*exrIn, nullptr));

    headerModel->addFile(*exrIn, "Stream");

    m_input->file = std::move(exrIn);
    m_headerModel = std::move(headerModel);
    m_layerModel  = std::move(layerModel);
}


OpenEXRImage::~OpenEXRImage() = default;
