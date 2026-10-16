// HelloWorld: the smallest possible extension. It registers one script of every type and logs the
// hooks it receives, so the discovery -> registration -> dispatch chain of each context can be
// checked end to end in Logs\Extensions.log. It is the template for a real extension; it does
// nothing else on purpose. Per-frame and per-object hooks log their first calls and counts
// afterwards, never every call.

void AddHelloWorldAssetScripts();
void AddHelloWorldClientDBScripts();
void AddHelloWorldClientScripts();
void AddHelloWorldLuaScripts();
void AddHelloWorldModelScripts();
void AddHelloWorldNetScripts();
void AddHelloWorldRenderScripts();
void AddHelloWorldWorldScripts();

// The entry point the generated loader calls: Add<DirectoryName>Extension(). Instantiate every
// script of the extension from here; a script registers itself in its constructor and the registry
// owns it from then on.
void AddHelloWorldExtension() {
    AddHelloWorldAssetScripts();
    AddHelloWorldClientDBScripts();
    AddHelloWorldClientScripts();
    AddHelloWorldLuaScripts();
    AddHelloWorldModelScripts();
    AddHelloWorldNetScripts();
    AddHelloWorldRenderScripts();
    AddHelloWorldWorldScripts();
}
