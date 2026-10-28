#ifndef ENGINE_SOURCE_FRAME_MODELSNAPSHOT_H
#define ENGINE_SOURCE_FRAME_MODELSNAPSHOT_H

#include <cstddef>
#include <cstdint>

class CGxTex;
class CTexture;

// NOT IN BINARY. Still pictures of models, named "Snapshot:<key>". A frame draws a model into a working target with
// CSimpleModel::RenderModelOffscreen; Commit reads that back, checks the picture is not empty and stores it, compressed,
// under Cache/Snapshots. Any texture region shows one with SetTexture("Snapshot:<key>"): the picture is loaded from
// the file into a texture only while someone wants it on screen, so a grid of previews costs one textured quad each
// and video memory only for the cards that are visible.
namespace ModelSnapshot {

constexpr const char* kPrefix = "Snapshot:";
constexpr size_t kPrefixLength = 9;
// Power-of-two like every other render target in the client.
constexpr uint32_t kWidth = 256;
constexpr uint32_t kHeight = 256;

// True when a valid picture for key is stored.
bool Has(const char* key);
// The working render target of owner (a frame), created on first use; null when render targets are unavailable.
CGxTex* Scratch(const void* owner);
// Reads target back, and stores it under key unless it is empty or, when refKey names a stored picture of the bare
// character, no different from that one; force 1 stores it whatever it shows (but not a picture with nothing in it), force 2
// stores any readable picture (the bare character itself, which is a dark figure on black). False when the
// picture was not stored (see Failed), in which case the job is to be done again.
bool Commit(const char* key, CGxTex* target, int force, const char* refKey);
// A new reference to a texture holding the picture for key, or null; the caller closes it with HandleClose.
CTexture* Acquire(const char* key);
// Frees the textures of every picture not listed. A texture a
// region still shows stays alive until the region lets go of it.
void Keep(const char* const* keys, size_t count);
// Logs a card queueing a snapshot texture to be drawn (Logs/snapshot.log); other textures are ignored.
void TraceQueue(CTexture* texture, CGxTex* gxTex);
// The same for the moment the batch is drawn: logged for the first few draws of each picture, with what it holds.
void TraceDraw(CTexture* texture, CGxTex* gxTex, const char* meshInfo);
// Call before drawing a batch: true (and the target captured) when this draw is to be traced by TraceState.
bool TraceBefore(CTexture* texture);
// Logs D3D's own state right after a snapshot texture's batch was drawn (first two draws per picture).
void TraceState(CTexture* texture, float left, float top, float right, float bottom);
// Called before a frame is presented: rereads the pixels TraceState sampled and logs which were drawn over.
void FrameEnd();
// Writes one line to Logs/snapshot.log (diagnostics from other modules).
void Note(const char* text);
// Frees the working targets (the window closed).
void ReleaseTargets();
// Frees everything and deletes the stored pictures.
void Clear();
// For /tmog shot: textures alive, pictures stored and rejected this run, and why the last Commit or Scratch failed:
// 0 nothing, 1 the device cannot render to an ARGB texture, 2 the texture could not be created, 5 the item is not in
// the picture (it equals the bare character), 3 the picture could
// not be read back, 4 the picture was empty.
size_t Count();
unsigned Saved();
unsigned Rejected();
int Failed();

}  // namespace ModelSnapshot

#endif
