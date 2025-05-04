Restart and monitor the specified command.

Usage
-----

```
rerun [OPTIONS] -- <command> [args...]
```

Options:

| Option                          | Description                                                              | Default                  |
| ------------------------------- | ------------------------------------------------------------------------ | ------------------------ |
| --repeat <int>                  | Number of times to repeat the command (including failed executions).     | Repeat forever           |
| --retries-on-failure <int>      | Number of retries allowed on failure.                                    | 0                        |
| --metrics-host <string>         | Host to run embedded metrics HTTP server on.                             | Do not start the server  |
| --metrics-port <int>            | Port for the metrics HTTP server.                                        | 3535                     |
| --verbose                       | Enable debug output.                                                     | No extra output          |
| --help                          | Show this help message and exit.                                         | -                        |

Metrics
-------

The embedded HTTP server provides Prometheus-compatible metrics (if enabled) at `GET /metrics`.
* success_total <int>
* failures_total <int>

Examples
--------

Run ./script.sh and repeat forever. Exit on failure. Do NOT start the metrics server.
```
rerun ./script.sh
```

Run ./script.sh 5 times. Exit on failure.
```
rerun --repeat 5 -- ./script.sh
```

Run ./script.sh arg1 arg2 10 times. On failure retry 2 times.
```
rerun --repeat 10 --retries-on-failure 2 -- ./script.sh arg1 arg2
```

Run ./script.sh and repeat forever. Start metrics server at 127.0.0.1:8080.
```
rerun --metrics-host 127.0.0.1 --metrics-port 8080 -- ./script.sh
```
