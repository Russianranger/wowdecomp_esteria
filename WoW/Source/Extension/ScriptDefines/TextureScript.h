#ifndef WOW_SOURCE_EXTENSION_SCRIPTDEFINES_TEXTURESCRIPT_H
#define WOW_SOURCE_EXTENSION_SCRIPTDEFINES_TEXTURESCRIPT_H

#include "WoW/Source/Extension/ScriptObject.h"

class CTexture;

// File textures (Engine/Source/Services/Texture.cpp). TextureCreate(fileName, ...) is the one
// entry every BLP/TGA texture request goes through (models, terrain, WMOs, UI): it consults the
// texture cache first and, on a miss, creates the CTexture (CreateBlpTexture / CreateTgaTexture,
// the pixel data arriving asynchronously) and registers it in the cache. Procedural textures
// (TextureCreate with dimensions, TextureCreateSolid, blobs) do not pass through this hook.
class TextureScript : public ScriptObject {
    protected:
    explicit TextureScript(const char* name);

    public:
    // TextureCreate(const char* fileName, ...) right after the new texture is registered in the
    // cache: the CTexture exists and will be handed to the caller; its image data may still be
    // loading. fileName is the path as requested (before the .blp/.tga extension retry).
    // Cache hits do not fire: this is once per distinct file and flag set, not once per use.
    virtual void OnTextureCreated(CTexture& texture, const char* fileName) {}
};

#endif
