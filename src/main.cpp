#include "HttpsServer.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHostAddress>
#include <QSslSocket>

namespace {

QString envOrDefault(const char *name, const QString &fallback)
{
    const QByteArray value = qgetenv(name);
    return value.isEmpty() ? fallback : QString::fromUtf8(value);
}

bool parsePositiveInt(const QString &value, int *result)
{
    if (!result) {
        return false;
    }

    bool ok = false;
    const int parsed = value.toInt(&ok);
    if (!ok || parsed <= 0) {
        return false;
    }

    *result = parsed;
    return true;
}

QString resolveExistingFilePath(const QString &inputPath)
{
    if (inputPath.isEmpty()) {
        return inputPath;
    }

    QFileInfo directInfo(inputPath);
    if (directInfo.exists()) {
        return directInfo.absoluteFilePath();
    }
    if (directInfo.isAbsolute()) {
        return directInfo.absoluteFilePath();
    }

    const QDir appDir(QCoreApplication::applicationDirPath());
    const QString candidateInAppDir = QDir::cleanPath(appDir.filePath(inputPath));
    const QString candidateInParentDir =
        QDir::cleanPath(appDir.filePath(QStringLiteral("../") + inputPath));
    const QString candidateInGrandParentDir =
        QDir::cleanPath(appDir.filePath(QStringLiteral("../../") + inputPath));

    QFileInfo appDirInfo(candidateInAppDir);
    if (appDirInfo.exists()) {
        return appDirInfo.absoluteFilePath();
    }

    QFileInfo parentDirInfo(candidateInParentDir);
    if (parentDirInfo.exists()) {
        return parentDirInfo.absoluteFilePath();
    }

    QFileInfo grandParentDirInfo(candidateInGrandParentDir);
    if (grandParentDirInfo.exists()) {
        return grandParentDirInfo.absoluteFilePath();
    }

    return inputPath;
}

}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName("DockerRoboshopServer");
    app.setApplicationVersion("1.0.0");

    QCommandLineParser parser;
    parser.setApplicationDescription("HTTPS server based on Qt 6.7.x and OpenSSL.");
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption hostOption(
        {"H", "host"},
        "Bind host, default from SERVER_HOST or 0.0.0.0",
        "host",
        envOrDefault("SERVER_HOST", "0.0.0.0"));

    const QCommandLineOption portOption(
        {"p", "port"},
        "Bind port, default from SERVER_PORT or 8443",
        "port",
        envOrDefault("SERVER_PORT", "8443"));

    const QCommandLineOption certOption(
        {"c", "cert"},
        "TLS certificate PEM file, default from SSL_CERT_FILE or certs/server.crt",
        "cert",
        envOrDefault("SSL_CERT_FILE", "certs/server.crt"));

    const QCommandLineOption keyOption(
        {"k", "key"},
        "TLS private key PEM file, default from SSL_KEY_FILE or certs/server.key",
        "key",
        envOrDefault("SSL_KEY_FILE", "certs/server.key"));

    const QCommandLineOption maxPendingOption(
        {"m", "max-pending"},
        "Maximum pending accepted sockets, default from SERVER_MAX_PENDING or 1024",
        "maxPending",
        envOrDefault("SERVER_MAX_PENDING", "1024"));

    const QCommandLineOption backlogOption(
        {"b", "listen-backlog"},
        "OS listen backlog size, default from SERVER_LISTEN_BACKLOG or 2048",
        "listenBacklog",
        envOrDefault("SERVER_LISTEN_BACKLOG", "2048"));

    const QCommandLineOption maxActiveOption(
        {"a", "max-active"},
        "Maximum active TLS sockets, default from SERVER_MAX_ACTIVE or 4096",
        "maxActive",
        envOrDefault("SERVER_MAX_ACTIVE", "4096"));

    parser.addOption(hostOption);
    parser.addOption(portOption);
    parser.addOption(certOption);
    parser.addOption(keyOption);
    parser.addOption(maxPendingOption);
    parser.addOption(backlogOption);
    parser.addOption(maxActiveOption);
    parser.process(app);

    if (!QSslSocket::supportsSsl()) {
        qCritical() << "Current runtime does not support OpenSSL.";
        return 1;
    }

    bool ok = false;
    const quint16 port = parser.value(portOption).toUShort(&ok);
    if (!ok || port == 0) {
        qCritical() << "Invalid port:" << parser.value(portOption);
        return 1;
    }

    QHostAddress hostAddress;
    if (!hostAddress.setAddress(parser.value(hostOption))) {
        qCritical() << "Invalid host:" << parser.value(hostOption);
        return 1;
    }

    int maxPending = 0;
    if (!parsePositiveInt(parser.value(maxPendingOption), &maxPending)) {
        qCritical() << "Invalid max-pending:" << parser.value(maxPendingOption);
        return 1;
    }

    int listenBacklog = 0;
    if (!parsePositiveInt(parser.value(backlogOption), &listenBacklog)) {
        qCritical() << "Invalid listen-backlog:" << parser.value(backlogOption);
        return 1;
    }

    int maxActive = 0;
    if (!parsePositiveInt(parser.value(maxActiveOption), &maxActive)) {
        qCritical() << "Invalid max-active:" << parser.value(maxActiveOption);
        return 1;
    }

    HttpsServer server;
    if (!server.configureConcurrency(maxPending, listenBacklog, maxActive)) {
        return 1;
    }

    const QString certPathInput = parser.value(certOption);
    const QString keyPathInput = parser.value(keyOption);
    const QString certPath = resolveExistingFilePath(certPathInput);
    const QString keyPath = resolveExistingFilePath(keyPathInput);

    if (certPath != certPathInput) {
        qInfo() << "Resolved certificate path:" << certPathInput << "->" << certPath;
    }
    if (keyPath != keyPathInput) {
        qInfo() << "Resolved key path:" << keyPathInput << "->" << keyPath;
    }

    if (!QFileInfo::exists(certPath)) {
        qCritical() << "Certificate file does not exist:" << certPath;
        return 1;
    }
    if (!QFileInfo::exists(keyPath)) {
        qCritical() << "Private key file does not exist:" << keyPath;
        return 1;
    }

    if (!server.loadSslFiles(certPath, keyPath)) {
        return 1;
    }

    if (!server.startListening(hostAddress, port)) {
        return 1;
    }

    qInfo() << "Server startup finished.";
    qInfo() << "GET  /health";
    qInfo() << "POST /api/echo";
    return app.exec();
}
