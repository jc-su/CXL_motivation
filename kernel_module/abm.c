#include <asm/pgtable.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/highmem.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/uaccess.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("SU");
MODULE_DESCRIPTION("Monitor and set accessed bits for processes");
MODULE_VERSION("0.1");

#define DEVICE_NAME "accessbit_monitor"
#define CLASS_NAME "abm"
#define MAX_PIDS 10
#define TIMER_INTERVAL 5000 // 5 seconds

static int majorNumber;
static struct class *abmClass = NULL;
static struct device *abmDevice = NULL;
static struct timer_list abmTimer;
static pid_t pids[MAX_PIDS];
static int num_pids = 0;

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

      if (pte_present(*pte)) {
        *pte = pte_mkold(*pte);
        set_pte_at(mm, address, pte, *pte);
      }
      pte_unmap(pte);
    }
  }
}

static void timer_callback(struct timer_list *timer) {
  int i;
  for (i = 0; i < num_pids; i++) {
    set_accessed_bit_for_pid(pids[i]);
  }
  mod_timer(timer, jiffies + msecs_to_jiffies(TIMER_INTERVAL));
}

ssize_t dev_write(struct file *filep, const char __user *buffer, size_t len,
                  loff_t *offset) {
  char pid_buffer[10];
  int i, ret;

  if (len > 10 * MAX_PIDS)
    len = 10 * MAX_PIDS;
  if (copy_from_user(pid_buffer, buffer, len))
    return -EFAULT;
  pid_buffer[len] = '\0';

  num_pids = 0;
  for (i = 0; i < len && num_pids < MAX_PIDS; i += 10) {
    ret = sscanf(pid_buffer + i, "%d", &pids[num_pids]);
    if (ret > 0)
      num_pids++;
  }

  return len;
}

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .write = dev_write,
};

static int __init abm_init(void) {
  majorNumber = register_chrdev(0, DEVICE_NAME, &fops);
  if (majorNumber < 0)
    return majorNumber;

  abmClass = class_create(THIS_MODULE, CLASS_NAME);
  if (IS_ERR(abmClass)) {
    unregister_chrdev(majorNumber, DEVICE_NAME);
    return PTR_ERR(abmClass);
  }

  abmDevice =
      device_create(abmClass, NULL, MKDEV(majorNumber, 0), NULL, DEVICE_NAME);
  if (IS_ERR(abmDevice)) {
    class_destroy(abmClass);
    unregister_chrdev(majorNumber, DEVICE_NAME);
    return PTR_ERR(abmDevice);
  }

  timer_setup(&abmTimer, timer_callback, 0);
  mod_timer(&abmTimer, jiffies + msecs_to_jiffies(TIMER_INTERVAL));

  return 0;
}

static void __exit abm_exit(void) {
  del_timer(&abmTimer);
  device_destroy(abmClass, MKDEV(majorNumber, 0));
  class_destroy(abmClass);
  unregister_chrdev(majorNumber, DEVICE_NAME);
}

module_init(abm_init);
module_exit(abm_exit);
