#include "ScalarMappingControls.h"

#include "ComboBoxBehavior.h"
#include "RangeSliderWidget.h"
#include "ScaleWidget.h"
#include "ScientificDoubleSpinBox.h"
#include "ViewerIcons.h"
#include "WorkspaceWidgets.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QSignalBlocker>
#include <QToolButton>

#include <algorithm>
#include <cmath>

namespace {
class SourceLabel: public QLabel
{
  public:
    explicit SourceLabel(QWidget* parent): QLabel(parent)
    {
        setMaximumWidth(96);
        setMinimumWidth(0);
        setFixedHeight(28);
        setTextFormat(Qt::PlainText);
    }

    QSize sizeHint() const override
    {
        const QSize hint = QLabel::sizeHint();
        return QSize(std::min(96, hint.width()), 28);
    }

    QSize minimumSizeHint() const override { return sizeHint(); }

  protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setPen(palette().color(isEnabled() ? QPalette::Active : QPalette::Disabled,
                                       QPalette::WindowText));
        painter.drawText(contentsRect(), Qt::AlignLeft | Qt::AlignVCenter,
          fontMetrics().elidedText(text(), Qt::ElideRight, contentsRect().width()));
    }
};
}

ScalarMappingControls::ScalarMappingControls(
  ColormapModule::Map defaultMap, ScaleWidget* scale, QWidget* parent)
  : QWidget(parent)
  , m_defaultMap(defaultMap)
  , m_scale(scale)
  , m_source(new SourceLabel(this))
  , m_colormap(new QComboBox(this))
  , m_rangeSlider(new RangeSliderWidget(this))
  , m_minimum(new ScientificDoubleSpinBox(this))
  , m_maximum(new ScientificDoubleSpinBox(this))
  , m_autoButton(new QToolButton(this))
  , m_scaleButton(new QToolButton(this))
{
    setObjectName("scalarMappingControls");
    setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    layout->addWidget(m_source);
    layout->addWidget(m_colormap);
    layout->addWidget(createToolbarSeparator(this), 0, Qt::AlignVCenter);
    layout->addWidget(new QLabel(tr("Range"), this));
    layout->addWidget(m_minimum);
    layout->addWidget(m_rangeSlider);
    layout->addWidget(m_maximum);
    layout->addWidget(createToolbarSeparator(this), 0, Qt::AlignVCenter);
    layout->addWidget(m_autoButton);

    m_source->setAccessibleName(tr("Mapping Source"));
    m_colormap->setAccessibleName(tr("Colormap"));
    m_colormap->setToolTip(tr("Colormap"));
    m_colormap->setMaximumWidth(130);
    m_colormap->setFixedHeight(28);
    for (int i = 0; i < ColormapModule::N_MAPS; ++i)
        m_colormap->addItem(QString::fromStdString(
          ColormapModule::toString(static_cast<ColormapModule::Map>(i))));
    m_colormap->setCurrentIndex(defaultMap);
    m_rangeSlider->setObjectName("scalarMappingRangeSlider");
    m_rangeSlider->setFixedSize(140, 28);
    m_rangeSlider->setAccessibleName(tr("Mapping Range"));
    m_minimum->setAccessibleName(tr("Mapping Minimum"));
    m_maximum->setAccessibleName(tr("Mapping Maximum"));
    m_minimum->setToolTip(tr("Mapping Minimum"));
    m_maximum->setToolTip(tr("Mapping Maximum"));
    m_minimum->setToolbarCompact(true);
    m_maximum->setToolbarCompact(true);
    m_maximum->setValue(1.);
    m_autoButton->setCheckable(true);
    m_scaleButton->setCheckable(true);
    m_scaleButton->setChecked(true);
    ViewerIcons::setupButton(m_autoButton, ViewerIcons::AutoRange,
      tr("Automatic Range"),
      tr("Use the full finite value range.\nTurn off to restore the previous manual range.\nAdjust either bound to return to a manual range."));
    ViewerIcons::setupButton(m_scaleButton, ViewerIcons::ColorScale,
      tr("Color Scale"), tr("Show or hide the color scale"));

    connect(m_colormap, QOverload<int>::of(&QComboBox::currentIndexChanged),
      this, [this](int) {
          updateScale();
          emit colormapChanged(colormap());
      });
    connect(m_minimum, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
      this, [this](double value) { setRange(value, maximum(), true); });
    connect(m_maximum, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
      this, [this](double value) { setRange(minimum(), value, true); });
    connect(m_rangeSlider, &RangeSliderWidget::rangeChanged,
      this, [this](double low, double high) { setRange(low, high, true); });
    connect(m_autoButton, &QToolButton::clicked,
      this, &ScalarMappingControls::toggleAutomatic);
    connect(m_scaleButton, &QToolButton::toggled,
      this, [this] { updateScale(); });
    updateRangeWidgets();
    applyComboBoxBehavior(this);
}

ColormapModule::Map ScalarMappingControls::colormap() const
{
    return static_cast<ColormapModule::Map>(m_colormap->currentIndex());
}

double ScalarMappingControls::minimum() const { return m_minimum->value(); }
double ScalarMappingControls::maximum() const { return m_maximum->value(); }
QWidget* ScalarMappingControls::colorScaleButton() const { return m_scaleButton; }

void ScalarMappingControls::setSourceName(const QString& name)
{
    m_source->setText(name);
    m_source->setToolTip(name);
}

void ScalarMappingControls::setMappingVisible(bool visible)
{
    m_mappingVisible = visible;
    setVisible(visible);
    m_scaleButton->setVisible(visible);
    updateScale();
}

void ScalarMappingControls::setFiniteRange(bool available, double low, double high)
{
    m_hasFiniteRange = available && std::isfinite(low) && std::isfinite(high) && low <= high;
    if (m_hasFiniteRange) {
        m_finiteMinimum = low;
        m_finiteMaximum = high;
        if (m_automatic && (minimum() != low || maximum() != high))
            setRange(low, high, false);
    }
    updateRangeWidgets();
}

void ScalarMappingControls::setRange(double low, double high, bool manual)
{
    if (!std::isfinite(low) || !std::isfinite(high)) return;
    if (low > high) std::swap(low, high);
    const bool wasAutomatic = m_automatic;
    if (manual) m_automatic = false;
    const QSignalBlocker lowBlocker(m_minimum), highBlocker(m_maximum);
    m_minimum->setRange(-ScientificDoubleSpinBox::sampleLimit(), high);
    m_maximum->setRange(low, ScientificDoubleSpinBox::sampleLimit());
    m_minimum->setValue(low);
    m_maximum->setValue(high);
    if (manual) {
        m_savedMinimum = minimum();
        m_savedMaximum = maximum();
    }
    updateRangeWidgets();
    // Store the new bounds while the model is still in Auto; disabling Auto
    // then renders the requested manual range rather than the old bounds.
    emit rangeChanged(minimum(), maximum());
    if (wasAutomatic && !m_automatic) emit automaticChanged(false);
}

void ScalarMappingControls::toggleAutomatic()
{
    if (m_automatic) {
        setRange(m_savedMinimum, m_savedMaximum, true);
        return;
    }
    if (!m_hasFiniteRange) {
        updateRangeWidgets();
        return;
    }
    m_savedMinimum = minimum();
    m_savedMaximum = maximum();
    m_automatic = true;
    emit automaticChanged(true);
    setRange(m_finiteMinimum, m_finiteMaximum, false);
}

void ScalarMappingControls::updateRangeWidgets()
{
    const QSignalBlocker autoBlocker(m_autoButton), sliderBlocker(m_rangeSlider);
    m_autoButton->setChecked(m_automatic);
    m_autoButton->setEnabled(m_hasFiniteRange || m_automatic);
    const double low = m_hasFiniteRange ? m_finiteMinimum : 0.;
    const double high = m_hasFiniteRange ? m_finiteMaximum : 1.;
    m_rangeSlider->setBounds(std::min(low, minimum()), std::max(high, maximum()));
    m_rangeSlider->setRange(minimum(), maximum());
    updateScale();
}

void ScalarMappingControls::updateScale()
{
    m_scale->setColormap(colormap());
    m_scale->setMin(minimum());
    m_scale->setMax(maximum());
    m_scale->setVisible(m_mappingVisible && m_scaleButton->isChecked());
}

void ScalarMappingControls::saveState(PreviewState& state) const
{
    state.colormap = colormap();
    state.minimum = minimum();
    state.maximum = maximum();
    state.savedMinimum = m_savedMinimum;
    state.savedMaximum = m_savedMaximum;
    state.automatic = m_automatic;
    state.scaleVisible = m_scaleButton->isChecked();
}

void ScalarMappingControls::restoreState(const PreviewState& state)
{
    const QSignalBlocker selfBlocker(this), mapBlocker(m_colormap), scaleBlocker(m_scaleButton);
    m_colormap->setCurrentIndex(state.colormap);
    m_scaleButton->setChecked(state.scaleVisible);
    m_automatic = state.automatic;
    m_savedMinimum = state.savedMinimum;
    m_savedMaximum = state.savedMaximum;
    setRange(state.minimum, state.maximum, false);
}

void ScalarMappingControls::reset()
{
    m_colormap->setCurrentIndex(m_defaultMap);
    setRange(0., 1., true);
    m_scaleButton->setChecked(true);
    updateScale();
}

void ScalarMappingControls::adjustMaximum(double steps)
{
    if (!std::isfinite(steps) || steps == 0.) return;
    m_maximum->setValue(maximum() + steps * m_maximum->singleStep());
}

QString ScalarMappingControls::parameterText() const
{
    return tr("%1 | %2 | %3 - %4").arg(m_source->text(), m_colormap->currentText())
      .arg(minimum(), 0, 'g', 4).arg(maximum(), 0, 'g', 4);
}
