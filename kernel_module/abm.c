#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/pid.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/timer.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("SU");
MODULE_DESCRIPTION("Monitor and set accessed bits for processes");
MODULE_VERSION("0.1");

#define DEVICE_NAME "accessbit_monitor"
#define CLASS_NAME "abm"
#define MAX_PIDS 10
#define TIMER_INTERVAL 5000 // 5 seconds

static int majorNumber;
static struct class* abmClass = NULL;
static struct device* abmDevice = NULL;
static struct cdev abmCdev;
static struct timer_list my_timer;
static pid_t pids[MAX_PIDS];
static int num_pids = 0;

// Linked list to store access information
struct access_info {
    pid_t pid;
    unsigned long address;
    struct access_info *next;
};

static struct access_info *head = NULL;
static DEFINE_MUTEX(access_info_mutex);

// Function to log page access
static void log_page_access(pid_t pid, unsigned long address) {
    struct access_info *new_node = kmalloc(sizeof(struct access_info), GFP_KERNEL);
    if (!new_node) {
        return;
    }

    new_node->pid = pid;
    new_node->address = address;
    new_node->next = NULL;

    mutex_lock(&access_info_mutex);
    new_node->next = head;
    head = new_node;
    mutex_unlock(&access_info_mutex);
}

// Function to set accessed bit for a PID
static void set_accessed_bit_for_pid(pid_t pid) {
    struct task_struct *task;
    struct mm_struct *mm;
    struct vm_area_struct *vma;
    unsigned long address;
    pgd_t *pgd;
    p4d_t *p4d;
    pud_t *pud;
    pmd_t *pmd;
    pte_t *pte;

    task = pid_task(find_vpid(pid), PIDTYPE_PID);
    if (!task || !(mm = task->mm))
        return;

    for (vma = mm->mmap; vma; vma = vma->vm_next) {
        for (address = vma->vm_start; address < vma->vm_end; address += PAGE_SIZE) {
            pgd = pgd_offset(mm, address);
            if (pgd_none(*pgd) || pgd_bad(*pgd))
                continue;
            p4d = p4d_offset(pgd, address);
            if (p4d_none(*p4d) || p4d_bad(*p4d))
                continue;
            pud = pud_offset(p4d, address);
            if (pud_none(*pud) || pud_bad(*pud))
                continue;
            pmd = pmd_offset(pud, address);
            if (pmd_none(*pmd) || pmd_bad(*pmd))
                continue;
            pte = pte_offset_map(pmd, address);
            if (!pte)
                continue;

            if (pte_young(*pte)) {
                log_page_access(pid, address);
                *pte = pte_mkold(*pte);
                set_pte_at(mm, address, pte, *pte);
            }
            pte_unmap(pte);
        }
    }
}

// Timer callback function
static void timer_callback(struct timer_list *t) {
    for (int i = 0; i < num_pids; i++) {
        set_accessed_bit_for_pid(pids[i]);
    }
    mod_timer(&my_timer, jiffies + msecs_to_jiffies(TIMER_INTERVAL));
}

// Write operation for the character device
ssize_t dev_write(struct file *filep, const char __user *buffer, size_t len, loff_t *offset) {
    int ret;
    char pid_buffer[10];

    if (len > sizeof(pid_buffer) - 1)
        len = sizeof(pid_buffer) - 1;

    if (copy_from_user(pid_buffer, buffer, len))
        return -EFAULT;

    pid_buffer[len] = '\0';
    ret = kstrtoint(pid_buffer, 10, &pids[num_pids]);
    if (ret)
        return -EINVAL;

    if (num_pids < MAX_PIDS - 1)
        num_pids++;

    return len;
}

// Read operation for the character device
static ssize_t dev_read(struct file *filep, char __user *buffer, size_t len, loff_t *offset) {
    struct access_info *current_node;
    int error_count = 0;
    size_t count = 0;

    mutex_lock(&access_info_mutex);
    current_node = head;
    while (current_node != NULL && count < len) {
        count += snprintf(buffer + count, len - count, "PID: %d, Address: 0x%lx\n", current_node->pid, current_node->address);
        current_node = current_node->next;
    }
    mutex_unlock(&access_info_mutex);

    error_count = copy_to_user(buffer, buffer, count);

    return (error_count == 0) ? count : -EFAULT;
}

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .read = dev_read,
    .write = dev_write,
};

static int __init abm_init(void) {
    majorNumber = register_chrdev(0, DEVICE_NAME, &fops);
    if (majorNumber < 0) {
        return majorNumber;
    }

    abmClass = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(abmClass)) {
        unregister_chrdev(majorNumber, DEVICE_NAME);
        return PTR_ERR(abmClass);
    }

    abmDevice = device_create(abmClass, NULL, MKDEV(majorNumber, 0), NULL, DEVICE_NAME);
    if (IS_ERR(abmDevice)) {
        class_destroy(abmClass);
        unregister_chrdev(majorNumber, DEVICE_NAME);
        return PTR_ERR(abmDevice);
    }

    cdev_init(&abmCdev, &fops);
    cdev_add(&abmCdev, MKDEV(majorNumber, 0), 1);

    // Setup and start the timer
    timer_setup(&my_timer, timer_callback, 0);
    mod_timer(&my_timer, jiffies + msecs_to_jiffies(TIMER_INTERVAL));

    printk(KERN_INFO "%s: device class created correctly\n", DEVICE_NAME); 
    return 0;
}

static void __exit abm_exit(void) {
    cdev_del(&abmCdev);
    device_destroy(abmClass, MKDEV(majorNumber, 0));
    class_destroy(abmClass);
    unregister_chrdev(majorNumber, DEVICE_NAME);

    // Deactivate the timer
    del_timer(&my_timer);

    // Free the access info list
    while (head != NULL) {
        struct access_info *temp = head;
        head = head->next;
        kfree(temp);
    }

    printk(KERN_INFO "%s: device class destroyed\n", DEVICE_NAME);
}

module_init(abm_init);
module_exit(abm_exit);
