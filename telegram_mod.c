#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/netlink.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <linux/mutex.h>
#include <linux/wait.h>

#define NETLINK_TELEGRAM 31
#define MAX_PAYLOAD 1024

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

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Student");
MODULE_DESCRIPTION("Telegram file interface via procfs + netlink");

static struct sock *nl_sk = NULL;
static int daemon_pid = -1;

static DEFINE_MUTEX(tg_mutex);
static DECLARE_WAIT_QUEUE_HEAD(tg_wait);
static int tg_reply_ready = 0;
static struct tg_msg global_reply;

static struct proc_dir_entry *proc_dir;

static int send_to_daemon(struct tg_msg *msg)
{
    struct sk_buff *skb;
    struct nlmsghdr *nlh;
    int res;

    if (daemon_pid == -1) {
        pr_warn("telegram: Daemon not registered\n");
        return -ENODEV;
    }

    skb = nlmsg_new(sizeof(struct tg_msg), GFP_KERNEL);
    if (!skb)
        return -ENOMEM;

    nlh = nlmsg_put(skb, 0, 0, NLMSG_DONE, sizeof(struct tg_msg), 0);
    memcpy(nlmsg_data(nlh), msg, sizeof(struct tg_msg));

    res = nlmsg_unicast(nl_sk, skb, daemon_pid);
    if (res < 0)
        pr_err("telegram: nlmsg_unicast error %d\n", res);
    return res;
}

static void nl_recv_msg(struct sk_buff *skb)
{
    struct nlmsghdr *nlh = (struct nlmsghdr *)skb->data;
    struct tg_msg *msg = (struct tg_msg *)nlmsg_data(nlh);

    if (msg->type == TG_INIT) {
        daemon_pid = nlh->nlmsg_pid;
        pr_info("telegram: Daemon registered (PID %d)\n", daemon_pid);
    } else if (msg->type == TG_READ_RES || msg->type == TG_WRITE_RES) {
        memcpy(&global_reply, msg, sizeof(struct tg_msg));
        tg_reply_ready = 1;
        wake_up_interruptible(&tg_wait);
    }
}

static int chat_open(struct inode *inode, struct file *file)
{
    file->private_data = pde_data(inode);
    return 0;
}

static int chat_release(struct inode *inode, struct file *file)
{
    return 0;
}

static ssize_t chat_read(struct file *file, char __user *ubuf,
                         size_t count, loff_t *ppos)
{
    struct tg_msg req;
    long chat_id = (long)file->private_data;
    size_t len;

    if (*ppos > 0)
        return 0;

    mutex_lock(&tg_mutex);
    tg_reply_ready = 0;

    req.type = TG_READ_REQ;
    req.chat_id = (int)chat_id;
    if (send_to_daemon(&req) < 0) {
        mutex_unlock(&tg_mutex);
        return -ENODEV;
    }

    if (wait_event_interruptible(tg_wait, tg_reply_ready != 0)) {
        mutex_unlock(&tg_mutex);
        return -ERESTARTSYS;
    }

    len = strnlen(global_reply.data, MAX_PAYLOAD);
    if (len > count)
        len = count;

    if (copy_to_user(ubuf, global_reply.data, len)) {
        mutex_unlock(&tg_mutex);
        return -EFAULT;
    }

    *ppos = len;
    mutex_unlock(&tg_mutex);
    return len;
}

static ssize_t chat_write(struct file *file, const char __user *ubuf,
                          size_t count, loff_t *ppos)
{
    struct tg_msg req;
    long chat_id = (long)file->private_data;
    size_t copy_len;

    mutex_lock(&tg_mutex);
    tg_reply_ready = 0;

    req.type = TG_WRITE_REQ;
    req.chat_id = (int)chat_id;
    copy_len = min_t(size_t, count, MAX_PAYLOAD - 1);
    if (copy_from_user(req.data, ubuf, copy_len)) {
        mutex_unlock(&tg_mutex);
        return -EFAULT;
    }
    req.data[copy_len] = '\0';

    if (send_to_daemon(&req) < 0) {
        mutex_unlock(&tg_mutex);
        return -ENODEV;
    }

    if (wait_event_interruptible(tg_wait, tg_reply_ready != 0)) {
        mutex_unlock(&tg_mutex);
        return -ERESTARTSYS;
    }

    mutex_unlock(&tg_mutex);
    return count;
}

static const struct proc_ops chat_fops = {
    .proc_open    = chat_open,
    .proc_read    = chat_read,
    .proc_write   = chat_write,
    .proc_release = chat_release,
};

static int __init tg_init(void)
{
    struct netlink_kernel_cfg cfg = {
        .input = nl_recv_msg,
    };

    nl_sk = netlink_kernel_create(&init_net, NETLINK_TELEGRAM, &cfg);
    if (!nl_sk)
        return -ENOMEM;

    proc_dir = proc_mkdir("telegram", NULL);
    if (!proc_dir) {
        netlink_kernel_release(nl_sk);
        return -ENOMEM;
    }

    proc_create_data("chat1", 0666, proc_dir, &chat_fops, (void *)1);
    proc_create_data("chat2", 0666, proc_dir, &chat_fops, (void *)2);
    proc_create_data("chat3", 0666, proc_dir, &chat_fops, (void *)3);

    pr_info("telegram: module loaded, /proc/telegram/chat[1-3]\n");
    return 0;
}

static void __exit tg_exit(void)
{
    remove_proc_entry("chat1", proc_dir);
    remove_proc_entry("chat2", proc_dir);
    remove_proc_entry("chat3", proc_dir);
    remove_proc_entry("telegram", NULL);
    netlink_kernel_release(nl_sk);
    pr_info("telegram: module unloaded\n");
}

module_init(tg_init);
module_exit(tg_exit);
