#include "bridge/DocumentController.h"
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
void installCaptureHook(QQmlApplicationEngine &engine)
{
    const QByteArray target = qgetenv("LUMEN_CAPTURE");
    if (target.isEmpty())
        return;

    bool ok = false;
    const int delayMs = qEnvironmentVariableIntValue("LUMEN_CAPTURE_DELAY", &ok);

    QTimer::singleShot(ok && delayMs > 0 ? delayMs : 2500, &engine, [&engine, target] {
        const auto roots = engine.rootObjects();
        auto *window = roots.isEmpty() ? nullptr : qobject_cast<QQuickWindow *>(roots.first());
        if (!window) {
            qWarning("capture: no QQuickWindow root");
            QCoreApplication::exit(2);
            return;
        }

        const QImage shot = window->grabWindow();
        const QString path = QString::fromLocal8Bit(target);
        if (shot.isNull() || !shot.save(path)) {
            qWarning("capture: failed to write %s", qPrintable(path));
            QCoreApplication::exit(3);
            return;
        }

        qInfo("capture: wrote %s (%dx%d)", qPrintable(path), shot.width(), shot.height());
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
    QGuiApplication::setApplicationName(QStringLiteral("LumenPDF"));
    QGuiApplication::setOrganizationName(QStringLiteral("Lumen"));
    QGuiApplication::setApplicationVersion(QStringLiteral(LUMEN_VERSION));

    // Lumen ships its own component set; the built-in styles are never used.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    lumen::PdfEngine::initialize();

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

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, []() { QCoreApplication::exit(-1); },
                     Qt::QueuedConnection);

    engine.loadFromModule("App", "Main");

    // Open a file passed on the command line (or by the shell's "Open with").
    const QStringList args = QGuiApplication::arguments();
    if (args.size() > 1)
        controller->open(QUrl::fromLocalFile(args.at(1)));

    installCaptureHook(engine);

    const int result = app.exec();

    lumen::PdfEngine::shutdown();
    return result;
}
