#ifndef ENGINE_SOURCE_RETAIL_BUSYNOTICE_H
#define ENGINE_SOURCE_RETAIL_BUSYNOTICE_H

namespace Retail {

// Shows a small always-on-top "downloading" window while a slow Retail read blocks the calling thread.
// The window lives on its own thread, so it keeps painting while the game thread waits. It appears only if
// the read lasts longer than a moment and lingers briefly so a run of reads shows one steady notice.
class BusyNotice {
public:
    explicit BusyNotice(const char* text);
    ~BusyNotice();
    BusyNotice(const BusyNotice&) = delete;
    BusyNotice& operator=(const BusyNotice&) = delete;
};

}
#endif
