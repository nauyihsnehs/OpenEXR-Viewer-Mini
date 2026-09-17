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

#include "FramebufferModel.h"

#include <util/ColormapModule.h>
#include <model/ExrInput.h>

#include <memory>

class YFramebufferModel: public FramebufferModel
{
  public:
    YFramebufferModel(const std::string& layerName, QObject* parent = nullptr);

    virtual ~YFramebufferModel();

    virtual void
    load(const std::shared_ptr<ExrInput>& file, int partId, ResolutionLevel level = {});

    const std::string& getLayerName() const { return m_layer; }
    int                getPartId() const { return m_partID; }
    double displayMinimum() const { return m_data->deep ? m_data->displayMinimum : getDatasetMin(); }
    double displayMaximum() const { return m_data->deep ? m_data->displayMaximum : getDatasetMax(); }
    bool hasFiniteDisplay() const { return m_data->deep ? m_data->hasFiniteDisplay : hasFiniteSamples(); }

    virtual std::string              getColorInfo(int x, int y) const;
    virtual std::vector<std::string> rawChannelNames() const;

  public slots:
    void setRange(double min, double max);
    void setAutomaticRange(bool enabled);
    void setMinValue(double value);
    void setMaxValue(double value);
    void setColormap(ColormapModule::Map map);

  protected:
    void updateImage();

  private:
    int         m_partID;
    std::string m_layer;

    double m_min;
    double m_max;
    bool m_automaticRange = false;

    std::shared_ptr<const Colormap> m_cmap;
};
