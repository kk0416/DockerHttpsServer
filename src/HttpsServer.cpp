#include "HttpsServer.h"

#include <QDateTime>
#include <QFile>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSslCertificate>
#include <QSslError>
#include <QSslKey>
#include <QSslSocket>
#include <QTcpSocket>
#include <QStringList>

namespace {

constexpr qint64 kMaxHeaderBytes = 16 * 1024;
constexpr qint64 kMaxBodyBytes = 1024 * 1024;
constexpr int kKeepAliveTimeoutSeconds = 15;
constexpr int kMaxRequestsPerConnection = 100;
const QByteArray kHeaderTerminator = "\r\n\r\n";

QByteArray buildErrorBody(int code, const QByteArray &message, const QByteArray &detail = {})
{
    QJsonObject obj;
    obj.insert("code", code);
    obj.insert("message", QString::fromUtf8(message));
    if (!detail.isEmpty()) {
        obj.insert("detail", QString::fromUtf8(detail));
    }
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

}

HttpsServer::HttpsServer(QObject *parent)
    : QTcpServer(parent)
{
}

bool HttpsServer::configureConcurrency(
    int maxPendingConnections,
    int listenBacklogSize,
    int maxActiveConnections)
{
    if (maxPendingConnections <= 0 || listenBacklogSize <= 0 || maxActiveConnections <= 0) {
        qCritical() << "Invalid concurrency settings:"
                    << "maxPendingConnections =" << maxPendingConnections
                    << "listenBacklogSize =" << listenBacklogSize
                    << "maxActiveConnections =" << maxActiveConnections;
        return false;
    }

    _maxPendingConnections = maxPendingConnections;
    _listenBacklogSize = listenBacklogSize;
    _maxActiveConnections = maxActiveConnections;
    setMaxPendingConnections(_maxPendingConnections);

    if (isListening()) {
        updateAcceptingState();
    }
    return true;
}

bool HttpsServer::loadSslFiles(const QString &certPath, const QString &keyPath)
{
    if (!QSslSocket::supportsSsl()) {
        qCritical() << "OpenSSL support is not available in this Qt build.";
        return false;
    }

    QFile certFile(certPath);
    if (!certFile.open(QIODevice::ReadOnly)) {
        qCritical() << "Failed to open cert file:" << certPath << certFile.errorString();
        return false;
    }
    const QSslCertificate cert(&certFile, QSsl::Pem);
    certFile.close();
    if (cert.isNull()) {
        qCritical() << "Invalid certificate file:" << certPath;
        return false;
    }

    QFile keyFile(keyPath);
    if (!keyFile.open(QIODevice::ReadOnly)) {
        qCritical() << "Failed to open key file:" << keyPath << keyFile.errorString();
        return false;
    }
    const QByteArray keyData = keyFile.readAll();
    keyFile.close();

    QSslKey key(keyData, QSsl::Rsa, QSsl::Pem);
    if (key.isNull()) {
        key = QSslKey(keyData, QSsl::Ec, QSsl::Pem);
    }
    if (key.isNull()) {
        qCritical() << "Invalid private key file:" << keyPath;
        return false;
    }

    _sslConfiguration = QSslConfiguration::defaultConfiguration();
    _sslConfiguration.setProtocol(QSsl::TlsV1_2OrLater);
    _sslConfiguration.setPeerVerifyMode(QSslSocket::VerifyNone);
    _sslConfiguration.setLocalCertificate(cert);
    _sslConfiguration.setPrivateKey(key);

    _certPath = certPath;
    _keyPath = keyPath;
    return true;
}

bool HttpsServer::startListening(const QHostAddress &host, quint16 port)
{
    if (_sslConfiguration.localCertificate().isNull() || _sslConfiguration.privateKey().isNull()) {
        qCritical() << "SSL configuration is not ready. Call loadSslFiles first.";
        return false;
    }

    setMaxPendingConnections(_maxPendingConnections);
    setListenBacklogSize(_listenBacklogSize);

    if (!listen(host, port)) {
        qCritical() << "Listen failed:" << errorString();
        return false;
    }

    _acceptingPaused = false;
    updateAcceptingState();
    qInfo() << "HTTPS server listening on" << serverAddress().toString() << ":" << serverPort();
    qInfo() << "Concurrency settings:"
            << "maxPendingConnections =" << _maxPendingConnections
            << "listenBacklogSize =" << _listenBacklogSize
            << "maxActiveConnections =" << _maxActiveConnections;
    return true;
}

void HttpsServer::stopListening()
{
    if (isListening()) {
        close();
    }

    const auto sockets = _connectionMap.keys();
    for (QSslSocket *socket : sockets) {
        if (!socket) {
            continue;
        }
        socket->disconnectFromHost();
        socket->deleteLater();
    }
    _connectionMap.clear();
    _acceptingPaused = false;
}

QString HttpsServer::certFilePath() const
{
    return _certPath;
}

QString HttpsServer::keyFilePath() const
{
    return _keyPath;
}

void HttpsServer::incomingConnection(qintptr socketDescriptor)
{
    if (_connectionMap.size() >= _maxActiveConnections) {
        QTcpSocket dropSocket;
        if (dropSocket.setSocketDescriptor(socketDescriptor)) {
            dropSocket.abort();
        } else {
            qWarning() << "Drop overloaded connection failed to attach socket descriptor.";
        }
        qWarning() << "Connection dropped due to active-connection limit:"
                   << _maxActiveConnections;
        updateAcceptingState();
        return;
    }

    auto *socket = new QSslSocket(this);
    if (!socket->setSocketDescriptor(socketDescriptor)) {
        qCritical() << "setSocketDescriptor failed:" << socket->errorString();
        socket->deleteLater();
        return;
    }

    socket->setSslConfiguration(_sslConfiguration);

    connect(socket, &QSslSocket::encrypted, this, &HttpsServer::slotSocketEncrypted);
    connect(socket, &QSslSocket::readyRead, this, &HttpsServer::slotSocketReadyRead);
    connect(socket, &QSslSocket::disconnected, this, &HttpsServer::slotSocketDisconnected);
    connect(socket, &QSslSocket::errorOccurred, this, &HttpsServer::slotSocketError);
    connect(socket, &QSslSocket::sslErrors, this, &HttpsServer::slotSslErrors);

    _connectionMap.insert(socket, ConnectionContext{});
    updateAcceptingState();
    socket->startServerEncryption();
}

void HttpsServer::slotSocketEncrypted()
{
    auto *socket = qobject_cast<QSslSocket *>(sender());
    if (!socket) {
        return;
    }
    qDebug() << "TLS handshake success from"
             << socket->peerAddress().toString() << ":" << socket->peerPort();
}

void HttpsServer::slotSocketReadyRead()
{
    auto *socket = qobject_cast<QSslSocket *>(sender());
    if (!socket) {
        return;
    }

    auto it = _connectionMap.find(socket);
    if (it == _connectionMap.end()) {
        return;
    }

    it->buffer.append(socket->readAll());
    while (true) {
        if (it->buffer.size() > (kMaxHeaderBytes + kMaxBodyBytes + kHeaderTerminator.size())) {
            qWarning() << "request exceeds max buffer size from"
                       << socket->peerAddress().toString() << ":" << socket->peerPort()
                       << "size:" << it->buffer.size();
            const QByteArray response = buildResponse(
                413,
                "application/json; charset=utf-8",
                buildErrorBody(413, "payload too large"),
                true);
            socket->write(response);
            socket->flush();
            socket->disconnectFromHost();
            return;
        }

        HttpRequest request;
        QByteArray parseError;
        int errorStatusCode = 400;
        const ParseResult result =
            tryExtractRequest(&it->buffer, &request, &parseError, &errorStatusCode);
        if (result == E_Incomplete) {
            return;
        }
        if (result == E_Invalid) {
            qWarning() << "invalid request from" << socket->peerAddress().toString() << ":"
                       << socket->peerPort() << "reason:" << parseError;
            const QByteArray response = buildResponse(
                errorStatusCode,
                "application/json; charset=utf-8",
                buildErrorBody(errorStatusCode, "bad request", parseError),
                true);
            socket->write(response);
            socket->flush();
            socket->disconnectFromHost();
            return;
        }

        ++it->requestCount;
        const bool closeConnection = !shouldKeepConnectionAlive(request, it->requestCount);
        const QByteArray response = processRequest(request, closeConnection);
        const qint64 wrote = socket->write(response);
        if (wrote < 0) {
            qCritical() << "socket write failed:" << socket->errorString();
            socket->disconnectFromHost();
            return;
        }

        if (closeConnection) {
            socket->flush();
            socket->disconnectFromHost();
            return;
        }

        if (it->buffer.isEmpty()) {
            return;
        }
    }
}

void HttpsServer::slotSocketDisconnected()
{
    auto *socket = qobject_cast<QSslSocket *>(sender());
    cleanupConnection(socket);
}

void HttpsServer::slotSocketError(QAbstractSocket::SocketError socketError)
{
    Q_UNUSED(socketError)

    auto *socket = qobject_cast<QSslSocket *>(sender());
    if (!socket) {
        return;
    }

    qWarning() << "socket error:" << socket->errorString();
}

void HttpsServer::slotSslErrors(const QList<QSslError> &errors)
{
    auto *socket = qobject_cast<QSslSocket *>(sender());
    if (!socket) {
        return;
    }

    QStringList messages;
    messages.reserve(errors.size());
    for (const QSslError &err : errors) {
        messages.append(err.errorString());
    }
    qWarning() << "SSL errors from" << socket->peerAddress().toString() << messages;
}

HttpsServer::ParseResult HttpsServer::tryExtractRequest(
    QByteArray *buffer,
    HttpRequest *request,
    QByteArray *errorReason,
    int *errorStatusCode) const
{
    if (!buffer || !request || !errorReason || !errorStatusCode) {
        return E_Invalid;
    }
    errorReason->clear();
    *errorStatusCode = 400;

    const qint64 headerEnd = buffer->indexOf(kHeaderTerminator);
    if (headerEnd < 0) {
        if (buffer->size() > kMaxHeaderBytes) {
            *errorReason = "request header too large";
            *errorStatusCode = 413;
            return E_Invalid;
        }
        return E_Incomplete;
    }

    if (headerEnd == 0) {
        *errorReason = "empty request line";
        return E_Invalid;
    }

    if (headerEnd > kMaxHeaderBytes) {
        *errorReason = "request header exceeds limit";
        *errorStatusCode = 413;
        return E_Invalid;
    }

    const QByteArray headerBlock = buffer->left(headerEnd);
    const QList<QByteArray> lines = headerBlock.split('\n');
    if (lines.isEmpty()) {
        *errorReason = "missing request line";
        return E_Invalid;
    }

    if (!parseRequestLine(lines.first().trimmed(), request)) {
        *errorReason = "invalid request line";
        return E_Invalid;
    }

    request->headers.clear();
    for (int i = 1; i < lines.size(); ++i) {
        const QByteArray line = lines.at(i).trimmed();
        if (line.isEmpty()) {
            continue;
        }

        const int colonPos = line.indexOf(':');
        if (colonPos <= 0) {
            continue;
        }

        const QByteArray key = line.left(colonPos).trimmed().toLower();
        const QByteArray value = line.mid(colonPos + 1).trimmed();
        request->headers.insert(key, value);
    }

    qint64 bodyLength = 0;
    const QByteArray contentLength = request->headers.value("content-length");
    if (!contentLength.isEmpty()) {
        bool ok = false;
        bodyLength = contentLength.toLongLong(&ok);
        if (!ok || bodyLength < 0) {
            *errorReason = "invalid content-length";
            return E_Invalid;
        }
        if (bodyLength > kMaxBodyBytes) {
            *errorReason = "content-length exceeds limit";
            *errorStatusCode = 413;
            return E_Invalid;
        }
    }

    const qint64 fullSize = headerEnd + kHeaderTerminator.size() + bodyLength;
    if (buffer->size() < fullSize) {
        return E_Incomplete;
    }

    request->body = buffer->mid(headerEnd + kHeaderTerminator.size(), bodyLength);
    buffer->remove(0, fullSize);
    return E_Complete;
}

bool HttpsServer::parseRequestLine(const QByteArray &requestLine, HttpRequest *request) const
{
    if (!request) {
        return false;
    }

    const QList<QByteArray> parts = requestLine.split(' ');
    if (parts.size() < 3) {
        return false;
    }

    request->method = parts.at(0).trimmed().toUpper();
    request->path = parts.at(1).trimmed();
    request->version = parts.at(2).trimmed().toUpper();

    if (request->method.isEmpty() || request->path.isEmpty() || request->version.isEmpty()) {
        return false;
    }
    if (!request->path.startsWith('/')) {
        return false;
    }
    return request->version.startsWith("HTTP/1.");
}

bool HttpsServer::shouldKeepConnectionAlive(const HttpRequest &request, int requestCount) const
{
    if (requestCount >= kMaxRequestsPerConnection) {
        return false;
    }

    const QByteArray connection = request.headers.value("connection").trimmed().toLower();
    if (request.version == "HTTP/1.0") {
        return connection == "keep-alive";
    }
    return connection != "close";
}

QByteArray HttpsServer::processRequest(const HttpRequest &request, bool closeConnection) const
{
    const QByteArray method = request.method.toUpper();

    QString path = QString::fromUtf8(request.path);
    const int queryPos = path.indexOf('?');
    if (queryPos >= 0) {
        path = path.left(queryPos);
    }

    if (method == "GET" && path == "/") {
        QJsonObject root;
        root.insert("service", "DockerRoboshopServer");
        root.insert("protocol", "https");
        root.insert("status", "running");
        root.insert("health", "/health");
        root.insert("echo", "/api/echo");
        return buildJsonResponse(200, root, closeConnection);
    }

    if (method == "GET" && path == "/health") {
        QJsonObject health;
        health.insert("status", "ok");
        health.insert("service", "DockerRoboshopServer");
        health.insert("timeUtc", QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        return buildJsonResponse(200, health, closeConnection);
    }

    if (method == "GET" && path == "/api/version") {
        QJsonObject version;
        version.insert("name", "DockerRoboshopServer");
        version.insert("version", "1.0.0");
        version.insert("qt", QString::fromLatin1(QT_VERSION_STR));
        return buildJsonResponse(200, version, closeConnection);
    }

    if (path == "/api/echo" && method != "POST") {
        QJsonObject result;
        result.insert("code", 405);
        result.insert("message", "method not allowed");
        return buildJsonResponse(405, result, closeConnection);
    }

    if (method == "POST" && path == "/api/echo") {
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(request.body, &parseError);

        QJsonObject payload;
        if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
            payload = doc.object();
        } else {
            payload.insert("rawBody", QString::fromUtf8(request.body));
        }

        QJsonObject result;
        result.insert("code", 0);
        result.insert("message", "ok");
        result.insert("method", QString::fromUtf8(request.method));
        result.insert("path", path);
        result.insert("data", payload);
        return buildJsonResponse(200, result, closeConnection);
    }

    QJsonObject notFound;
    notFound.insert("code", 404);
    notFound.insert("message", "not found");
    notFound.insert("path", path);
    return buildJsonResponse(404, notFound, closeConnection);
}

QByteArray HttpsServer::buildResponse(
    int statusCode,
    const QByteArray &contentType,
    const QByteArray &body,
    bool closeConnection) const
{
    QByteArray response;
    response += "HTTP/1.1 ";
    response += QByteArray::number(statusCode);
    response += " ";
    response += statusReason(statusCode);
    response += "\r\n";

    response += "Content-Type: ";
    response += contentType;
    response += "\r\n";

    response += "Content-Length: ";
    response += QByteArray::number(body.size());
    response += "\r\n";

    response += "Connection: ";
    response += closeConnection ? "close\r\n" : "keep-alive\r\n";
    if (!closeConnection) {
        response += "Keep-Alive: timeout=";
        response += QByteArray::number(kKeepAliveTimeoutSeconds);
        response += ", max=";
        response += QByteArray::number(kMaxRequestsPerConnection);
        response += "\r\n";
    }
    response += "Server: DockerRoboshopServer/1.0.0\r\n";
    response += "Date: ";
    response += QDateTime::currentDateTimeUtc().toString(Qt::RFC2822Date).toUtf8();
    response += "\r\n\r\n";
    response += body;

    return response;
}

QByteArray HttpsServer::buildJsonResponse(
    int statusCode,
    const QJsonObject &obj,
    bool closeConnection) const
{
    const QByteArray jsonBody = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    return buildResponse(statusCode, "application/json; charset=utf-8", jsonBody, closeConnection);
}

QByteArray HttpsServer::statusReason(int statusCode) const
{
    switch (statusCode) {
    case 200:
        return "OK";
    case 400:
        return "Bad Request";
    case 404:
        return "Not Found";
    case 405:
        return "Method Not Allowed";
    case 413:
        return "Payload Too Large";
    default:
        return "Internal Server Error";
    }
}

void HttpsServer::updateAcceptingState()
{
    if (!isListening()) {
        return;
    }

    const bool shouldPause = (_connectionMap.size() >= _maxActiveConnections);
    if (shouldPause && !_acceptingPaused) {
        pauseAccepting();
        _acceptingPaused = true;
        qWarning() << "Accepting paused at active connections:" << _connectionMap.size();
        return;
    }

    if (!shouldPause && _acceptingPaused) {
        resumeAccepting();
        _acceptingPaused = false;
        qInfo() << "Accepting resumed at active connections:" << _connectionMap.size();
    }
}

void HttpsServer::cleanupConnection(QSslSocket *socket)
{
    if (!socket) {
        return;
    }

    _connectionMap.remove(socket);
    updateAcceptingState();
    socket->deleteLater();
}
