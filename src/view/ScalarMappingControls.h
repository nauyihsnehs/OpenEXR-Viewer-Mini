#pragma once

#include <QWidget>
#include <util/Colormap.h>

#include "PreviewState.h"

class QComboBox;
class QLabel;
class QToolButton;
class RangeSliderWidget;
class ScaleWidget;
class ScientificDoubleSpinBox;

// Each preview owns its controls and mapping state; the source model supplies
// the finite range and consumes the parameter signals.
class ScalarMappingControls: public QWidget
{
    Q_OBJECT

  public:
    ScalarMappingControls(ColormapModule::Map defaultMap, ScaleWidget* scale,
                          QWidget* parent = nullptr);

    ColormapModule::Map colormap() const;
    double minimum() const;
    double maximum() const;
    bool automatic() const { return m_automatic; }
    QWidget* colorScaleButton() const;
    void setSourceName(const QString& name);
    void setFiniteRange(bool available, double minimum, double maximum);
    void setMappingVisible(bool visible);
    void saveState(PreviewState& state) const;
    void restoreState(const PreviewState& state);
    QString parameterText() const;
    void reset();
    void adjustMaximum(double steps);

  signals:
    void colormapChanged(ColormapModule::Map map);
    void rangeChanged(double minimum, double maximum);
    void automaticChanged(bool enabled);

  private:
    void setRange(double minimum, double maximum, bool manual);
    void toggleAutomatic();
    void updateRangeWidgets();
    void updateScale();

    ColormapModule::Map m_defaultMap;
    ScaleWidget* m_scale;
    QLabel* m_source;
    QComboBox* m_colormap;
    RangeSliderWidget* m_rangeSlider;
    ScientificDoubleSpinBox* m_minimum;
    ScientificDoubleSpinBox* m_maximum;
    QToolButton* m_autoButton;
    QToolButton* m_scaleButton;
    bool m_automatic = false;
    bool m_hasFiniteRange = false;
    bool m_mappingVisible = true;
    double m_finiteMinimum = 0.;
    double m_finiteMaximum = 1.;
    double m_savedMinimum = 0.;
    double m_savedMaximum = 1.;
};
