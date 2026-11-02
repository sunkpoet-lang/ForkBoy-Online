// The link cable (src/link/serial_link.h): the serial port's surfaces and the two links, against the
// Game Boy's serial port behaviour the original's two-player mode relies on.

#include <gtest/gtest.h>

#include <type_traits>

#include "link/serial_link.h"

namespace {

using kirpich::link::InProcessCable;
using kirpich::link::NoCable;
using kirpich::link::SerialPort;

constexpr std::uint8_t kMaster = 0x29;           // SerialRole::MASTER, the handshake code
constexpr std::uint8_t kSlave = 0x55;            // SerialRole::SLAVE
constexpr std::uint8_t kStartInternal = 0x81;    // SerialClockMode::INTERNAL: start, our clock
constexpr std::uint8_t kArmExternal = 0x80;      // SerialClockMode::EXTERNAL: arm, the partner's clock

TEST(SerialLink, PortBootsIdleWithTheLineHigh) {
    SerialPort port;
    EXPECT_EQ(port.data, 0x00);
    EXPECT_FALSE(port.transferring);
    EXPECT_FALSE(port.completed);
    EXPECT_EQ(port.idleLine(), 0xFF);
    EXPECT_EQ(port.readControl(), 0x7E);
}

TEST(SerialLink, ControlWritesStartAndClock) {
    SerialPort port;
    port.writeControl(kStartInternal);
    EXPECT_TRUE(port.transferring);
    EXPECT_TRUE(port.internalClock);
    EXPECT_EQ(port.readControl(), 0xFF);
    port.writeControl(kArmExternal);
    EXPECT_TRUE(port.transferring);
    EXPECT_FALSE(port.internalClock);
    port.writeControl(0x01); // (Handshake's master write: internal clock, nothing started)
    EXPECT_FALSE(port.transferring);
    EXPECT_TRUE(port.internalClock);
}

TEST(SerialLink, CompletionIsTakenOnce) {
    SerialPort port;
    port.finish(0x42);
    EXPECT_TRUE(port.takeCompleted());
    EXPECT_FALSE(port.takeCompleted());
}

TEST(SerialLink, NoCableMasterReceivesFF) {
    NoCable link;
    link.port.data = kMaster;
    link.port.writeControl(kStartInternal);
    link.resolve();
    EXPECT_EQ(link.port.data, 0xFF);
    EXPECT_FALSE(link.port.transferring);
    EXPECT_TRUE(link.port.takeCompleted());
}

TEST(SerialLink, NoCableSlaveWaitsForever) {
    NoCable link;
    link.port.data = kSlave;
    link.port.writeControl(kArmExternal);
    for (int i = 0; i < 100; i++) {
        link.resolve();
    }
    EXPECT_EQ(link.port.data, kSlave);
    EXPECT_TRUE(link.port.transferring);
    EXPECT_FALSE(link.port.completed);
}

TEST(SerialLink, BothBytesCrossInOneTransfer) {
    InProcessCable cable;
    cable.a.data = kMaster;
    cable.a.writeControl(kStartInternal);
    cable.b.data = kSlave;
    cable.b.writeControl(kArmExternal);
    cable.resolve();
    EXPECT_EQ(cable.a.data, kSlave);
    EXPECT_EQ(cable.b.data, kMaster);
    EXPECT_FALSE(cable.a.transferring);
    EXPECT_FALSE(cable.b.transferring);
    EXPECT_TRUE(cable.a.takeCompleted());
    EXPECT_TRUE(cable.b.takeCompleted());
}

TEST(SerialLink, TheSidesAreSymmetric) {
    InProcessCable cable;
    cable.a.data = kSlave;
    cable.a.writeControl(kArmExternal);
    cable.b.data = kMaster;
    cable.b.writeControl(kStartInternal);
    cable.resolve();
    EXPECT_EQ(cable.a.data, kMaster);
    EXPECT_EQ(cable.b.data, kSlave);
}

TEST(SerialLink, AnArmedSlaveWaitsForTheMaster) {
    InProcessCable cable;
    cable.b.data = kSlave;
    cable.b.writeControl(kArmExternal);
    cable.resolve();
    EXPECT_TRUE(cable.b.transferring);
    EXPECT_FALSE(cable.b.completed);
    EXPECT_EQ(cable.b.data, kSlave);
    // ... and moves when the master starts
    cable.a.data = 0x60;
    cable.a.writeControl(kStartInternal);
    cable.resolve();
    EXPECT_EQ(cable.b.data, 0x60);
    EXPECT_TRUE(cable.b.takeCompleted());
}

TEST(SerialLink, AMasterWithAnUnarmedPartnerReadsItsLine) {
    InProcessCable cable;
    // After boot the partner's line is high: $FF, as with no cable
    cable.a.data = kMaster;
    cable.a.writeControl(kStartInternal);
    cable.resolve();
    EXPECT_EQ(cable.a.data, 0xFF);
    EXPECT_FALSE(cable.b.completed); // (the partner shifted nothing and was not interrupted)
    EXPECT_EQ(cable.b.data, 0x00);
    // Once the partner's last byte ended in a 0 bit, its line is low: $00
    cable.b.data = 0x30;
    cable.b.writeControl(kArmExternal);
    cable.a.writeControl(kStartInternal);
    cable.resolve();
    EXPECT_TRUE(cable.b.takeCompleted());
    cable.a.writeControl(kStartInternal);
    cable.resolve();
    EXPECT_EQ(cable.a.data, 0x00);
}

TEST(SerialLink, NothingMovesWithoutAMaster) {
    InProcessCable cable;
    cable.a.data = 0x12;
    cable.b.data = 0x34;
    cable.resolve();
    EXPECT_EQ(cable.a.data, 0x12);
    EXPECT_EQ(cable.b.data, 0x34);
    EXPECT_FALSE(cable.a.completed);
    EXPECT_FALSE(cable.b.completed);
}

TEST(SerialLink, ThePortIsPlainStateForSaveStates) {
    static_assert(std::is_trivially_copyable_v<SerialPort>);
    static_assert(std::is_trivially_copyable_v<InProcessCable>);
    InProcessCable cable;
    cable.a.data = kMaster;
    cable.a.writeControl(kStartInternal);
    const InProcessCable saved = cable;
    cable.resolve();
    cable = saved; // (a rollback restores the cable as it was)
    EXPECT_TRUE(cable.a.transferring);
    EXPECT_EQ(cable.a.data, kMaster);
}

}  // namespace
