eMMC Memory
============

The plan is now to use the H7 chip that has a very high clock rate and can support the eMMC memory at a much higher clock rate (read and write speeds)

Advantages
----------

Something that we might potentially use for our case

1. Dual Core - Cortex M7 (~400MHz) and Cortex M4 (~200MHz)
	a. One for data collection / purely sampling the sensors (M0)
	b. One for external interface (Ethernet/Wi-Fi/RS 485)
2. Ethernet port is present
3. eMMC interface
4. USB present - Can be used as a mass storage as well
5. Pinouts for add on Wi-Fi board for scalability


1. PCB design notes for H7 Chip
	a. https://community.st.com/t5/stm32-mcus-products/stm32h7-with-emmc-and-usb-mass-storage-class-electronics/td-p/710280
