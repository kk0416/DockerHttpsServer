#pragma once

#include <QAbstractSocket>
#include <QByteArray>
#include <QHash>
#include <QMap>
#include <QSslConfiguration>
#include <QString>
#include <QTcpServer>

class QSslError;
class QSslSocket;
class QJsonObject;
class QHostAddress;

/**
 * @brief Lightweight HTTPS server based on QTcpServer + QSslSocket.
 *
 * The server supports HTTP/1.x keep-alive and can process multiple requests per TLS connection.
 * Current public endpoints are implemented in `processRequest`.
 */
class HttpsServer : public QTcpServer
{
    Q_OBJECT

public:
    /**
     * @brief Constructs an HTTPS server instance.
     * @param parent Parent QObject used for Qt ownership.
     */
    explicit HttpsServer(QObject *parent = nullptr);

    /**
     * @brief Configures limits used for high-concurrency traffic.
     * @param maxPendingConnections Maximum accepted sockets queued by QTcpServer.
     * @param listenBacklogSize OS-level listen backlog hint.
     * @param maxActiveConnections Maximum active TLS client sockets tracked by this server.
     * @return `true` when all values are valid and applied.
     */
    bool configureConcurrency(
        int maxPendingConnections,
        int listenBacklogSize,
        int maxActiveConnections);

    /**
     * @brief Loads TLS certificate and private key from PEM files.
     * @param certPath Path to the certificate file in PEM format.
     * @param keyPath Path to the private key file in PEM format.
     * @return `true` when both files are readable and parsed successfully.
     */
    bool loadSslFiles(const QString &certPath, const QString &keyPath);

    /**
     * @brief Starts listening for incoming TLS connections.
     * @param host Local bind address.
     * @param port Local bind port.
     * @return `true` on success; `false` if SSL is not configured or listen fails.
     */
    bool startListening(const QHostAddress &host, quint16 port);

    /**
     * @brief Stops listening and closes all active client sockets.
     */
    void stopListening();

    /**
     * @brief Returns the loaded certificate file path.
     * @return Certificate file path passed to loadSslFiles().
     */
    QString certFilePath() const;

    /**
     * @brief Returns the loaded private key file path.
     * @return Key file path passed to loadSslFiles().
     */
    QString keyFilePath() const;

protected:
    /**
     * @brief Handle incoming TCP connection and start TLS handshake.
     * @param socketDescriptor Native socket descriptor.
     */
    void incomingConnection(qintptr socketDescriptor) override;

private slots:
    void slotSocketEncrypted();
    void slotSocketReadyRead();
    void slotSocketDisconnected();
    void slotSocketError(QAbstractSocket::SocketError socketError);
    void slotSslErrors(const QList<QSslError> &errors);

private:
    /**
     * @brief Parse result state for an HTTP request in stream buffer.
     */
    enum ParseResult
    {
        E_Incomplete,
        E_Complete,
        E_Invalid
    };

    /**
     * @brief Parsed HTTP request data.
     */
    struct HttpRequest
    {
        QByteArray method;
        QByteArray path;
        QByteArray version;
        QMap<QByteArray, QByteArray> headers;
        QByteArray body;
    };

    /**
     * @brief Per-connection receive state.
     */
    struct ConnectionContext
    {
        QByteArray buffer;
        int requestCount = 0;
    };

    ParseResult tryExtractRequest(
        QByteArray *buffer,
        HttpRequest *request,
        QByteArray *errorReason,
        int *errorStatusCode) const;
    bool parseRequestLine(const QByteArray &requestLine, HttpRequest *request) const;
    bool shouldKeepConnectionAlive(const HttpRequest &request, int requestCount) const;

    QByteArray processRequest(const HttpRequest &request, bool closeConnection) const;
    QByteArray buildResponse(
        int statusCode,
        const QByteArray &contentType,
        const QByteArray &body,
        bool closeConnection) const;
    QByteArray buildJsonResponse(int statusCode, const QJsonObject &obj, bool closeConnection) const;
    QByteArray statusReason(int statusCode) const;

    void updateAcceptingState();
    void cleanupConnection(QSslSocket *socket);

private:
    QHash<QSslSocket *, ConnectionContext> _connectionMap;
    QSslConfiguration _sslConfiguration;
    QString _certPath;
    QString _keyPath;
    int _maxPendingConnections = 1024;
    int _listenBacklogSize = 2048;
    int _maxActiveConnections = 4096;
    bool _acceptingPaused = false;
};
