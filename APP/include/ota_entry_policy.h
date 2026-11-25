#ifndef OTA_ENTRY_POLICY_H
#define OTA_ENTRY_POLICY_H

#include <stdint.h>

typedef void (*OtaEntrySaveAction)(void);
typedef void (*OtaEntryWriteRequestAction)(uint8_t reason);

static inline void OtaEntryFlushBeforeRequest(
    OtaEntrySaveAction save,
    OtaEntryWriteRequestAction write_request,
    uint8_t reason
)
{
    save();
    write_request(reason);
}

#endif
