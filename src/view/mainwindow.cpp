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
#include "./ui_mainwindow.h"
#include <view/about.h>

#include <cassert>

#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QDebug>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QIcon>
#include <QMessageBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QMenuBar>
#include <QMimeData>
#include <QMouseEvent>
#include <QImage>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QStyle>
#include <QSettings>
#include <QTabBar>
#include <QTextStream>
#include <QToolButton>
#include <QVariant>

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


static const int s_windowResizeBorder = 8;
static const int s_clipboardMaxWidth = 1024;
static const char* s_darkTheme = "dark";
static const char* s_lightTheme = "light";


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
    return widget
      && widget->isVisible()
      && widget->rect().contains(widget->mapFromGlobal(point));
}


static const DWORD s_dwmWindowCornerPreference = 33;
static const DWORD s_dwmWindowCornerDefault = 0;
static const DWORD s_dwmWindowCornerRound = 2;


static int scaledWindowsMetric(HWND hwnd, int value)
{
    UINT dpi = 96;

    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) return value;

    typedef UINT(WINAPI* DpiForWindow)(HWND);
    DpiForWindow dpiForWindow =
      reinterpret_cast<DpiForWindow>(GetProcAddress(user32, "GetDpiForWindow"));

    if (dpiForWindow) dpi = dpiForWindow(hwnd);

    const int scaledValue = MulDiv(value, dpi, 96);

    return scaledValue > 0 ? scaledValue : value;
}


static void setWindowsCornerPreference(HWND hwnd, bool rounded)
{
    if (!hwnd) return;

    typedef HRESULT(WINAPI* DwmSetWindowAttributeFn)(
      HWND, DWORD, LPCVOID, DWORD);

    static bool initialized = false;
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

    const DWORD preference =
      rounded ? s_dwmWindowCornerRound : s_dwmWindowCornerDefault;

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
  , m_splitterImageState()
  , m_splitterPropertiesState()
  , m_titleDragPosition()
  , m_titleBarDragging(false)
{
    ui->setupUi(this);
    setAttribute(Qt::WA_StyledBackground, true);
    setupTitleBar();
    setupThemeActions();
    setAcceptDrops(true);

    m_openFileTabs->setMovable(true);
    m_openFileTabs->setTabsClosable(true);

    // clang-format off
    connect(m_openFileTabs, SIGNAL(currentChanged(int)),
            this,           SLOT(onCurrentChanged(int)));
    connect(m_openFileTabs, SIGNAL(tabCloseRequested(int)),
            this,           SLOT(onTabCloseRequested(int)));
    // clang-format on

    setCentralWidget(m_openFileTabs);
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
    delete ui;
}


void MainWindow::setupTitleBar()
{
    setWindowFlags(windowFlags() | Qt::FramelessWindowHint);

    m_titleBar = new QWidget(this);
    m_titleBar->setObjectName("customTitleBar");
    m_titleBar->setAttribute(Qt::WA_StyledBackground, true);
    m_titleBar->setFixedHeight(30);

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
      QSizePolicy::Expanding, QSizePolicy::Expanding);

    m_minimizeButton = new QToolButton(m_titleBar);
    m_minimizeButton->setObjectName("titleMinimizeButton");
    m_minimizeButton->setAutoRaise(true);
    m_minimizeButton->setToolTip(tr("Minimize"));
    m_minimizeButton->setFixedSize(46, 30);

    m_maximizeButton = new QToolButton(m_titleBar);
    m_maximizeButton->setObjectName("titleMaximizeButton");
    m_maximizeButton->setAutoRaise(true);
    m_maximizeButton->setToolTip(tr("Maximize"));
    m_maximizeButton->setFixedSize(46, 30);

    m_closeButton = new QToolButton(m_titleBar);
    m_closeButton->setObjectName("titleCloseButton");
    m_closeButton->setAutoRaise(true);
    m_closeButton->setToolTip(tr("Close"));
    m_closeButton->setFixedSize(46, 30);

    connect(
      m_minimizeButton, &QToolButton::clicked, this, &MainWindow::showMinimized);
    connect(
      m_maximizeButton, &QToolButton::clicked, this, &MainWindow::toggleMaximized);
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
    QFile stylesheet(themeStyleSheetPath(normalizedTheme));

    if (!stylesheet.open(QFile::ReadOnly | QFile::Text)) {
        qWarning() << "Unable to set stylesheet, file not found";
        return;
    }

    QTextStream stream(&stylesheet);
    qApp->setStyleSheet(stream.readAll());

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

    const QColor iconColor =
      m_currentTheme == s_lightTheme
        ? QColor(32, 35, 40)
        : QColor(214, 214, 214);

    const TitleButtonIcon maximizeIcon =
      isMaximized()
        ? TitleButtonRestore
        : TitleButtonMaximize;

    m_minimizeButton->setIcon(titleButtonIcon(TitleButtonMinimize, iconColor));
    m_maximizeButton->setIcon(titleButtonIcon(maximizeIcon, iconColor));
    m_closeButton->setIcon(titleButtonIcon(TitleButtonClose, iconColor));
    m_maximizeButton->setToolTip(isMaximized() ? tr("Restore") : tr("Maximize"));
}


void MainWindow::updateWindowFrame()
{
    const bool frameActive = !isMaximized() && !isFullScreen();
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
    const LONG_PTR style =
      oldStyle
      | WS_THICKFRAME
      | WS_MAXIMIZEBOX
      | WS_MINIMIZEBOX
      | WS_SYSMENU;

    if (style != oldStyle) SetWindowLongPtr(hwnd, GWL_STYLE, style);

    SetWindowPos(
      hwnd,
      nullptr,
      0,
      0,
      0,
      0,
      SWP_FRAMECHANGED
        | SWP_NOMOVE
        | SWP_NOSIZE
        | SWP_NOZORDER
        | SWP_NOOWNERZORDER
        | SWP_NOACTIVATE);
}
#endif


bool MainWindow::isTitleBarDragArea(const QPoint& pos) const
{
    if (!m_titleBar || !m_titleBar->geometry().contains(pos)) return false;

    QWidget* child = childAt(pos);

    return child == m_titleBar
      || child == m_windowTitleLabel
      || (child && m_windowTitleLabel->isAncestorOf(child));
}


ImageFileWidget* MainWindow::currentFileWidget() const
{
    return qobject_cast<ImageFileWidget*>(m_openFileTabs->currentWidget());
}


void MainWindow::copyActiveImage(bool fullResolution) const
{
    ImageFileWidget* widget = currentFileWidget();
    const FramebufferModel* model =
      widget ? widget->activeFramebufferModel() : nullptr;

    if (!model || !model->isImageLoaded()) return;

    QImage image = model->getLoadedImage();
    if (image.isNull()) return;

    if (!fullResolution && image.width() > s_clipboardMaxWidth) {
        image = image.scaledToWidth(
          s_clipboardMaxWidth,
          Qt::SmoothTransformation);
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
        ImageFileWidget* widget =
          qobject_cast<ImageFileWidget*>(m_openFileTabs->widget(i));

        applyPanelVisibility(widget);
    }
}


void MainWindow::updateShowActions()
{
    ImageFileWidget* widget = currentFileWidget();
    const FramebufferModel* model =
      widget ? widget->activeFramebufferModel() : nullptr;
    const bool enabled = widget && widget->hasActiveFramebuffer();
    const bool copyEnabled = model && model->isImageLoaded();

    ui->action_ShowDataWindow->blockSignals(true);
    ui->action_ShowDisplayWindow->blockSignals(true);

    ui->action_ShowDataWindow->setEnabled(enabled);
    ui->action_ShowDisplayWindow->setEnabled(enabled);
    ui->action_CopyImage->setEnabled(copyEnabled);
    ui->action_CopyImageFullResolution->setEnabled(copyEnabled);

    ui->action_ShowDataWindow->setChecked(
      enabled && widget->isDataWindowVisible());
    ui->action_ShowDisplayWindow->setChecked(
      enabled && widget->isDisplayWindowVisible());

    ui->action_ShowDataWindow->blockSignals(false);
    ui->action_ShowDisplayWindow->blockSignals(false);
}


void MainWindow::updateFileTabPresentation()
{
    const int count = m_openFileTabs->count();
    QTabBar* tabBar = m_openFileTabs->findChild<QTabBar*>(
      QString(), Qt::FindDirectChildrenOnly);

    if (tabBar) tabBar->setVisible(count > 1);

    QString title;

    if (count == 1) title = m_openFileTabs->tabText(0).trimmed();

    if (m_windowTitleLabel) {
        m_windowTitleLabel->setText(title);
        m_windowTitleLabel->setToolTip(title);
    }

    setWindowTitle(title.isEmpty() ? tr("OpenEXR Viewer") : title);
}


void MainWindow::installEmptyOpenEventFilters()
{
    m_openFileTabs->installEventFilter(this);

    for (QWidget* widget : m_openFileTabs->findChildren<QWidget*>(
           QString(), Qt::FindDirectChildrenOnly)) {
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
      && !m_openFileTabs->isAncestorOf(watchedWidget)) {
        return false;
    }
    if (qobject_cast<QTabBar*>(watchedWidget)) return false;

    QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
    if (mouseEvent->button() != Qt::LeftButton) return false;
    if (mouseEvent->modifiers() != Qt::NoModifier) return false;

    on_action_Open_triggered();
    return true;
}


void MainWindow::open(std::istream& stream)
{
    //QString filename_no_path = QFileInfo(filename).fileName();

    ImageFileWidget* fileWidget = new ImageFileWidget(stream, m_openFileTabs);
    fileWidget->setSplitterImageState(m_splitterImageState);
    fileWidget->setSplitterPropertiesState(m_splitterPropertiesState);
    applyPanelVisibility(fileWidget);

    m_openFileTabs->addTab(fileWidget, "Stream");
    m_openFileTabs->setCurrentWidget(fileWidget);
    updateFileTabPresentation();

    connect(
      fileWidget,
      SIGNAL(openFileOnDropEvent(QString)),
      this,
      SLOT(open(QString)));

    connect(
      fileWidget,
      SIGNAL(activeFramebufferChanged()),
      this,
      SLOT(updateShowActions()));

    updateShowActions();
}


void MainWindow::open(const QString& filename)
{
    QString filename_no_path = QFileInfo(filename).fileName();

    ImageFileWidget* fileWidget = new ImageFileWidget(filename, m_openFileTabs);
    fileWidget->setSplitterImageState(m_splitterImageState);
    fileWidget->setSplitterPropertiesState(m_splitterPropertiesState);
    applyPanelVisibility(fileWidget);

    m_openFileTabs->addTab(fileWidget, filename_no_path);
    m_openFileTabs->setCurrentWidget(fileWidget);
    updateFileTabPresentation();

    connect(
      fileWidget,
      SIGNAL(openFileOnDropEvent(QString)),
      this,
      SLOT(open(QString)));

    connect(
      fileWidget,
      SIGNAL(activeFramebufferChanged()),
      this,
      SLOT(updateShowActions()));

    updateShowActions();
}


void MainWindow::on_action_Open_triggered()
{
    const QString filename = QFileDialog::getOpenFileName(
      this,
      tr("Open OpenEXR Image"),
      m_currentOpenedFolder,
      tr("Images (*.exr)"));

    if (filename.size() != 0) {
        open(filename);
    }
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
    writeSettings();
    event->accept();
}


void MainWindow::changeEvent(QEvent* event)
{
    QMainWindow::changeEvent(event);

    if (event->type() == QEvent::WindowStateChange) {
        updateTitleBarButtons();
        updateWindowFrame();
    }
}


void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    updateWindowFrame();
}


bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (handleEmptyOpenClick(watched, event)) return true;

    const bool titleBarTarget =
      watched == m_titleBar || watched == m_windowTitleLabel;

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
            m_titleBarDragging  = true;
            m_titleDragPosition =
              mouseGlobalPosition(mouseEvent) - frameGeometry().topLeft();
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

    const bool insideWindow =
      x >= windowRect.left
      && x < windowRect.right
      && y >= windowRect.top
      && y < windowRect.bottom;

    if (!insideWindow) {
        return QMainWindow::nativeEvent(eventType, message, result);
    }

    const QPoint globalPos(x, y);
    const bool onTitleButton =
      containsGlobalPoint(m_minimizeButton, globalPos)
      || containsGlobalPoint(m_maximizeButton, globalPos)
      || containsGlobalPoint(m_closeButton, globalPos);

    if (onTitleButton) {
        *result = HTCLIENT;
        return true;
    }

    const int resizeBorder = scaledWindowsMetric(msg->hwnd, s_windowResizeBorder);
    const bool onLeft      = x < windowRect.left + resizeBorder;
    const bool onRight     = x >= windowRect.right - resizeBorder;
    const bool onTop       = y < windowRect.top + resizeBorder;
    const bool onBottom    = y >= windowRect.bottom - resizeBorder;

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


void MainWindow::dropEvent(QDropEvent* ev)
{
    QList<QUrl> urls = ev->mimeData()->urls();

    for (const QUrl& url: urls) {
        QString filename = url.toString();
        const QString startFileTypeString =
#ifdef _WIN32
          "file:///";
#else
          "file://";
#endif

        if (filename.startsWith(startFileTypeString)) {
            filename = filename.remove(0, startFileTypeString.length());
            open(filename);
        }
    }
}


void MainWindow::dragEnterEvent(QDragEnterEvent* ev)
{
    ev->acceptProposedAction();
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
    settings.setValue("splitterImage", m_splitterImageState);
    settings.setValue("splitterProperties", m_splitterPropertiesState);
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

    m_splitterImageState = settings.value("splitterImage").toByteArray();
    m_splitterPropertiesState
      = settings.value("splitterProperties").toByteArray();

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
    // Saves state in case this is the last opened tab
    ImageFileWidget* widget = (ImageFileWidget*)m_openFileTabs->widget(idx);

    m_currentOpenedFolder     = widget->getOpenedFolder();
    m_splitterImageState      = widget->getSplitterImageState();
    m_splitterPropertiesState = widget->getSplitterPropertiesState();

    m_openFileTabs->removeTab(idx);
    updateFileTabPresentation();
    updateShowActions();
}


void MainWindow::on_action_Tabbed_triggered()
{
    ImageFileWidget* widget = (ImageFileWidget*)m_openFileTabs->currentWidget();

    if (widget) {
        widget->setTabbed();
    }
}


void MainWindow::on_action_Cascade_triggered()
{
    ImageFileWidget* widget = (ImageFileWidget*)m_openFileTabs->currentWidget();

    if (widget) {
        widget->setCascade();
    }
}


void MainWindow::on_action_Tiled_triggered()
{
    ImageFileWidget* widget = (ImageFileWidget*)m_openFileTabs->currentWidget();

    if (widget) {
        widget->setTiled();
    }
}


void MainWindow::on_action_ShowDataWindow_toggled(bool checked)
{
    ImageFileWidget* widget = currentFileWidget();

    if (widget) widget->setDataWindowVisible(checked);

    updateShowActions();
}


void MainWindow::on_action_ShowDisplayWindow_toggled(bool checked)
{
    ImageFileWidget* widget = currentFileWidget();

    if (widget) widget->setDisplayWindowVisible(checked);

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
    ImageFileWidget* widget = (ImageFileWidget*)m_openFileTabs->currentWidget();
    widget->refresh();
}


void MainWindow::onCurrentChanged(int index)
{
    if (index == -1) {
        // deactivate close and refresh functions
        ui->action_Refresh->setEnabled(false);
        ui->action_Close->setEnabled(false);
        updateShowActions();
        updateFileTabPresentation();
        return;
    }

    ui->action_Refresh->setEnabled(true);
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
