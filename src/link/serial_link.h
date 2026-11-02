#pragma once

// The link cable: what stands in for the Game Boy's serial port (rSB, rSC and the serial interrupt) so two
// Game Boys can play head to head. The original drives its two-player mode entirely through that port
// (docs/contracts/two-player-protocol.md): it writes the outgoing byte to rSB, starts or arms a transfer
// through rSC, and runs its serial dispatch when the interrupt says a transfer finished. The port keeps
// exactly those three surfaces, and a link decides when transfers finish and what each side receives.
//
// How the hardware behaves, which the link reproduces (Pan Docs, "Serial Data Transfer"):
//
//   - A transfer moves one byte each way at once: both sides shift their rSB out, most significant bit
//     first, and shift the other's in. There are no start or stop bits.
//   - The side with the internal clock (rSC bit 0 set) is the master: setting rSC bit 7 starts the
//     transfer at once. The side with the external clock is the slave: setting bit 7 only arms it, and
//     it shifts when the master's clock arrives. An armed slave with no master waits forever.
//   - When a transfer ends, rSB holds the received byte, rSC bit 7 is cleared, and the serial interrupt
//     fires (IF bit 3) — here, the port's completed flag.
//   - The output line keeps the last bit shifted out until the next transfer; idle, it is high. So a
//     master whose partner has not armed its transfer shifts in eight copies of that line: $FF after
//     boot (and with no cable at all), $00 if the partner's last byte ended in a 0 bit.
//
// What it does not model: the shift clock's timing (8192 Hz on a DMG). A transfer finishes at the
// link's next resolve() — the two-instance scheduler resolves after each side's frame, so a byte costs
// at most one step, and the original's waits (DelayMillisecond, WAIT_FOR_SERIAL_INTERRUPT) become "wait
// for the next resolve". Two masters at once is undefined on hardware (both drive the clock); here they
// exchange their bytes as if their clocks agreed.
//
// Everything here is plain state, so a link and its ports go into a save state as they are (online
// rollback saves both Game Boys and the cable between them).

#include <cstdint>

namespace kirpich::link {

// One Game Boy's serial port, as the game sees it.
struct SerialPort {
    std::uint8_t data = 0;        // rSB: the byte to send; the received byte once a transfer ends
    bool transferring = false;    // rSC bit 7: started (internal clock) or armed (external clock)
    bool internalClock = false;   // rSC bit 0: this side drives the clock (the master)
    bool completed = false;       // the serial interrupt: a transfer ended and the game has not taken it
    bool lineHigh = true;         // the output line: the last bit shifted out (idle high)

    // rSC as the original writes it: bit 7 starts or arms a transfer, bit 0 selects the internal clock.
    void writeControl(std::uint8_t value) {
        transferring = (value & 0x80) != 0;
        internalClock = (value & 0x01) != 0;
    }
    // rSC as the original reads it (bits 1-6 read as set on a DMG).
    [[nodiscard]] std::uint8_t readControl() const {
        return static_cast<std::uint8_t>(0x7E | (transferring ? 0x80 : 0) | (internalClock ? 0x01 : 0));
    }
    // The serial interrupt, once: true if a transfer ended since the last call (then the game runs its
    // serial dispatch), false otherwise.
    bool takeCompleted() {
        const bool was = completed;
        completed = false;
        return was;
    }
    // Ends a transfer on this side: `received` lands in rSB, bit 7 clears, the interrupt is raised, and
    // the output line keeps the last bit this side shifted out (bit 0 of what it sent).
    void finish(std::uint8_t received) {
        lineHigh = (data & 0x01) != 0;
        data = received;
        transferring = false;
        completed = true;
    }
    // What this side's partner shifts in from it when this side has not armed a transfer: its line.
    [[nodiscard]] std::uint8_t idleLine() const { return lineHigh ? 0xFF : 0x00; }
};

// A Game Boy with nothing in its link port: a transfer it starts with its own clock ends at once with $FF
// (its partner's line is "high" because there is no partner); a transfer it arms for an external clock
// never ends.
struct NoCable {
    SerialPort port;

    void resolve() {
        if (port.transferring && port.internalClock) {
            port.finish(0xFF);
        }
    }
};

// Two Game Boys joined by a cable in the same process, delivering every transfer exactly. Side A and side
// B are symmetric: whichever has started a transfer with its own clock is the master for that transfer.
struct InProcessCable {
    SerialPort a;
    SerialPort b;

    // Ends every transfer that can end now: a master's transfer always ends (with the partner's byte if
    // the partner armed its own transfer, else with the partner's idle line); a slave's ends only with a
    // master's. Called by the scheduler after each side's frame.
    void resolve() {
        const bool aMaster = a.transferring && a.internalClock;
        const bool bMaster = b.transferring && b.internalClock;
        if (!aMaster && !bMaster) {
            return; // (a slave waits for a clock; nothing is moving)
        }
        if (a.transferring && b.transferring) {
            // Both sides shift: the bytes swap.
            const std::uint8_t fromA = a.data;
            const std::uint8_t fromB = b.data;
            a.finish(fromB);
            b.finish(fromA);
            return;
        }
        // Only the master shifts; the other side's register and interrupt stay as they are.
        if (aMaster) {
            a.finish(b.idleLine());
        } else {
            b.finish(a.idleLine());
        }
    }
};

}  // namespace kirpich::link
