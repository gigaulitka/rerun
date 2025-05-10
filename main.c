// See LICENSE file for copyright and license details.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#define _GNU_SOURCE  // For getopt_long
#include <getopt.h>

#include <microhttpd.h>


struct Metrics {
    int success_total;
    int failure_total;
};


static unsigned int verbose = 0;


const char *help_message =
    "Usage: rerun [OPTIONS] -- <command> [args...]\n"
    "\n"
    "Restart and monitor the specified command.\n"
    "\n"
    "Options:\n"
    "  -r, --repeat <int>              Number of times to repeat the command (including failed executions).\n"
    "                                  If not specified, the command is restarted indefinitely.\n"
    "  -f, --retries-on-failure <int>  Number of retries allowed on failure (default: 0).\n"
    "  -s, --metrics-host <string>     Host to run embedded metrics HTTP server on.\n"
    "                                  If not specified, the server will not be started.\n"
    "  -p, --metrics-port <int>        Port for the metrics HTTP server (default: 3535).\n"
    "  -v, --verbose                   Enable debug output.\n"
    "      --help                      Show this help message and exit.\n"
    "\n"
    "The embedded HTTP server provides Prometheus-compatible metrics (if enabled) at:\n"
    "  GET /metrics\n"
    "    success_total <int>\n"
    "    failures_total <int>\n"
    "\n"
    "Examples:\n"
    "  rerun --repeat 5 -- ./script.sh\n"
    "  rerun --repeat 10 --retries-on-failure 2 -- ./script.sh arg1 arg2\n"
    "  rerun --metrics-host 127.0.0.1 --metrics-port 8080 -- ./script.sh\n";


const char *usage_message =
    "Usage: rerun [--repeat <int>] [--retries-on-failure <int>] [--metrics-host <string>] [--metrics-port <int>] [--verbose]\n";


#define LOG(fmt, ...) \
    do { \
        if (verbose) { \
            fprintf(stderr, "[DEBUG] rerun: " fmt "\n", ##__VA_ARGS__); \
        } \
    } while (0)


enum MHD_Result handle_http_request(
    void *cls,
    struct MHD_Connection *connection,
    const char *url,
    const char *method,
    const char *version,
    const char *upload_data,
    long unsigned int *upload_data_size,
    void **con_cls
)
{
    struct Metrics *metrics = (struct Metrics *) cls;

    if (strcmp(method, "GET") != 0) {
        return MHD_NO;
    }

    char response_buffer[512] = {0};
    int response_code = MHD_HTTP_OK;

    if (strcmp(url, "/metrics") == 0) {
        LOG("Metrics server: GET /metrics");

        const char *metrics_tpl =
            "success_total %d\n"
            "failures_total %d\n";

        snprintf(
            response_buffer,
            sizeof(response_buffer) - 1,
            metrics_tpl,
            metrics->success_total,
            metrics->failure_total
        );
        response_code = MHD_HTTP_OK;
    } else if (strcmp(url, "/") == 0) {
        LOG("Metrics server: GET /");

        strncpy(response_buffer, "See /metrics.", sizeof(response_buffer) - 1);
        response_code = MHD_HTTP_OK;
    } else {
        LOG("Metrics server: GET {TODO: url}");

        strncpy(response_buffer, "Not found", sizeof(response_buffer) - 1);
        response_code = MHD_HTTP_NOT_FOUND;
    }

    struct MHD_Response *response = MHD_create_response_from_buffer(
        strlen(response_buffer),
        (void *) response_buffer,
        MHD_RESPMEM_MUST_COPY
    );
    enum MHD_Result ret = MHD_queue_response(connection, response_code, response);
    MHD_destroy_response(response);

    return ret;
}


int run_command(char **argv, int repeat, int retries_on_failure, struct Metrics* metrics)
{
    while (repeat == -1 || repeat--) {
        pid_t pid = fork();

        if (pid < 0) {
            perror("fork");
            return 1;
        }

        if (pid == 0) {
            execvp(argv[0], argv);
            perror("execvp");
            return 127;
        } else {
            int status;
            if (waitpid(pid, &status, 0) == -1) {
                perror("waitpid");
                return 1;
            }

            if (WIFEXITED(status)) {
                int exit_code = WEXITSTATUS(status);
                LOG("Child process exited with code: %d", exit_code);
                if (exit_code != 0) {
                    metrics->failure_total++;
                    if (retries_on_failure == 0) {
                        break;
                    }
                    retries_on_failure--;
                } else {
                    metrics->success_total++;
                }
            } else {
                LOG("Child process exited abnormally");
                metrics->failure_total++;
                if (retries_on_failure == 0) {
                    break;
                }
                retries_on_failure--;
            }
        }
    }

    return 0;
}


int main(int argc, char *argv[])
{
    struct Metrics metrics = {0};

    struct option long_options[] = {
        {"repeat",              required_argument, 0, 'r'},
        {"retries-on-failure",  required_argument, 0, 'f'},
        {"metrics-host",        required_argument, 0, 's'},
        {"metrics-port",        required_argument, 0, 'p'},
        {"verbose",             no_argument,       0, 'v'},
        {"help",                no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int repeat = -1;
    int retries_on_failure = 0;
    const char *metrics_host = NULL;
    int metrics_port = 3535;

    int opt;
    while ((opt = getopt_long(argc, argv, "r:f:h:p:v", long_options, NULL)) != -1) {
        switch (opt) {
            case 'r':
                repeat = atoi(optarg);
                break;
            case 'f':
                retries_on_failure = atoi(optarg);
                break;
            case 's':
                metrics_host = optarg;
                break;
            case 'p':
                metrics_port = atoi(optarg);
                break;
            case 'v':
                verbose = 1;
                break;
            case 'h':
                fprintf(stderr, "%s", help_message);
                return 0;
            default:
                fprintf(stderr, "%s", usage_message);
                return 1;
        }
    }

    struct MHD_Daemon *daemon = NULL;

    if (metrics_host != NULL) {
        daemon = MHD_start_daemon(
            MHD_USE_SELECT_INTERNALLY,
            metrics_port,  // TODO: metrics_host ?
            NULL,
            NULL,
            &handle_http_request,
            &metrics,
            MHD_OPTION_END
        );
        if (daemon == NULL) {
            fprintf(stderr, "Failed to start server\n");
            return 1;
        }
    }

    int exit_code = run_command(&argv[optind], repeat, retries_on_failure, &metrics);

    if (metrics_host != NULL) {
        MHD_stop_daemon(daemon);
    }

    return exit_code;
}
