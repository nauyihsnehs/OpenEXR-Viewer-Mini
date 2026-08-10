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


class QFileIStream: public Imf::IStream
{
  public:
    QFileIStream(const QByteArray& filename, QFile& file)
      : Imf::IStream(filename.constData())
      , m_file(file)
    {}

    bool read(char c[], int n) override { return m_file.read(c, n) == n; }

    uint64_t tellg() override { return static_cast<uint64_t>(m_file.pos()); }

    void seekg(uint64_t pos) override { m_file.seek(static_cast<qint64>(pos)); }

    bool isMemoryMapped() const override { return false; }

  private:
    QFile& m_file;
};


OpenEXRImage::OpenEXRImage(const QString& filename, QObject* parent)
  : QObject(parent)
  , m_filename(filename)
  , m_isStream(false)
{
    std::unique_ptr<QFile> file(new QFile(filename));

    if (!file->open(QFile::ReadOnly)) {
        throw std::runtime_error(QString("Cannot read image file \"%1\". %2.")
                                   .arg(filename, file->errorString())
                                   .toStdString());
    }

    m_streamName = filename.toUtf8();

    std::unique_ptr<Imf::IStream> stream(new QFileIStream(m_streamName, *file));
    std::unique_ptr<Imf::MultiPartInputFile> exrIn(
      new Imf::MultiPartInputFile(*stream));
    std::unique_ptr<HeaderModel> headerModel(
      new HeaderModel(*exrIn, exrIn->parts(), nullptr));
    std::unique_ptr<LayerModel> layerModel(new LayerModel(*exrIn, nullptr));

    headerModel->addFile(*exrIn, filename);

    m_file        = std::move(file);
    m_stream      = std::move(stream);
    m_exrIn       = std::move(exrIn);
    m_headerModel = std::move(headerModel);
    m_layerModel  = std::move(layerModel);
}


OpenEXRImage::OpenEXRImage(std::istream& stream, QObject* parent)
  : QObject(parent)
  , m_isStream(true)
{
    std::unique_ptr<Imf::IStream> inputStream(new StdIStream(stream));
    std::unique_ptr<Imf::MultiPartInputFile> exrIn(
      new Imf::MultiPartInputFile(*inputStream));
    std::unique_ptr<HeaderModel> headerModel(
      new HeaderModel(*exrIn, exrIn->parts(), nullptr));
    std::unique_ptr<LayerModel> layerModel(new LayerModel(*exrIn, nullptr));

    headerModel->addFile(*exrIn, "Stream");

    m_stream      = std::move(inputStream);
    m_exrIn       = std::move(exrIn);
    m_headerModel = std::move(headerModel);
    m_layerModel  = std::move(layerModel);
}


OpenEXRImage::~OpenEXRImage() = default;
