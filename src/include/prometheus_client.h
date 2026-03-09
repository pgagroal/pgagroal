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

#ifndef PGAGROAL_PROMETHEUS_CLIENT_H
#define PGAGROAL_PROMETHEUS_CLIENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <art.h>
#include <deque.h>

#include <time.h>

/**
 * @struct prometheus_bridge
 * Parsed representation of a Prometheus scrape result.
 */
struct prometheus_bridge
{
   struct art* metrics; /**< ART keyed by metric name, values are @ref prometheus_metric */
};

/**
 * @struct prometheus_metric
 * A single Prometheus metric family, including metadata and sample definitions.
 */
struct prometheus_metric
{
   char* name;                /**< Metric name */
   char* help;                /**< HELP text */
   char* type;                /**< TYPE value (counter, gauge, histogram, summary, ...) */
   struct deque* definitions; /**< Deque of @ref prometheus_attributes entries */
};

/**
 * @struct prometheus_attributes
 * A unique label-set for a metric family and the collected values for that label-set.
 */
struct prometheus_attributes
{
   struct deque* attributes; /**< Deque of @ref prometheus_attribute */
   struct deque* values;     /**< Deque of @ref prometheus_value */
};

/**
 * @struct prometheus_value
 * A time-stamped sample value.
 */
struct prometheus_value
{
   time_t timestamp; /**< Timestamp when the sample was observed */
   char* value;      /**< Sample value as text */
};

/**
 * @struct prometheus_attribute
 * A single Prometheus label key/value pair.
 */
struct prometheus_attribute
{
   char* key;   /**< Label key */
   char* value; /**< Label value */
};

/**
 * Create an empty bridge object used to hold scraped Prometheus metrics.
 *
 * @param bridge Output pointer that receives the allocated bridge.
 * @return 0 on success, otherwise 1.
 */
int
pgagroal_prometheus_client_create_bridge(struct prometheus_bridge** bridge);

/**
 * Destroy a bridge and all parsed metric data contained within it.
 *
 * @param bridge Bridge to destroy. NULL is accepted.
 * @return 0 on success, otherwise 1.
 */
int
pgagroal_prometheus_client_destroy_bridge(struct prometheus_bridge* bridge);

/**
 * Scrape the local metrics endpoint and populate the provided bridge.
 *
 * @param endpoint Reserved endpoint selector. Current implementation uses the configured metrics endpoint.
 * @param bridge Destination bridge to populate.
 * @return 0 on success, otherwise 1.
 */
int
pgagroal_prometheus_client_get(int endpoint, struct prometheus_bridge* bridge);

#ifdef __cplusplus
}
#endif

#endif
