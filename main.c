#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>


int main(int argc, char *argv[])
{
    if (argc < 2) {
        // TODO: Print help
        fprintf(stderr, "TODO: print help\n");
        return 1;
    }

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
