#include "app/StateReport.h"
#include "app/TestFixtures.h"
#include "bridge/AnnotationController.h"
#include "bridge/DocumentController.h"
#include "bridge/OutlineModel.h"
#include "bridge/ExportController.h"
#include "bridge/FormController.h"
#include "bridge/PageOperations.h"
#include "bridge/RedactionController.h"
#include "bridge/SearchController.h"
#include "bridge/SelectionController.h"
#include "core/PdfEngine.h"
#include "platform/PlatformWindow.h"
#include "render/PageImageProvider.h"

#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>

namespace {

// UI capture mode, used for screenshot review and visual regression checks.
//
// Set LUMEN_CAPTURE to an output .png path and the app grabs its own window
// once and exits. Grabbing from inside the process is the only reliable route
// here: an external screen capture needs an interactive desktop session, which
// automated runs do not have.
//
//   LUMEN_CAPTURE=work\ui-check\shot.png  LUMEN_CAPTURE_DELAY=3000  lumenpdf.exe file.pdf
void installCaptureHook(QQmlApplicationEngine &engine,
                        lumen::DocumentController *controller)
{
    const QByteArray shotTarget = qgetenv("LUMEN_CAPTURE");
    const QByteArray reportTarget = qgetenv("LUMEN_REPORT");
    if (shotTarget.isEmpty() && reportTarget.isEmpty())
        return;

    bool ok = false;
    const int delayMs = qEnvironmentVariableIntValue("LUMEN_CAPTURE_DELAY", &ok);

    QTimer::singleShot(ok && delayMs > 0 ? delayMs : 2500, &engine,
                       [&engine, shotTarget, reportTarget, controller] {
        int exitCode = 0;

        // The state report comes first: it is what the tests assert on, and it
        // should still be written even if grabbing the window fails.
        if (!reportTarget.isEmpty()) {
            const QString path = QString::fromLocal8Bit(reportTarget);
            if (lumen::report::write(path, controller)) {
                qInfo("report: wrote %s", qPrintable(path));
            } else {
                qWarning("report: failed to write %s", qPrintable(path));
                exitCode = 4;
            }
        }

        if (!shotTarget.isEmpty()) {
            const auto roots = engine.rootObjects();
            auto *window = roots.isEmpty() ? nullptr
                                           : qobject_cast<QQuickWindow *>(roots.first());
            const QString path = QString::fromLocal8Bit(shotTarget);

            if (!window) {
                qWarning("capture: no QQuickWindow root");
                exitCode = 2;
            } else {
                const QImage shot = window->grabWindow();
                if (shot.isNull() || !shot.save(path)) {
                    qWarning("capture: failed to write %s", qPrintable(path));
                    exitCode = 3;
                } else {
                    qInfo("capture: wrote %s (%dx%d)",
                          qPrintable(path), shot.width(), shot.height());
                }
            }
        }

        if (exitCode != 0)
            QCoreApplication::exit(exitCode);
        else
            QCoreApplication::quit();
    });
}

} // namespace

int main(int argc, char *argv[])
{
    // Qt Quick needs the GPU pipeline chosen before the application exists.
    // D3D11 is the safe default on Windows; the backend gets revisited when
    // macOS (Metal) and Linux (Vulkan/OpenGL) come online.
#ifdef Q_OS_WIN
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
#endif

    QGuiApplication app(argc, argv);

    // Fixture generation needs Qt's font database and QPdfWriter but no window,
    // so it runs and exits before anything else is set up.
    if (qEnvironmentVariableIsSet("LUMEN_MAKE_FIXTURES")) {
        const int written = lumen::fixtures::writeAll(
            qEnvironmentVariable("LUMEN_MAKE_FIXTURES"));
        return written > 0 ? 0 : 1;
    }

    QGuiApplication::setApplicationName(QStringLiteral("LumenPDF"));
    QGuiApplication::setOrganizationName(QStringLiteral("Lumen"));
    QGuiApplication::setApplicationVersion(QStringLiteral(LUMEN_VERSION));

    // Lumen ships its own component set; the built-in styles are never used.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    lumen::PdfEngine::initialize();

    QGuiApplication::setFont(lumen::PlatformWindow::preferredUiFont());

    // Scoped so the engine -- and with it every open document and every
    // worker thread holding one -- is destroyed *before* PDFium is torn down.
    // Getting this backwards means FPDF_CloseDocument runs after
    // FPDF_DestroyLibrary, which is an access violation on exit.
    int result = 0;
    {
    QQmlApplicationEngine engine;

    // The engine takes ownership of the provider.
    auto *provider = new lumen::PageImageProvider;
    engine.addImageProvider(QStringLiteral("pdfpage"), provider);

    auto *controller = new lumen::DocumentController(&engine);
    controller->setImageProvider(provider);

    auto *platform = new lumen::PlatformWindow(&engine);

    qmlRegisterSingletonInstance("Lumen.Backend", 1, 0, "Document", controller);
    qmlRegisterSingletonInstance("Lumen.Backend", 1, 0, "Platform", platform);
    qmlRegisterUncreatableType<lumen::DocumentController>(
        "Lumen.Backend", 1, 0, "DocumentStatus",
        QStringLiteral("Use Document.status"));

    // Registered so QML can read their enums and use them as property types;
    // both are owned by the controller and never constructed from QML.
    qmlRegisterUncreatableType<lumen::SearchController>(
        "Lumen.Backend", 1, 0, "SearchStatus",
        QStringLiteral("Use Document.search"));
    qmlRegisterUncreatableType<lumen::OutlineModel>(
        "Lumen.Backend", 1, 0, "Outline",
        QStringLiteral("Use Document.outline"));
    qmlRegisterUncreatableType<lumen::AnnotationController>(
        "Lumen.Backend", 1, 0, "AnnotationType",
        QStringLiteral("Use Document.annotate"));
    qmlRegisterUncreatableType<lumen::FormController>(
        "Lumen.Backend", 1, 0, "FormFieldKind",
        QStringLiteral("Use Document.forms"));

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, []() { QCoreApplication::exit(-1); },
                     Qt::QueuedConnection);

    engine.loadFromModule("App", "Main");

    // Open a file passed on the command line (or by the shell's "Open with").
    const QStringList args = QGuiApplication::arguments();
    if (args.size() > 1)
        controller->open(QUrl::fromLocalFile(args.at(1)));

    // Drive a search from the environment, so capture runs and smoke tests can
    // exercise the results path without synthesising input events.
    //
    // Single-shot, always. Saving reopens the document and re-emits
    // documentChanged, so a persistent connection turns
    // "select -> annotate -> save" into an infinite loop that spins the CPU
    // and grows the file without ever finishing.
    if (qEnvironmentVariableIsSet("LUMEN_SEARCH")) {
        const QString query = qEnvironmentVariable("LUMEN_SEARCH");
        QObject::connect(controller, &lumen::DocumentController::documentChanged,
                         controller, [controller, query] {
                             if (controller->pageCount() > 0)
                                 controller->search()->setQuery(query);
                         },
                         Qt::SingleShotConnection);
    }

    // Test hook: double-click word selection, as "page,x,y" in PDF points.
    if (qEnvironmentVariableIsSet("LUMEN_SELECT_WORD")) {
        const QStringList parts = qEnvironmentVariable("LUMEN_SELECT_WORD").split(u',');
        if (parts.size() == 3) {
            QObject::connect(controller, &lumen::DocumentController::documentChanged,
                             controller, [controller, parts] {
                                 if (controller->pageCount() <= 0)
                                     return;
                                 controller->selection()->selectWordAt(
                                     parts.at(0).toInt(),
                                     QPointF(parts.at(1).toDouble(), parts.at(2).toDouble()));
                             },
                             Qt::SingleShotConnection);
        }
    }

    // Test hook: drive a drag-selection from the environment, as
    // "page,x1,y1,x2,y2" in PDF points with a top-left origin.
    if (qEnvironmentVariableIsSet("LUMEN_SELECT")) {
        const QStringList parts = qEnvironmentVariable("LUMEN_SELECT").split(u',');
        if (parts.size() == 5) {
            QObject::connect(controller, &lumen::DocumentController::documentChanged,
                             controller, [controller, parts] {
                                 if (controller->pageCount() <= 0)
                                     return;
                                 auto *selection = controller->selection();
                                 const int page = parts.at(0).toInt();
                                 selection->begin(page, QPointF(parts.at(1).toDouble(),
                                                                parts.at(2).toDouble()));
                                 selection->extend(page, QPointF(parts.at(3).toDouble(),
                                                                 parts.at(4).toDouble()));
                                 selection->end();

                                 // Optional follow-on actions, so a single run
                                 // can exercise select -> annotate -> save.
                                 const QString action = qEnvironmentVariable("LUMEN_ANNOTATE");
                                 if (action == QLatin1String("highlight"))
                                     controller->annotate()->applyToSelection(
                                         lumen::AnnotationController::Highlight);
                                 else if (action == QLatin1String("underline"))
                                     controller->annotate()->applyToSelection(
                                         lumen::AnnotationController::Underline);
                                 else if (action == QLatin1String("strikeout"))
                                     controller->annotate()->applyToSelection(
                                         lumen::AnnotationController::StrikeOut);
                                 else if (action == QLatin1String("redact"))
                                     controller->redact()->redactSelection();

                                 if (qEnvironmentVariableIsSet("LUMEN_SAVE_AS")) {
                                     controller->saveAs(QUrl::fromLocalFile(
                                         qEnvironmentVariable("LUMEN_SAVE_AS")));
                                 }
                             },
                             Qt::SingleShotConnection);
        }
    }

    // Test hook: run a page operation, as "rotate,<page>,<turns>",
    // "delete,<page>" or "move,<from>,<to>". Append ",undo" to exercise the
    // undo path in the same run.
    if (qEnvironmentVariableIsSet("LUMEN_PAGEOP")) {
        const QStringList parts = qEnvironmentVariable("LUMEN_PAGEOP").split(u',');
        QObject::connect(controller, &lumen::DocumentController::documentChanged,
                         controller, [controller, parts] {
                             if (controller->pageCount() <= 0 || parts.isEmpty())
                                 return;

                             auto *pages = controller->pages();
                             const QString op = parts.at(0);

                             if (op == QLatin1String("rotate") && parts.size() >= 3)
                                 pages->rotate(parts.at(1).toInt(), parts.at(2).toInt());
                             else if (op == QLatin1String("delete") && parts.size() >= 2)
                                 pages->remove(parts.at(1).toInt());
                             else if (op == QLatin1String("move") && parts.size() >= 3)
                                 pages->move(parts.at(1).toInt(), parts.at(2).toInt());

                             else if (op == QLatin1String("merge") && parts.size() >= 2)
                                 pages->mergeFrom(QUrl::fromLocalFile(parts.at(1)));
                             else if (op == QLatin1String("extract") && parts.size() >= 3)
                                 pages->extractTo(QUrl::fromLocalFile(parts.at(1)),
                                                  parts.at(2).toInt(),
                                                  parts.size() >= 4 ? parts.at(3).toInt()
                                                                    : parts.at(2).toInt());

                             if (parts.contains(QLatin1String("undo")))
                                 pages->undo();
                         },
                         Qt::SingleShotConnection);
    }

    // Test hook: export. "text,<path>" or "images,<dir>,<dpi>".
    if (qEnvironmentVariableIsSet("LUMEN_EXPORT")) {
        const QStringList parts = qEnvironmentVariable("LUMEN_EXPORT").split(u',');
        QObject::connect(controller, &lumen::DocumentController::documentChanged,
                         controller, [controller, parts] {
                             if (controller->pageCount() <= 0 || parts.size() < 2)
                                 return;

                             auto *exporter = controller->exporter();
                             if (parts.at(0) == QLatin1String("text")) {
                                 exporter->exportText(QUrl::fromLocalFile(parts.at(1)));
                             } else if (parts.at(0) == QLatin1String("images")) {
                                 if (parts.size() >= 3)
                                     exporter->setDpi(parts.at(2).toInt());
                                 exporter->exportImages(QUrl::fromLocalFile(parts.at(1)),
                                                        QStringLiteral("png"), -1, -1);
                             }
                         },
                         Qt::SingleShotConnection);
    }

    // Test hook: compress. "<path>,<dpi>".
    if (qEnvironmentVariableIsSet("LUMEN_COMPRESS")) {
        const QStringList parts = qEnvironmentVariable("LUMEN_COMPRESS").split(u',');
        if (parts.size() >= 2) {
            QObject::connect(controller, &lumen::DocumentController::documentChanged,
                             controller, [controller, parts] {
                                 if (controller->pageCount() > 0)
                                     controller->compressTo(QUrl::fromLocalFile(parts.at(0)),
                                                            parts.at(1).toInt());
                             },
                             Qt::SingleShotConnection);
        }
    }

    // Test hook: fill a form field. "<page>,<x>,<y>,<text>" -- clicks at the
    // point in PDF points, then types. Repeat the whole group with ';' between
    // to fill several fields in one run.
    if (qEnvironmentVariableIsSet("LUMEN_FORM_FILL")) {
        const QStringList groups = qEnvironmentVariable("LUMEN_FORM_FILL").split(u';');
        QObject::connect(controller, &lumen::DocumentController::documentChanged,
                         controller, [controller, groups] {
                             if (controller->pageCount() <= 0)
                                 return;

                             auto *forms = controller->forms();
                             for (const QString &group : groups) {
                                 const QStringList parts = group.split(u',');
                                 if (parts.size() < 3)
                                     continue;

                                 const int page = parts.at(0).toInt();
                                 const QPointF point(parts.at(1).toDouble(),
                                                     parts.at(2).toDouble());

                                 if (!forms->press(page, point, 0))
                                     continue;
                                 forms->release(page, point, 0);

                                 if (parts.size() >= 4 && !parts.at(3).isEmpty())
                                     forms->text(parts.at(3));
                             }
                             forms->clearFocus();

                             if (qEnvironmentVariableIsSet("LUMEN_SAVE_AS")) {
                                 controller->saveAs(QUrl::fromLocalFile(
                                     qEnvironmentVariable("LUMEN_SAVE_AS")));
                             }
                         },
                         Qt::SingleShotConnection);
    }

    installCaptureHook(engine, controller);

    result = app.exec();
    }

    lumen::PdfEngine::shutdown();
    return result;
}
