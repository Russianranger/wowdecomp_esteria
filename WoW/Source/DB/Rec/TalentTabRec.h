#ifndef WOW_SOURCE_DB_REC_TALENTTABREC_H
#define WOW_SOURCE_DB_REC_TALENTTABREC_H

#include <cstddef>
#include <cstdint>
#include "WoW/Common/ClassMask.h"

class SFileImpl;

class TalentTabRec {
    public:
    static const int32_t s_numColumns = 24 + CLASS_MASK_EXTRA_COLUMNS;
    static const int32_t s_rowSize = 96 + 4 * CLASS_MASK_EXTRA_COLUMNS;
    static const bool s_generatedID = false;

    /* 0000 */ int32_t m_id;
    /* 0004 */ const char* m_name;
    /* 0008 */ int32_t m_spellIconID;
    /* 000C */ int32_t m_raceMask;
    /* 0010 */ ClassMask m_classMask;
    /* 0014 */ int32_t m_categoryEnumID;
    /* 0018 */ int32_t m_orderIndex;
    /* 001C */ const char* m_backgroundFile;

    TalentTabRec();
    ~TalentTabRec();
    static const char* GetFilename();
    bool Read(SFileImpl* file, const char* stringBuffer);
};

#endif
