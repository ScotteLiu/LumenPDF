#include "app/Settings.h"
#include "app/StateReport.h"
#include "app/TestFixtures.h"
#include "app/Timing.h"
#include "app/UpdateChecker.h"
#include "bridge/PrintController.h"
#include "bridge/AnnotationController.h"
#include "bridge/DocumentController.h"
#include "bridge/OutlineModel.h"
#include "bridge/ExportController.h"
#include "bridge/FormController.h"
#include "bridge/OcrController.h"
#include "bridge/PageOperations.h"
#include "bridge/RedactionController.h"
#include "bridge/SearchController.h"
#include "bridge/SelectionController.h"
#include "core/PdfEngine.h"
#include "platform/PlatformWindow.h"
#include "render/PageImageProvider.h"

#include <QGuiApplication>
#include <QIcon>
#include <QLibraryInfo>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <QTranslator>

namespace {

// Loads the interface language.
//
// `preferred` is a locale name from settings, or empty to follow the system --
// which is the default, because guessing English for everyone is exactly the
// assumption this project keeps having to unlearn.
//
// Two translators are installed: Qt's own, for the standard dialogs and text
// Qt itself produces, and ours. Installing them in this order means our
// strings win where both define one.
void installTranslations(QCoreApplication &app, const QString &preferred)
{
    const QLocale locale = preferred.isEmpty() ? QLocale::system() : QLocale(preferred);

    auto *qtTranslator = new QTranslator(&app);
    if (qtTranslator->load(locale, QStringLiteral("qtbase"), QStringLiteral("_"),
                           QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
        app.installTranslator(qtTranslator);
    }

    auto *appTranslator = new QTranslator(&app);
    if (appTranslator->load(locale, QStringLiteral("lumenpdf"), QStringLiteral("_"),
                            QStringLiteral(":/i18n"))) {
        app.installTranslator(appTranslator);
    }
}

// UI capture mode, used for screenshot review and visual regression checks.
//
// Set LUMEN_CAPTURE to an output .png path and the app grabs its own window
// once and exits. Grabbing from inside the process is the only reliable route
// here: an external screen capture needs an interactive desktop session, which
// automated runs do not have.
//
//   LUMEN_CAPTURE=work\ui-check\shot.png  LUMEN_CAPTURE_DELAY=3000  lumenpdf.exe file.pdf
void installCaptureHook(QQmlApplicationEngine &engine,
                        lumen::DocumentController *controller,
                        lumen::PlatformWindow *platform,
                        lumen::Settings *settings)
{
    const QByteArray shotTarget = qgetenv("LUMEN_CAPTURE");
    const QByteArray reportTarget = qgetenv("LUMEN_REPORT");
    if (shotTarget.isEmpty() && reportTarget.isEmpty())
        return;

    bool ok = false;
    const int delayMs = qEnvironmentVariableIntValue("LUMEN_CAPTURE_DELAY", &ok);

    QTimer::singleShot(ok && delayMs > 0 ? delayMs : 2500, &engine,
                       [&engine, shotTarget, reportTarget, controller, platform, settings] {
        int exitCode = 0;

        // The state report comes first: it is what the tests assert on, and it
        // should still be written even if grabbing the window fails.
        if (!reportTarget.isEmpty()) {
            const QString path = QString::fromLocal8Bit(reportTarget);
            if (lumen::report::write(path, controller, platform, settings)) {
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
    // First statement in the program: everything after this is measured, and
    // a startup number that quietly excludes part of startup is worthless.
    lumen::Timing::instance().start();

    // Qt Quick needs the GPU pipeline chosen before the application exists.
    // D3D11 is the safe default on Windows; the backend gets revisited when
    // macOS (Metal) and Linux (Vulkan/OpenGL) come online.
#ifdef Q_OS_WIN
    // LUMEN_GRAPHICS exists for continuous integration, where there is no GPU
    // and D3D11 falls back to a software rasteriser that is occasionally
    // absent. It is not a user-facing setting.
    const QByteArray backend = qgetenv("LUMEN_GRAPHICS");
    if (backend == "software")
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    else if (backend == "opengl")
        QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    else
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
#endif

    QGuiApplication app(argc, argv);

    // Fixture generation needs Qt's font database and QPdfWriter but no window,
    // so it runs and exits before anything else is set up.
    if (qEnvironmentVariableIsSet("LUMEN_MAKE_FIXTURES")) {
        const int written = lumen::fixtures::writeAll(
            qEnvironmentVariable("LUMEN_MAKE_FIXTURES"),
            qEnvironmentVariableIsSet("LUMEN_MAKE_LARGE"));
        return written > 0 ? 0 : 1;
    }

    QGuiApplication::setApplicationName(QStringLiteral("LumenPDF"));
    QGuiApplication::setOrganizationName(QStringLiteral("Lumen"));
    QGuiApplication::setApplicationVersion(QStringLiteral(LUMEN_VERSION));

    // Lumen ships its own component set; the built-in styles are never used.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    lumen::Timing::instance().mark("qguiapplication-ready");

    lumen::PdfEngine::initialize();
    lumen::Timing::instance().mark("pdfium-initialized");

    QGuiApplication::setFont(lumen::PlatformWindow::preferredUiFont());
    lumen::Timing::instance().mark("font-resolved");

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
    auto *settings = new lumen::Settings(&engine);
    installTranslations(app, settings->language());

    // The update checker shares the controller's network stack rather than
    // opening a second one; it is idle unless someone asks it to check.
    auto *network = new QNetworkAccessManager(&engine);
    auto *updates = new lumen::UpdateChecker(network, &engine);
    QObject::connect(updates, &lumen::UpdateChecker::quitRequested,
                     &app, &QCoreApplication::quit, Qt::QueuedConnection);

    qmlRegisterSingletonInstance("Lumen.Backend", 1, 0, "Document", controller);
    qmlRegisterSingletonInstance("Lumen.Backend", 1, 0, "Platform", platform);
    qmlRegisterSingletonInstance("Lumen.Backend", 1, 0, "Prefs", settings);
    qmlRegisterSingletonInstance("Lumen.Backend", 1, 0, "Updates", updates);

    // Remember where each document was left, and offer it back on reopening.
    QObject::connect(controller, &lumen::DocumentController::documentChanged,
                     settings, [controller, settings] {
                         if (controller->pageCount() > 0)
                             settings->noteOpened(controller->filePath());
                     });
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
    lumen::Timing::instance().mark("qml-loaded");

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

    // Test hook: double-click word selection. Either "page,x,y" in PDF points,
    // or "page,find:<text>" to click the middle of the first occurrence of
    // some text -- which is what the tests use, because a baked coordinate
    // follows the machine's font metrics and lands somewhere else on a runner.
    if (qEnvironmentVariableIsSet("LUMEN_SELECT_WORD")) {
        const QStringList parts = qEnvironmentVariable("LUMEN_SELECT_WORD").split(u',');
        if (parts.size() >= 2) {
            QObject::connect(controller, &lumen::DocumentController::documentChanged,
                             controller, [controller, parts] {
                                 if (controller->pageCount() <= 0)
                                     return;

                                 const int page = parts.at(0).toInt();
                                 QPointF point(-1, -1);

                                 if (parts.at(1).startsWith(QLatin1String("find:"))) {
                                     point = controller->pointOfText(
                                         page, parts.mid(1).join(u',').mid(5));
                                 } else if (parts.size() >= 3) {
                                     point = QPointF(parts.at(1).toDouble(),
                                                     parts.at(2).toDouble());
                                 }

                                 if (point.x() < 0) {
                                     qWarning("select-word: nothing at that target");
                                     return;
                                 }
                                 controller->selection()->selectWordAt(page, point);
                             },
                             Qt::SingleShotConnection);
        }
    }

    // Test hook: drive a drag-selection from the environment, as
    // "page,x1,y1,x2,y2" in PDF points with a top-left origin.
    // Three forms:
    //   "page,x1,y1,x2,y2"           a drag within one page
    //   "p1,x1,y1,p2,x2,y2"          a drag across pages -- without this, no
    //                                test could ever produce a multi-page
    //                                selection, and the cross-page branches of
    //                                SelectionController were unreachable
    //   "all"                        select the whole document
    if (qEnvironmentVariableIsSet("LUMEN_SELECT")) {
        const QString spec = qEnvironmentVariable("LUMEN_SELECT");
        const QStringList parts = spec.split(u',');
        if (spec == QLatin1String("all") || parts.size() == 5 || parts.size() == 6) {
            QObject::connect(controller, &lumen::DocumentController::documentChanged,
                             controller, [controller, spec, parts] {
                                 if (controller->pageCount() <= 0)
                                     return;
                                 auto *selection = controller->selection();

                                 if (spec == QLatin1String("all")) {
                                     selection->selectAll();
                                 } else if (parts.size() == 6) {
                                     selection->begin(parts.at(0).toInt(),
                                                      QPointF(parts.at(1).toDouble(),
                                                              parts.at(2).toDouble()));
                                     selection->extend(parts.at(3).toInt(),
                                                       QPointF(parts.at(4).toDouble(),
                                                               parts.at(5).toDouble()));
                                     selection->end();
                                 } else {
                                     const int page = parts.at(0).toInt();
                                     selection->begin(page, QPointF(parts.at(1).toDouble(),
                                                                    parts.at(2).toDouble()));
                                     selection->extend(page, QPointF(parts.at(3).toDouble(),
                                                                     parts.at(4).toDouble()));
                                     selection->end();
                                 }

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
                                 {
                                     // A partial redaction reports success and
                                     // a failure; the test needs to see both.
                                     QObject::connect(controller->redact(),
                                                      &lumen::RedactionController::failed,
                                                      qApp, [](const QString &reason) {
                                                          qWarning("redact-failed: %s",
                                                                   qPrintable(reason));
                                                      });
                                     QObject::connect(controller->redact(),
                                                      &lumen::RedactionController::flattenedPages,
                                                      qApp, [](int pages) {
                                                          qInfo("redact-flattened: %d", pages);
                                                      });
                                     controller->redact()->redactSelection();
                                 }

                                 if (qEnvironmentVariableIsSet("LUMEN_SAVE_AS")) {
                                     controller->saveAs(QUrl::fromLocalFile(
                                         qEnvironmentVariable("LUMEN_SAVE_AS")));
                                 }
                             },
                             Qt::SingleShotConnection);
        }
    }

    // Test hook: run page operations. Steps are separated by ';' and run in
    // order, so a whole undo/redo sequence is one run:
    //
    //   "rotate,<page>,<turns>"  "delete,<page>"  "move,<from>,<to>"
    //   "merge,<path>"           "extract,<path>,<from>[,<to>]"
    //   "undo"                   "redo"
    //
    //   LUMEN_PAGEOP="delete,1;undo;redo;undo"
    //
    // Sequencing is what makes redo() reachable at all -- it had no coverage,
    // so the stash-index write-back in PageOperations::redo() had never run.
    if (qEnvironmentVariableIsSet("LUMEN_PAGEOP")) {
        const QStringList steps = qEnvironmentVariable("LUMEN_PAGEOP").split(u';');
        QObject::connect(controller, &lumen::DocumentController::documentChanged,
                         controller, [controller, steps] {
                             if (controller->pageCount() <= 0 || steps.isEmpty())
                                 return;

                             auto *pages = controller->pages();

                             for (const QString &step : steps) {
                                 const QStringList parts = step.split(u',');
                                 if (parts.isEmpty())
                                     continue;
                                 const QString op = parts.at(0).trimmed();

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

                                 else if (op == QLatin1String("undo"))
                                     pages->undo();
                                 else if (op == QLatin1String("redo"))
                                     pages->redo();
                             }
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
                             // Deliberately does NOT clear focus here. The
                             // production save path has to flush the edited
                             // field itself -- it did not, and this hook's
                             // clearFocus() was hiding that from the suite.
                             // Leaving it out makes this case the regression
                             // guard for saveAs's flush.
                             if (qEnvironmentVariableIsSet("LUMEN_SAVE_AS")) {
                                 controller->saveAs(QUrl::fromLocalFile(
                                     qEnvironmentVariable("LUMEN_SAVE_AS")));
                             }
                         },
                         Qt::SingleShotConnection);
    }

    // Test hook: edit a text run. Either "<page>,<x>,<y>,<new text>" or
    // "<page>,find:<existing text>,<new text>" -- see LUMEN_SELECT_WORD above
    // for why the tests use the second form.
    if (qEnvironmentVariableIsSet("LUMEN_EDIT_TEXT")) {
        const QStringList parts = qEnvironmentVariable("LUMEN_EDIT_TEXT").split(u',');
        const bool byText = parts.size() >= 2
                            && parts.at(1).startsWith(QLatin1String("find:"));
        if (parts.size() >= (byText ? 3 : 4)) {
            QObject::connect(controller, &lumen::DocumentController::documentChanged,
                             controller, [controller, parts, byText] {
                                 if (controller->pageCount() <= 0)
                                     return;

                                 const int page = parts.at(0).toInt();
                                 const QPointF point =
                                     byText ? controller->pointOfText(page, parts.at(1).mid(5))
                                            : QPointF(parts.at(1).toDouble(),
                                                      parts.at(2).toDouble());

                                 if (point.x() < 0) {
                                     qWarning("edit-text: target text not found");
                                     return;
                                 }

                                 const QVariantMap run = controller->textRunAt(page, point);
                                 if (!run.value(QStringLiteral("valid")).toBool()) {
                                     qWarning("edit-text: no text run at that point");
                                     return;
                                 }

                                 // Everything after the target is the new text,
                                 // so it may contain commas itself.
                                 const QString text = parts.mid(byText ? 2 : 3).join(u',');
                                 controller->pages()->editText(
                                     page,
                                     run.value(QStringLiteral("objectIndex")).toInt(),
                                     text);

                                 if (parts.contains(QLatin1String("--undo")))
                                     controller->pages()->undo();

                                 if (qEnvironmentVariableIsSet("LUMEN_SAVE_AS")) {
                                     controller->saveAs(QUrl::fromLocalFile(
                                         qEnvironmentVariable("LUMEN_SAVE_AS")));
                                 }
                             },
                             Qt::SingleShotConnection);
        }
    }

    // Test hook: run OCR over the document, then optionally save.
    if (qEnvironmentVariableIsSet("LUMEN_OCR")) {
        const QString language = qEnvironmentVariable("LUMEN_OCR");
        QObject::connect(controller, &lumen::DocumentController::documentChanged,
                         controller, [controller, language] {
                             if (controller->pageCount() <= 0)
                                 return;

                             auto *ocr = controller->ocr();
                             if (language != QLatin1String("auto"))
                                 ocr->setLanguage(language);

                             // Saving has to wait for recognition, which runs
                             // on the worker pool.
                             QObject::connect(ocr, &lumen::OcrController::finished,
                                              controller, [controller](int, int) {
                                                  if (qEnvironmentVariableIsSet("LUMEN_SAVE_AS")) {
                                                      controller->saveAs(QUrl::fromLocalFile(
                                                          qEnvironmentVariable("LUMEN_SAVE_AS")));
                                                  }
                                              },
                                              Qt::SingleShotConnection);

                             ocr->recogniseDocument();
                         },
                         Qt::SingleShotConnection);
    }

    // Test hook: exercise the update checker's version ordering without a
    // network call. "a,b" prints the comparison result.
    if (qEnvironmentVariableIsSet("LUMEN_VERSION_COMPARE")) {
        const QStringList parts = qEnvironmentVariable("LUMEN_VERSION_COMPARE").split(u',');
        if (parts.size() == 2) {
            qInfo("version-compare: %d",
                  lumen::UpdateChecker::compareVersions(parts.at(0), parts.at(1)));
        }
    }

    // Test hook: run a release asset name through the sanitiser and print what
    // came back. Empty means rejected.
    if (qEnvironmentVariableIsSet("LUMEN_UPDATE_ASSET")) {
        const QString accepted = lumen::UpdateChecker::sanitiseAssetNameForTest(
            qEnvironmentVariable("LUMEN_UPDATE_ASSET"));
        qInfo("update-asset: [%s]", qPrintable(accepted));
    }

    // Test hook: drive the verify-and-promote step over a local file, with no
    // network. "<path>,<expected-sha256-hex>".
    if (qEnvironmentVariableIsSet("LUMEN_UPDATE_VERIFY")) {
        const QStringList parts = qEnvironmentVariable("LUMEN_UPDATE_VERIFY").split(u',');
        if (parts.size() >= 2) {
            QString promoted;
            QString reason;
            const bool ok = lumen::UpdateChecker::verifyAndPromote(
                parts.at(0), parts.at(1), &promoted, &reason);
            qInfo("update-verify: ok=%d promoted=[%s] reason=[%s]",
                  ok ? 1 : 0, qPrintable(promoted), qPrintable(reason));
        }
    }

    // Test hook: record a reading position, so a following run can assert that
    // it came back. The QML side does this on a timer as the user scrolls.
    if (qEnvironmentVariableIsSet("LUMEN_NOTE_PAGE")) {
        const int page = qEnvironmentVariableIntValue("LUMEN_NOTE_PAGE");
        QObject::connect(controller, &lumen::DocumentController::documentChanged,
                         settings, [controller, settings, page] {
                             if (controller->pageCount() > 0)
                                 settings->notePosition(controller->filePath(), page);
                         },
                         Qt::SingleShotConnection);
    }

    // Test hook: supply a password for an encrypted document. Answering the
    // request rather than passing it to open() is deliberate -- it exercises
    // the same Locked -> unlock path the dialog uses.
    if (qEnvironmentVariableIsSet("LUMEN_PASSWORD")) {
        const QString password = qEnvironmentVariable("LUMEN_PASSWORD");
        QObject::connect(controller, &lumen::DocumentController::passwordRequired,
                         controller, [controller, password](bool retry) {
                             if (retry) {
                                 qWarning("password: rejected");
                                 return; // Never loop on a wrong password.
                             }
                             controller->unlock(password);
                         });
    }

    // Test hook: print to a PDF. "<path>[,<from>,<to>]", pages zero-based.
    if (qEnvironmentVariableIsSet("LUMEN_PRINT")) {
        const QStringList parts = qEnvironmentVariable("LUMEN_PRINT").split(u',');
        QObject::connect(controller, &lumen::DocumentController::documentChanged,
                         controller, [controller, parts] {
                             if (controller->pageCount() <= 0 || parts.isEmpty())
                                 return;

                             auto *printer = controller->printer();
                             QObject::connect(printer, &lumen::PrintController::finished,
                                              qApp, [](int pages) {
                                                  qInfo("print: %d pages", pages);
                                              });
                             QObject::connect(printer, &lumen::PrintController::failed,
                                              qApp, [](const QString &reason) {
                                                  qWarning("print failed: %s",
                                                           qPrintable(reason));
                                              });

                             printer->printToFile(QUrl::fromLocalFile(parts.at(0)),
                                                  parts.size() >= 2 ? parts.at(1).toInt() : -1,
                                                  parts.size() >= 3 ? parts.at(2).toInt() : -1,
                                                  false, true);
                         },
                         Qt::SingleShotConnection);
    }

    installCaptureHook(engine, controller, platform, settings);

    result = app.exec();
    }

    lumen::PdfEngine::shutdown();
    return result;
}
