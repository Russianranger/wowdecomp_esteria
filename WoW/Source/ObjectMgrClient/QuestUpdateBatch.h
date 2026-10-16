#ifndef WOW_SOURCE_OBJECTMGRCLIENT_QUESTUPDATEBATCH_H
#define WOW_SOURCE_OBJECTMGRCLIENT_QUESTUPDATEBATCH_H

// Object data is copied before its mirror callbacks run. A thousand changed
// quest slots therefore need one refresh of the sorted log and quest-giver
// status, while still running each slot's acceptance/removal notifications.
class QuestUpdateBatch {
    bool m_previousActive;
    bool m_previousRefreshed;
    inline static thread_local bool s_active = false;
    inline static thread_local bool s_refreshed = false;

public:
    QuestUpdateBatch() : m_previousActive(s_active), m_previousRefreshed(s_refreshed) {
        s_active = true;
        s_refreshed = false;
    }
    ~QuestUpdateBatch() {
        s_active = m_previousActive;
        s_refreshed = m_previousRefreshed;
    }
    QuestUpdateBatch(const QuestUpdateBatch&) = delete;
    QuestUpdateBatch& operator=(const QuestUpdateBatch&) = delete;

    static bool NeedsRefresh() {
        if (!s_active) {
            return true;
        }
        if (s_refreshed) {
            return false;
        }
        s_refreshed = true;
        return true;
    }
};

#endif
