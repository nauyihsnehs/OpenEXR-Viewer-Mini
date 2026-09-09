#pragma once

class QAction;
class QBoxLayout;
class QToolBar;
class QWidget;

// These helpers only decorate controls; actions and image state stay with callers.
QWidget* createWelcomePage(QAction* openAction, QWidget* parent);
void animateToolbarButtons(QToolBar* toolbar);
void wrapPreviewControls(QBoxLayout* layout);
