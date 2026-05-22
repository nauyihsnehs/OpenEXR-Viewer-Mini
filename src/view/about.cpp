#include "about.h"
#include "ui_about.h"

About::About(QWidget* parent): QDialog(parent), ui(new Ui::About)
{
    ui->setupUi(this);
    setWindowTitle(tr("Help"));

    ui->textBrowser->setHtml(
      QString(
        "<h1>OpenEXR Viewer</h1>"
        "<h2>Features</h2>"
        "<ul>"
        "<li>Open .exr files from the menu, command line, or drag and drop.</li>"
        "<li>Browse OpenEXR headers, attributes, parts, and layer groups.</li>"
        "<li>View RGB, RGBA, Y, YA, YC, YCA, alpha, and generic channels.</li>"
        "<li>Use tabbed, cascaded, or tiled layer previews.</li>"
        "<li>Inspect pixel values, data/display windows, compression, size, "
        "pixel type, and dataset min/max information.</li>"
        "<li>Switch light/dark themes and copy the active image to the "
        "clipboard.</li>"
        "</ul>"
        "<h2>Basic Use</h2>"
        "<ol>"
        "<li>Choose File &gt; Open or drop an .exr file into the viewer.</li>"
        "<li>Double-click a layer or displayable attribute to open it.</li>"
        "<li>Use the View menu to show panels, overlays, layout, and theme.</li>"
        "<li>Use the info button on a preview to show file and framebuffer "
        "details.</li>"
        "</ol>"
        "<h2>Image Controls</h2>"
        "<ul>"
        "<li>Mouse wheel: zoom. Left or middle drag: pan.</li>"
        "<li>Zoom button: switch between 100% and fit-to-view.</li>"
        "<li>RGB-like views: adjust exposure with the slider, value box, or "
        "Ctrl + mouse wheel. Click Exposure to reset.</li>"
        "<li>Single-channel views: choose a colormap, edit min/max range, use "
        "Auto, and show or hide the scale.</li>"
        "</ul>"
        "<h2>Shortcuts</h2>"
        "<ul>"
        "<li>Ctrl+O open, Ctrl+W close, F5 refresh, Esc quit.</li>"
        "<li>Ctrl+C copy scaled image, Ctrl+Shift+C copy full resolution.</li>"
        "<li>Ctrl+T tabbed, Ctrl+Y cascaded, Ctrl+U tiled.</li>"
        "</ul>"));
}

About::~About()
{
    delete ui;
}

void About::on_pushButton_clicked()
{
    close();
}
