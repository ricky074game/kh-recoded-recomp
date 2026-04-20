#pragma once

// ============================================================================
// hw_ipc.h — Nintendo DS Inter-Process Communication (IPC) Emulation
//
// The ARM9 and ARM7 communicate via:
//   - IPCSYNC (0x04000180): Simple 4-bit bidirectional sync register
//   - IPC FIFO (0x04000184-0x0400018C): 16-word deep hardware FIFO queue
//
// The FIFO is modeled as a lock-free Single-Producer Single-Consumer queue
// for maximum throughput between the two CPU threads.
//
// Reference: GBATEK §DS Inter Process Communication
// ============================================================================

#include <atomic>
#include <cstdint>
#include <cstddef>

class HWIRQ;

// ============================================================================
// Lock-Free SPSC Queue
// Used for the 16-word IPC FIFO between ARM9 and ARM7 threads.
// ============================================================================
template <typename T, size_t Capacity>
class SPSCQueue {
private:
    T buffer[Capacity + 1];
    std::atomic<size_t> head{0};
    std::atomic<size_t> tail{0};

    static constexpr size_t kSize = Capacity + 1;

public:
    SPSCQueue() = default;

    // Push an item. Returns false if the queue is full.
    bool push(const T& item) {
        size_t current_tail = tail.load(std::memory_order_relaxed);
        size_t next_tail = (current_tail + 1) % kSize;

        if (next_tail == head.load(std::memory_order_acquire)) {
            return false; // Full
        }

        buffer[current_tail] = item;
        tail.store(next_tail, std::memory_order_release);
        return true;
    }

    // Pop an item. Returns false if the queue is empty.
    bool pop(T& item) {
        size_t current_head = head.load(std::memory_order_relaxed);

        if (current_head == tail.load(std::memory_order_acquire)) {
            return false; // Empty
        }

        item = buffer[current_head];
        head.store((current_head + 1) % kSize, std::memory_order_release);
        return true;
    }

    // Peek at the front item without removing it.
    bool peek(T& item) const {
        size_t current_head = head.load(std::memory_order_relaxed);
        if (current_head == tail.load(std::memory_order_acquire)) {
            return false;
        }
        item = buffer[current_head];
        return true;
    }

    // Check if the queue is empty.
    bool empty() const {
        return head.load(std::memory_order_acquire) == tail.load(std::memory_order_acquire);
    }

    // Check if the queue is full.
    bool full() const {
        size_t current_tail = tail.load(std::memory_order_relaxed);
        size_t next_tail = (current_tail + 1) % kSize;
        return next_tail == head.load(std::memory_order_acquire);
    }

    // Returns the current number of items in the queue.
    size_t size() const {
        size_t h = head.load(std::memory_order_acquire);
        size_t t = tail.load(std::memory_order_acquire);
        if (t >= h) return t - h;
        return kSize - h + t;
    }

    // Clear the queue.
    void clear() {
        head.store(0, std::memory_order_release);
        tail.store(0, std::memory_order_release);
    }
};

// ============================================================================
// IPCSYNC Register Emulation
// 0x04000180 — Bidirectional 4-bit sync register.
//   Bits 0-3:  Data input from remote CPU (read-only)
//   Bits 8-11: Data output to remote CPU (read-write)
//   Bit 13:    Send IRQ to remote CPU (write-only)
//   Bit 14:    Enable IRQ from remote CPU (read-write)
// ============================================================================
struct IPCSync {
    uint32_t local_send = 0; // Bits 8-11 — what THIS cpu sends
    uint32_t remote_send = 0; // Bits 0-3 — what the OTHER cpu sends (read-only from this side)
    bool     irq_enable = false; // Bit 14

    // Read the register from this CPU's perspective.
    uint32_t Read() const {
        return (remote_send & 0xF) | ((local_send & 0xF) << 8) | (irq_enable ? (1u << 14) : 0);
    }

    // Write the register from this CPU's perspective.
    // Returns true if bit 13 was set (send IRQ to remote).
    bool Write(uint32_t value, IPCSync& remote) {
        local_send = (value >> 8) & 0xF;
        irq_enable = (value >> 14) & 1;

        // Update the remote side's "remote_send" to be our "local_send"
        remote.remote_send = local_send;

        // Bit 13: send IRQ to remote CPU
        return (value >> 13) & 1;
    }
};

// ============================================================================
// IPC FIFO Control Register (IPCFIFOCNT, 0x04000184)
//   Bit 0:   Send FIFO Empty (read-only)
//   Bit 1:   Send FIFO Full (read-only)
//   Bit 2:   Send FIFO Empty IRQ enable
//   Bit 3:   Send FIFO Clear (write-only)
//   Bit 8:   Recv FIFO Empty (read-only)
//   Bit 9:   Recv FIFO Full (read-only)
//   Bit 10:  Recv FIFO Not-Empty IRQ enable
//   Bit 14:  Error flag (read-only, set on read-when-empty or write-when-full)
//   Bit 15:  FIFO Enable
// ============================================================================
template <size_t FIFOSize = 16>
struct IPCFIFOControl {
    bool send_empty_irq   = false;
    bool recv_nempty_irq  = false;
    bool fifo_enabled     = false;
    bool error_flag       = false;

    SPSCQueue<uint32_t, FIFOSize>* send_fifo = nullptr;
    SPSCQueue<uint32_t, FIFOSize>* recv_fifo = nullptr;

    uint16_t Read() const {
        uint16_t val = 0;
        if (send_fifo) {
            if (send_fifo->empty()) val |= (1u << 0);
            if (send_fifo->full())  val |= (1u << 1);
        }
        if (send_empty_irq)  val |= (1u << 2);
        if (recv_fifo) {
            if (recv_fifo->empty()) val |= (1u << 8);
            if (recv_fifo->full())  val |= (1u << 9);
        }
        if (recv_nempty_irq) val |= (1u << 10);
        if (error_flag)      val |= (1u << 14);
        if (fifo_enabled)    val |= (1u << 15);
        return val;
    }

    void Write(uint16_t value) {
        send_empty_irq  = (value >> 2) & 1;
        recv_nempty_irq = (value >> 10) & 1;
        fifo_enabled    = (value >> 15) & 1;
        if ((value >> 3) & 1) {
            // Clear send FIFO
            if (send_fifo) send_fifo->clear();
        }
        // Writing 1 to bit 14 acknowledges the error flag
        if ((value >> 14) & 1) error_flag = false;
    }
};

void InitIPC(HWIRQ* arm9_irq = nullptr, HWIRQ* arm7_irq = nullptr);
void SendFIFO_ARM9toARM7(uint32_t data);
uint32_t ReceiveFIFO_ARM9toARM7();
void SendFIFO_ARM7toARM9(uint32_t data);
uint32_t ReceiveFIFO_ARM7toARM9();
void AcknowledgeIPC_ARM7();
