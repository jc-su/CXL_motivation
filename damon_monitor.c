#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/signal.h>
#include <linux/sched/task.h>
#include <linux/damon.h>


static struct task_struct *registered_task = NULL;
static struct damon_ctx *ctx = NULL;

static int register_pid_for_damon(unsigned long pid) {
    struct damon_target *target;

    if (!ctx) {
        ctx = damon_new_ctx();
        if (!ctx)
            return -ENOMEM;
    }

    target = damon_new_target();
    if (!target)
        return -ENOMEM;

    target->id = pid;
    damon_add_target(ctx, target);
    damon_set_attrs(ctx, 1000, 1000, 1000, 1, 100);

    if (damon_start(&ctx, 1)) {
        printk(KERN_ERR "Failed to start DAMON for PID %lu\n", pid);
        return -EIO;
    }

    printk(KERN_INFO "DAMON monitoring started for PID %lu\n", pid);
    return 0;
}

static void unregister_damon(void) {
    if (ctx) {
        damon_stop(&ctx, 1);
        damon_destroy_ctx(ctx);
        ctx = NULL;
        printk(KERN_INFO "DAMON monitoring stopped\n");
    }
}

static void signal_handler(struct task_struct *task, struct siginfo *info) {
    unsigned long pid;

    if (!task || !info)
        return;

    if (task != registered_task)
        return;

    pid = (unsigned long)info->si_value.sival_ptr;

    switch(info->si_signo) {
    case SIGUSR1:
        register_pid_for_damon(pid);
        break;
    case SIGUSR2:
        unregister_damon();
        break;
    default:
        break;
    }
}

static int __init test_module_init(void) {
    struct sigaction act = {
        .sa_sigaction = (void (*)(int, struct siginfo *, void *))signal_handler,
        .sa_flags = SA_SIGINFO
    };

    sigaction(SIGUSR1, &act, NULL);
    sigaction(SIGUSR2, &act, NULL);


    registered_task = current;

    printk(KERN_INFO "Module loaded\n");
    return 0;
}

static void __exit test_module_exit(void) {
    unregister_damon();
    printk(KERN_INFO "Module unloaded\n");
}

module_init(test_module_init);
module_exit(test_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("SU");
MODULE_DESCRIPTION("DAMON monitoring");
