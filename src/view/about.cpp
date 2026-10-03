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
        "<li>Open .exr and .hdr (Radiance RGBE) files from the menu, command line, or drag and drop.</li>"
        "<li>Browse OpenEXR attributes and Radiance header fields, parts, and layer groups.</li>"
        "<li>View RGB, RGBA, Y, YA, YC, YCA, alpha, and generic channels.</li>"
        "<li>Switch between layer previews using tabs.</li>"
        "<li>Complete 2:1 color EXR/HDR images with square pixels are recognized as "
        "LatLong environments. Start with the original image; use View &gt; Show "
        "&gt; Projection for perspective, cube, or sphere views. EXR envmap tags take precedence.</li>"
        "<li>Inspect pixel values, compression, size, "
        "pixel type, and dataset min/max information.</li>"
        "<li>Switch light/dark themes and copy the active image to the "
        "clipboard.</li>"
        "<li>Export previews, source data, layers, exposure brackets, or projections.</li>"
        "</ul>"
        "<h2>Basic Use</h2>"
        "<ol>"
        "<li>Choose File &gt; Open or drop an .exr or .hdr file into the viewer.</li>"
        "<li>Double-click a layer or displayable attribute to open it.</li>"
        "<li>Use the View menu to choose the preview mode, show Attributes "
        "and Layers panels, and change the theme.</li>"
        "<li>Use the info button on a preview to show file and framebuffer "
        "details.</li>"
        "</ol>"
        "<h2>Image Controls</h2>"
        "<ul>"
        "<li>Mouse wheel: zoom. Left or middle drag: pan.</li>"
        "<li>Click the zoom percentage to switch between 100% and fit-to-view.</li>"
        "<li>RGB-like views: adjust exposure with the slider, value box, or "
        "Ctrl + mouse wheel. Click EV to reset.</li>"
        "<li>Single-channel views: choose a colormap, edit min/max range, use "
        "the automatic range button, and toggle the color scale.</li>"
        "<li>Hover over an icon for its name and shortcut.</li>"
        "</ul>"
        "<h2>Shortcuts</h2>"
        "<ul>"
        "<li>Ctrl+O open, Ctrl+S export, Ctrl+W close, F5 refresh, Esc quit.</li>"
        "<li>Ctrl+C copy scaled image, Ctrl+Shift+C copy full resolution.</li>"
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
