#ifndef FEEDBACK_H
#define FEEDBACK_H

#include "common.h"
#include "config.h"

#include <stddef.h>

typedef struct {
    FeedbackId feedback_id;
    UserId user_id;

    char message[MAX_FEEDBACK_LEN + 1];
    char created_at[32];
} FeedbackRecord;

OperationStatus submit_feedback(
    UserId user_id,
    const char *message,
    FeedbackRecord *created_feedback
);

OperationStatus get_all_feedback(
    char *output,
    size_t output_size
);

#endif
