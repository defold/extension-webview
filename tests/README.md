# Native lifecycle tests

Run on macOS with a built Defold SDK (including the host's `script`, `lua`,
`dlib`, `profile_null`, and `ddf` libraries):

```sh
python3 tests/run_native_tests.py --defold-home /path/to/defold/tmp/dynamo_home
```

The tests compile the production callback code and use Defold's real Lua and
callback helpers. They exercise callback arguments, navigation return values,
deleted script instances, destruction/replacement during callbacks with forced
garbage collection, error handling, stack restoration, and reference cleanup.
AddressSanitizer and UndefinedBehaviorSanitizer instrument the test and extension
code. The two platform navigation operations are test doubles; this does not
replace testing the native WebView lifecycle on Android and Apple devices.
