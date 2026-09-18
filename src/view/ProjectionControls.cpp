#include "ProjectionControls.h"
#include <model/framebuffer/FramebufferModel.h>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QStylePainter>
#include <QStyleOptionToolButton>

namespace {
class ResetButton : public QToolButton {
public:
    explicit ResetButton(QWidget* parent) : QToolButton(parent) {
        setObjectName("projectionResetButton");
        setFixedSize(28, 28);
        setIconSize(QSize(16, 16));
        setToolButtonStyle(Qt::ToolButtonIconOnly);
        setFocusPolicy(Qt::StrongFocus);
        setAccessibleName(tr("Reset Projection View"));
        setToolTip(tr("Reset Projection View\nFace +Z, +Y up; horizontal FOV 90°. Image zoom and colors are unchanged."));
        setAccessibleDescription(toolTip());
    }
    void paintEvent(QPaintEvent*) override {
        QStyleOptionToolButton option;
        initStyleOption(&option);
        option.text.clear(); option.icon = QIcon();
        QStylePainter painter(this);
        painter.drawComplexControl(QStyle::CC_ToolButton, option);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.translate((width() - 16.) / 2., (height() - 16.) / 2.);
        QPen pen(option.palette.color(isEnabled() ? QPalette::Active : QPalette::Disabled,
                                       QPalette::ButtonText), 1.4);
        pen.setCapStyle(Qt::RoundCap); pen.setJoinStyle(Qt::RoundJoin);
        painter.setPen(pen); painter.setBrush(Qt::NoBrush);
        painter.drawArc(QRectF(2., 2., 12., 12.), 35 * 16, -295 * 16);
        painter.drawLine(QPointF(13., 1.), QPointF(13., 5.));
        painter.drawLine(QPointF(13., 5.), QPointF(9., 5.));
        painter.drawLine(QPointF(8., 11.), QPointF(8., 5.));
        painter.drawLine(QPointF(8., 5.), QPointF(6., 7.));
        painter.drawLine(QPointF(8., 5.), QPointF(10., 7.));
    }
};
}

ProjectionControls::ProjectionControls(QWidget* parent) : QWidget(parent)
{
    setObjectName("projectionControls");
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0); row->setSpacing(6);
    m_reset = new ResetButton(this);
    m_angles = new QLabel(this);
    m_angles->setObjectName("projectionAngles");
    m_angles->setTextFormat(Qt::PlainText);
    m_angles->setMinimumWidth(0);
    m_angles->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    row->addWidget(m_reset); row->addWidget(m_angles);
    connect(m_reset, &QToolButton::clicked, this, [this] {
        if (m_model) const_cast<FramebufferModel*>(m_model.data())->resetProjectionView();
    });
    hide();
}

void ProjectionControls::setModel(const FramebufferModel* model)
{
    if (m_model != model) {
        if (m_model) disconnect(m_model, nullptr, this, nullptr);
        m_model = model;
        if (model) {
            connect(model, &FramebufferModel::imageChanged, this, &ProjectionControls::refresh);
            connect(model, &FramebufferModel::readinessChanged, this, &ProjectionControls::refresh);
            connect(model, &QObject::destroyed, this, [this] { m_model.clear(); refresh(); });
        }
    }
    refresh();
}

void ProjectionControls::refresh()
{
    const auto state = m_model ? m_model->projectionState() : EnvironmentProjection::State();
    const bool camera = m_model && m_model->isImageLoaded() && m_model->environmentSource().available()
      && (state.type == EnvironmentProjection::Perspective || state.type == EnvironmentProjection::Sphere);
    setVisible(camera);
    m_reset->setEnabled(camera);
    if (!camera) return;
    QString text = tr("Yaw %1°  Pitch %2°").arg(state.yaw, 0, 'f', 1).arg(state.pitch, 0, 'f', 1);
    if (state.type == EnvironmentProjection::Perspective)
        text += tr("  FOV %1°").arg(state.fieldOfView, 0, 'f', 1);
    m_angles->setText(text);
    m_angles->setToolTip(tr("Displayed orientation: %1\nYaw 0°, pitch 0° faces +Z with +Y up.").arg(text));
}
