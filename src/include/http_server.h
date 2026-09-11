#ifndef PGAGROAL_HTTP_SERVER_H
#define PGAGROAL_HTTP_SERVER_H

#include <openssl/ssl.h>

#define PGAGROAL_HTTP_TLS_PROBE_SIZE  5
#define PGAGROAL_HTTP_TLS_HANDSHAKE   0x16
#define PGAGROAL_HTTP_TLS_SSL2        0x80

/* Forward declarations & Data Structures */

/**
 * @struct http_server_request
 * @brief Holds the parsed details of an incoming HTTP request.
 */
struct http_server_request {
   char method[16];  /**< HTTP request method (e.g., GET, POST, HEAD) */
   char path[256];   /**< Extracted request path (e.g., "/" or "/metrics") */
};

/**
 * @typedef http_handler_fn
 * @brief Function pointer signature for route handler functions.
 * 
 * @param ssl Pointer to the SSL connection context (or NULL for plain HTTP)
 * @param fd Client socket file descriptor
 * @return Operation status code (MESSAGE_STATUS_OK on success)
 */
typedef int (*http_handler_fn)(SSL* ssl, int fd);

/**
 * @struct http_route
 * @brief Maps a specific URI path to its corresponding handler function.
 */
struct http_route {
   const char* path;          /**< The target URI route path (e.g., "/metrics") */
   http_handler_fn handler;   /**< Pointer to the callback handler function */
};

/* Core Functions */

/**
 * @brief Detects TLS/SSL capability on an incoming socket and completes the handshake.
 *
 * Performs a non-destructive MSG_PEEK on the first 5 bytes of the payload:
 * - Completes the SSL handshake if the incoming stream is encrypted (TLS).
 * - Returns MESSAGE_STATUS_ZERO if a plain HTTP request lands on a TLS-enabled socket 
 *   (allowing the caller to handle HTTP-to-HTTPS redirects).
 *
 * @param ssl SSL connection handle pointer
 * @param fd Client socket file descriptor
 * @return MESSAGE_STATUS_OK if TLS handshake succeeds or if SSL is disabled
 *         MESSAGE_STATUS_ZERO if plain HTTP is detected on a TLS socket
 *         MESSAGE_STATUS_ERROR on socket peek error or handshake failure
 */
int pgagroal_http_server_ssl_accept(SSL* ssl, int fd);

/**
 * @brief Reads and parses the initial HTTP Request Line ("GET <path> HTTP/1.1").
 * 
 * Extracts the HTTP method and requested URI path into an allocated request structure.
 *
 * @param ssl SSL connection handle pointer (can be NULL)
 * @param fd Client socket file descriptor
 * @param req [Output] Pointer to store the newly allocated request structure
 * @return MESSAGE_STATUS_OK on successful parsing, or MESSAGE_STATUS_ERROR on timeout/malformed request
 */
int pgagroal_http_server_parse(SSL* ssl, int fd, struct http_server_request** req);

/**
 * @brief Routes an inbound request to its registered callback handler.
 *
 * Iterates through the provided `routes` array matching `req->path`. Executes 
 * the matching handler or automatically transmits a 404 Not Found response if no route matches.
 *
 * @param ssl SSL connection handle pointer (can be NULL)
 * @param fd Client socket file descriptor
 * @param req The parsed HTTP request
 * @param routes Array of registered application routes
 * @param n_routes Total number of routes in the array
 * @return Status returned by the executed route handler
 */
int pgagroal_http_server_dispatch(SSL* ssl, int fd, struct http_server_request* req, struct http_route* routes, int n_routes);

/**
 * @brief Frees the allocated memory for an HTTP request structure.
 *
 * @param req Pointer to the request object to be deallocated
 */
void pgagroal_http_request_free(struct http_server_request* req);

/* HTTP Response Helpers */

/**
 * @brief Sends an HTTP 301 Moved Permanently redirect response.
 *
 * Typically utilized for redirecting plain HTTP connection attempts to HTTPS.
 *
 * @param ssl SSL connection handle pointer (can be NULL)
 * @param fd Client socket file descriptor
 * @param location Target redirect URL string
 * @return MESSAGE_STATUS_OK on success
 */
int pgagroal_http_respond_redirect(SSL* ssl, int fd, const char* location);

/**
 * @brief Transmits a standard HTTP 200 OK response with a complete payload body.
 *
 * Automatically computes `Content-Length` and appends standard HTTP headers (`Content-Type`, `Date`).
 *
 * @param ssl SSL connection handle pointer (can be NULL)
 * @param fd Client socket file descriptor
 * @param content_type MIME type string (e.g., "text/html; charset=utf-8")
 * @param payload The complete response body buffer
 * @return MESSAGE_STATUS_OK on success
 */
int pgagroal_http_respond_200(SSL* ssl, int fd, const char* content_type, const char* payload);

/**
 * @brief Sends an HTTP 400 Bad Request error response.
 *
 * @param ssl SSL connection handle pointer (can be NULL)
 * @param fd Client socket file descriptor
 * @return MESSAGE_STATUS_OK on success
 */
int pgagroal_http_respond_400(SSL* ssl, int fd);

/**
 * @brief Sends an HTTP 404 Not Found error response.
 *
 * @param ssl SSL connection handle pointer (can be NULL)
 * @param fd Client socket file descriptor
 * @return MESSAGE_STATUS_OK on success
 */
int pgagroal_http_respond_404(SSL* ssl, int fd);

/* HTTP Chunked Response Helpers (For Streaming Data like Prometheus /metrics) */

/**
 * @brief Begins an HTTP chunked streaming response session.
 *
 * Writes initial headers specifying chunked transfer encoding (`Transfer-Encoding: chunked`).
 *
 * @param ssl SSL connection handle pointer (can be NULL)
 * @param fd Client socket file descriptor
 * @param content_type Content MIME type (e.g., "text/plain; version=0.0.4")
 * @return MESSAGE_STATUS_OK on success
 */
int pgagroal_http_respond_chunked_start(SSL* ssl, int fd, const char* content_type);

/**
 * @brief Writes a single data chunk to the active HTTP streaming session.
 *
 * Formats data according to the HTTP chunked framing specification (`<hex_size>\r\n<data>\r\n`).
 *
 * @param ssl SSL connection handle pointer (can be NULL)
 * @param fd Client socket file descriptor
 * @param data String chunk buffer to send
 * @return MESSAGE_STATUS_OK on success
 */
int pgagroal_http_respond_chunked_write(SSL* ssl, int fd, const char* data);

/**
 * @brief Finalizes an HTTP chunked transfer sequence.
 *
 * Sends the zero-length terminal chunk (`0\r\n\r\n`) indicating end-of-stream to the client.
 *
 * @param ssl SSL connection handle pointer (can be NULL)
 * @param fd Client socket file descriptor
 * @return MESSAGE_STATUS_OK on success
 */
int pgagroal_http_respond_chunked_end(SSL* ssl, int fd);

#endif