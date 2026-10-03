# Native lifecycle tests

Run on macOS with a built Defold SDK (including the host's `script`, `lua`,
`dlib`, `profile_null`, and `ddf` libraries):

```sh
python3 tests/run_native_tests.py --defold-home /path/to/defold/tmp/dynamo_home
```

The tests compile the production callback code and use Defold's real Lua and
callback helpers. They exercise callback arguments, navigation return values,
deleted script instances, stale generations after view-slot reuse, destruction/replacement during callbacks with forced
garbage collection, error handling, stack restoration, and reference cleanup.
AddressSanitizer and UndefinedBehaviorSanitizer instrument the test and extension
code. Pass `--sanitizer thread` to use ThreadSanitizer instead of AddressSanitizer.
A second executable exercises the production Darwin queue and delegates,
including invalid IDs, slot reuse within one drained batch, cancellation of
pending navigation decisions, concurrent queue draining, a producer racing shutdown, and late events after
finalization/reinitialization. These tests use platform/script-boundary test
doubles and create no browser windows; they do not replace testing the native
WebView lifecycle on Android and Apple devices.

## Device regression project

Copy `tests/lifecycle` to a temporary directory, then copy this checkout's
`webview` directory into that project. Open its `game.project` in Defold and
bundle it for Android, iOS, or macOS. It has no remote dependencies.

The project destroys an unopened view, repeatedly destroys/recreates views in
the same slot while JavaScript emits events, alternates visible and hidden views,
and destroys a view from inside its callback. It must print
`WebView lifecycle regression passed` and exit without crashes, assertion errors,
callbacks after destruction, or events delivered to a replacement view.

Run Android debug and release bundles, including armeabi-v7a, for issue #40.
The host runner checks this script's Lua syntax but does not execute the device test.
