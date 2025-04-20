#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <pthread.h>
#include <netinet/in.h>
#include <arpa/inet.h>


#define METRICS_SERVER_HOST "127.0.0.1"
#define METRICS_SERVER_PORT 8080


struct RerunStats {
    int runs;
};


// TODO: Make local;
struct RerunStats rerun_stats;


void *metrics_server_handler(void *arg) {
    printf("Start metrics server at %s:%d...\n", METRICS_SERVER_HOST, METRICS_SERVER_PORT);

    struct sockaddr_in address;
    socklen_t addrlen = sizeof(address);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0) {
        perror("socket failed");
        exit(1);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr(METRICS_SERVER_HOST);
    address.sin_port = htons(METRICS_SERVER_PORT);

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
        sprintf(stats_buffer, "runs: %d\n", rerun_stats.runs);

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
    rerun_stats.runs = 0;

    if (argc < 2) {
        // TODO: Print help
        fprintf(stderr, "TODO: print help\n");
        return 1;
    }

    pthread_t metrics_thread;

    if (pthread_create(&metrics_thread, NULL, metrics_server_handler, NULL) != 0) {
        perror("pthread_create metrics_thread");
        return 1;
    }
    pthread_detach(metrics_thread);

    while (1) {
        pid_t pid = fork();

        if (pid < 0) {
            perror("fork");
            return 1;
        }

        if (pid == 0) {
            execvp(argv[1], &argv[1]);
            perror("execvp");
            return 127;
        } else {
            rerun_stats.runs++;

            int status;
            if (waitpid(pid, &status, 0) == -1) {
                perror("waitpid");
                return 1;
            }

            if (WIFEXITED(status)) {
                int exit_code = WEXITSTATUS(status);
                printf("Child process exited with code: %d\n", exit_code);
                if (exit_code != 0) {
                    break;
                }
            } else {
                printf("Child process exited abnormally\n");
                break;
            }
        }
    }

    return 0;
}
