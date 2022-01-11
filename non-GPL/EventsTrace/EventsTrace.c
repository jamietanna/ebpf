// SPDX-License-Identifier: LicenseRef-Elastic-License-2.0

/*
 * Copyright 2021 Elasticsearch B.V. and/or licensed to Elasticsearch B.V. under
 * one or more contributor license agreements. Licensed under the Elastic
 * License 2.0; you may not use this file except in compliance with the Elastic
 * License 2.0.
 */

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>

#define __aligned_u64 __u64 __attribute__((aligned(8)))
#include <LibEbpfEvents.h>

static volatile sig_atomic_t exiting = 0;

static void sig_int(int signo)
{
    exiting = 1;
    fprintf(stdout, "Received SIGINT, Exiting...\n");
}

static void ebpf_file_event_path__tostring(struct ebpf_file_path path, char *pathbuf)
{
    strcpy(pathbuf, "/");
    for (int i = 0; i < path.patharray_len; i++) {
        strcat(pathbuf, path.path_array[i]);

        if (i != path.patharray_len - 1) {
            strcat(pathbuf, "/");
        }
    }
}

static void out_comma()
{
    printf(",");
}

static void out_newline()
{
    printf("\n");
}

static void out_object_start()
{
    printf("{");
}

static void out_object_end()
{
    printf("}");
}

static void out_event_type(const char *type)
{
    printf("\"event_type\":\"%s\"", type);
}

static void out_uint(const char *name, const unsigned long value)
{
    printf("\"%s\":%lu", name, value);
}

static void out_int(const char *name, const long value)
{
    printf("\"%s\":%ld", name, value);
}

static void out_string(const char *name, const char *value)
{
    printf("\"%s\":\"%s\"", name, value);
}

static void out_tty_dev(const char *name, struct ebpf_tty_dev *tty_dev)
{
    printf("\"%s\":", name);
    out_object_start();
    out_int("major", tty_dev->major);
    out_comma();
    out_int("minor", tty_dev->minor);
    out_object_end();
}

static void out_pid_info(const char *name, struct ebpf_pid_info *pid_info)
{
    printf("\"%s\":", name);
    out_object_start();
    out_int("tid", pid_info->tid);
    out_comma();
    out_int("tgid", pid_info->tgid);
    out_comma();
    out_int("ppid", pid_info->ppid);
    out_comma();
    out_int("pgid", pid_info->pgid);
    out_comma();
    out_int("sid", pid_info->tgid);
    out_comma();
    out_uint("start_time_ns", pid_info->start_time_ns);
    out_object_end();
}

static void out_cred_info(const char *name, struct ebpf_cred_info *cred_info)
{
    printf("\"%s\":", name);
    out_object_start();
    out_int("ruid", cred_info->ruid);
    out_comma();
    out_int("rgid", cred_info->rgid);
    out_comma();
    out_int("euid", cred_info->euid);
    out_comma();
    out_int("egid", cred_info->egid);
    out_comma();
    out_int("suid", cred_info->suid);
    out_comma();
    out_int("sgid", cred_info->sgid);
    out_object_end();
}

static void out_argv(const char *name, char *buf, size_t buf_size)
{
    printf("\"%s\":", name);

    char scratch_space[buf_size];

    // Buf is the argv array, with each argument delimited by a '\0', rework
    // it in a scratch space so it's a space-separated string we can print
    memcpy(scratch_space, buf, buf_size);

    for (int i = 0; i < buf_size; i++) {
        if (scratch_space[i] == '\0')
            scratch_space[i] = ' ';
    }

    for (int i = buf_size - 2; i >= 0; i--) {
        if (scratch_space[i] != ' ') {
            scratch_space[i + 1] = '\0';
            break;
        }
    }

    printf("\"%s\"", scratch_space);
}

static void out_file_delete(struct ebpf_file_delete_event *evt)
{
    out_object_start();
    out_event_type("FILE_DELETE");
    out_comma();

    out_pid_info("pid_info", &evt->pids);
    out_comma();

    char pathbuf[MAX_FILEPATH_LENGTH];
    ebpf_file_event_path__tostring(evt->path, pathbuf);
    out_string("path", pathbuf);

    out_object_end();
    out_newline();
}

static void out_process_fork(struct ebpf_process_fork_event *evt)
{
    out_object_start();
    out_event_type("PROCESS_FORK");
    out_comma();

    out_pid_info("parent_pids", &evt->parent_pids);
    out_comma();

    out_pid_info("child_pids", &evt->child_pids);

    out_object_end();
    out_newline();
}

static void out_process_exec(struct ebpf_process_exec_event *evt)
{
    out_object_start();
    out_event_type("PROCESS_EXEC");
    out_comma();

    out_pid_info("pids", &evt->pids);
    out_comma();

    out_cred_info("creds", &evt->creds);
    out_comma();

    out_tty_dev("ctty", &evt->ctty);
    out_comma();

    out_string("filename", evt->filename);
    out_comma();

    out_argv("argv", evt->argv, sizeof(evt->argv));

    out_object_end();
    out_newline();
}

static void out_process_setsid(struct ebpf_process_setsid_event *evt)
{
    out_object_start();
    out_event_type("PROCESS_SETSID");
    out_comma();

    out_pid_info("pids", &evt->pids);

    out_object_end();
    out_newline();
}

static void out_processs_exit(struct ebpf_process_exit_event *evt)
{
    out_object_start();
    out_event_type("PROCESS_EXIT");
    out_comma();

    out_pid_info("pids", &evt->pids);

    out_object_end();
    out_newline();
}

static int event_ctx_callback(struct ebpf_event_header *evt_hdr)
{
    switch (evt_hdr->type) {
    case EBPF_EVENT_PROCESS_FORK:
        out_process_fork((struct ebpf_process_fork_event *)evt_hdr);
        break;

    case EBPF_EVENT_PROCESS_EXEC:
        out_process_exec((struct ebpf_process_exec_event *)evt_hdr);
        break;

    case EBPF_EVENT_PROCESS_EXIT:
        out_processs_exit((struct ebpf_process_exit_event *)evt_hdr);
        break;

    case EBPF_EVENT_PROCESS_SETSID:
        out_process_setsid((struct ebpf_process_setsid_event *)evt_hdr);

    case EBPF_EVENT_FILE_DELETE:
        out_file_delete((struct ebpf_file_delete_event *)evt_hdr);
        break;
    }

    return 0;
}

int main(int argc, char const *argv[])
{
    int err                      = 0;
    struct FileEvents_bpf *probe = NULL;

    if (signal(SIGINT, sig_int) == SIG_ERR) {
        fprintf(stderr, "Failed to register SIGINT handler\n");
        goto cleanup;
    }

    struct ebpf_event_ctx *ctx;
    uint64_t features = EBPF_KERNEL_FEATURE_BPF;
    uint64_t events   = EBPF_EVENT_FILE_DELETE;
    err               = ebpf_event_ctx__new(&ctx, event_ctx_callback, features, events);
    if (err < 0) {
        fprintf(stderr, "Could not create event context: %d %s\n", err, strerror(-err));
        goto cleanup;
    }

    while (!exiting) {
        err = ebpf_event_ctx__next(ctx, 10);
        if (err < 0) {
            fprintf(stderr, "Failed to poll event context\n");
            break;
        }
    }

cleanup:
    if (probe) {
        ebpf_event_ctx__destroy(ctx);
    }
    return err != 0;
}
