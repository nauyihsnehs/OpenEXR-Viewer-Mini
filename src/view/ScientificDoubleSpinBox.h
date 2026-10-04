#pragma once

#include <QDoubleSpinBox>
#include <QDoubleValidator>
#include <QLocale>
#include <QVariant>
#include <cmath>
#include <limits>

// Keep the QDoubleSpinBox API without rounding small FLOAT samples to zero.
class ScientificDoubleSpinBox: public QDoubleSpinBox
{
  public:
    explicit ScientificDoubleSpinBox(QWidget* parent = nullptr)
      : QDoubleSpinBox(parent)
    {
        setLocale(QLocale::c());
        setDecimals(100);
        setRange(-sampleLimit(), sampleLimit());
        setSingleStep(0.01);
        setKeyboardTracking(false);
        setProperty("scientificRange", true);
        setMinimumWidth(180);
        setMaximumWidth(240);
    }

    static double sampleLimit() { return std::numeric_limits<float>::max(); }

    void setScientificMode(bool enabled)
    {
        setProperty("scientificRange", enabled);
        setDecimals(enabled ? 100 : 2);
    }

  protected:
    QString textFromValue(double value) const override
    {
        if (!property("scientificRange").toBool()) return QDoubleSpinBox::textFromValue(value);
        return locale().toString(value, 'g', std::numeric_limits<double>::max_digits10);
    }

    double valueFromText(const QString& text) const override
    {
        if (!property("scientificRange").toBool()) return QDoubleSpinBox::valueFromText(text);
        bool valid = false;
        const double parsed = locale().toDouble(text.trimmed(), &valid);
        return valid && std::isfinite(parsed) ? parsed : value();
    }

    QValidator::State validate(QString& text, int& position) const override
    {
        if (!property("scientificRange").toBool()) return QDoubleSpinBox::validate(text, position);
        QDoubleValidator validator(minimum(), maximum(), decimals());
        validator.setLocale(locale());
        validator.setNotation(QDoubleValidator::ScientificNotation);
        return validator.validate(text, position);
    }
};
