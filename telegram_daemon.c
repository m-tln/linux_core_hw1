#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <time.h>

#define NETLINK_TELEGRAM 31
#define MAX_PAYLOAD 1024
#define MAX_CHATS 4
#define MSG_LIMIT 10

enum tg_msg_type {
    TG_INIT      = 0,
    TG_READ_REQ  = 1,
    TG_READ_RES  = 2,
    TG_WRITE_REQ = 3,
    TG_WRITE_RES = 4,
};

struct tg_msg {
    int type;
    int chat_id;
    char data[MAX_PAYLOAD];
};

struct chat_history {
    char messages[MSG_LIMIT][256];
    int count;
    int head;
};

static struct chat_history chats[MAX_CHATS];

static void add_message(int chat_id, const char *raw_msg)
{
    if (chat_id < 1 || chat_id >= MAX_CHATS)
        return;

    struct chat_history *ch = &chats[chat_id];
    char formatted[256];
    int len;

    len = snprintf(formatted, sizeof(formatted), "[User] %s", raw_msg);
    if (len > 0 && formatted[len-1] == '\n')
        formatted[len-1] = '\0';
    strncat(formatted, "\n", sizeof(formatted) - strlen(formatted) - 1);

    strncpy(ch->messages[ch->head], formatted, 255);
    ch->messages[ch->head][255] = '\0';
    ch->head = (ch->head + 1) % MSG_LIMIT;
    if (ch->count < MSG_LIMIT)
        ch->count++;
}

static void get_history(int chat_id, char *out, size_t out_size)
{
    out[0] = '\0';
    if (chat_id < 1 || chat_id >= MAX_CHATS) {
        snprintf(out, out_size, "Chat not found\n");
        return;
    }

    struct chat_history *ch = &chats[chat_id];
    if (ch->count == 0) {
        snprintf(out, out_size, "No messages yet\n");
        return;
    }

    int start = (ch->count < MSG_LIMIT) ? 0 : ch->head;
    for (int i = 0; i < ch->count; i++) {
        int idx = (start + i) % MSG_LIMIT;
        strncat(out, ch->messages[idx], out_size - strlen(out) - 1);
    }
}

int main(void)
{
    struct sockaddr_nl src_addr, dest_addr;
    struct nlmsghdr *nlh;
    struct iovec iov;
    struct msghdr msg;
    int sock_fd;
    struct tg_msg *payload;

    add_message(1, "Привет! Это чат 1");
    add_message(1, "Добро пожаловать в обсуждение");
    add_message(2, "Чат 2: здесь будет важная информация");
    add_message(3, "Чат 3 пока пуст");

    sock_fd = socket(PF_NETLINK, SOCK_RAW, NETLINK_TELEGRAM);
    if (sock_fd < 0) {
        perror("socket");
        return 1;
    }

    memset(&src_addr, 0, sizeof(src_addr));
    src_addr.nl_family = AF_NETLINK;
    src_addr.nl_pid = getpid();
    if (bind(sock_fd, (struct sockaddr *)&src_addr, sizeof(src_addr)) < 0) {
        perror("bind");
        close(sock_fd);
        return 1;
    }

    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.nl_family = AF_NETLINK;
    dest_addr.nl_pid = 0;
    dest_addr.nl_groups = 0;

    nlh = malloc(NLMSG_SPACE(sizeof(struct tg_msg)));
    if (!nlh) {
        perror("malloc");
        close(sock_fd);
        return 1;
    }
    memset(nlh, 0, NLMSG_SPACE(sizeof(struct tg_msg)));
    nlh->nlmsg_len = NLMSG_SPACE(sizeof(struct tg_msg));
    nlh->nlmsg_pid = getpid();
    nlh->nlmsg_flags = 0;

    payload = (struct tg_msg *)NLMSG_DATA(nlh);

    payload->type = TG_INIT;
    iov.iov_base = nlh;
    iov.iov_len = nlh->nlmsg_len;
    msg.msg_name = &dest_addr;
    msg.msg_namelen = sizeof(dest_addr);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    if (sendmsg(sock_fd, &msg, 0) < 0) {
        perror("sendmsg INIT");
        free(nlh);
        close(sock_fd);
        return 1;
    }

    printf("Telegram daemon started. Waiting for kernel requests...\n");

    while (1) {
        ssize_t ret = recvmsg(sock_fd, &msg, 0);
        if (ret < 0) {
            perror("recvmsg");
            break;
        }

        switch (payload->type) {
        case TG_READ_REQ:
            printf("READ request for chat %d\n", payload->chat_id);
            payload->type = TG_READ_RES;
            get_history(payload->chat_id, payload->data, MAX_PAYLOAD);
            sendmsg(sock_fd, &msg, 0);
            break;

        case TG_WRITE_REQ:
            printf("WRITE request for chat %d: %s\n", payload->chat_id, payload->data);
            add_message(payload->chat_id, payload->data);
            payload->type = TG_WRITE_RES;
            strcpy(payload->data, "OK\n");
            sendmsg(sock_fd, &msg, 0);
            break;

        default:
            fprintf(stderr, "Unknown message type %d\n", payload->type);
        }
    }

    free(nlh);
    close(sock_fd);
    return 0;
}
