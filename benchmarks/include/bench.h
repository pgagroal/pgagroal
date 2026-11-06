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

#ifndef PGAGROAL_BENCH_H
#define PGAGROAL_BENCH_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Compare two runs of bench.sh (see compare.c)
 * @param argc The number of arguments
 * @param argv The arguments, after "compare"
 * @return 0 when compared, 1 on wrong arguments, 2 on bad or missing data
 */
int
bench_compare(int argc, char** argv);

/**
 * Refuse a build compiled with -O0, sanitizers or coverage
 * @param compile_commands The compile_commands.json of the build
 * @param src_dir The pgagroal source directory, ending in '/'
 * @return 0 if the build can be measured, otherwise 1
 */
int
bench_check_build(char* compile_commands, char* src_dir);

/**
 * Check that a TCP port can be bound on 127.0.0.1 and ::1
 * @param port The port
 * @return 0 if the port is free, otherwise 1
 */
int
bench_port_free(int port);

/**
 * Write the meta.json of a run
 * @param path The file
 * @param argc The number of key=value arguments
 * @param argv The key=value arguments
 * @return 0 upon success, otherwise 1
 */
int
bench_meta(char* path, int argc, char** argv);

#ifdef __cplusplus
}
#endif

#endif
