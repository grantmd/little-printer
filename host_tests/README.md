# Host-side unit tests

Tiny standalone tests for pure-logic helpers that don't touch hardware. Compile and run with plain `cc`; no IDF required.

```bash
cc -Wall -o /tmp/test_weather_code host_tests/test_weather_code.c
/tmp/test_weather_code

cc -Wall -o /tmp/test_text_wrap host_tests/test_text_wrap.c
/tmp/test_text_wrap
```

Each test prints `PASS` on success or `FAIL: ...` and exits non-zero on failure.

These tests duplicate the function under test rather than linking to `main/*.c` — the IDF build pulls in too many dependencies for a host compile. Keep the duplicates in sync when the function changes; the test suite is small enough that drift is easy to spot.
