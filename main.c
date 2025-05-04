// See LICENSE file for copyright and license details.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <pthread.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <fcntl.h>
#define _GNU_SOURCE  // For getopt_long
#include <getopt.h>


struct Metrics {
    int success_total;
    int failure_total;
};


struct MetricsThreadArgs {
    const char *host;
    int port;
    struct Metrics *metrics;
    int exit_pipe_fd;
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


void handle_http_request(int server_fd, struct sockaddr_in address, struct Metrics *metrics)
{
    socklen_t addrlen = sizeof(address);
    int client_fd = accept(server_fd, (struct sockaddr*)&address, &addrlen);
    if (client_fd == -1) {
        perror("accept");
        close(server_fd);
        return;
    }

    char buffer[1024] = {0};

    read(client_fd, buffer, sizeof(buffer) - 1);

    const char *response_tpl =
        "HTTP/1.1 %s\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s";

    if (strncmp(buffer, "GET /metrics ", strlen("GET /metrics ")) == 0) {
        LOG("Metrics server: GET /metrics");

        const char *metrics_tpl =
            "success_total %d\n"
            "failures_total %d\n";

        char metrics_buffer[128] = {0};
        snprintf(
            metrics_buffer,
            sizeof(metrics_buffer) - 1,
            metrics_tpl,
            metrics->success_total,
            metrics->failure_total
        );

        snprintf(
            buffer,
            sizeof(buffer) - 1,
            response_tpl,
            "200 OK",
            strlen(metrics_buffer),
            metrics_buffer
        );
    } else if (strncmp(buffer, "GET / ", strlen("GET / ")) == 0) {
        LOG("Metrics server: GET /");

        const char *body = "See /metrics.";
        snprintf(
            buffer,
            sizeof(buffer) - 1,
            response_tpl,
            "200 OK",
            strlen(body),
            body
        );
    } else {
        LOG("Metrics server: Unexpected request");

        const char *body = "Not found.";
        snprintf(
            buffer,
            sizeof(buffer) - 1,
            response_tpl,
            "404 Not Found",
            strlen(body),
            body
        );
    }

    write(client_fd, buffer, strlen(buffer));

    close(client_fd);
}


void run_metrics_server(const char *host, int port, struct Metrics *metrics, int exit_pipe_fd)
{
    LOG("Start metrics server at %s:%d...", host, port);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0) {
        perror("socket failed");
        return;
    }
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("setsockopt");
        close(server_fd);
        return;
    }
    int flags = fcntl(server_fd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl (get)");
        close(server_fd);
        return;
    }
    if (fcntl(server_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl (set)");
        close(server_fd);
        return;
    }

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr(host);
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) == -1) {
        perror("bind failed");
        close(server_fd);
        return;
    }
    if (listen(server_fd, 1) == -1) {
        perror("listen");
        close(server_fd);
        return;
    }

    struct pollfd poll_fds[2];

    poll_fds[0].fd = server_fd;
    poll_fds[0].events = POLLIN;

    poll_fds[1].fd = exit_pipe_fd;
    poll_fds[1].events = POLLIN;

    LOG("Metrics server: listening...");

    while (1) {
        if (poll(poll_fds, 2, -1) == -1) {
            perror("poll");
            break;
        }

        if (poll_fds[1].revents & POLLIN) {
            break;
        }

        if (poll_fds[0].revents & POLLIN) {
            handle_http_request(server_fd, address, metrics);
        }
    }

    close(server_fd);

    LOG("Stop metrics server.");
}


void *metrics_server_handler(void *raw_args)
{
    struct MetricsThreadArgs *args = (struct MetricsThreadArgs *) raw_args;
    run_metrics_server(args->host, args->port, args->metrics, args->exit_pipe_fd);
    return NULL;
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

    // Pipe to gracefully terminate metrics thread.
    int metrics_thread_pipe_fd[2];

    pthread_t metrics_thread;

    if (metrics_host != NULL) {
        if (pipe(metrics_thread_pipe_fd) == -1) {
            perror("pipe");
            return 1;
        }

        struct MetricsThreadArgs args;
        args.host = metrics_host;
        args.port = metrics_port;
        args.metrics = &metrics;
        args.exit_pipe_fd = metrics_thread_pipe_fd[0];

        if (pthread_create(&metrics_thread, NULL, metrics_server_handler, &args) != 0) {
            perror("pthread_create metrics_thread");
            close(metrics_thread_pipe_fd[0]);
            close(metrics_thread_pipe_fd[1]);

            return 1;
        }
    }

    int exit_code = run_command(&argv[optind], repeat, retries_on_failure, &metrics);

    if (metrics_host != NULL) {
        write(metrics_thread_pipe_fd[1], "x", 1);

        pthread_join(metrics_thread, NULL);

        close(metrics_thread_pipe_fd[0]);
        close(metrics_thread_pipe_fd[1]);
    }

    return exit_code;
}
