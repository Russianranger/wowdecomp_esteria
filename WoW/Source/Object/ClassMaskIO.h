#ifndef WOW_SOURCE_OBJECT_CLASSMASKIO_H
#define WOW_SOURCE_OBJECT_CLASSMASKIO_H

#include "WoW/Common/ClassMask.h"
#include "Engine/Source/Base/CDataStore.h"

inline void ReadClassMask(CDataStore* msg, ClassMask& mask) {
    for (uint32_t& word : mask.words) msg->Get(word);
}
inline void WriteClassMask(CDataStore* msg, const ClassMask& mask) {
    for (uint32_t word : mask.words) msg->Put(word);
}

#endif
