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

    const int result = app.exec();

    lumen::PdfEngine::shutdown();
    return result;
}
