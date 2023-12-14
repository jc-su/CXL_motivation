from bcc import BPF
import ctypes

target_pid = 45408

b = BPF(text='''
#include <uapi/linux/ptrace.h>
#include <linux/mm_types.h>

struct data_t {
    u64 addr;
    u64 page;
};
BPF_PERF_OUTPUT(events);
BPF_HASH(target_pid, u32, u8);

int fault_handler(struct pt_regs *ctx, struct vm_area_struct *vma, unsigned long address) {
    u32 pid = bpf_get_current_pid_tgid() >> 32;
    u8 *exists = target_pid.lookup(&pid);
    if (!exists) {
        return 0;
    }

    struct data_t data = {};
    data.addr = address;
    data.page = address >> 12; // page size 4KB
    events.perf_submit(ctx, &data, sizeof(data));
    return 0;
}
''')

b.attach_kprobe(event="handle_mm_fault", fn_name="fault_handler")

# Set target PID
pid_key = ctypes.c_uint32(target_pid)
pid_value = ctypes.c_uint8(1)
b["target_pid"][pid_key] = pid_value

class Data(ctypes.Structure):
    _fields_ = [("addr", ctypes.c_ulonglong), ("page", ctypes.c_ulonglong)]

def print_event(cpu, data, size):
    event = ctypes.cast(data, ctypes.POINTER(Data)).contents
    print("Page Fault at address: 0x%x, Page Number: %d" % (event.addr, event.page))

b["events"].open_perf_buffer(print_event)
while True:
    try:
        b.perf_buffer_poll()
    except KeyboardInterrupt:
        exit()
