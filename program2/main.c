#include <zr/types.h>

#include <zr/syscalls.h>

void main(void) {
    err_t err;
    handle_t video_data_channel;
    err = resource_get(&resource_name("video/data"), RESOURCE_TYPE_CHANNEL_SEND, &video_data_channel);
    if (err)
        return;
    handle_t test_channel;
    err = resource_get(&resource_name("test/1"), RESOURCE_TYPE_CHANNEL_RECEIVE, &test_channel);
    if (err)
        return;
    handle_t test_mqueue;
    err = mqueue_create(&test_mqueue);
    if (err)
        return;
    mqueue_add_channel(test_mqueue, test_channel, (MessageTag){0, 0});
    handle_t msg1;
    err = mqueue_receive(test_mqueue, NULL, &msg1);
    if (err)
        return;
    ReceiveAttachedHandle msg1_handles[] = {{ATTACHED_HANDLE_TYPE_CHANNEL_RECEIVE, 0}};
    err = message_read_bounded(msg1, &(ReceiveMessage){0, NULL, 1, msg1_handles}, NULL, &error_replies(ERR_INVALID_ARG));
    if (err)
        return;
    message_reply(msg1, NULL);
    handle_t ch1 = msg1_handles[0].handle_i;
    handle_t mq1;
    err = mqueue_create(&mq1);
    if (err)
        return;
    mqueue_add_channel(mq1, ch1, (MessageTag){0, 0});
    handle_t msg2;
    err = mqueue_receive(mq1, NULL, &msg2);
    if (err)
        return;
    u64 key;
    err = message_read_bounded(msg2, &(ReceiveMessage){sizeof(u64), &key, 0, NULL}, NULL, &error_replies(ERR_INVALID_ARG));
    if (err)
        return;
    if (key != UINT64_C(0x0123456789ABCDEF))
        return;
    message_reply(msg2, &(SendMessage){0, NULL, 1, &(SendMessageHandles){1, &(SendAttachedHandle){0, video_data_channel}}});
}
