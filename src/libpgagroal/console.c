/*
 * Copyright (C) 2026 The pgagroal community
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list
 * of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this
 * list of conditions and the following disclaimer in the documentation and/or other
 * materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may
 * be used to endorse or promote products derived from this software without specific
 * prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* pgagroal */
#include <pgagroal.h>
#include <console.h>
#include <logging.h>
#include <memory.h>
#include <network.h>
#include <management.h>
#include <message.h>
#include <prometheus_client.h>
#include <security.h>
#include <utils.h>

/* system */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <limits.h>

/**
 * @struct console_metric
 * A lightweight metric structure optimized for console display
 */
struct console_metric
{
   char* name;                   /**< Full metric name */
   char* type;                   /**< Metric type (gauge, counter, histogram, etc.) */
   char* help;                   /**< Description of the metric */
   double value;                 /**< The numeric value of the metric */
   char* server;                 /**< Server name associated with this metric */
   struct console_label* labels; /**< Array of key/value labels */
   int label_count;              /**< Number of labels */
};

struct console_label
{
   char* key;
   char* value;
};

/**
 * @struct console_category
 * A category of related metrics, grouped by name prefix
 */
struct console_category
{
   char* name;                     /**< Category name */
   struct console_metric* metrics; /**< Array of metrics in this category */
   int metric_count;               /**< Number of metrics in category */
};

/**
 * @struct console_server
 * Server information for console display
 */
struct console_server
{
   char* name;  /**< Server name */
   bool active; /**< Whether server is active */
};

/**
 * @struct console_status
 * Management status information for display in console
 */
struct console_status
{
   char* status;                   /**< Overall status */
   char* version;                  /**< pgagroal version */
   int num_servers;                /**< Number of configured servers */
   char* last_updated;             /**< ISO timestamp of last update */
   struct console_server* servers; /**< Array of server information */
};

/**
 * @struct console_page
 * Complete console state for rendering a page
 */
struct console_page
{
   struct console_category* categories; /**< Array of metric categories */
   int category_count;                  /**< Number of categories */
   struct console_status* status;       /**< Management status info */
   time_t refresh_time;                 /**< When metrics were last refreshed */
   char* brand_name;                    /**< Application name for branding */
   char* metric_prefix;                 /**< Metric prefix to strip */
};

struct prefix_count
{
   char* prefix;
   int count;
};

struct category_candidate
{
   char* prefix;
   int count;
   int depth;
   double score;
};

/* Constants for category selection */
#define MIN_GROUP_SIZE                 2
#define MAX_DEPTH                      4

#define METRIC_LIST_INITIAL_CAP        64
#define PREFIX_COUNT_INITIAL_CAP       32
#define CATEGORY_CANDIDATE_INITIAL_CAP 16
#define CATEGORY_SELECT_INITIAL_CAP    16
#define TLS_PROBE_SIZE                 5
#define TLS_HANDSHAKE_BYTE             0x16
#define TLS_SSL2_BYTE                  0x80

/* Page routing constants */
#define PAGE_UNKNOWN 0
#define PAGE_HOME    1
#define PAGE_API     2
#define BAD_REQUEST  3

static int build_categories_from_bridge(struct prometheus_bridge* bridge, struct console_page* console);
static int record_prefix_counts(const char* metric_name, struct prefix_count** counts, int* size, int* capacity);
static int add_or_increment_prefix(struct prefix_count** counts, int* size, int* capacity, const char* prefix);
static int send_http_response(SSL* client_ssl, int client_fd, const char* content_type, void* body, size_t body_len, const char* page_name);
static int count_prefix_depth(const char* prefix);
static int build_category_candidates(struct prefix_count* counts, int size, struct category_candidate** candidates, int* candidate_count);
static int compare_candidates_by_score(const void* a, const void* b);
static char** select_global_categories(struct category_candidate* candidates, int candidate_count, int* selected_count);
static char* find_best_category(const char* metric_name, char** categories, int category_count);
static char* extract_category_prefix(char* metric_name);
static char* fallback_category_from_last_underscore(char* metric_name);
static struct console_category* find_or_create_category(struct console_page* console, char* category_name);
static int add_metric_to_category(struct console_category* category, struct console_metric* metric);
static struct console_metric* create_metric_from_prometheus_attrs(struct prometheus_metric* prom_metric, const char* display_name, struct prometheus_attributes* attrs);
static int extract_labels_from_prometheus_attrs(struct prometheus_attributes* attrs, struct console_metric* metric);
static int collect_simple_label_columns(struct console_category* category, char*** label_keys, int* label_key_count);
static const char* find_metric_label_value(struct console_metric* metric, const char* key);
static char* generate_metrics_table(struct console_category* category);
static char* generate_category_tabs(struct console_page* console);
static int resolve_page(struct message* msg);
static int badrequest_page(SSL* client_ssl, int client_fd);
static int home_page(SSL* client_ssl, int client_fd);
static int api_page(SSL* client_ssl, int client_fd);
static int console_init(int endpoint, const char* brand_name, const char* metric_prefix, struct console_page** result);
static int console_refresh_metrics(int endpoint, struct console_page* console);
static int console_refresh_status(struct console_page* console);
static int console_generate_html(struct console_page* console, char** html, size_t* html_size);
static int console_generate_json(struct console_page* console, char** json, size_t* json_size);
static int console_destroy(struct console_page* console);

static int
resolve_page(struct message* msg)
{
   char* from = NULL;
   int index;

   if (msg->length < 3 || strncmp((char*)msg->data, "GET", 3) != 0)
   {
      return BAD_REQUEST;
   }

   index = 4;
   from = (char*)msg->data + index;

   while (pgagroal_read_byte(msg->data + index) != ' ')
   {
      index++;
   }

   pgagroal_write_byte(msg->data + index, '\0');

   if (strcmp(from, "/") == 0 || strcmp(from, "/index.html") == 0)
   {
      return PAGE_HOME;
   }
   else if (strcmp(from, "/api") == 0 || strcmp(from, "/api/") == 0)
   {
      return PAGE_API;
   }
   return PAGE_UNKNOWN;
}

static int
send_http_response(SSL* client_ssl, int client_fd, const char* content_type, void* body, size_t body_len, const char* page_name)
{
   struct message msg;
   char response_header[512];
   int header_len;
   int status = MESSAGE_STATUS_OK;

   memset(&msg, 0, sizeof(struct message));
   header_len = pgagroal_snprintf(response_header, sizeof(response_header),
                                  "HTTP/1.1 200 OK\r\n"
                                  "Content-Type: %s\r\n"
                                  "Content-Length: %zu\r\n"
                                  "Connection: close\r\n"
                                  "\r\n",
                                  content_type,
                                  body_len);

   msg.data = response_header;
   msg.length = header_len;
   status = pgagroal_write_message(client_ssl, client_fd, &msg);
   if (status != MESSAGE_STATUS_OK)
   {
      pgagroal_log_error("console %s: failed to write header (status=%d, len=%d)", page_name, status, header_len);
   }

   if (status == MESSAGE_STATUS_OK && body_len > 0)
   {
      memset(&msg, 0, sizeof(struct message));
      msg.data = body;
      msg.length = body_len;
      status = pgagroal_write_message(client_ssl, client_fd, &msg);
      if (status != MESSAGE_STATUS_OK)
      {
         pgagroal_log_error("console %s: failed to write body (status=%d, len=%zu)", page_name, status, body_len);
      }
   }

   return status;
}

static int
badrequest_page(SSL* client_ssl, int client_fd)
{
   struct message msg;
   char* data = NULL;
   int status;

   memset(&msg, 0, sizeof(struct message));

   data = pgagroal_append(data, "HTTP/1.1 400 Bad Request\r\n");
   data = pgagroal_append(data, "Content-Length: 0\r\n");
   data = pgagroal_append(data, "Connection: close\r\n\r\n");

   msg.kind = 0;
   msg.length = strlen(data);
   msg.data = data;

   status = pgagroal_write_message(client_ssl, client_fd, &msg);

   free(data);

   return status;
}

static int
console_init(int endpoint, const char* brand_name, const char* metric_prefix, struct console_page** result)
{
   struct console_page* console = NULL;

   if (result == NULL)
   {
      pgagroal_log_error("Invalid parameters for console init");
      goto error;
   }

   console = (struct console_page*)malloc(sizeof(struct console_page));
   if (console == NULL)
   {
      pgagroal_log_error("Failed to allocate console page");
      goto error;
   }

   memset(console, 0, sizeof(struct console_page));

   console->brand_name = brand_name ? strdup(brand_name) : strdup("Metrics Console");
   console->metric_prefix = metric_prefix ? strdup(metric_prefix) : NULL;

   console->status = (struct console_status*)malloc(sizeof(struct console_status));
   if (console->status == NULL)
   {
      pgagroal_log_error("Failed to allocate console status");
      goto error;
   }

   memset(console->status, 0, sizeof(struct console_status));

   if (console_refresh_metrics(endpoint, console))
   {
      pgagroal_log_error("Failed to refresh metrics");
      goto error;
   }

   if (console_refresh_status(console))
   {
      pgagroal_log_warn("Failed to refresh status");
   }

   console->refresh_time = time(NULL);

   *result = console;

   return 0;

error:
   if (console != NULL)
   {
      console_destroy(console);
   }

   return 1;
}

static int
home_page(SSL* client_ssl, int client_fd)
{
   struct console_page* console = NULL;
   char* html = NULL;
   size_t html_size = 0;
   int status = 0;

   if (console_init(0, "pgagroal", "pgagroal_", &console))
   {
      pgagroal_log_error("Failed to initialize console");
      status = 1;
      goto error;
   }

   if (console_generate_html(console, &html, &html_size))
   {
      pgagroal_log_error("Failed to generate HTML");
      status = 1;
      goto error;
   }

   status = send_http_response(client_ssl, client_fd, "text/html; charset=utf-8", html, html_size, "home_page");

error:
   if (status != 0)
   {
      badrequest_page(client_ssl, client_fd);
   }
   free(html);
   if (console != NULL)
   {
      console_destroy(console);
   }

   return status;
}

static int
api_page(SSL* client_ssl, int client_fd)
{
   struct console_page* console = NULL;
   char* json = NULL;
   size_t json_size = 0;
   int status = 0;

   if (console_init(0, "pgagroal", "pgagroal_", &console))
   {
      pgagroal_log_error("Failed to initialize console for API");
      status = 1;
      goto error;
   }

   if (console_generate_json(console, &json, &json_size))
   {
      pgagroal_log_error("Failed to generate JSON");
      status = 1;
      goto error;
   }

   status = send_http_response(client_ssl, client_fd, "application/json; charset=utf-8", json, json_size, "api_page");

error:
   if (status != 0)
   {
      badrequest_page(client_ssl, client_fd);
   }
   free(json);
   if (console != NULL)
   {
      console_destroy(console);
   }

   return status;
}

static int
console_refresh_metrics(int endpoint, struct console_page* console)
{
   struct prometheus_bridge* bridge = NULL;
   struct main_configuration* config = NULL;
   int effective_endpoint = endpoint;
   const char* resolved_host = NULL;
   char original_host[MISC_LENGTH];
   bool override_host = false;

   if (console == NULL)
   {
      pgagroal_log_error("Invalid console parameter");
      goto error;
   }

   config = (struct main_configuration*)shmem;
   if (config != NULL)
   {
      if (config->common.metrics > 0)
      {
         if (effective_endpoint != 0)
         {
            pgagroal_log_debug("Console requested endpoint %d, using default endpoint 0", effective_endpoint);
         }

         effective_endpoint = 0;
         resolved_host = (strlen(config->common.host) == 0 || strcmp(config->common.host, "*") == 0 || strcmp(config->common.host, "0.0.0.0") == 0) ? "127.0.0.1" : config->common.host;

         if (strcmp(resolved_host, config->common.host) != 0)
         {
            memset(original_host, 0, sizeof(original_host));
            pgagroal_snprintf(original_host, sizeof(original_host), "%s", config->common.host);
            pgagroal_snprintf(config->common.host, MISC_LENGTH, "%s", resolved_host);
            override_host = true;
         }

         pgagroal_log_debug("Console metrics endpoint resolved to %s:%d", resolved_host, config->common.metrics);
      }
      else
      {
         pgagroal_log_error("No Prometheus endpoint configured and metrics listener disabled");
         goto error;
      }
   }

   if (pgagroal_prometheus_client_create_bridge(&bridge))
   {
      pgagroal_log_error("Failed to create Prometheus bridge");
      goto error;
   }

   if (pgagroal_prometheus_client_get(effective_endpoint, bridge))
   {
      pgagroal_log_error("Failed to fetch metrics from endpoint %d", effective_endpoint);
      goto error;
   }

   if (build_categories_from_bridge(bridge, console))
   {
      pgagroal_log_error("Failed to build categories from metrics");
      goto error;
   }

   if (override_host)
   {
      pgagroal_snprintf(config->common.host, MISC_LENGTH, "%s", original_host);
   }

   pgagroal_prometheus_client_destroy_bridge(bridge);

   console->refresh_time = time(NULL);

   return 0;

error:
   if (override_host && config != NULL)
   {
      pgagroal_snprintf(config->common.host, MISC_LENGTH, "%s", original_host);
   }

   if (bridge != NULL)
   {
      pgagroal_prometheus_client_destroy_bridge(bridge);
   }

   return 1;
}

static int
console_refresh_status(struct console_page* console)
{
   int socket = -1;
   uint8_t compression = MANAGEMENT_COMPRESSION_NONE;
   uint8_t encryption = MANAGEMENT_ENCRYPTION_NONE;
   struct json* payload = NULL;
   struct json* response = NULL;
   struct json* servers = NULL;
   struct main_configuration* config = NULL;
   char timestamp[128];
   time_t now;
   struct tm* time_info;
   int num_servers = 0;
   int status = 0;

   if (console == NULL || console->status == NULL)
   {
      pgagroal_log_error("Invalid console or status parameter");
      status = 1;
      goto error;
   }

   /* Initialize status with defaults first */
   if (console->status->status == NULL)
   {
      console->status->status = strdup("Unavailable");
   }
   if (console->status->version == NULL)
   {
      console->status->version = strdup(PGAGROAL_VERSION);
   }
   if (console->status->last_updated == NULL)
   {
      console->status->last_updated = strdup("Unknown");
   }
   if (console->status->num_servers == 0)
   {
      console->status->num_servers = 1;
   }

   config = (struct main_configuration*)shmem;
   if (config == NULL)
   {
      goto error;
   }

   if (pgagroal_connect_unix_socket(config->unix_socket_dir, MAIN_UDS, &socket))
   {
      pgagroal_log_debug("Failed to connect to management socket, using default values");
      goto error;
   }

   if (pgagroal_management_request_status(NULL, socket, compression, encryption, MANAGEMENT_OUTPUT_FORMAT_JSON))
   {
      pgagroal_log_warn("Failed to send status request");
      goto error;
   }

   if (pgagroal_management_read_json(NULL, socket, &compression, &encryption, &payload))
   {
      pgagroal_log_warn("Failed to read status response");
      goto error;
   }

   response = (struct json*)pgagroal_json_get(payload, MANAGEMENT_CATEGORY_RESPONSE);
   if (response == NULL)
   {
      pgagroal_log_warn("No response in payload");
      goto error;
   }

   char* server_version = (char*)pgagroal_json_get(response, MANAGEMENT_ARGUMENT_SERVER_VERSION);
   free(console->status->version);
   if (server_version != NULL)
   {
      console->status->version = strdup(server_version);
   }
   else
   {
      console->status->version = strdup(PGAGROAL_VERSION);
   }

   if (console->status->servers != NULL)
   {
      for (int i = 0; i < console->status->num_servers; i++)
      {
         free(console->status->servers[i].name);
      }
      free(console->status->servers);
      console->status->servers = NULL;
   }

   num_servers = (int32_t)(uintptr_t)pgagroal_json_get(response, MANAGEMENT_ARGUMENT_NUMBER_OF_SERVERS);
   console->status->num_servers = num_servers;

   servers = (struct json*)pgagroal_json_get(response, MANAGEMENT_ARGUMENT_SERVERS);
   if (servers != NULL && servers->type == JSONArray && num_servers > 0)
   {
      int server_idx = 0;
      struct json_iterator* iter = NULL;

      console->status->servers = (struct console_server*)malloc(num_servers * sizeof(struct console_server));
      if (console->status->servers != NULL)
      {
         memset(console->status->servers, 0, num_servers * sizeof(struct console_server));
      }

      if (pgagroal_json_iterator_create(servers, &iter) == 0)
      {
         while (pgagroal_json_iterator_next(iter) && server_idx < num_servers)
         {
            struct json* server = (struct json*)(iter->value->data);
            char* state = (char*)pgagroal_json_get(server, MANAGEMENT_ARGUMENT_STATE);
            bool active = state != NULL && (!strcmp(state, "Primary") || !strcmp(state, "Replica"));
            char* server_name = (char*)pgagroal_json_get(server, MANAGEMENT_ARGUMENT_SERVER);

            if (console->status->servers != NULL)
            {
               console->status->servers[server_idx].name = server_name ? strdup(server_name) : strdup("unknown");
               console->status->servers[server_idx].active = active;
            }

            server_idx++;
         }

         pgagroal_json_iterator_destroy(iter);
      }
   }

   free(console->status->status);
   console->status->status = strdup("Running");

   now = time(NULL);
   time_info = localtime(&now);
   free(console->status->last_updated);
   if (time_info != NULL)
   {
      strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", time_info);
      console->status->last_updated = strdup(timestamp);
   }
   else
   {
      console->status->last_updated = strdup("Unknown");
   }

   status = 0;

error:
   if (payload != NULL)
   {
      pgagroal_json_destroy(payload);
   }

   if (socket != -1)
   {
      pgagroal_disconnect(socket);
   }

   if (status != 0)
   {
      free(console->status->status);
      console->status->status = strdup("Unavailable");
   }

   return status;
}

static int
console_generate_html(struct console_page* console, char** html, size_t* html_size)
{
   char* tabs_html = NULL;
   char* final_html = NULL;
   int status = 0;

   if (console == NULL || html == NULL || html_size == NULL)
   {
      pgagroal_log_error("Invalid parameters for HTML generation");
      status = 1;
      goto error;
   }

   final_html = pgagroal_append(final_html,
                                "<!DOCTYPE html>\n"
                                "<html>\n"
                                "<head>\n"
                                "<meta charset=\"UTF-8\">\n"
                                "<title>Web Console</title>\n"
                                "<style>\n"
                                ":root { --bg: #fff; --text: #000; --border: #ccc; --header-bg: #fff; --header-border: #ddd; --th-bg: #eee; --hover-bg: #f5f5f5; --btn-bg: #f5f5f5; --btn-active-bg: #222; --btn-active-text: #fff; --shadow: rgba(0,0,0,0.03); --dropdown-shadow: rgba(0,0,0,0.08); }\n"
                                "body.dark-mode { --bg: #1a1a1a; --text: #e0e0e0; --border: #444; --header-bg: #222; --header-border: #333; --th-bg: #2a2a2a; --hover-bg: #333; --btn-bg: #2a2a2a; --btn-active-bg: #0d7377; --btn-active-text: #fff; --shadow: rgba(0,0,0,0.3); --dropdown-shadow: rgba(0,0,0,0.5); }\n"
                                "body { font-family: monospace; margin: 20px; background: var(--bg); color: var(--text); transition: background 0.3s, color 0.3s; }\n"
                                "h1 { border-bottom: 1px solid var(--text); }\n"
                                "h2 { margin-top: 12px; }\n"
                                "table { border-collapse: collapse; width: 100%%; margin: 10px 0; }\n"
                                "th, td { border: 1px solid var(--border); padding: 8px; text-align: left; }\n"
                                "th { background-color: var(--th-bg); font-weight: bold; }\n"
                                ".tab-bar { display: flex; gap: 18px; flex-wrap: wrap; align-items: center; justify-content: flex-start; margin: 12px 0 20px 0; }\n"
                                ".tab-btn { border: 1px solid var(--border); background: var(--btn-bg); padding: 6px 10px; cursor: pointer; border-radius: 4px; font-weight: 600; color: var(--text); }\n"
                                ".tab-btn.active { background: var(--btn-active-bg); color: var(--btn-active-text); }\n"
                                ".tab-panel { display: none; }\n"
                                ".tab-panel.active { display: block; }\n"
                                ".view-toggle { display: flex; gap: 8px; align-items: center; }\n"
                                ".view-btn { border: 1px solid var(--border); background: var(--btn-bg); padding: 6px 10px; cursor: pointer; border-radius: 4px; font-weight: 600; color: var(--text); }\n"
                                ".view-btn.active { background: var(--btn-active-bg); color: var(--btn-active-text); }\n"
                                ".col-simple-label { display: none; }\n"
                                ".simple .col-simple-label { display: table-cell; }\n"
                                ".simple .col-type, .simple .col-labels { display: none; }\n"
                                ".tab-bar label { margin: 0; font-weight: 600; }\n"
                                ".tab-bar select { padding: 6px 8px; border-radius: 4px; border: 1px solid var(--border); background: var(--bg); color: var(--text); }\n"
                                ".dropdown { position: relative; display: inline-block; min-width: 180px; }\n"
                                ".dropdown-btn { width: 100%%; text-align: left; padding: 6px 8px; border-radius: 4px; border: 1px solid var(--border); background: var(--bg); color: var(--text); cursor: pointer; font-family: inherit; }\n"
                                ".dropdown-menu { display: none; position: absolute; top: 100%%; left: 0; right: 0; background: var(--bg); border: 1px solid var(--border); border-radius: 4px; margin-top: 4px; z-index: 2; max-height: 220px; overflow-y: auto; box-shadow: 0 2px 6px var(--dropdown-shadow); }\n"
                                ".dropdown-menu.show { display: block; }\n"
                                ".dropdown-option { display: block; padding: 6px 8px; cursor: pointer; color: var(--text); }\n"
                                ".dropdown-option:hover { background: var(--hover-bg); }\n"
                                ".dropdown-divider { border: 0; border-top: 1px solid var(--border); margin: 4px 0; }\n"
                                ".header-box { position: relative; padding: 12px; background: var(--header-bg); border: 1px solid var(--header-border); border-radius: 8px; box-shadow: 0 1px 2px var(--shadow); margin-bottom: 14px; }\n"
                                ".theme-toggle { position: absolute; top: 12px; right: 12px; background: var(--btn-bg); border: 1px solid var(--border); padding: 8px 14px; border-radius: 6px; cursor: pointer; font-weight: 600; font-size: 14px; transition: all 0.2s; }\n"
                                ".theme-toggle:hover { background: var(--hover-bg); transform: scale(1.05); }\n"
                                ".refresh-btn { background: var(--btn-bg); border: 1px solid var(--border); padding: 6px 12px; border-radius: 6px; cursor: pointer; font-weight: 600; font-size: 12px; transition: all 0.2s; color: var(--text); margin-left: 8px; }\n"
                                ".refresh-btn:hover { background: var(--hover-bg); transform: scale(1.05); }\n"
                                ".refresh-btn.loading { opacity: 0.6; cursor: not-allowed; }\n"
                                ".tab-container { margin-top: 8px; }\n"
                                "</style>\n"
                                "</head>\n"
                                "<body>\n"
                                "<div class=\"header-box\">\n"
                                "<button id=\"theme-toggle\" class=\"theme-toggle\" title=\"Toggle dark mode\">🌙 Dark</button>\n"
                                "<h1>Web Console</h1>\n");
   final_html = pgagroal_format_and_append(final_html, "<p><strong>Service:</strong> %s | <strong>Version:</strong> %s | <strong>Updated:</strong> %s | <button id=\"refresh-btn\" class=\"refresh-btn\" title=\"Refresh all metrics\">Refresh</button></p> </div>\n",
                                           console->status->status ? console->status->status : "Unknown",
                                           console->status->version ? console->status->version : "Unknown",
                                           console->status->last_updated ? console->status->last_updated : "Never");

   tabs_html = generate_category_tabs(console);
   if (tabs_html != NULL)
   {
      final_html = pgagroal_append(final_html, tabs_html);
      free(tabs_html);
      tabs_html = NULL;
   }

   final_html = pgagroal_append(final_html,
                                "<script>\n"
                                "(function(){\n"
                                "  const themeToggle = document.getElementById('theme-toggle');\n"
                                "  const savedTheme = localStorage.getItem('theme');\n"
                                "  if(savedTheme === 'dark'){\n"
                                "    document.body.classList.add('dark-mode');\n"
                                "    themeToggle.textContent = '☀️ Light';\n"
                                "  }\n"
                                "  themeToggle.addEventListener('click', function(){\n"
                                "    document.body.classList.toggle('dark-mode');\n"
                                "    if(document.body.classList.contains('dark-mode')){\n"
                                "      themeToggle.textContent = '☀️ Light';\n"
                                "      localStorage.setItem('theme', 'dark');\n"
                                "    } else {\n"
                                "      themeToggle.textContent = '🌙 Dark';\n"
                                "      localStorage.setItem('theme', 'light');\n"
                                "    }\n"
                                "  });\n"
                                "  const refreshBtn = document.getElementById('refresh-btn');\n"
                                "  if (refreshBtn) {\n"
                                "    refreshBtn.addEventListener('click', function(){\n"
                                "      location.reload();\n"
                                "    });\n"
                                "  }\n"
                                "})();\n"
                                "</script>\n"
                                "</body>\n</html>\n");

   *html = final_html;
   *html_size = strlen(final_html);

   return 0;

error:
   if (tabs_html != NULL)
   {
      free(tabs_html);
   }
   if (final_html != NULL)
   {
      free(final_html);
   }
   return status;
}

static int
console_generate_json(struct console_page* console, char** json, size_t* json_size)
{
   char* json_buffer = NULL;
   int status = 0;

   if (console == NULL || json == NULL || json_size == NULL)
   {
      pgagroal_log_error("Invalid parameters for JSON generation");
      status = 1;
      goto error;
   }

   json_buffer = pgagroal_append(json_buffer, "{\"categories\":[");

   for (int i = 0; i < console->category_count; i++)
   {
      struct console_category* cat = &console->categories[i];

      if (i > 0)
      {
         json_buffer = pgagroal_append(json_buffer, ",");
      }

      json_buffer = pgagroal_format_and_append(json_buffer,
                                               "{\"name\":\"%s\",\"metrics\":[",
                                               cat->name);

      for (int j = 0; j < cat->metric_count; j++)
      {
         struct console_metric* metric = &cat->metrics[j];

         if (j > 0)
         {
            json_buffer = pgagroal_append(json_buffer, ",");
         }

         json_buffer = pgagroal_format_and_append(json_buffer,
                                                  "{\"name\":\"%s\",\"type\":\"%s\",\"value\":%.2f}",
                                                  metric->name,
                                                  metric->type,
                                                  metric->value);
      }

      json_buffer = pgagroal_append(json_buffer, "]}");
   }

   json_buffer = pgagroal_append(json_buffer, "]}");

   *json = json_buffer;
   *json_size = strlen(json_buffer);

   return 0;

error:
   if (json_buffer != NULL)
   {
      free(json_buffer);
   }
   return status;
}

static int
console_destroy(struct console_page* console)
{
   if (console != NULL)
   {
      if (console->categories != NULL)
      {
         for (int i = 0; i < console->category_count; i++)
         {
            struct console_category* cat = &console->categories[i];
            free(cat->name);

            if (cat->metrics != NULL)
            {
               for (int j = 0; j < cat->metric_count; j++)
               {
                  struct console_metric* metric = &cat->metrics[j];
                  free(metric->name);
                  free(metric->type);
                  free(metric->help);
                  free(metric->server);

                  if (metric->labels != NULL)
                  {
                     for (int k = 0; k < metric->label_count; k++)
                     {
                        free(metric->labels[k].key);
                        free(metric->labels[k].value);
                     }
                     free(metric->labels);
                  }
               }
               free(cat->metrics);
            }
         }
         free(console->categories);
      }

      if (console->status != NULL)
      {
         free(console->status->status);
         free(console->status->version);
         free(console->status->last_updated);

         if (console->status->servers != NULL)
         {
            for (int i = 0; i < console->status->num_servers; i++)
            {
               free(console->status->servers[i].name);
            }
            free(console->status->servers);
         }

         free(console->status);
      }

      free(console->brand_name);
      free(console->metric_prefix);
      free(console);
   }

   return 0;
}

/**
 * Helper: Build categories from Prometheus bridge
 */
static int
build_categories_from_bridge(struct prometheus_bridge* bridge, struct console_page* console)
{
   struct art_iterator* iter = NULL;
   struct prometheus_metric** metrics = NULL;
   int metric_count = 0;
   int metric_capacity = 0;

   struct prefix_count* prefix_counts = NULL;
   int prefix_count_size = 0;
   int prefix_count_capacity = 0;

   struct category_candidate* candidates = NULL;
   int candidate_count = 0;
   char** selected_categories = NULL;
   int selected_count = 0;

   int status = 0;

   if (bridge == NULL || console == NULL)
   {
      pgagroal_log_error("Invalid parameters for building categories");
      status = 1;
      goto error;
   }

   console->categories = NULL;
   console->category_count = 0;

   if (pgagroal_art_iterator_create(bridge->metrics, &iter))
   {
      pgagroal_log_error("Failed to create ART iterator");
      status = 1;
      goto error;
   }

   /* collect metrics and count shared prefixes */
   while (pgagroal_art_iterator_next(iter))
   {
      struct prometheus_metric* prom_metric = (struct prometheus_metric*)iter->value->data;
      const char* base_name = NULL;

      if (prom_metric == NULL || prom_metric->name == NULL)
      {
         continue;
      }

      base_name = prom_metric->name;
      if (strncmp(base_name, "pgagroal_", strlen("pgagroal_")) == 0)
      {
         base_name = base_name + strlen("pgagroal_");
      }

      if (metric_count == metric_capacity)
      {
         metric_capacity = metric_capacity == 0 ? METRIC_LIST_INITIAL_CAP : metric_capacity * 2;
         struct prometheus_metric** resized = realloc(metrics, metric_capacity * sizeof(struct prometheus_metric*));
         if (resized == NULL)
         {
            pgagroal_log_error("Failed to grow metric list");
            status = 1;
            goto error;
         }
         metrics = resized;
      }

      metrics[metric_count++] = prom_metric;

      if (record_prefix_counts(base_name, &prefix_counts, &prefix_count_size, &prefix_count_capacity))
      {
         pgagroal_log_error("Failed to record prefix counts");
         status = 1;
         goto error;
      }
   }

   pgagroal_art_iterator_destroy(iter);
   iter = NULL;

   /* Build and rank category candidates globally */
   if (build_category_candidates(prefix_counts, prefix_count_size, &candidates, &candidate_count))
   {
      pgagroal_log_error("Failed to build category candidates");
      status = 1;
      goto error;
   }

   selected_categories = select_global_categories(candidates, candidate_count, &selected_count);
   if (selected_categories == NULL)
   {
      pgagroal_log_warn("No categories selected, using fallback");
   }

   /* assign metrics to selected categories */
   for (int i = 0; i < metric_count; i++)
   {
      struct prometheus_metric* prom_metric = metrics[i];
      struct console_category* category = NULL;
      char* category_name = NULL;
      char* leaf_name = NULL;
      const char* base_name = NULL;
      struct deque_iterator* def_iter = NULL;

      if (prom_metric == NULL || prom_metric->name == NULL)
      {
         continue;
      }

      base_name = prom_metric->name;
      if (console->metric_prefix != NULL && strncmp(base_name, console->metric_prefix, strlen(console->metric_prefix)) == 0)
      {
         base_name = base_name + strlen(console->metric_prefix);
      }

      /* Find the best matching category from the globally selected set */
      category_name = find_best_category(base_name, selected_categories, selected_count);
      if (category_name == NULL)
      {
         category_name = extract_category_prefix((char*)base_name);
      }
      if (category_name == NULL)
      {
         category_name = strdup("uncategorized");
      }

      /* Leaf = remainder after category prefix */
      size_t cat_len = strlen(category_name);
      size_t base_len = strlen(base_name);
      if (base_len > cat_len + 1 && base_name[cat_len] == '_')
      {
         leaf_name = strdup(base_name + cat_len + 1);
      }
      else
      {
         leaf_name = strdup(base_name);
      }

      category = find_or_create_category(console, category_name);
      free(category_name);
      category_name = NULL;

      if (category == NULL)
      {
         pgagroal_log_warn("Failed to find/create category for %s", prom_metric->name);
         free(leaf_name);
         continue;
      }

      if (prom_metric->definitions != NULL && pgagroal_deque_size(prom_metric->definitions) > 0)
      {
         if (pgagroal_deque_iterator_create(prom_metric->definitions, &def_iter) == 0)
         {
            while (pgagroal_deque_iterator_next(def_iter))
            {
               struct prometheus_attributes* attrs = (struct prometheus_attributes*)def_iter->value->data;
               struct console_metric* console_metric = NULL;

               if (attrs == NULL)
               {
                  continue;
               }

               console_metric = create_metric_from_prometheus_attrs(prom_metric, leaf_name, attrs);
               if (console_metric == NULL)
               {
                  pgagroal_log_warn("Failed to create console metric for %s", prom_metric->name);
                  continue;
               }

               if (add_metric_to_category(category, console_metric))
               {
                  pgagroal_log_warn("Failed to add metric %s to category", prom_metric->name);
                  free(console_metric->name);
                  free(console_metric->type);
                  free(console_metric->help);
                  free(console_metric->server);
                  if (console_metric->labels != NULL)
                  {
                     for (int k = 0; k < console_metric->label_count; k++)
                     {
                        free(console_metric->labels[k].key);
                        free(console_metric->labels[k].value);
                     }
                     free(console_metric->labels);
                  }
                  free(console_metric);
                  continue;
               }

               free(console_metric);
            }
            pgagroal_deque_iterator_destroy(def_iter);
         }
      }

      free(leaf_name);
   }

error:
   if (iter != NULL)
   {
      pgagroal_art_iterator_destroy(iter);
   }

   if (prefix_counts != NULL)
   {
      for (int i = 0; i < prefix_count_size; i++)
      {
         free(prefix_counts[i].prefix);
      }
      free(prefix_counts);
   }

   if (metrics != NULL)
   {
      free(metrics);
   }

   if (candidates != NULL)
   {
      for (int i = 0; i < candidate_count; i++)
      {
         free(candidates[i].prefix);
      }
      free(candidates);
   }

   if (selected_categories != NULL)
   {
      for (int i = 0; i < selected_count; i++)
      {
         free(selected_categories[i]);
      }
      free(selected_categories);
   }

   return status;
}

/**
 * Helper: Extract category prefix from metric name
 * Example: "pg_stat_statements_calls" -> "pg_stat_statements"
 */
static char*
extract_category_prefix(char* metric_name)
{
   char* prefix = NULL;
   if (metric_name == NULL)
   {
      return NULL;
   }

   prefix = fallback_category_from_last_underscore(metric_name);

   return prefix;
}

static char*
fallback_category_from_last_underscore(char* metric_name)
{
   char* prefix = NULL;
   char* last_underscore = NULL;

   if (metric_name == NULL)
   {
      return NULL;
   }

   prefix = strdup(metric_name);
   if (prefix == NULL)
   {
      return NULL;
   }

   last_underscore = strrchr(prefix, '_');
   if (last_underscore != NULL)
   {
      *last_underscore = '\0';
   }

   return prefix;
}

/**
 * Helper: Find or create category
 */
static struct console_category*
find_or_create_category(struct console_page* console, char* category_name)
{
   /* Try to find existing */
   for (int i = 0; i < console->category_count; i++)
   {
      if (strcmp(console->categories[i].name, category_name) == 0)
      {
         return &console->categories[i];
      }
   }

   /* Create new category */
   struct console_category* new_categories = realloc(console->categories,
                                                     (console->category_count + 1) * sizeof(struct console_category));
   if (new_categories == NULL)
   {
      pgagroal_log_error("Failed to reallocate categories");
      return NULL;
   }

   console->categories = new_categories;
   struct console_category* new_cat = &console->categories[console->category_count];
   memset(new_cat, 0, sizeof(struct console_category));

   new_cat->name = strdup(category_name);
   if (new_cat->name == NULL)
   {
      pgagroal_log_error("Failed to duplicate category name");
      return NULL;
   }

   console->category_count++;

   return new_cat;
}

/**
 * Helper: Add metric to category
 */
static int
add_metric_to_category(struct console_category* category, struct console_metric* metric)
{
   struct console_metric* new_metrics = NULL;

   if (category == NULL || metric == NULL)
   {
      pgagroal_log_error("Invalid parameters for adding metric");
      return 1;
   }

   new_metrics = realloc(category->metrics,
                         (category->metric_count + 1) * sizeof(struct console_metric));
   if (new_metrics == NULL)
   {
      pgagroal_log_error("Failed to reallocate metrics");
      return 1;
   }

   category->metrics = new_metrics;
   memcpy(&category->metrics[category->metric_count], metric, sizeof(struct console_metric));
   category->metric_count++;

   return 0;
}

/**
 * Helper: Create console_metric from prometheus_metric using a specific attributes object
 */
static struct console_metric*
create_metric_from_prometheus_attrs(struct prometheus_metric* prom_metric, const char* display_name, struct prometheus_attributes* attrs)
{
   struct console_metric* metric = NULL;
   struct prometheus_value* value_data = NULL;

   if (prom_metric == NULL || attrs == NULL)
   {
      return NULL;
   }

   metric = (struct console_metric*)malloc(sizeof(struct console_metric));
   if (metric == NULL)
   {
      pgagroal_log_error("Failed to allocate console metric");
      return NULL;
   }

   memset(metric, 0, sizeof(struct console_metric));

   metric->name = strdup(display_name != NULL ? display_name : prom_metric->name);
   metric->type = strdup(prom_metric->type != NULL ? prom_metric->type : "gauge");
   metric->help = strdup(prom_metric->help != NULL ? prom_metric->help : "");
   metric->value = 0.0;
   metric->server = NULL;
   metric->label_count = 0;
   metric->labels = NULL;

   /* Get the last value (most recent timestamp) */
   if (attrs->values != NULL && pgagroal_deque_size(attrs->values) > 0)
   {
      value_data = (struct prometheus_value*)pgagroal_deque_peek_last(attrs->values, NULL);
      if (value_data != NULL && value_data->value != NULL)
      {
         metric->value = atof(value_data->value);
      }
   }

   if (extract_labels_from_prometheus_attrs(attrs, metric))
   {
      pgagroal_log_warn("Failed to extract labels for metric %s", prom_metric->name);
   }

   return metric;
}

static int
extract_labels_from_prometheus_attrs(struct prometheus_attributes* attrs, struct console_metric* metric)
{
   struct deque_iterator* attr_iter = NULL;
   int label_idx = 0;

   if (attrs == NULL || metric == NULL)
   {
      goto error;
   }

   if (attrs->attributes == NULL || pgagroal_deque_size(attrs->attributes) == 0)
   {
      return 0;
   }

   metric->label_count = pgagroal_deque_size(attrs->attributes);
   metric->labels = (struct console_label*)malloc(metric->label_count * sizeof(struct console_label));
   if (metric->labels == NULL)
   {
      metric->label_count = 0;
      goto error;
   }

   memset(metric->labels, 0, metric->label_count * sizeof(struct console_label));

   if (pgagroal_deque_iterator_create(attrs->attributes, &attr_iter))
   {
      free(metric->labels);
      metric->labels = NULL;
      metric->label_count = 0;
      goto error;
   }

   while (pgagroal_deque_iterator_next(attr_iter) && label_idx < metric->label_count)
   {
      struct prometheus_attribute* attr = (struct prometheus_attribute*)attr_iter->value->data;

      if (attr == NULL || attr->key == NULL || attr->value == NULL)
      {
         continue;
      }

      if (strcmp(attr->key, "server") == 0 || strcmp(attr->key, "name") == 0)
      {
         free(metric->server);
         metric->server = strdup(attr->value);
      }

      metric->labels[label_idx].key = strdup(attr->key);
      metric->labels[label_idx].value = strdup(attr->value);

      if (metric->labels[label_idx].key != NULL && metric->labels[label_idx].value != NULL)
      {
         label_idx++;
      }
      else
      {
         free(metric->labels[label_idx].key);
         free(metric->labels[label_idx].value);
         metric->labels[label_idx].key = NULL;
         metric->labels[label_idx].value = NULL;
      }
   }

   pgagroal_deque_iterator_destroy(attr_iter);
   metric->label_count = label_idx;

   return 0;
error:
   if (attr_iter != NULL)
   {
      pgagroal_deque_iterator_destroy(attr_iter);
   }
   return 1;
}

/**
 * Helper: Add or increment a prefix in the counts array
 */
static int
add_or_increment_prefix(struct prefix_count** counts, int* size, int* capacity, const char* prefix)
{
   int found = -1;

   if (counts == NULL || size == NULL || capacity == NULL || prefix == NULL)
   {
      return 1;
   }

   /* Find existing prefix */
   for (int j = 0; j < *size; j++)
   {
      if (strcmp((*counts)[j].prefix, prefix) == 0)
      {
         found = j;
         break;
      }
   }

   if (found == -1)
   {
      /* Prefix not found, add it */
      if (*size == *capacity)
      {
         *capacity = (*capacity == 0) ? PREFIX_COUNT_INITIAL_CAP : (*capacity * 2);
         struct prefix_count* resized = realloc(*counts, *capacity * sizeof(struct prefix_count));
         if (resized == NULL)
         {
            return 1;
         }
         *counts = resized;
      }

      (*counts)[*size].prefix = strdup(prefix);
      if ((*counts)[*size].prefix == NULL)
      {
         return 1;
      }
      (*counts)[*size].count = 1;
      (*size)++;
   }
   else
   {
      (*counts)[found].count += 1;
   }

   return 0;
}

/**
 * Helper: Increment shared prefix counts for a metric name
 */
static int
record_prefix_counts(const char* metric_name, struct prefix_count** counts, int* size, int* capacity)
{
   size_t len = 0;

   if (metric_name == NULL || counts == NULL || size == NULL || capacity == NULL)
   {
      return 1;
   }

   len = strlen(metric_name);

   /* Traverse the string and record prefixes at every underscore boundary */
   for (size_t i = 0; i < len; i++)
   {
      if (metric_name[i] == '_')
      {
         char* prefix = strndup(metric_name, i);
         if (prefix == NULL)
         {
            return 1;
         }

         if (add_or_increment_prefix(counts, size, capacity, prefix))
         {
            free(prefix);
            return 1;
         }
         free(prefix);
      }
   }

   /* Also record the full metric name as a prefix */
   if (add_or_increment_prefix(counts, size, capacity, metric_name))
   {
      return 1;
   }

   return 0;
}

/**
 * Helper: Count the depth (number of underscores) in a prefix
 */
static int
count_prefix_depth(const char* prefix)
{
   int depth = 0;
   if (prefix == NULL)
   {
      return 0;
   }

   for (int i = 0; prefix[i] != '\0'; i++)
   {
      if (prefix[i] == '_')
      {
         depth++;
      }
   }
   return depth;
}

/**
 * Helper: Build category candidates from prefix counts
 * Filters by MIN_GROUP_SIZE and MAX_DEPTH, calculates scores
 */
static int
build_category_candidates(struct prefix_count* counts, int size, struct category_candidate** candidates, int* candidate_count)
{
   struct category_candidate* cands = NULL;
   int count = 0;
   int capacity = 0;
   int status = 0;

   if (counts == NULL || candidates == NULL || candidate_count == NULL)
   {
      status = 1;
      goto error;
   }

   for (int i = 0; i < size; i++)
   {
      int depth = count_prefix_depth(counts[i].prefix);

      if (counts[i].count >= MIN_GROUP_SIZE && depth > 0 && depth <= MAX_DEPTH)
      {
         if (count == capacity)
         {
            capacity = capacity == 0 ? CATEGORY_CANDIDATE_INITIAL_CAP : capacity * 2;
            struct category_candidate* resized = realloc(cands, capacity * sizeof(struct category_candidate));
            if (resized == NULL)
            {
               pgagroal_log_error("Failed to grow category candidates");
               status = 1;
               goto error;
            }
            cands = resized;
         }

         cands[count].prefix = strdup(counts[i].prefix);
         if (cands[count].prefix == NULL)
         {
            pgagroal_log_error("Failed to allocate prefix string");
            status = 1;
            goto error;
         }
         cands[count].count = counts[i].count;
         cands[count].depth = depth;
         /* higher count and moderate depth preferred */
         cands[count].score = counts[i].count * (1.0 + depth * 0.2);
         count++;
      }
   }

   *candidates = cands;
   *candidate_count = count;
   return 0;

error:
   if (cands != NULL)
   {
      for (int i = 0; i < count; i++)
      {
         free(cands[i].prefix);
      }
      free(cands);
   }
   return status;
}

/**
 * Helper: Compare candidates by score (descending)
 */
static int
compare_candidates_by_score(const void* a, const void* b)
{
   const struct category_candidate* ca = (const struct category_candidate*)a;
   const struct category_candidate* cb = (const struct category_candidate*)b;

   if (cb->score > ca->score)
   {
      return 1;
   }
   else if (cb->score < ca->score)
   {
      return -1;
   }
   return 0;
}

/**
 * Helper: Select non-overlapping category prefixes globally
 * Sort by score descending, accept prefix only if not already covered by a longer accepted prefix
 */
static char**
select_global_categories(struct category_candidate* candidates, int candidate_count, int* selected_count)
{
   char** selected = NULL;
   int count = 0;
   int capacity = 0;

   if (candidates == NULL || candidate_count == 0 || selected_count == NULL)
   {
      *selected_count = 0;
      return NULL;
   }

   /* Sort candidates by score descending */
   qsort(candidates, candidate_count, sizeof(struct category_candidate), compare_candidates_by_score);

   for (int i = 0; i < candidate_count; i++)
   {
      int is_covered = 0;

      /* Check if this candidate is already covered by a longer accepted prefix */
      for (int j = 0; j < count; j++)
      {
         size_t sel_len = strlen(selected[j]);
         size_t cand_len = strlen(candidates[i].prefix);

         if (cand_len > sel_len && strncmp(candidates[i].prefix, selected[j], sel_len) == 0 && candidates[i].prefix[sel_len] == '_')
         {
            is_covered = 1;
            break;
         }
      }

      if (!is_covered)
      {
         if (count == capacity)
         {
            capacity = capacity == 0 ? CATEGORY_SELECT_INITIAL_CAP : capacity * 2;
            char** resized = realloc(selected, capacity * sizeof(char*));
            if (resized == NULL)
            {
               for (int k = 0; k < count; k++)
               {
                  free(selected[k]);
               }
               free(selected);
               return NULL;
            }
            selected = resized;
         }

         selected[count] = strdup(candidates[i].prefix);
         count++;
      }
   }

   *selected_count = count;
   return selected;
}

/**
 * Helper: Find the longest matching category for a metric name
 */
static char*
find_best_category(const char* metric_name, char** categories, int category_count)
{
   char* best = NULL;
   size_t best_len = 0;

   if (metric_name == NULL || categories == NULL || category_count == 0)
   {
      return NULL;
   }

   for (int i = 0; i < category_count; i++)
   {
      size_t cat_len = strlen(categories[i]);
      size_t name_len = strlen(metric_name);

      /* Check if category is a prefix of metric_name followed by _ */
      if (name_len > cat_len && strncmp(metric_name, categories[i], cat_len) == 0 && metric_name[cat_len] == '_')
      {
         if (cat_len > best_len)
         {
            best_len = cat_len;
            best = categories[i];
         }
      }
   }

   return best != NULL ? strdup(best) : NULL;
}

/**
 * Helper: Generate HTML table for metrics in a category
 */
static char*
generate_metrics_table(struct console_category* category)
{
   char* table_html = NULL;
   char** label_keys = NULL;
   int label_key_count = 0;

   if (category == NULL || category->metric_count == 0)
   {
      return pgagroal_append(NULL, "<p>No metrics</p>\n");
   }

   if (collect_simple_label_columns(category, &label_keys, &label_key_count))
   {
      pgagroal_log_warn("Failed to collect simple view label columns for category %s", category->name != NULL ? category->name : "unknown");
   }

   table_html = pgagroal_append(table_html,
                                "<table class=\"metrics-table\">\n"
                                "<tr><th class=\"col-name\">Name</th><th class=\"col-type\">Type</th><th class=\"col-value\">Value</th><th class=\"col-labels\">Labels</th>");

   for (int i = 0; i < label_key_count; i++)
   {
      table_html = pgagroal_format_and_append(table_html,
                                              "<th class=\"col-simple-label\">%s</th>",
                                              label_keys[i]);
   }

   table_html = pgagroal_append(table_html, "</tr>\n");

   for (int m_idx = 0; m_idx < category->metric_count; m_idx++)
   {
      struct console_metric* metric = &category->metrics[m_idx];
      char* labels_str = NULL;
      bool first_label = true;

      if (metric->label_count > 0)
      {
         for (int j = 0; j < metric->label_count; j++)
         {
            if (metric->labels[j].key == NULL || metric->labels[j].value == NULL)
            {
               continue;
            }

            if (!first_label)
            {
               labels_str = pgagroal_append(labels_str, ", ");
            }

            labels_str = pgagroal_append(labels_str, metric->labels[j].key);
            labels_str = pgagroal_append_char(labels_str, '=');
            labels_str = pgagroal_append(labels_str, metric->labels[j].value);
            first_label = false;
         }
      }

      char value_str[64];
      long long int_value = (long long)metric->value;
      if ((double)int_value == metric->value && int_value >= LLONG_MIN && int_value <= LLONG_MAX)
      {
         pgagroal_snprintf(value_str, sizeof(value_str), "%lld", int_value);
      }
      else
      {
         pgagroal_snprintf(value_str, sizeof(value_str), "%.2f", metric->value);
      }

      table_html = pgagroal_format_and_append(table_html,
                                              "<tr data-server=\"%s\"><td class=\"col-name\">%s</td><td class=\"col-type\">%s</td><td class=\"col-value\">%s</td><td class=\"col-labels\">%s</td>",
                                              metric->server != NULL ? metric->server : "all",
                                              metric->name,
                                              metric->type,
                                              value_str,
                                              labels_str != NULL ? labels_str : "");

      for (int k = 0; k < label_key_count; k++)
      {
         const char* label_value = find_metric_label_value(metric, label_keys[k]);
         table_html = pgagroal_format_and_append(table_html,
                                                 "<td class=\"col-simple-label\">%s</td>",
                                                 label_value != NULL ? label_value : "");
      }

      table_html = pgagroal_append(table_html, "</tr>\n");

      free(labels_str);
   }

   table_html = pgagroal_append(table_html, "</table>\n");

   if (label_keys != NULL)
   {
      for (int i = 0; i < label_key_count; i++)
      {
         free(label_keys[i]);
      }
      free(label_keys);
   }

   return table_html;
}

static int
collect_simple_label_columns(struct console_category* category, char*** label_keys, int* label_key_count)
{
   char** keys = NULL;
   int count = 0;

   if (category == NULL || label_keys == NULL || label_key_count == NULL)
   {
      return 1;
   }

   *label_keys = NULL;
   *label_key_count = 0;

   for (int m_idx = 0; m_idx < category->metric_count; m_idx++)
   {
      struct console_metric* metric = &category->metrics[m_idx];

      for (int l_idx = 0; l_idx < metric->label_count; l_idx++)
      {
         char* key = metric->labels[l_idx].key;
         bool exists = false;

         if (key == NULL || metric->labels[l_idx].value == NULL)
         {
            continue;
         }

         if (strcmp(key, "endpoint") == 0)
         {
            continue;
         }

         for (int i = 0; i < count; i++)
         {
            if (strcmp(keys[i], key) == 0)
            {
               exists = true;
               break;
            }
         }

         if (!exists)
         {
            char** resized = realloc(keys, (count + 1) * sizeof(char*));

            if (resized == NULL)
            {
               goto error;
            }

            keys = resized;
            keys[count] = strdup(key);
            if (keys[count] == NULL)
            {
               goto error;
            }
            count++;
         }
      }
   }

   *label_keys = keys;
   *label_key_count = count;

   return 0;

error:
   if (keys != NULL)
   {
      for (int i = 0; i < count; i++)
      {
         free(keys[i]);
      }
      free(keys);
   }

   return 1;
}

static const char*
find_metric_label_value(struct console_metric* metric, const char* key)
{
   if (metric == NULL || key == NULL)
   {
      return NULL;
   }

   for (int i = 0; i < metric->label_count; i++)
   {
      if (metric->labels[i].key == NULL || metric->labels[i].value == NULL)
      {
         continue;
      }

      if (strcmp(metric->labels[i].key, key) == 0)
      {
         return metric->labels[i].value;
      }
   }

   return NULL;
}

/**
 * Helper: Generate tab buttons and content sections for all categories
 */
static char*
generate_category_tabs(struct console_page* console)
{
   char* tabs_html = NULL;

   if (console == NULL || console->category_count == 0)
   {
      return strdup("<p>No metrics available</p>\n");
   }

   tabs_html = pgagroal_append(tabs_html, "<div class=\"tab-container\">\n<div class=\"tab-bar\">\n");

   {
      char view_buf[256];
      pgagroal_snprintf(view_buf, sizeof(view_buf),
                        "<div class=\"view-toggle\">\n"
                        "<label for=\"view-select\">View:</label>\n"
                        "<select id=\"view-select\">\n"
                        "<option value=\"simple\" selected>Simple</option>\n"
                        "<option value=\"detailed\">Advanced</option>\n"
                        "</select>\n"
                        "</div>\n");

      tabs_html = pgagroal_append(tabs_html, view_buf);
   }

   tabs_html = pgagroal_append(tabs_html, "<label for=\"category-select\">Category:</label>\n");
   tabs_html = pgagroal_append(tabs_html, "<select id=\"category-select\">\n");

   for (int i = 0; i < console->category_count; i++)
   {
      tabs_html = pgagroal_format_and_append(tabs_html,
                                             "<option value=\"cat-%d\"%s>%s</option>\n",
                                             i,
                                             i == 0 ? " selected" : "",
                                             console->categories[i].name);
   }

   tabs_html = pgagroal_append(tabs_html, "</select>\n");

   tabs_html = pgagroal_append(tabs_html,
                               "<label for=\"server-dropdown-btn\">Servers:</label>\n"
                               "<div class=\"dropdown\" id=\"server-dropdown\">\n"
                               "<button type=\"button\" id=\"server-dropdown-btn\" class=\"dropdown-btn\">All Selected</button>\n"
                               "<div id=\"server-dropdown-menu\" class=\"dropdown-menu\">\n"
                               "<label class=\"dropdown-option\"><input type=\"checkbox\" id=\"server-all\" checked> <strong>All</strong></label>\n"
                               "<hr class=\"dropdown-divider\">\n");

   if (console->status && console->status->servers && console->status->num_servers > 0)
   {
      for (int s = 0; s < console->status->num_servers; s++)
      {
         const char* name = console->status->servers[s].name ? console->status->servers[s].name : "server";
         tabs_html = pgagroal_format_and_append(tabs_html,
                                                "<label class=\"dropdown-option\"><input type=\"checkbox\" class=\"server-item\" value=\"%s\" checked> %s</label>\n",
                                                name, name);
      }
   }
   else
   {
      tabs_html = pgagroal_append(tabs_html,
                                  "<label class=\"dropdown-option\"><input type=\"checkbox\" disabled> No servers</label>\n");
   }

   tabs_html = pgagroal_append(tabs_html, "</div>\n</div>\n");

   tabs_html = pgagroal_append(tabs_html, "</div>\n<div class=\"tab-panels\">\n");

   for (int i = 0; i < console->category_count; i++)
   {
      char* metrics_table = NULL;
      tabs_html = pgagroal_format_and_append(tabs_html,
                                             "<div class=\"tab-panel\" id=\"cat-%d\" style=\"display:%s\">\n<h2>%s</h2>\n",
                                             i,
                                             i == 0 ? "block" : "none",
                                             console->categories[i].name);

      metrics_table = generate_metrics_table(&console->categories[i]);
      if (metrics_table != NULL)
      {
         tabs_html = pgagroal_append(tabs_html, metrics_table);
         free(metrics_table);
      }

      tabs_html = pgagroal_append(tabs_html, "</div>\n");
   }

   tabs_html = pgagroal_append(tabs_html, "</div>\n</div>\n");

   tabs_html = pgagroal_append(tabs_html,
                               "<script>\n"
                               "(function(){\n"
                               "  const select = document.getElementById('category-select');\n"
                               "  const panels = document.querySelectorAll('.tab-panel');\n"
                               "  const container = document.querySelector('.tab-container');\n"
                               "  const viewButtons = document.querySelectorAll('.view-btn');\n"
                               "  function show(id){\n"
                               "    panels.forEach(p=>p.style.display = (p.id===id) ? 'block' : 'none');\n"
                               "  }\n"
                               "  select.addEventListener('change', function(){ show(this.value); });\n"
                               "  // View mode dropdown\n"
                               "  const viewSelect = document.getElementById('view-select');\n"
                               "  if(viewSelect){\n"
                               "    if(viewSelect.value === 'simple'){ container.classList.add('simple'); } else { container.classList.remove('simple'); }\n"
                               "    viewSelect.addEventListener('change', function(){\n"
                               "      if(this.value === 'simple'){ container.classList.add('simple'); } else { container.classList.remove('simple'); }\n"
                               "    });\n"
                               "  }\n"
                               "  // Server dropdown\n"
                               "  const serverBtn = document.getElementById('server-dropdown-btn');\n"
                               "  const serverMenu = document.getElementById('server-dropdown-menu');\n"
                               "  const serverAll = document.getElementById('server-all');\n"
                               "  const serverItems = document.querySelectorAll('.server-item');\n"
                               "  if(serverBtn && serverMenu && serverAll){\n"
                               "    serverBtn.addEventListener('click', function(e){\n"
                               "      e.stopPropagation();\n"
                               "      serverMenu.classList.toggle('show');\n"
                               "    });\n"
                               "    document.addEventListener('click', function(e){\n"
                               "      if(!e.target.closest('#server-dropdown')){\n"
                               "        serverMenu.classList.remove('show');\n"
                               "      }\n"
                               "    });\n"
                               "    function updateServerText(){\n"
                               "      const checked = document.querySelectorAll('.server-item:checked');\n"
                               "      if(serverAll.checked){\n"
                               "        serverBtn.textContent = 'All Selected';\n"
                               "      } else if(checked.length === 0){\n"
                               "        serverBtn.textContent = 'None Selected';\n"
                               "      } else {\n"
                               "        const vals = Array.from(checked).map(i => i.value);\n"
                               "        serverBtn.textContent = vals.join(', ');\n"
                               "      }\n"
                               "    }\n"
                               "    function filterMetricsByServer(){\n"
                               "      const checked = document.querySelectorAll('.server-item:checked');\n"
                               "      const selectedServers = Array.from(checked).map(i => i.value);\n"
                               "      const allRows = document.querySelectorAll('.metrics-table tr[data-server]');\n"
                               "      allRows.forEach(row => {\n"
                               "        const rowServer = row.getAttribute('data-server');\n"
                               "        if(serverAll.checked || selectedServers.includes(rowServer) || rowServer === 'all'){\n"
                               "          row.style.display = '';\n"
                               "        } else {\n"
                               "          row.style.display = 'none';\n"
                               "        }\n"
                               "      });\n"
                               "    }\n"
                               "    serverAll.addEventListener('change', function(){\n"
                               "      serverItems.forEach(i => { i.checked = serverAll.checked; });\n"
                               "      updateServerText();\n"
                               "      filterMetricsByServer();\n"
                               "    });\n"
                               "    serverItems.forEach(i => {\n"
                               "      i.addEventListener('change', function(){\n"
                               "        const checkedCount = document.querySelectorAll('.server-item:checked').length;\n"
                               "        serverAll.checked = (checkedCount === serverItems.length);\n"
                               "        serverAll.indeterminate = (checkedCount > 0 && checkedCount < serverItems.length);\n"
                               "        updateServerText();\n"
                               "        filterMetricsByServer();\n"
                               "      });\n"
                               "    });\n"
                               "    updateServerText();\n"
                               "    filterMetricsByServer();\n"
                               "  }\n"
                               "  if(select && select.options.length){ show(select.value); }\n"
                               "})();\n"
                               "</script>\n");

   return tabs_html;
}

void
pgagroal_console(SSL* client_ssl, int client_fd)
{
   struct main_configuration* config = (struct main_configuration*)shmem;
   struct message* msg = NULL;
   int page;
   int status = MESSAGE_STATUS_OK;

   pgagroal_start_logging();
   pgagroal_memory_init();

   if (client_ssl)
   {
      char buffer[TLS_PROBE_SIZE] = {0};

      recv(client_fd, buffer, TLS_PROBE_SIZE, MSG_PEEK);

      if ((unsigned char)buffer[0] == TLS_HANDSHAKE_BYTE || (unsigned char)buffer[0] == TLS_SSL2_BYTE) // SSL/TLS request
      {
         if (SSL_accept(client_ssl) <= 0)
         {
            pgagroal_log_error("Failed to accept SSL connection");
            goto error;
         }
      }
   }

   status = pgagroal_read_timeout_message(client_ssl, client_fd,
                                          (int)pgagroal_time_convert(config->common.authentication_timeout, FORMAT_TIME_S),
                                          &msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   page = resolve_page(msg);

   if (page == PAGE_HOME)
   {
      status = home_page(client_ssl, client_fd);
   }
   else if (page == PAGE_API)
   {
      status = api_page(client_ssl, client_fd);
   }
   else
   {
      status = badrequest_page(client_ssl, client_fd);
   }

error:
   pgagroal_close_ssl(client_ssl);
   pgagroal_disconnect(client_fd);

   pgagroal_memory_destroy();
   pgagroal_stop_logging();

   if (status == MESSAGE_STATUS_OK)
   {
      exit(0);
   }

   exit(1);
}
