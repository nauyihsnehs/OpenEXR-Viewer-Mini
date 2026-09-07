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

#include "RangeSliderWidget.h"

#include <QMouseEvent>
#include <QPainter>
#include <QtGlobal>

#include <algorithm>
#include <cmath>

RangeSliderWidget::RangeSliderWidget(QWidget* parent)
  : QWidget(parent)
  , m_boundMin(0.)
  , m_boundMax(1.)
  , m_min(0.)
  , m_max(1.)
  , m_activeHandle(Handle_None)
{
    setMouseTracking(true);
}


QSize RangeSliderWidget::minimumSizeHint() const
{
    return QSize(120, 24);
}


QSize RangeSliderWidget::sizeHint() const
{
    return QSize(180, 24);
}


void RangeSliderWidget::setBounds(double min, double max)
{
    if (!std::isfinite(min) || !std::isfinite(max)) return;
    if (min > max) std::swap(min, max);
    if (min == max) max = min + 1.;

    m_boundMin = min;
    m_boundMax = max;
    m_min      = std::max(m_boundMin, std::min(m_min, m_boundMax));
    m_max      = std::max(m_boundMin, std::min(m_max, m_boundMax));

    if (m_min > m_max) std::swap(m_min, m_max);

    update();
}


void RangeSliderWidget::setRange(double min, double max)
{
    if (!std::isfinite(min) || !std::isfinite(max)) return;
    if (min > max) std::swap(min, max);

    m_min = std::max(m_boundMin, std::min(min, m_boundMax));
    m_max = std::max(m_boundMin, std::min(max, m_boundMax));

    update();
}


int RangeSliderWidget::trackLeft() const
{
    return 8;
}


int RangeSliderWidget::trackRight() const
{
    return std::max(trackLeft() + 1, width() - 8);
}


int RangeSliderWidget::positionFromValue(double value) const
{
    const double span = m_boundMax - m_boundMin;
    const double a    = span > 0. ? (value - m_boundMin) / span : 0.;

    return trackLeft()
           + static_cast<int>(std::round(a * (trackRight() - trackLeft())));
}


double RangeSliderWidget::valueFromPosition(int x) const
{
    const int    left  = trackLeft();
    const int    right = trackRight();
    const int    pos   = std::max(left, std::min(x, right));
    const double a     = double(pos - left) / double(right - left);

    return m_boundMin + a * (m_boundMax - m_boundMin);
}


RangeSliderWidget::Handle RangeSliderWidget::nearestHandle(int x) const
{
    const int minDistance = std::abs(x - positionFromValue(m_min));
    const int maxDistance = std::abs(x - positionFromValue(m_max));

    return minDistance <= maxDistance ? Handle_Min : Handle_Max;
}


void RangeSliderWidget::setRangeFromHandle(Handle handle, double value)
{
    const double previousMin = m_min;
    const double previousMax = m_max;
    if (handle == Handle_Min) {
        m_min = std::max(m_boundMin, std::min(value, m_max));
    }

    if (handle == Handle_Max) {
        m_max = std::min(m_boundMax, std::max(value, m_min));
    }

    if (previousMin != m_min || previousMax != m_max) {
        update();
        emit rangeChanged(m_min, m_max);
    }
}


void RangeSliderWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }
    m_activeHandle = nearestHandle(event->pos().x());
    setRangeFromHandle(m_activeHandle, valueFromPosition(event->pos().x()));
}


void RangeSliderWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (m_activeHandle == Handle_None || !(event->buttons() & Qt::LeftButton))
        return;

    setRangeFromHandle(m_activeHandle, valueFromPosition(event->pos().x()));
}


void RangeSliderWidget::mouseReleaseEvent(QMouseEvent* event)
{
    QWidget::mouseReleaseEvent(event);
    m_activeHandle = Handle_None;
}


void RangeSliderWidget::paintEvent(QPaintEvent* event)
{
    QWidget::paintEvent(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const int centerY   = height() / 2;
    const int barHeight = 5;
    const int minX      = positionFromValue(m_min);
    const int maxX      = positionFromValue(m_max);

    const QRect trackRect(
      trackLeft(),
      centerY - barHeight / 2,
      trackRight() - trackLeft(),
      barHeight);
    const QRect selectedRect(
      minX,
      centerY - barHeight / 2,
      maxX - minX,
      barHeight);

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(80, 80, 80));
    painter.drawRoundedRect(trackRect, 2, 2);

    painter.setBrush(QColor(210, 210, 210));
    painter.drawRoundedRect(selectedRect, 2, 2);

    painter.setPen(QColor(35, 35, 35));
    painter.setBrush(QColor(245, 245, 245));
    painter.drawEllipse(QPoint(minX, centerY), 6, 6);
    painter.drawEllipse(QPoint(maxX, centerY), 6, 6);
}
