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

  protected:
    QString textFromValue(double value) const override
    {
        return locale().toString(value, 'g', std::numeric_limits<double>::max_digits10);
    }

    double valueFromText(const QString& text) const override
    {
        bool valid = false;
        const double parsed = locale().toDouble(text.trimmed(), &valid);
        return valid && std::isfinite(parsed) ? parsed : value();
    }

    QValidator::State validate(QString& text, int& position) const override
    {
        QDoubleValidator validator(minimum(), maximum(), decimals());
        validator.setLocale(locale());
        validator.setNotation(QDoubleValidator::ScientificNotation);
        return validator.validate(text, position);
    }
};
