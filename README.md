Project: Embedded Linux Parking Management System (STM32 + BeagleBone Green)

 - Full end-to-end system spanning STM32, BBG, and a C++ TCP server with SQLite
 - STM32 firmware as an interrupt-driven I2C slave with non-blocking timer state machine
 - Dual-process BBG architecture connected via named pipe (FIFO)
 - C++ TCP server with poll()-based event loop, shared-memory pricing, SQLite persistence, and a C CLI price-update tool
