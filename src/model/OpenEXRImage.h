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

#pragma once

#include <QByteArray>
#include <QObject>

#include <OpenEXR/ImfIO.h>
#include <OpenEXR/ImfMultiPartInputFile.h>

#include <model/ExrInput.h>
#include <model/attribute/HeaderModel.h>
#include <model/attribute/LayerModel.h>
#include <model/framebuffer/FramebufferModel.h>

#include <iosfwd>
#include <memory>

class QFile;

class OpenEXRImage: public QObject
{
    Q_OBJECT

  public:
    OpenEXRImage(const QString& filename, QObject* parent);
    OpenEXRImage(std::istream& stream, QObject* parent);

    ~OpenEXRImage();

    HeaderModel* getHeaderModel() const { return m_headerModel.get(); }
    LayerModel*  getLayerModel() const { return m_layerModel.get(); }

    Imf::MultiPartInputFile&                 getEXR() { return *m_input->file; }
    std::shared_ptr<ExrInput> sharedEXR() const
    {
        return m_input;
    }

    const QString& getFilename() const { return m_filename; }

    bool isStream() const { return m_isStream; }

  private:
    QString m_filename;
    bool    m_isStream;

    QByteArray m_streamName;

    // Keep the declaration order aligned with the dependency order. Destruction
    // happens in reverse: models, EXR input, stream adapter, then backing file.
    std::shared_ptr<ExrInput> m_input = std::make_shared<ExrInput>();
    std::unique_ptr<HeaderModel>             m_headerModel;
    std::unique_ptr<LayerModel>              m_layerModel;
};
