#include "hw_ipc.h"
#include "hw_irq.h"

// Instantiate IPC hardware states
static IPCSync sync_arm9;
static IPCSync sync_arm7;

static IPCFIFOControl<16> fifo_control_arm9;
static IPCFIFOControl<16> fifo_control_arm7;

static SPSCQueue<uint32_t, 16> queue_9to7;
static SPSCQueue<uint32_t, 16> queue_7to9;

static HWIRQ* irq_arm9 = nullptr;
static HWIRQ* irq_arm7 = nullptr;

void InitIPC(HWIRQ* arm9_irq, HWIRQ* arm7_irq) {
    fifo_control_arm9.send_fifo = &queue_9to7;
    fifo_control_arm9.recv_fifo = &queue_7to9;
    
    fifo_control_arm7.send_fifo = &queue_7to9;
    fifo_control_arm7.recv_fifo = &queue_9to7;

    irq_arm9 = arm9_irq;
    irq_arm7 = arm7_irq;
}

void SendFIFO_ARM9toARM7(uint32_t data) {
    if (fifo_control_arm9.fifo_enabled) {
        if (!queue_9to7.push(data)) {
            fifo_control_arm9.error_flag = true;
        } else {
            // If the receiver has "recv not empty IRQ" enabled and it just became not empty
            if (fifo_control_arm7.recv_nempty_irq && queue_9to7.size() == 1 && irq_arm7) {
                irq_arm7->RaiseIRQ(IRQBits::IPCRecvFIFO);
            }
        }
    }
}

uint32_t ReceiveFIFO_ARM9toARM7() {
    uint32_t item = 0;
    if (fifo_control_arm9.fifo_enabled) {
        if (!queue_7to9.pop(item)) {
            fifo_control_arm9.error_flag = true;
        } else {
            // Trigger remote Send Empty IRQ if it became empty
            if (fifo_control_arm7.send_empty_irq && queue_7to9.empty() && irq_arm7) {
                irq_arm7->RaiseIRQ(IRQBits::IPCSendFIFO);
            }
        }
    }
    return item;
}

void SendFIFO_ARM7toARM9(uint32_t data) {
    if (fifo_control_arm7.fifo_enabled) {
        if (!queue_7to9.push(data)) {
            fifo_control_arm7.error_flag = true;
        } else {
            if (fifo_control_arm9.recv_nempty_irq && queue_7to9.size() == 1 && irq_arm9) {
                irq_arm9->RaiseIRQ(IRQBits::IPCRecvFIFO);
            }
        }
    }
}

uint32_t ReceiveFIFO_ARM7toARM9() {
    uint32_t item = 0;
    if (fifo_control_arm7.fifo_enabled) {
        if (!queue_9to7.pop(item)) {
            fifo_control_arm7.error_flag = true;
        } else {
            if (fifo_control_arm9.send_empty_irq && queue_9to7.empty() && irq_arm9) {
                irq_arm9->RaiseIRQ(IRQBits::IPCSendFIFO);
            }
        }
    }
    return item;
}

void AcknowledgeIPC_ARM7() {
    if (irq_arm7) {
        irq_arm7->AcknowledgeIRQ(IRQBits::IPCSync | IRQBits::IPCRecvFIFO | IRQBits::IPCSendFIFO);
    }
}
