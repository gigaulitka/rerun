Usage
-----

```
rerun [OPTIONS] -- COMMAND [ARGS...]
````

Runs and monitors the given command, restarts it automatically when it exits with status code 0. Exposes Prometheus-compatible metrics via an embedded HTTP server.
Options:
* --repeat (-r) N - Repeat the given command only N times. Default: repeat forewer.
* --retries-on-failure (-f) N - Retry the given command N times if it fails. Default: do not repeat on failure.
* --metrics-host (-h) HOST - Host to bind the metrics server. Do not start metrics server if not provided.
* --metrics-port (-p) PORT - Port for the Prometheus metrics server (default: 3535).
* --verbose (-v) - Enables debug logging to stderr.

Examples
--------

Run a script and expose metrics on port 9100:
```
rerun --metrics-port 9100 -- ./my-script.sh
```
Run a binary and bind the metrics server to a specific interface:
```
rerun --host 0.0.0.0 --port 9090 -- ./my-script.sh
```
Enable verbose logging:
```
rerun --verbose -- ./my-script.sh
```
Run command `./my-script.sh -q -w -e` 5 times, retry 2 times on failure, expose metrics on 127.0.0.1:8080:
```
rerun --metrics-host 127.0.0.1 --metrics-port 8080 --repeat 5 --retries-on-failure 2 --verbose ./my-script.sh -q -w -e
```
