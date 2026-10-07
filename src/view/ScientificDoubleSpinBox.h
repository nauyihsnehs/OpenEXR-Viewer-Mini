#pragma once

#include <QDoubleSpinBox>
#include <QDoubleValidator>
#include <QFocusEvent>
#include <QHelpEvent>
#include <QLineEdit>
#include <QLocale>
#include <QSignalBlocker>
#include <QToolTip>
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
        connect(lineEdit(), &QLineEdit::textEdited, this, [this] {
            m_textEdited = true;
        });
        connect(this, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, [this] { m_textEdited = false; });
    }

    static double sampleLimit() { return std::numeric_limits<float>::max(); }

    // Opt in only for toolbar fields. Display precision never changes decimals()
    // or the stored double, including when Qt interprets an untouched readout.
    void setToolbarCompact(bool enabled)
    {
        m_toolbarCompact = enabled;
        setProperty("toolbarCompact", enabled);
        m_textEdited = false;
        m_editing = hasFocus();
        if (enabled) {
            setFixedSize(72, 28);
            setButtonSymbols(QAbstractSpinBox::UpDownArrows);
        }
        refreshDisplay();
    }

    void setScientificMode(bool enabled)
    {
        setProperty("scientificRange", enabled);
        setDecimals(enabled ? 100 : 2);
        if (m_toolbarCompact) refreshDisplay();
    }

  protected:
    QString textFromValue(double value) const override
    {
        if (!property("scientificRange").toBool()) return QDoubleSpinBox::textFromValue(value);
        const int precision = m_toolbarCompact && !m_editing
          ? 6 : std::numeric_limits<double>::max_digits10;
        return locale().toString(value, 'g', precision);
    }

    double valueFromText(const QString& text) const override
    {
        if (isUnchangedReadout(text)) return value();
        if (!property("scientificRange").toBool()) return QDoubleSpinBox::valueFromText(text);
        bool valid = false;
        const double parsed = locale().toDouble(text.trimmed(), &valid);
        return valid && std::isfinite(parsed) ? parsed : value();
    }

    QValidator::State validate(QString& text, int& position) const override
    {
        // A rounded readout can lie just outside a boundary. It is a display of
        // the current valid value, not a newly entered out-of-range number.
        if (isUnchangedReadout(text)) return QValidator::Acceptable;
        if (!property("scientificRange").toBool()) return QDoubleSpinBox::validate(text, position);
        QDoubleValidator validator(minimum(), maximum(), decimals());
        validator.setLocale(locale());
        validator.setNotation(QDoubleValidator::ScientificNotation);
        return validator.validate(text, position);
    }

    void focusInEvent(QFocusEvent* event) override
    {
        if (m_toolbarCompact) {
            m_editing = true;
            m_textEdited = false;
            refreshDisplay();
        }
        QDoubleSpinBox::focusInEvent(event);
    }

    void focusOutEvent(QFocusEvent* event) override
    {
        // Let Qt commit real user edits while the full-precision text is still
        // displayed. Only then return to the shortened readout.
        QDoubleSpinBox::focusOutEvent(event);
        if (m_toolbarCompact) {
            m_editing = false;
            m_textEdited = false;
            refreshDisplay();
        }
    }

    bool event(QEvent* event) override
    {
        if (m_toolbarCompact && event->type() == QEvent::ToolTip) {
            auto* help = static_cast<QHelpEvent*>(event);
            const QString exact = locale().toString(value(), 'g', std::numeric_limits<double>::max_digits10);
            QToolTip::showText(help->globalPos(), toolTip().isEmpty()
              ? exact : toolTip() + "\n" + exact, this);
            return true;
        }
        return QDoubleSpinBox::event(event);
    }

  private:
    bool isUnchangedReadout(const QString& text) const
    {
        return m_toolbarCompact && !m_textEdited
          && text.trimmed() == prefix() + textFromValue(value()) + suffix();
    }

    void refreshDisplay()
    {
        const QSignalBlocker blocker(lineEdit());
        const QString display = value() == minimum() && !specialValueText().isEmpty()
          ? specialValueText() : prefix() + textFromValue(value()) + suffix();
        lineEdit()->setText(display);
        lineEdit()->setCursorPosition(prefix().size());
    }

    bool m_toolbarCompact = false;
    bool m_editing = false;
    bool m_textEdited = false;
};
