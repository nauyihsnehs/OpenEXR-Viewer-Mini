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

#include "mainwindow.h"
#include "FileDrop.h"
#include "WorkspaceWidgets.h"
#include "ViewerIcons.h"
#include "FramebufferInfo.h"
#include "MinimalImageWidget.h"
#include "RGBFramebufferWidget.h"
#include <util/PreviewImage.h>
#include <OpenEXR/ImfTileDescription.h>
#include "YFramebufferWidget.h"
#include <QShortcut>
#include <QSignalBlocker>
#include "./ui_mainwindow.h"
#include <view/about.h>
#include <io/ImageSave.h>
#include <view/SaveImageDialog.h>

#include <cassert>

#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QDebug>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QIcon>
#include <QMessageBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QMenuBar>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QImage>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QStyle>
#include <QSettings>
#include <QTabBar>
#include <QTextStream>
#include <QToolButton>
#include <QToolBar>
#include <QStackedWidget>
#include <QMdiArea>
#include <QPalette>
#include <QScreen>
#include <QWindow>
#include <QMoveEvent>
#include <QScopedValueRollback>
#include <QUrl>
#include <QVariant>
#include <QtAlgorithms>
#include <cmath>

#ifdef _WIN32
#    include <windows.h>
#    include <windowsx.h>
#endif

#include <model/attribute/HeaderModel.h>
#include <model/attribute/LayerItem.h>

static QPoint mouseGlobalPosition(QMouseEvent* event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event->globalPosition().toPoint();
#else
    return event->globalPos();
#endif
}


static const int   s_windowResizeBorder = 8;
static const int   s_clipboardMaxWidth  = 1024;
static const char* s_darkTheme          = "dark";
static const char* s_lightTheme         = "light";


static ImageSave::ConflictPolicy
conflictChoice(QWidget* parent, const QStringList& paths)
{
    QMessageBox box(parent);
    box.setWindowTitle(QObject::tr("Export"));
    box.setIcon(QMessageBox::Warning);
    box.setText(QObject::tr("The output file already exists."));
    box.setInformativeText(paths.join("\n"));

    QPushButton* overwriteButton
      = box.addButton(QObject::tr("Overwrite"), QMessageBox::AcceptRole);
    QPushButton* renameButton
      = box.addButton(QObject::tr("Auto Rename"), QMessageBox::ActionRole);
    box.addButton(QMessageBox::Cancel);

    box.exec();

    if (box.clickedButton() == overwriteButton) {
        return ImageSave::ConflictOverwrite;
    }
    if (box.clickedButton() == renameButton) {
        return ImageSave::ConflictRename;
    }

    return ImageSave::ConflictCancel;
}


enum TitleButtonIcon
{
    TitleButtonMinimize,
    TitleButtonMaximize,
    TitleButtonRestore,
    TitleButtonClose
};


static QIcon titleButtonIcon(TitleButtonIcon icon, const QColor& color)
{
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QPen pen(color, 2);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    switch (icon) {
        case TitleButtonMinimize:
            painter.drawLine(9, 20, 23, 20);
            break;

        case TitleButtonMaximize:
            painter.drawRect(10, 10, 12, 12);
            break;

        case TitleButtonRestore:
            painter.drawRect(12, 9, 10, 10);
            painter.drawRect(9, 13, 10, 10);
            break;

        case TitleButtonClose:
            painter.drawLine(10, 10, 22, 22);
            painter.drawLine(22, 10, 10, 22);
            break;
    }

    QIcon result;
    result.addPixmap(pixmap, QIcon::Normal, QIcon::Off);
    result.addPixmap(pixmap, QIcon::Active, QIcon::Off);

    return result;
}


#ifdef _WIN32
static bool containsGlobalPoint(QWidget* widget, const QPoint& point)
{
    return widget && widget->isVisible()
           && widget->rect().contains(widget->mapFromGlobal(point));
}


static const DWORD s_dwmWindowCornerPreference = 33;
static const DWORD s_dwmWindowCornerDefault    = 0;
static const DWORD s_dwmWindowCornerRound      = 2;


static int scaledWindowsMetric(HWND hwnd, int value)
{
    UINT dpi = 96;

    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) return value;

    typedef UINT(WINAPI * DpiForWindow)(HWND);
    DpiForWindow dpiForWindow = reinterpret_cast<DpiForWindow>(
      GetProcAddress(user32, "GetDpiForWindow"));

    if (dpiForWindow) dpi = dpiForWindow(hwnd);

    const int scaledValue = MulDiv(value, dpi, 96);

    return scaledValue > 0 ? scaledValue : value;
}


static void setWindowsCornerPreference(HWND hwnd, bool rounded)
{
    if (!hwnd) return;

    typedef HRESULT(
      WINAPI * DwmSetWindowAttributeFn)(HWND, DWORD, LPCVOID, DWORD);

    static bool                    initialized        = false;
    static DwmSetWindowAttributeFn setWindowAttribute = nullptr;

    if (!initialized) {
        initialized = true;

        HMODULE dwmapi = LoadLibraryW(L"dwmapi.dll");
        if (dwmapi) {
            setWindowAttribute = reinterpret_cast<DwmSetWindowAttributeFn>(
              GetProcAddress(dwmapi, "DwmSetWindowAttribute"));
        }
    }

    if (!setWindowAttribute) return;

    const DWORD preference
      = rounded ? s_dwmWindowCornerRound : s_dwmWindowCornerDefault;

    setWindowAttribute(
      hwnd,
      s_dwmWindowCornerPreference,
      &preference,
      sizeof(preference));
}
#endif


MainWindow::MainWindow(QWidget* parent)
  : QMainWindow(parent)
  , ui(new Ui::MainWindow)
  , m_openFileTabs(new QTabWidget(this))
  , m_windowTitleLabel(nullptr)
  , m_titleBar(nullptr)
  , m_minimizeButton(nullptr)
  , m_maximizeButton(nullptr)
  , m_closeButton(nullptr)
  , m_currentTheme(s_darkTheme)
  , m_currentOpenedFolder()
  , m_rgbPreviewMode(RGBFramebufferModel::Preview_Exposure)
  , m_splitterImageState()
  , m_splitterPropertiesState()
  , m_titleDragPosition()
  , m_titleBarDragging(false)
{
    ui->setupUi(this);
    setAttribute(Qt::WA_StyledBackground, true);
    setupTitleBar();
    setupPreviewModeActions();
    setupStereoActions();
    m_projectionMenu = ui->menu_Show->addMenu(tr("Projection"));
    m_projectionMenu->setObjectName("menu_Projection");
    m_projectionMenu->setToolTipsVisible(true);
    m_projectionActions = new QActionGroup(this);
    m_projectionActions->setExclusive(true);
    for (int i = EnvironmentProjection::LatLong; i <= EnvironmentProjection::Sphere; ++i) {
        auto* action = m_projectionMenu->addAction(EnvironmentProjection::name(EnvironmentProjection::Type(i)));
        action->setCheckable(true);
        action->setData(i);
        action->setObjectName(QString("action_Projection%1").arg(i));
        m_projectionActions->addAction(action);
    }
    connect(m_projectionActions, &QActionGroup::triggered, this, [this](QAction* action) {
        auto* document = currentFileWidget();
        auto* model = document ? const_cast<FramebufferModel*>(document->activeFramebufferModel()) : nullptr;
        if (!model) return;
        auto state = model->requestedProjectionState();
        state.type = EnvironmentProjection::Type(action->data().toInt());
        model->setProjectionState(state);
        updateShowActions();
    });
    m_projectionMenu->addSeparator();
    m_resetProjection = m_projectionMenu->addAction(tr("Reset View"));
    m_resetProjection->setObjectName("action_ResetProjectionView");
    connect(m_resetProjection, &QAction::triggered, this, [this] {
        auto* document = currentFileWidget();
        auto* model = document ? const_cast<FramebufferModel*>(document->activeFramebufferModel()) : nullptr;
        if (!model) return;
        model->resetProjectionView();
    });
    m_resolutionMenu = ui->menu_Show->addMenu(tr("Resolution"));
    m_resolutionMenu->setObjectName("menu_ResolutionLevel");
    m_resolutionActions = new QActionGroup(this);
    m_resolutionActions->setExclusive(true);
    connect(m_resolutionActions, &QActionGroup::triggered, this, [this](QAction* action) {
        const QPoint level = action->data().toPoint();
        if (auto* widget = currentFileWidget()) widget->setResolutionLevel({level.x(), level.y()});
        updateShowActions();
    });
    setupThemeActions();
    setAcceptDrops(true);

    m_openFileTabs->setMovable(true);
    m_openFileTabs->setTabsClosable(true);
    m_openFileTabs->setTabBarAutoHide(true);
    for (int direction : {-1, 1}) {
        auto* shortcut = new QShortcut(
          QKeySequence(direction == 1 ? "Ctrl+Tab" : "Ctrl+Shift+Tab"),
          this);
        connect(shortcut, &QShortcut::activated, this, [this, direction] {
            const int count = m_openFileTabs->count();
            if (count > 1)
                m_openFileTabs->setCurrentIndex(
                  (m_openFileTabs->currentIndex() + direction + count) % count);
        });
    }

    // clang-format off
    connect(m_openFileTabs, SIGNAL(currentChanged(int)),
            this,           SLOT(onCurrentChanged(int)));
    connect(m_openFileTabs, SIGNAL(tabCloseRequested(int)),
            this,           SLOT(onTabCloseRequested(int)));
    // clang-format on

    setupWorkspace();
    installEmptyOpenEventFilters();
    updateFileTabPresentation();

    readSettings();
#ifdef _WIN32
    applyWindowsWindowStyle();
#endif
    updateWindowFrame();
}


MainWindow::~MainWindow()
{
    m_minimalView = false;
    for (const auto& connection : m_minimalConnections) disconnect(connection);
    const QSignalBlocker blockTabs(m_openFileTabs);
    while (m_openFileTabs->count()) {
        QWidget* widget = m_openFileTabs->widget(0);
        disconnect(widget, nullptr, this, nullptr);
        m_openFileTabs->removeTab(0);
        delete widget;
    }
    delete ui;
}


void MainWindow::setupWorkspace()
{
    auto* toolbar = new QToolBar(tr("Workspace"), this);
    m_workspaceToolbar = toolbar;
    toolbar->setObjectName("workspaceToolbar");
    toolbar->setMovable(false);
    toolbar->setFloatable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    toolbar->setIconSize(QSize(18, 18));
    const auto decorate = [](QAction* action, ViewerIcons::Kind kind, const QString& name) {
        action->setIcon(ViewerIcons::icon(kind));
        action->setIconText(name);
        const QString shortcut = action->shortcut().toString(QKeySequence::NativeText);
        action->setToolTip(shortcut.isEmpty() ? name : name + " (" + shortcut + ")");
    };
    decorate(ui->action_Open, ViewerIcons::Open, tr("Open Image"));
    decorate(ui->action_Save, ViewerIcons::Export, tr("Export Image"));
    decorate(ui->action_ModeExposure, ViewerIcons::Exposure, tr("Exposure"));
    decorate(ui->action_ModeToneMapping, ViewerIcons::ToneMapping, tr("Tone Mapping"));
    decorate(ui->action_ModeFalseColor, ViewerIcons::FalseColor, tr("False Color"));
    decorate(ui->action_ShowLayers, ViewerIcons::Layers, tr("Layers"));
    decorate(ui->action_ShowAttributes, ViewerIcons::Attributes, tr("Attributes"));
    toolbar->addAction(ui->action_Open);
    toolbar->addAction(ui->action_Save);
    toolbar->addSeparator();
    toolbar->addAction(ui->action_ModeExposure);
    toolbar->addAction(ui->action_ModeToneMapping);
    toolbar->addAction(ui->action_ModeFalseColor);
    toolbar->addSeparator();
    toolbar->addAction(ui->action_ShowLayers);
    toolbar->addAction(ui->action_ShowAttributes);
    for (QAction* action : toolbar->actions()) {
        auto* button = qobject_cast<QToolButton*>(toolbar->widgetForAction(action));
        if (!button) continue;
        button->setFixedSize(32, 32);
        button->setAccessibleName(action->iconText());
    }
    addToolBar(Qt::TopToolBarArea, toolbar);
    animateToolbarButtons(toolbar);

    m_workspace = new QStackedWidget(this);
    m_workspace->setObjectName("workspace");
    m_welcomePage = createWelcomePage(ui->action_Open, m_workspace);
    m_workspace->addWidget(m_welcomePage);
    m_openFileTabs->setObjectName("fileTabs");
    m_openFileTabs->setDocumentMode(true);
    m_workspace->addWidget(m_openFileTabs);
    setCentralWidget(m_workspace);
    // Menu widgets are hidden in image-window mode; keep their shortcuts on the window.
    for (QAction* action : findChildren<QAction*>()) {
        if (!action->shortcut().isEmpty()) addAction(action);
    }
}


void MainWindow::setupTitleBar()
{
    setWindowFlags(windowFlags() | Qt::FramelessWindowHint);

    m_titleBar = new QWidget(this);
    m_titleBar->setObjectName("customTitleBar");
    m_titleBar->setAttribute(Qt::WA_StyledBackground, true);
    m_titleBar->setFixedHeight(36);

    QHBoxLayout* layout = new QHBoxLayout(m_titleBar);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    QMenuBar* menuBar = ui->menubar;
    menuBar->setParent(m_titleBar);
    menuBar->setNativeMenuBar(false);
    menuBar->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Expanding);

    m_windowTitleLabel = new QLabel("", m_titleBar);
    m_windowTitleLabel->setAlignment(Qt::AlignCenter);
    m_windowTitleLabel->setContentsMargins(12, 0, 12, 0);
    m_windowTitleLabel->setSizePolicy(
      QSizePolicy::Expanding,
      QSizePolicy::Expanding);

    m_minimizeButton = new QToolButton(m_titleBar);
    m_minimizeButton->setObjectName("titleMinimizeButton");
    m_minimizeButton->setAutoRaise(true);
    m_minimizeButton->setToolTip(tr("Minimize"));
    m_minimizeButton->setFixedSize(46, 36);

    m_maximizeButton = new QToolButton(m_titleBar);
    m_maximizeButton->setObjectName("titleMaximizeButton");
    m_maximizeButton->setAutoRaise(true);
    m_maximizeButton->setToolTip(tr("Maximize"));
    m_maximizeButton->setFixedSize(46, 36);

    m_closeButton = new QToolButton(m_titleBar);
    m_closeButton->setObjectName("titleCloseButton");
    m_closeButton->setAutoRaise(true);
    m_closeButton->setToolTip(tr("Close"));
    m_closeButton->setFixedSize(46, 36);
    for (QToolButton* button : {m_minimizeButton, m_maximizeButton, m_closeButton})
        button->setFocusPolicy(Qt::NoFocus);

    connect(
      m_minimizeButton,
      &QToolButton::clicked,
      this,
      &MainWindow::showMinimized);
    connect(
      m_maximizeButton,
      &QToolButton::clicked,
      this,
      &MainWindow::toggleMaximized);
    connect(m_closeButton, &QToolButton::clicked, this, &MainWindow::close);

    layout->addWidget(menuBar);
    layout->addWidget(m_windowTitleLabel);
    layout->addWidget(m_minimizeButton);
    layout->addWidget(m_maximizeButton);
    layout->addWidget(m_closeButton);

    m_titleBar->installEventFilter(this);
    m_windowTitleLabel->installEventFilter(this);

    updateTitleBarButtons();
    setMenuWidget(m_titleBar);
}


void MainWindow::setupThemeActions()
{
    QActionGroup* themeGroup = new QActionGroup(this);
    themeGroup->setExclusive(true);
    themeGroup->addAction(ui->action_ThemeLight);
    themeGroup->addAction(ui->action_ThemeDark);

    ui->action_ThemeLight->setCheckable(true);
    ui->action_ThemeDark->setCheckable(true);
    ui->action_ThemeDark->setChecked(true);
}


void MainWindow::setupPreviewModeActions()
{
    QActionGroup* modeGroup = new QActionGroup(this);
    modeGroup->setExclusive(true);
    modeGroup->addAction(ui->action_ModeExposure);
    modeGroup->addAction(ui->action_ModeToneMapping);
    modeGroup->addAction(ui->action_ModeFalseColor);

    ui->action_ModeExposure->setCheckable(true);
    ui->action_ModeToneMapping->setCheckable(true);
    ui->action_ModeFalseColor->setCheckable(true);
    ui->action_ModeExposure->setChecked(true);
}


void MainWindow::applyRgbPreviewMode(RGBFramebufferModel::PreviewMode mode)
{
    m_rgbPreviewMode = mode;

    ui->action_ModeExposure->blockSignals(true);
    ui->action_ModeToneMapping->blockSignals(true);
    ui->action_ModeFalseColor->blockSignals(true);

    ui->action_ModeExposure->setChecked(
      mode == RGBFramebufferModel::Preview_Exposure);
    ui->action_ModeToneMapping->setChecked(
      mode == RGBFramebufferModel::Preview_ToneMapping);
    ui->action_ModeFalseColor->setChecked(
      mode == RGBFramebufferModel::Preview_FalseColor);

    ui->action_ModeExposure->blockSignals(false);
    ui->action_ModeToneMapping->blockSignals(false);
    ui->action_ModeFalseColor->blockSignals(false);

    for (int i = 0; i < m_openFileTabs->count(); i++) {
        ImageFileWidget* widget
          = qobject_cast<ImageFileWidget*>(m_openFileTabs->widget(i));

        if (widget) widget->setRgbPreviewMode(mode);
    }
}

void MainWindow::setupStereoActions()
{
    auto* menu = ui->menu_Show->addMenu(tr("Stereo"));
    menu->setObjectName("menu_Stereo");
    menu->setToolTipsVisible(true);
    m_stereoActions = new QActionGroup(this);
    m_stereoActions->setExclusive(true);
    const QStringList labels = {tr("Default"), tr("Left Eye"), tr("Right Eye"), tr("Anaglyph 3D")};
    const QStringList names = {"Default", "Left", "Right", "Anaglyph"};
    for (int i = 0; i < labels.size(); ++i) {
        auto* action = menu->addAction(labels[i]);
        action->setObjectName("action_Stereo" + names[i]);
        action->setCheckable(true);
        action->setEnabled(false);
        action->setData(i);
        action->setChecked(i == 0);
        m_stereoActions->addAction(action);
    }
    connect(m_stereoActions, &QActionGroup::triggered, this, [this](QAction* action) {
        if (auto* document = currentFileWidget())
            document->setStereoMode(static_cast<ImageFileWidget::StereoMode>(action->data().toInt()));
        updateShowActions();
    });
}


QString MainWindow::normalizedThemeName(const QString& themeName) const
{
    if (themeName == s_lightTheme) return s_lightTheme;

    return s_darkTheme;
}


QString MainWindow::themeStyleSheetPath(const QString& themeName) const
{
    if (themeName == s_lightTheme) return ":/light_flat/theme.css";

    return ":/dark_flat/theme.css";
}


void MainWindow::applyTheme(const QString& themeName)
{
    const QString normalizedTheme = normalizedThemeName(themeName);
    QFile         stylesheet(themeStyleSheetPath(normalizedTheme));

    if (!stylesheet.open(QFile::ReadOnly | QFile::Text)) {
        qWarning() << "Unable to set stylesheet, file not found";
        return;
    }

    QTextStream stream(&stylesheet);
    const bool light = normalizedTheme == s_lightTheme;
    QPalette palette = qApp->palette();
    palette.setColor(QPalette::Window, QColor(light ? "#f2f3f5" : "#202226"));
    palette.setColor(QPalette::Base, QColor(light ? "#ffffff" : "#26282d"));
    palette.setColor(QPalette::AlternateBase, QColor(light ? "#f7f8fa" : "#2a2d32"));
    palette.setColor(QPalette::Button, QColor(light ? "#ffffff" : "#2b2e34"));
    palette.setColor(QPalette::WindowText, QColor(light ? "#272b32" : "#e2e5ea"));
    palette.setColor(QPalette::Text, palette.color(QPalette::WindowText));
    palette.setColor(QPalette::ButtonText, palette.color(QPalette::WindowText));
    palette.setColor(QPalette::Highlight, QColor(light ? "#507aaa" : "#8baed6"));
    palette.setColor(QPalette::HighlightedText, QColor(light ? "#ffffff" : "#15191f"));
    for (QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text,
                                   QPalette::ButtonText})
        palette.setColor(QPalette::Disabled, role, QColor(light ? "#989da6" : "#747b85"));
    qApp->setPalette(palette);
    qApp->setStyleSheet(stream.readAll());
    for (QMdiArea* area : findChildren<QMdiArea*>())
        area->setBackground(palette.brush(QPalette::Window));

    m_currentTheme = normalizedTheme;

    ui->action_ThemeLight->blockSignals(true);
    ui->action_ThemeDark->blockSignals(true);

    ui->action_ThemeLight->setChecked(m_currentTheme == s_lightTheme);
    ui->action_ThemeDark->setChecked(m_currentTheme == s_darkTheme);

    ui->action_ThemeLight->blockSignals(false);
    ui->action_ThemeDark->blockSignals(false);

    updateTitleBarButtons();
    updateWindowFrame();
}


void MainWindow::updateTitleBarButtons()
{
    if (!m_maximizeButton) return;

    const QColor iconColor = m_currentTheme == s_lightTheme
                               ? QColor(32, 35, 40)
                               : QColor(214, 214, 214);

    const TitleButtonIcon maximizeIcon
      = isMaximized() ? TitleButtonRestore : TitleButtonMaximize;

    m_minimizeButton->setIcon(titleButtonIcon(TitleButtonMinimize, iconColor));
    m_maximizeButton->setIcon(titleButtonIcon(maximizeIcon, iconColor));
    m_closeButton->setIcon(titleButtonIcon(TitleButtonClose, iconColor));
    m_maximizeButton->setToolTip(
      isMaximized() ? tr("Restore") : tr("Maximize"));
}


void MainWindow::updateWindowFrame()
{
    const bool     frameActive    = !m_minimalView && !isMaximized() && !isFullScreen();
    const QVariant oldFrameActive = property("windowFrameActive");

    setContentsMargins(0, 0, 0, 0);

    if (!oldFrameActive.isValid() || oldFrameActive.toBool() != frameActive) {
        setProperty("windowFrameActive", frameActive);
        style()->unpolish(this);
        style()->polish(this);
    }

#ifdef _WIN32
    setWindowsCornerPreference(reinterpret_cast<HWND>(winId()), frameActive);
#endif
}


#ifdef _WIN32
void MainWindow::applyWindowsWindowStyle()
{
    HWND hwnd = reinterpret_cast<HWND>(winId());
    if (!hwnd) return;

    const LONG_PTR oldStyle = GetWindowLongPtr(hwnd, GWL_STYLE);
    const LONG_PTR style
      = oldStyle | WS_THICKFRAME | WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_SYSMENU;

    if (style != oldStyle) SetWindowLongPtr(hwnd, GWL_STYLE, style);

    SetWindowPos(
      hwnd,
      nullptr,
      0,
      0,
      0,
      0,
      SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER
        | SWP_NOOWNERZORDER | SWP_NOACTIVATE);
}
#endif


bool MainWindow::isTitleBarDragArea(const QPoint& pos) const
{
    if (m_minimalView) return false;
    if (!m_titleBar || !m_titleBar->geometry().contains(pos)) return false;

    QWidget* child = childAt(pos);

    return child == m_titleBar || child == m_windowTitleLabel
           || (child && m_windowTitleLabel->isAncestorOf(child));
}


ImageFileWidget* MainWindow::currentFileWidget() const
{
    return qobject_cast<ImageFileWidget*>(m_openFileTabs->currentWidget());
}


void MainWindow::copyActiveImage(bool fullResolution)
{
    ImageFileWidget*        widget = currentFileWidget();
    const FramebufferModel* model
      = widget ? widget->activeFramebufferModel() : nullptr;

    if (!model || !model->isFullPreviewReady()) return;

    const QImage image = PreviewImage::render(*model, fullResolution ? 0 : s_clipboardMaxWidth);
    if (image.isNull()) {
        QMessageBox::warning(this, tr("Copy Preview"),
                            tr("Unable to allocate the preview image. Try a smaller output size."));
        return;
    }
    QApplication::clipboard()->setImage(image);
}


void MainWindow::applyPanelVisibility(ImageFileWidget* widget) const
{
    if (!widget) return;

    widget->setAttributesVisible(ui->action_ShowAttributes->isChecked());
    widget->setLayersVisible(ui->action_ShowLayers->isChecked());
}


void MainWindow::applyPanelVisibilityToAllTabs() const
{
    for (int i = 0; i < m_openFileTabs->count(); i++) {
        ImageFileWidget* widget
          = qobject_cast<ImageFileWidget*>(m_openFileTabs->widget(i));

        applyPanelVisibility(widget);
    }
}


void MainWindow::updateShowActions()
{
    if (m_minimalView && !m_switchingMinimalView) {
        const auto* currentModel = currentFileWidget()
                                     ? currentFileWidget()->activeFramebufferModel() : nullptr;
        if (!m_minimalModel || (currentModel && currentModel != m_minimalModel.data()))
            leaveMinimalView();
        else
            updateMinimalSummary();
    }
    ImageFileWidget*        widget = currentFileWidget();
    const FramebufferModel* model
      = widget ? widget->activeFramebufferModel() : nullptr;
    const bool copyEnabled = model && model->isFullPreviewReady();

    ui->action_Save->setEnabled(model && !model->getLoadedImage().isNull());
    ui->action_CopyImage->setEnabled(copyEnabled);
    ui->action_CopyImageFullResolution->setEnabled(copyEnabled);
    if (m_projectionMenu) {
        const bool enabled = model && model->isImageLoaded() && model->environmentSource().available();
        m_projectionMenu->setEnabled(enabled);
        m_projectionMenu->setToolTip(model ? model->environmentSource().unavailableReason : tr("No image."));
        for (auto* action : m_projectionActions->actions())
            action->setChecked(model && model->rawEnvmap() >= 0 && action->data().toInt() == model->projectionState().type);
    }
    if (m_resolutionMenu) {
        const auto levels = widget ? widget->resolutionLevels() : std::vector<ResolutionLevel>();
        const bool ripmap = widget && widget->hasRipmapLevels();
        // Keep actions alive during asynchronous status updates and rapid selection.
        if (levels != m_menuResolutionLevels || ripmap != m_menuRipmap) {
            qDeleteAll(m_resolutionActions->actions());
            qDeleteAll(m_resolutionMenu->findChildren<QMenu*>(QString(), Qt::FindDirectChildrenOnly));
            m_resolutionMenu->clear();
            QMenu* submenu = m_resolutionMenu;
            int previousX = -1;
            for (const auto level : levels) {
                if (ripmap && level.x != previousX) {
                    submenu = m_resolutionMenu->addMenu(tr("X level %1").arg(level.x));
                    submenu->setObjectName(QString("menu_ResolutionX%1").arg(level.x));
                    previousX = level.x;
                }
                auto* action = submenu->addAction(QString());
                action->setCheckable(true);
                action->setData(QPoint(level.x, level.y));
                action->setObjectName(QString("action_ResolutionLevel%1_%2").arg(level.x).arg(level.y));
                m_resolutionActions->addAction(action);
            }
            m_menuResolutionLevels = levels;
            m_menuRipmap = ripmap;
        }
        for (auto* action : m_resolutionActions->actions()) {
            const QPoint point = action->data().toPoint();
            const ResolutionLevel level(point.x(), point.y());
            action->setText(widget->resolutionLevelLabel(level));
            action->setChecked(level == widget->resolutionLevel());
        }
        m_resolutionMenu->setEnabled(widget && widget->isDocumentReady() && levels.size() > 1);
    }
    if (m_stereoActions) {
        for (auto* action : m_stereoActions->actions()) {
            const auto mode = static_cast<ImageFileWidget::StereoMode>(action->data().toInt());
            const QString reason = widget ? widget->stereoUnavailableReason(mode) : tr("No ready document.");
            action->setEnabled(reason.isEmpty());
            action->setToolTip(reason.isEmpty() ? action->text() : reason);
            action->setChecked(mode == (widget ? widget->stereoMode() : ImageFileWidget::StereoDefault));
        }
    }
}


void MainWindow::updateFileTabPresentation()
{
    const int index = m_openFileTabs->currentIndex();
    m_workspace->setCurrentWidget(index < 0 ? m_welcomePage : m_openFileTabs);
    QString   title;

    if (index >= 0) {
        title = m_openFileTabs->tabText(index).trimmed();
    }

    ImageFileWidget* widget = currentFileWidget();
    const QString layer = widget ? widget->activeLayerTitleText() : QString();
    QString       displayTitle = title;

    if (!displayTitle.isEmpty() && !layer.isEmpty()) {
        displayTitle += " (" + layer + ")";
    }

    if (m_windowTitleLabel) {
        m_windowTitleLabel->setText(displayTitle);
        m_windowTitleLabel->setToolTip(displayTitle);
    }

    setWindowTitle(
      displayTitle.isEmpty() ? tr("OpenEXR Viewer") : displayTitle);
}


void MainWindow::installEmptyOpenEventFilters()
{
    m_welcomePage->installEventFilter(this);
    for (QWidget* widget : m_welcomePage->findChildren<QWidget*>()) {
        if (!qobject_cast<QPushButton*>(widget)) widget->installEventFilter(this);
    }
    m_openFileTabs->installEventFilter(this);

    for (QWidget* widget : m_openFileTabs->findChildren<QWidget*>(
           QString(),
           Qt::FindDirectChildrenOnly)) {
        widget->installEventFilter(this);
    }
}


bool MainWindow::handleEmptyOpenClick(QObject* watched, QEvent* event)
{
    if (m_openFileTabs->count() != 0) return false;
    if (event->type() != QEvent::MouseButtonPress) return false;

    QWidget* watchedWidget = qobject_cast<QWidget*>(watched);
    if (!watchedWidget) return false;
    if (
      watchedWidget != m_openFileTabs
      && !m_openFileTabs->isAncestorOf(watchedWidget)
      && watchedWidget != m_welcomePage
      && !m_welcomePage->isAncestorOf(watchedWidget)) {
        return false;
    }
    if (qobject_cast<QTabBar*>(watchedWidget)) return false;

    QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
    if (mouseEvent->button() != Qt::LeftButton) return false;
    if (mouseEvent->modifiers() != Qt::NoModifier) return false;

    on_action_Open_triggered();
    return true;
}


void MainWindow::addFileTab(ImageFileWidget* fileWidget, const QString& title)
{
    connect(fileWidget, &ImageFileWidget::minimalViewRequested,
            this, &MainWindow::toggleMinimalView);
    applyPanelVisibility(fileWidget);
    fileWidget->setRgbPreviewMode(m_rgbPreviewMode);
    connect(
      fileWidget,
      &ImageFileWidget::openFileOnDropEvent,
      this,
      static_cast<void (MainWindow::*)(const QString&)>(&MainWindow::open));
    connect(
      fileWidget,
      &ImageFileWidget::activeFramebufferChanged,
      this,
      &MainWindow::updateShowActions);
    connect(
      fileWidget,
      &ImageFileWidget::activeFramebufferChanged,
      this,
      &MainWindow::updateFileTabPresentation);
    connect(
      fileWidget,
      &ImageFileWidget::refreshInProgressChanged,
      this,
      [this, fileWidget](bool refreshing) {
          if (currentFileWidget() == fileWidget)
              ui->action_Refresh->setEnabled(
                !refreshing && !fileWidget->isStream());
      });
    const int index = m_openFileTabs->addTab(fileWidget, title);
    m_openFileTabs->setTabToolTip(
      index,
      fileWidget->isStream() ? tr("Stream") : fileWidget->getOpenedFilename());
    m_openFileTabs->setCurrentWidget(fileWidget);
    updateShowActions();
    updateFileTabPresentation();
}

void MainWindow::queueFileTab(
  ImageFileWidget* fileWidget, const QString& title)
{
    if (!fileWidget->sourceImage() || fileWidget->hasDocumentLoadFailed()) {
        delete fileWidget;
        return;
    }

    fileWidget->setSplitterImageState(m_splitterImageState);
    fileWidget->setSplitterPropertiesState(m_splitterPropertiesState);
    fileWidget->setRgbPreviewMode(m_rgbPreviewMode);
    applyPanelVisibility(fileWidget);
    fileWidget->hide();
    PendingOpen pending;
    pending.widget = fileWidget;
    pending.title  = title;
    m_pendingOpens.append(pending);
    connect(
      fileWidget,
      &ImageFileWidget::documentReady,
      this,
      [this, fileWidget] {
          resolvePendingOpen(fileWidget, true);
      });
    connect(
      fileWidget,
      &ImageFileWidget::documentLoadFailed,
      this,
      [this, fileWidget](const QString&) {
          resolvePendingOpen(fileWidget, false);
      });
    if (fileWidget->isDocumentReady())
        resolvePendingOpen(fileWidget, true);
}

QRect MainWindow::minimalScreenGeometry(const QPoint& center) const
{
    for (QScreen* screen : QGuiApplication::screens()) {
        if (screen->geometry().contains(center)) return screen->availableGeometry();
    }
    QScreen* screen = windowHandle() ? windowHandle()->screen() : QGuiApplication::primaryScreen();
    return screen ? screen->availableGeometry() : QRect(0, 0, 1024, 768);
}

void MainWindow::toggleMinimalView()
{
    if (m_switchingMinimalView) return;
    if (m_minimalView) {
        leaveMinimalView();
        return;
    }
    ImageFileWidget* document = currentFileWidget();
    const FramebufferModel* model = document ? document->activeFramebufferModel() : nullptr;
    GraphicsView* view = document ? document->activeGraphicsView() : nullptr;
    if (!model || !view || !model->isImageLoaded() || model->getLoadedImage().isNull()
        || model->width() <= 0 || model->height() <= 0
        || !std::isfinite(model->pixelAspectRatio()) || model->pixelAspectRatio() <= 0.)
        return;

    QScopedValueRollback<bool> switching(m_switchingMinimalView, true);
    const QPoint center = frameGeometry().center();
    m_completeGeometry = saveGeometry();
    m_completeMinimumSize = minimumSize();
    m_completeMaximumSize = maximumSize();
    m_completeToolbarVisible = m_workspaceToolbar->isVisible();
    m_completeTitleVisible = m_titleBar->isVisible();
    m_completeView = view;
    m_completeViewState = view->viewState();
    m_minimalPreview = document->activePreviewWidget();
    m_minimalModel = model;
    m_minimalView = true;

    // Keep the original workspace alive, outside the central layout's size constraints.
    QWidget* complete = takeCentralWidget();
    complete->setParent(this);
    complete->hide();
    m_workspaceToolbar->hide();
    m_titleBar->hide();
    m_minimalPage = new MinimalImageWidget(this);
    m_minimalPage->setSummary(QString(), model);
    setCentralWidget(m_minimalPage);
    setMinimumSize(1, m_minimalPage->footerHeight() + 1);
    setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    if (isMaximized() || isFullScreen()) showNormal();
    updateWindowFrame();
    m_minimalPage->view()->setModel(model);

    connect(m_minimalPage->view(), &GraphicsView::minimalViewRequested,
            this, &MainWindow::toggleMinimalView);
    connect(m_minimalPage->view(), &GraphicsView::imageWindowZoomRequested,
            this, &MainWindow::resizeMinimalView);
    connect(m_minimalPage->view(), &GraphicsView::imageWindowMoveRequested,
            this, &MainWindow::moveMinimalView);
    connect(m_minimalPage->view(), &GraphicsView::resetParametersRequested,
            this, &MainWindow::resetMinimalParameters);
    connect(m_minimalPage->view(), &GraphicsView::controlWheel,
            this, &MainWindow::adjustMinimalParameter);
    connect(m_minimalPage->view(), &GraphicsView::openFileOnDropEvent,
            this, static_cast<void (MainWindow::*)(const QString&)>(&MainWindow::open));
    m_minimalConnections.append(connect(model, &QObject::destroyed, this, [this] {
        leaveMinimalView();
    }));
    m_minimalConnections.append(connect(model, &FramebufferModel::readinessChanged,
                                        this, [this] {
        if (!m_minimalModel || !m_minimalModel->isImageLoaded()) leaveMinimalView();
        else updateMinimalSummary();
    }));
    const auto projectionCanvasSize = std::make_shared<QSize>(PreviewImage::Geometry(*model).outputSize());
    const auto projectionFooterHeight = std::make_shared<int>(m_minimalPage->footerHeight());
    m_minimalConnections.append(connect(model, &FramebufferModel::imageChanged, this,
      [this, projectionCanvasSize, projectionFooterHeight] {
          if (!m_minimalView || !m_minimalModel) return;
          updateMinimalSummary();
          const auto next = PreviewImage::Geometry(*m_minimalModel).outputSize();
          const int footer = m_minimalPage->footerHeight();
          if (next != *projectionCanvasSize || footer != *projectionFooterHeight) {
              *projectionCanvasSize = next; *projectionFooterHeight = footer;
              resizeMinimalView(m_minimalPage->view()->viewState().zoom);
          }
      }));
    m_minimalConnections.append(connect(model, &FramebufferModel::imageLoaded,
                                        this, [this] {
        if (m_minimalView) resizeMinimalView(m_minimalPage->view()->viewState().zoom);
    }));
    move(center - QPoint(width() / 2, height() / 2));
    resizeMinimalView(m_completeViewState.zoom);
    m_minimalPage->view()->setFocus(Qt::OtherFocusReason);
}

void MainWindow::leaveMinimalView()
{
    if (!m_minimalView || m_switchingMinimalView) return;
    QScopedValueRollback<bool> switching(m_switchingMinimalView, true);
    m_minimalView = false;
    for (const auto& connection : m_minimalConnections) disconnect(connection);
    m_minimalConnections.clear();
    m_minimalPage->view()->setModel(nullptr);
    QWidget* page = takeCentralWidget();
    page->setParent(this);
    page->hide();
    page->deleteLater();
    m_minimalPage = nullptr;
    m_minimalModel.clear();
    m_minimalPreview.clear();
    setCentralWidget(m_workspace);
    m_workspace->show();
    m_titleBar->setVisible(m_completeTitleVisible);
    m_workspaceToolbar->setVisible(m_completeToolbarVisible);
    setMinimumSize(m_completeMinimumSize);
    setMaximumSize(m_completeMaximumSize);
    restoreGeometry(m_completeGeometry);
    updateWindowFrame();
    layout()->activate();
    if (m_completeView) {
        m_completeView->restoreViewState(m_completeViewState);
        m_completeView->setFocus(Qt::OtherFocusReason);
    }
    m_completeView.clear();
}

void MainWindow::resizeMinimalView(double zoom)
{
    if (!m_minimalView || !m_minimalModel || m_resizingMinimalView || !std::isfinite(zoom))
        return;
    QScopedValueRollback<bool> resizing(m_resizingMinimalView, true);
    const QPoint center = frameGeometry().center();
    const QRect available = minimalScreenGeometry(center);
    const int footer = m_minimalPage->footerHeight();
    const QRectF canvas = PreviewImage::Geometry(*m_minimalModel).sceneWindow();
    const double imageWidth = canvas.width();
    const double imageHeight = canvas.height();
    if (imageWidth <= 0. || imageHeight <= 0. || !std::isfinite(imageWidth)) return;
    const double maximum = qMin(available.width() / imageWidth,
                                qMax(1, available.height() - footer) / imageHeight);
    if (maximum <= 0.) return;
    zoom = zoom <= 0. ? maximum : qBound(qMin(0.01, maximum), zoom, maximum);
    const QSize size(qBound(1, int(std::ceil(imageWidth * zoom)), available.width()),
                     qBound(1, int(std::ceil(imageHeight * zoom)),
                            qMax(1, available.height() - footer)) + footer);
    QPoint position = center - QPoint(size.width() / 2, size.height() / 2);
    position.setX(qBound(available.left(), position.x(), available.right() - size.width() + 1));
    position.setY(qBound(available.top(), position.y(), available.bottom() - size.height() + 1));
    setGeometry(QRect(position, size));
    layout()->activate();
    m_minimalPage->layout()->activate();
    m_minimalPage->view()->applyImageWindowZoom(zoom);
    updateMinimalSummary();
}

void MainWindow::moveMinimalView(const QPoint& position)
{
    if (!m_minimalView) return;
    // Moving first allows the destination monitor to determine the new zoom limit.
    move(position);
    resizeMinimalView(m_minimalPage->view()->viewState().zoom);
}

void MainWindow::updateMinimalSummary()
{
    if (!m_minimalView || !m_minimalModel || !m_minimalPage) return;
    QString parameter;
    if (auto* rgb = qobject_cast<RGBFramebufferWidget*>(m_minimalPreview.data()))
        parameter = rgb->currentParameterText();
    else if (auto* scalar = qobject_cast<YFramebufferWidget*>(m_minimalPreview.data()))
        parameter = scalar->currentParameterText();
    const QString file = m_openFileTabs->tabText(m_openFileTabs->currentIndex());
    const auto* document = currentFileWidget();
    const QString layer = document && document->sourceImage()
                            && (document->sourceImage()->getLayerModel()->hasViews() || m_minimalModel->resolutionLevelCount() > 1)
                            ? document->activeLayerTitleText() : QString();
    if (m_minimalModel->rawEnvmap() >= 0) {
        parameter += tr(" | %1 → %2")
          .arg(EnvironmentProjection::name(EnvironmentProjection::Type(m_minimalModel->rawEnvmap())))
          .arg(EnvironmentProjection::name(m_minimalModel->projectionState().type));
        if (!m_minimalModel->environmentSource().available())
            parameter += " | " + m_minimalModel->environmentSource().unavailableReason;
    }
    const QSize displaySize = m_minimalModel->isProjected() ? m_minimalModel->projectionCanvasSize()
      : m_minimalModel->previewDisplayWindow().size();
    const QString detail = tr("%1 | %2×%3 | %4% | %5")
      .arg(layer.isEmpty() ? file : file + " | " + layer)
      .arg(displaySize.width()).arg(displaySize.height())
      .arg(m_minimalPage->view()->viewState().zoom * 100., 0, 'f', 1).arg(parameter);
    m_minimalPage->setSummary(framebufferSummaryText(m_minimalModel), m_minimalModel,
      detail + "\n" + framebufferSummaryToolTip(m_minimalModel));
}

void MainWindow::resetMinimalParameters()
{
    if (auto* rgb = qobject_cast<RGBFramebufferWidget*>(m_minimalPreview.data()))
        rgb->resetCurrentMode();
    else if (auto* scalar = qobject_cast<YFramebufferWidget*>(m_minimalPreview.data()))
        scalar->resetCurrentMode();
    resizeMinimalView(1.);
}

void MainWindow::adjustMinimalParameter(double steps)
{
    if (auto* rgb = qobject_cast<RGBFramebufferWidget*>(m_minimalPreview.data()))
        rgb->onControlWheel(steps);
    updateMinimalSummary();
}


void MainWindow::resolvePendingOpen(
  ImageFileWidget* fileWidget, bool succeeded)
{
    for (PendingOpen& pending : m_pendingOpens) {
        if (pending.widget != fileWidget || pending.resolved) continue;
        pending.resolved  = true;
        pending.succeeded = succeeded;
        break;
    }
    flushPendingOpens();
}


void MainWindow::flushPendingOpens()
{
    while (!m_pendingOpens.isEmpty() && m_pendingOpens.front().resolved) {
        const PendingOpen pending = m_pendingOpens.takeFirst();
        if (pending.succeeded && pending.widget)
            addFileTab(pending.widget, pending.title);
        else if (pending.widget)
            pending.widget->deleteLater();
    }
}


void MainWindow::open(std::istream& stream)
{
    leaveMinimalView();
    queueFileTab(
      new ImageFileWidget(stream, m_openFileTabs),
      tr("Stream"));
}

void MainWindow::open(const QString& filename)
{
    leaveMinimalView();
    const QFileInfo info(filename);
    queueFileTab(
      new ImageFileWidget(info.absoluteFilePath(), m_openFileTabs),
      info.fileName());
}

void MainWindow::on_action_Open_triggered()
{
    const QStringList filenames = QFileDialog::getOpenFileNames(
      this,
      tr("Open OpenEXR Images"),
      m_currentOpenedFolder,
      tr("Images (*.exr *.EXR)"));
    for (const QString& filename : filenames)
        open(filename);
}

void MainWindow::on_action_Save_triggered()
{
    ImageFileWidget*        widget = currentFileWidget();
    const FramebufferModel* model
      = widget ? widget->activeFramebufferModel() : nullptr;

    if (!model || !model->isImageLoaded()) return;

    QString folder = m_currentOpenedFolder.isEmpty() ? QDir::homePath()
                                                     : m_currentOpenedFolder;
    QString base   = "image";

    if (widget && !widget->isStream()) {
        QFileInfo info(widget->getOpenedFilename());
        if (!info.completeBaseName().isEmpty()) base = info.completeBaseName();
        if (!info.absolutePath().isEmpty()) folder = info.absolutePath();
    }

    SaveImageDialog dialog(QDir(folder).filePath(base + ".png"), this);

    const QPointer<const FramebufferModel> guardedModel(model);
    const QPointer<OpenEXRImage> guardedImage(widget ? widget->sourceImage() : nullptr);
    const ResolutionLevel savedLevel = widget ? widget->resolutionLevel() : ResolutionLevel();
    bool multilevel = false;
    bool deepSource = false;
    if (guardedImage) {
        for (int part = 0; part < guardedImage->getEXR().parts(); ++part) {
            const auto& header = guardedImage->getEXR().header(part);
            deepSource |= header.hasType() && header.type() == "deepscanline";
            if (header.hasTileDescription() && header.tileDescription().mode != Imf::ONE_LEVEL)
                multilevel = true; // Also warn when another part restricts the document to level 0.
        }
    }
    const auto environment = std::make_shared<EnvironmentProjection::Snapshot>(model->projectionSnapshot());
    const auto preview = std::make_shared<PreviewImage::Snapshot>(PreviewImage::capture(*model));
    dialog.setEnvironmentSource(*environment);
    dialog.setResolutionLevelInfo(savedLevel, multilevel);
    dialog.setDeepSourceInfo(model && bool(model->deepSamples()), deepSource);
    const auto updateSource = [&dialog, guardedModel, guardedImage, preview, environment] {
        if (guardedModel && guardedModel->isFullPreviewReady() && preview->image.isNull()) {
            *preview = PreviewImage::capture(*guardedModel);
            *environment = guardedModel->projectionSnapshot();
            dialog.setEnvironmentSource(*environment);
        }
        dialog.setSourceState(
          guardedModel && guardedImage && guardedModel->isImageLoaded(),
          guardedModel && !preview->image.isNull(),
          guardedModel ? guardedModel->errorString() : QString(),
          guardedModel && guardedModel->isDerivedPreview());
    };
    updateSource();
    connect(model, &FramebufferModel::readinessChanged, &dialog, updateSource);
    connect(model, &QObject::destroyed, &dialog, [&dialog] {
        dialog.setSourceState(false, false);
    });
    if (guardedImage) {
        connect(guardedImage.data(), &QObject::destroyed, &dialog, [&dialog] {
            dialog.setSourceState(false, false);
        });
    }

    connect(
      &dialog,
      &SaveImageDialog::saveRequested,
      &dialog,
      [this, &dialog, guardedModel, guardedImage, updateSource, savedLevel, environment, preview]() {
          updateSource();
          if (!guardedModel || !guardedImage || !guardedModel->isImageLoaded()) return;
          ImageSave::Source source;
          source.activeModel = guardedModel.data();
          source.sourceImage = guardedImage.data();
          source.resolutionLevel = savedLevel;
          source.environment = environment;
          source.preview = preview;
          ImageSave::Options options    = dialog.options();
          if (options.target == ImageSave::TargetPreview && preview->image.isNull())
              return;
          ImageSave::Result  saveResult = ImageSave::save(source, options);

          if (saveResult.status == ImageSave::StatusConflict) {
              options.conflict = conflictChoice(this, saveResult.paths);
              updateSource();
              if (!guardedModel || !guardedImage || !guardedModel->isImageLoaded()) return;
              if (options.target == ImageSave::TargetPreview && preview->image.isNull())
                  return;
              saveResult       = ImageSave::save(source, options);
          }

          const bool saved = saveResult.status == ImageSave::StatusSaved;
          dialog.setStatus(saveResult.message, !saved);

          if (saved && !saveResult.paths.isEmpty()) {
              m_currentOpenedFolder
                = QFileInfo(saveResult.paths.front()).absolutePath();
          }
      });

    dialog.exec();
}


void MainWindow::on_action_Quit_triggered()
{
    close();
}


void MainWindow::on_action_CopyImage_triggered()
{
    copyActiveImage(false);
}


void MainWindow::on_action_CopyImageFullResolution_triggered()
{
    copyActiveImage(true);
}


void MainWindow::closeEvent(QCloseEvent* event)
{
    leaveMinimalView();
    writeSettings();
    event->accept();
}


void MainWindow::changeEvent(QEvent* event)
{
    QMainWindow::changeEvent(event);

    if (event->type() == QEvent::WindowStateChange) {
        if (m_minimalView && !m_switchingMinimalView && !isMinimized()) {
            if (isMaximized() || isFullScreen()) showNormal();
            resizeMinimalView(m_minimalPage->view()->viewState().zoom);
        }
        updateTitleBarButtons();
        updateWindowFrame();
    }
}


void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    updateWindowFrame();
}

void MainWindow::moveEvent(QMoveEvent* event)
{
    QMainWindow::moveEvent(event);
    if (m_minimalView && !m_switchingMinimalView && !m_resizingMinimalView)
        resizeMinimalView(m_minimalPage->view()->viewState().zoom);
}


bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (handleEmptyOpenClick(watched, event)) return true;

    const bool titleBarTarget
      = watched == m_titleBar || watched == m_windowTitleLabel;

    if (!titleBarTarget) return QMainWindow::eventFilter(watched, event);

    if (event->type() == QEvent::MouseButtonDblClick) {
        QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);

        if (mouseEvent->button() == Qt::LeftButton) {
            toggleMaximized();
            return true;
        }
    }

    if (event->type() == QEvent::MouseButtonPress) {
        QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);

        if (mouseEvent->button() == Qt::LeftButton) {
            m_titleBarDragging = true;
            m_titleDragPosition
              = mouseGlobalPosition(mouseEvent) - frameGeometry().topLeft();
            return true;
        }
    }

    if (event->type() == QEvent::MouseMove && m_titleBarDragging) {
        QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);

        if (!isMaximized()) {
            move(mouseGlobalPosition(mouseEvent) - m_titleDragPosition);
        }

        return true;
    }

    if (event->type() == QEvent::MouseButtonRelease) {
        QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);

        if (mouseEvent->button() == Qt::LeftButton) {
            m_titleBarDragging = false;
            return true;
        }
    }

    return QMainWindow::eventFilter(watched, event);
}


#ifdef _WIN32
#    if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
bool MainWindow::nativeEvent(
  const QByteArray& eventType, void* message, qintptr* result)
#    else
bool MainWindow::nativeEvent(
  const QByteArray& eventType, void* message, long* result)
#    endif
{
    MSG* msg = static_cast<MSG*>(message);

    if (m_minimalView && msg->message == WM_NCHITTEST) {
        *result = HTCLIENT;
        return true;
    }

    if (msg->message == WM_NCCALCSIZE) {
        *result = 0;
        return true;
    }

    if (msg->message != WM_NCHITTEST) {
        return QMainWindow::nativeEvent(eventType, message, result);
    }

    RECT windowRect;
    if (!GetWindowRect(msg->hwnd, &windowRect)) {
        return QMainWindow::nativeEvent(eventType, message, result);
    }

    const LONG x = GET_X_LPARAM(msg->lParam);
    const LONG y = GET_Y_LPARAM(msg->lParam);

    const bool insideWindow = x >= windowRect.left && x < windowRect.right
                              && y >= windowRect.top && y < windowRect.bottom;

    if (!insideWindow) {
        return QMainWindow::nativeEvent(eventType, message, result);
    }

    const QPoint globalPos(x, y);
    const bool   onTitleButton
      = containsGlobalPoint(m_minimizeButton, globalPos)
        || containsGlobalPoint(m_maximizeButton, globalPos)
        || containsGlobalPoint(m_closeButton, globalPos);

    if (onTitleButton) {
        *result = HTCLIENT;
        return true;
    }

    const int resizeBorder
      = scaledWindowsMetric(msg->hwnd, s_windowResizeBorder);
    const bool onLeft   = x < windowRect.left + resizeBorder;
    const bool onRight  = x >= windowRect.right - resizeBorder;
    const bool onTop    = y < windowRect.top + resizeBorder;
    const bool onBottom = y >= windowRect.bottom - resizeBorder;

    if (!isMaximized() && onTop && onLeft) {
        *result = HTTOPLEFT;
        return true;
    }

    if (!isMaximized() && onTop && onRight) {
        *result = HTTOPRIGHT;
        return true;
    }

    if (!isMaximized() && onBottom && onLeft) {
        *result = HTBOTTOMLEFT;
        return true;
    }

    if (!isMaximized() && onBottom && onRight) {
        *result = HTBOTTOMRIGHT;
        return true;
    }

    if (!isMaximized() && onLeft) {
        *result = HTLEFT;
        return true;
    }

    if (!isMaximized() && onRight) {
        *result = HTRIGHT;
        return true;
    }

    if (!isMaximized() && onTop) {
        *result = HTTOP;
        return true;
    }

    if (!isMaximized() && onBottom) {
        *result = HTBOTTOM;
        return true;
    }

    const QPoint pos = mapFromGlobal(globalPos);

    if (isTitleBarDragArea(pos)) {
        *result = HTCAPTION;
        return true;
    }

    return QMainWindow::nativeEvent(eventType, message, result);
}
#endif


void MainWindow::dropEvent(QDropEvent* event)
{
    const QStringList files = localExrFiles(event->mimeData());
    if (files.isEmpty()) {
        event->ignore();
        return;
    }
    event->acceptProposedAction();
    for (const QString& filename : files)
        open(filename);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (!localExrFiles(event->mimeData()).isEmpty())
        event->acceptProposedAction();
    else
        event->ignore();
}

void MainWindow::writeSettings()
{
    QSettings settings(
      QSettings::IniFormat,
      QSettings::UserScope,
      "afichet",
      "OpenEXR Viewer");

    // A bit hacky...
    QWidget* activeWidget = m_openFileTabs->currentWidget();

    if (activeWidget) {
        ImageFileWidget* widget = (ImageFileWidget*)activeWidget;

        m_currentOpenedFolder     = widget->getOpenedFolder();
        m_splitterImageState      = widget->getSplitterImageState();
        m_splitterPropertiesState = widget->getSplitterPropertiesState();
    }

    settings.beginGroup("MainWindow");
    settings.setValue("geometry", saveGeometry());
    settings.setValue("state", saveState());
    settings.setValue("workspaceV2/splitterImage", m_splitterImageState);
    settings.setValue("workspaceV2/splitterProperties", m_splitterPropertiesState);
    settings.setValue("openedFolder", m_currentOpenedFolder);
    settings.setValue("theme", m_currentTheme);
    settings.endGroup();
}


void MainWindow::readSettings()
{
    QSettings settings(
      QSettings::IniFormat,
      QSettings::UserScope,
      "afichet",
      "OpenEXR Viewer");

    settings.beginGroup("MainWindow");
    restoreGeometry(settings.value("geometry").toByteArray());
    restoreState(settings.value("state").toByteArray());

    m_splitterImageState = settings.value("workspaceV2/splitterImage").toByteArray();
    m_splitterPropertiesState
      = settings.value("workspaceV2/splitterProperties").toByteArray();

    if (settings.contains("openedFolder")) {
        m_currentOpenedFolder = settings.value("openedFolder").toString();
    } else {
        m_currentOpenedFolder = QDir::homePath();
    }

    applyTheme(settings.value("theme", s_darkTheme).toString());

    settings.endGroup();
}


void MainWindow::onTabCloseRequested(int idx)
{
    leaveMinimalView();
    // Saves state in case this is the last opened tab
    auto* widget = qobject_cast<ImageFileWidget*>(m_openFileTabs->widget(idx));
    if (!widget) return;

    m_currentOpenedFolder     = widget->getOpenedFolder();
    m_splitterImageState      = widget->getSplitterImageState();
    m_splitterPropertiesState = widget->getSplitterPropertiesState();

    m_openFileTabs->removeTab(idx);
    delete widget;
    updateFileTabPresentation();
    updateShowActions();
}


void MainWindow::on_action_ShowAttributes_toggled(bool)
{
    applyPanelVisibilityToAllTabs();
}


void MainWindow::on_action_ShowLayers_toggled(bool)
{
    applyPanelVisibilityToAllTabs();
}


void MainWindow::on_action_ModeExposure_triggered()
{
    applyRgbPreviewMode(RGBFramebufferModel::Preview_Exposure);
}


void MainWindow::on_action_ModeToneMapping_triggered()
{
    applyRgbPreviewMode(RGBFramebufferModel::Preview_ToneMapping);
}


void MainWindow::on_action_ModeFalseColor_triggered()
{
    applyRgbPreviewMode(RGBFramebufferModel::Preview_FalseColor);
}


void MainWindow::on_action_ThemeLight_triggered()
{
    applyTheme(s_lightTheme);
}


void MainWindow::on_action_ThemeDark_triggered()
{
    applyTheme(s_darkTheme);
}


void MainWindow::on_action_Refresh_triggered()
{
    if (ImageFileWidget* widget = currentFileWidget()) widget->refresh();
}


void MainWindow::onCurrentChanged(int index)
{
    if (!m_switchingMinimalView) leaveMinimalView();
    if (index == -1) {
        // deactivate close and refresh functions
        ui->action_Refresh->setEnabled(false);
        ui->action_Close->setEnabled(false);
        updateShowActions();
        updateFileTabPresentation();
        return;
    }

    ui->action_Refresh->setEnabled(
      !currentFileWidget()->isStream()
      && !currentFileWidget()->isRefreshInProgress());
    ui->action_Close->setEnabled(true);

    ImageFileWidget* widget = (ImageFileWidget*)m_openFileTabs->currentWidget();

    m_currentOpenedFolder     = widget->getOpenedFolder();
    m_splitterImageState      = widget->getSplitterImageState();
    m_splitterPropertiesState = widget->getSplitterPropertiesState();

    updateShowActions();
    updateFileTabPresentation();
}

void MainWindow::on_action_About_triggered()
{
    About about_window(this);
    about_window.exec();
}


void MainWindow::on_action_Close_triggered()
{
    onTabCloseRequested(m_openFileTabs->currentIndex());
}


void MainWindow::toggleMaximized()
{
    if (isMaximized()) {
        showNormal();
    } else {
        showMaximized();
    }

    updateTitleBarButtons();
    updateWindowFrame();
}
