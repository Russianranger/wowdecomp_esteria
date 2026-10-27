#ifndef ENGINE_SOURCE_UNITOUTLINE_UNITOUTLINE_H
#define ENGINE_SOURCE_UNITOUTLINE_UNITOUTLINE_H

#include <cstddef>
#include <cstdint>

class CGxBatch;
class CM2Model;

// Not in the original 3.3.5a binary. A reaction-coloured silhouette outline on the target and mouseover
// units (and quest givers), after the WXL module wxl-mana-unit-outline as ported to wowdecomp by manatactical
// (https://github.com/manatactical/wooooow). The donor draws it with D3D9-only shaders; this version keeps
// the idea and replaces the parts that only worked there:
//
//   1. While the model scene draws the world, every batch of a recorded unit is drawn once more into a
//      frame-sized mask (StampBatch). The batch is the real one, so the silhouette is the game's own skinning at
//      the frame's pose, and it keeps the batch's own shaders -- only its blend state is replaced, so it needs no
//      shader that pairs with the model's. The mask is cleared white and the batch ERASES it (inverse source alpha),
//      one channel for the whole silhouette and one for the part the depth test lets through. The mask has the
//      scene's size, so a backend that gives equal-sized targets one depth buffer (Vulkan) tests the stamp against
//      the scene's real occluders; on the others the two channels are the same.
//   2. At the end of the world draw (Composite) one edge pass per colour reads those masks and draws the line:
//      pixels within the width of the visible silhouette that are outside the full silhouette and outside the
//      local player's own. Vulkan has the pass as a program of its own (OutlineEdge.frag); D3D9 and D3D11 compile
//      the HLSL below.
//   3. The target list is spent and emptied; the game refills it as it draws the next frame.
//
// Options: the unitOutline cvar (on/off) and unitOutlineWidth (pixels), shown in the Esteria video options.
class CUnitOutline {
    public:
    // Registers the unitOutline cvars. Called once at client start so the options panel can read them.
    static void RegisterCVars();

    // Empties the target list and forgets the player model. The game calls this once per frame before it adds
    // this frame's selections.
    static void BeginTargets();
    static void SetPlayerModel(CM2Model* model);
    // A mounted player is two models; both go into the occluder mask.
    static void SetPlayerModel(CM2Model* model, CM2Model* mount);
    // One more model subtracted from the outline: the player's own gear is a separate model.
    static void AddOccluder(CM2Model* model);

    // Records one model with the reaction colour. Exact pointer, no attach chain: an effect hung off a unit must
    // not be outlined with it. footRef is unused by this version (the donor fades the line out at the unit's
    // feet); keepFull exempts the unit from the settle fade.
    static void AddTarget(CM2Model* model, const float rgb[3], bool keepFull, CM2Model* footRef = nullptr);
    // A world object the cursor is over.
    static void AddDoodad(CM2Model* model, const float rgb[3]);

    // One model batch about to be drawn. Stamps the model's silhouette when it is a recorded target, or the
    // player's when it is that, and leaves the device state as it found it.
    static void StampBatch(const CGxBatch& batch, CM2Model* model);

    // The end of the world draw: draws the line for what was stamped and arms the next frame.
    static void Composite();

    // Whether a model is one of this frame's outline targets (exact match). The model scene's opaque sort asks
    // this to keep those models at the end of the pass, which puts the rest of the world into the depth buffer
    // before they stamp against it.
    static bool IsTarget(CM2Model* model);
    // Whether this frame recorded anything to outline at all.
    static bool HasTargets();

    private:
    static bool Enabled();
    static bool EnsureResources();
    static int32_t FindTarget(CM2Model* model);
    static int32_t FindGroup(const float rgb[3], bool create);
    static void AddEntry(CM2Model* model, const float rgb[3], bool keepFull);
    static void Stamp(const CGxBatch& batch, void* texture, bool& cleared, uint32_t channel, bool depthTest);
    static void EdgePass(int32_t group);
};

#endif
