#include "about.h"
#include "ui_about.h"

About::About(QWidget* parent): QDialog(parent), ui(new Ui::About)
{
    ui->setupUi(this);
    setWindowTitle(tr("Help"));
    ui->textBrowser->setOpenExternalLinks(true);

    ui->textBrowser->setHtml(QStringLiteral(
      "<h1>OpenEXR Viewer Mini</h1>"
      "<h2>Open and inspect</h2>"
      "<p>Use File &gt; Open, drag and drop, or command-line paths to open EXR "
      "and Radiance RGBE (.hdr) files. Double-click a layer or displayable "
      "attribute to open a preview. Closing a tab cancels its pending work; "
      "failed refreshes keep the previous source.</p>"
      "<p>View &gt; Show controls the panels; View &gt; Theme selects Light or Dark. "
      "Hover over the image for original coordinates and values. The info and "
      "NaN/Inf controls show source statistics and invalid-value locations.</p>"
      "<h2>Image controls</h2>"
      "<ul>"
      "<li>Wheel or +/-: zoom. 0: fit. 1: 100%. The zoom percentage toggles fit/100%.</li>"
      "<li>Left or middle drag: pan. Double-click: enter or leave minimal view.</li>"
      "<li>Ctrl+wheel adjusts exposure, the first tone parameter, or the false-color "
      "upper bound. Right-click resets the current color mode.</li>"
      "<li>Tone mapping: Reinhard, ACES, Filmic/Hable, Log and Clamp. Scalar and "
      "false-color views offer seven colormaps, manual/automatic ranges and a scale.</li>"
      "<li>Stored Mipmap/Ripmap levels, compatible stereo pairs and Deep Scanline "
      "depth ranges have dedicated controls. Level sliders load on release.</li>"
      "<li>Environment images start in native view. View &gt; Show &gt; Projection "
      "offers LatLong, cube, perspective and sphere. Left drag rotates a "
      "perspective or sphere view; middle drag pans. Perspective wheel input over the image changes FOV.</li>"
      "</ul>"
      "<h2>Output and limits</h2>"
      "<p>Preview PNG/JPEG and clipboard copy use display colors and canvas geometry. "
      "Original EXR retains source channels and values; a selected tiled level "
      "exports as Scanline. The Projection export target has independent settings. "
      "HDR brackets commit one file at a time and report completed files on failure.</p>"
      "<p>Deep Tiled, ZBack volumes, Radiance XYZE, sequence playback and stdin "
      "are unsupported. Color conversion is not a full OCIO workflow. Native HDR "
      "requires Windows and compatible hardware; failures fall back to SDR. "
      "Copy and PNG/JPEG remain SDR at the selected EV.</p>"
      "<p>Windows integration is optional under File &gt; Windows integration. "
      "Use Undo all before moving or deleting a registered portable copy.</p>"
      "<h2>Shortcuts</h2>"
      "<p>Ctrl+O open; Ctrl+S export; Ctrl+W close; F5 refresh; Esc quit. "
      "Ctrl+Tab / Ctrl+Shift+Tab switch files. Ctrl+C copies up to 1024 pixels wide; "
      "Ctrl+Shift+C copies full size.</p>"
      "<p>See the User Guide and Release Readiness documents in the "
      "<a href=\"https://github.com/nauyihsnehs/OpenEXR-Viewer-Mini\">project repository</a> "
      "for format boundaries and pending release acceptance checks.</p>"));
}

About::~About()
{
    delete ui;
}

void About::on_pushButton_clicked()
{
    close();
}
