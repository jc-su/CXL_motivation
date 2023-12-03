from bcc import BPF

# eBPF program
prog = """
#include <linux/mm.h>

int monitor_pte_access(struct pt_regs *ctx, struct vm_area_struct *vma, unsigned long address) {
    pte_t *pte;

    // Check
    if (pte_young(*pte))
        bpf_trace_printk("Access bit set for address %lx\\n", address);
    else
        bpf_trace_printk("Access bit not set for address %lx\\n", address);

    return 0;
}
"""

b = BPF(text=prog)

b.attach_kprobe(event="handle_pte_fault", fn_name="monitor_pte_access")

while True:
    try:
        (task, pid, cpu, flags, ts, msg) = b.trace_fields()
        print("%-18.9f %-16s %-6d %s" % (ts, task, pid, msg))
    except KeyboardInterrupt:
        exit()
