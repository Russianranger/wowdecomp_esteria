// HelloWorld: the smallest possible extension. It registers one script of every type and logs the
// hooks it receives, so the discovery -> registration -> dispatch chain of each context can be
// checked end to end in Logs\Extensions.log. It is the template for a real extension; it does
// nothing else on purpose. Per-frame hooks log their first call and a count afterwards, never
// every frame.

void AddHelloWorldClientScripts();
void AddHelloWorldWorldScripts();
void AddHelloWorldRenderScripts();
void AddHelloWorldNetScripts();
void AddHelloWorldLuaScripts();

// The entry point the generated loader calls: Add<DirectoryName>Extension(). Instantiate every
// script of the extension from here; a script registers itself in its constructor and the registry
// owns it from then on.
void AddHelloWorldExtension() {
    AddHelloWorldClientScripts();
    AddHelloWorldWorldScripts();
    AddHelloWorldRenderScripts();
    AddHelloWorldNetScripts();
    AddHelloWorldLuaScripts();
}
