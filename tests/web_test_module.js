// Linked only into the test module, modular_web_test, with tests/web_test_module.cpp: hands
// each instance to the smoke test, which reads its memory through it. Emscripten places this
// inside the module's factory, where `Module` is the instance being made.
globalThis.modularTestInstances?.push(Module);
