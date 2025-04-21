#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <pthread.h>
#include <netinet/in.h>
#include <arpa/inet.h>
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
};


void *metrics_server_handler(void *raw_args)
{
    struct MetricsThreadArgs *args = (struct MetricsThreadArgs *) raw_args;

    printf("Start metrics server at %s:%d...\n", args->host, args->port);

    struct sockaddr_in address;
    socklen_t addrlen = sizeof(address);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0) {
        perror("socket failed");
        exit(1);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr(args->host);
    address.sin_port = htons(args->port);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("bind failed");
        close(server_fd);
        exit(1);
    }

    if (listen(server_fd, 1) < 0) {
        perror("listen");
        close(server_fd);
        exit(1);
    }

    printf("Metrics server: listening...\n");

    while (1) {
        int client_fd = accept(server_fd, (struct sockaddr*)&address, &addrlen);
        if (client_fd < 0) {
            perror("accept");
            close(server_fd);
            exit(1);
        }

        char buffer[1024] = {0};

        // TODO: Handle "GET /" only.
        read(client_fd, buffer, sizeof(buffer) - 1);
        printf("Received request:\n%s\n", buffer);

        char stats_buffer[64] = {0};
        sprintf(
            stats_buffer,
            "success_total %d\nfailures_total %d\n",
            args->metrics->success_total,
            args->metrics->failure_total
        );

        const char* response_tpl =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: %d\r\n"
            "Connection: close\r\n"
            "\r\n"
            "%s";

        snprintf(buffer, sizeof(buffer) - 1, response_tpl, strlen(stats_buffer), stats_buffer);

        write(client_fd, buffer, strlen(buffer));

        close(client_fd);
    }

    close(server_fd);
}


int main(int argc, char *argv[])
{
    struct Metrics metrics = {0};

    struct option long_options[] = {
        {"repeat",  required_argument, 0, 'r'},
        {"retries-on-failure",  required_argument, 0, 'f'},
        {"metrics-host",  required_argument, 0, 'h'},
        {"metrics-port",  required_argument, 0, 'p'},
        {0, 0, 0, 0}
    };

    int repeat = -1;
    int retries_on_failure = 0;
    const char *metrics_host = NULL;
    int metrics_port = 3535;

    int opt;
    while ((opt = getopt_long(argc, argv, "r:f:", long_options, NULL)) != -1) {
        switch (opt) {
            case 'r':
                repeat = atoi(optarg);
                break;
            case 'f':
                retries_on_failure = atoi(optarg);
                break;
            case 'h':
                metrics_host = optarg;
                break;
            case 'p':
                metrics_port = atoi(optarg);
                break;
            default:
                fprintf(stderr, "Usage: %s [--repeat <int>] [--retries-on-failure <int>] [--metrics-host <string>] [--metrics-port <int>]\n", argv[0]);
                return 1;
        }
    }

    if (metrics_host != NULL) {
        pthread_t metrics_thread;

        struct MetricsThreadArgs args;
        args.host = metrics_host;
        args.port = metrics_port;
        args.metrics = &metrics;
        if (pthread_create(&metrics_thread, NULL, metrics_server_handler, &args) != 0) {
            perror("pthread_create metrics_thread");
            return 1;
        }
        pthread_detach(metrics_thread);
    }

    while (repeat == -1 || repeat--) {
        pid_t pid = fork();

        if (pid < 0) {
            perror("fork");
            return 1;
        }

        if (pid == 0) {
            execvp(argv[optind], &argv[optind]);
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
                printf("Child process exited with code: %d\n", exit_code);
                if (exit_code != 0) {
                    metrics.failure_total++;
                    if (retries_on_failure == 0) {
                        break;
                    }
                    retries_on_failure--;
                } else {
                    metrics.success_total++;
                }
            } else {
                printf("Child process exited abnormally\n");
                metrics.failure_total++;
                if (retries_on_failure == 0) {
                    break;
                }
                retries_on_failure--;
            }
        }
    }

    return 0;
}
