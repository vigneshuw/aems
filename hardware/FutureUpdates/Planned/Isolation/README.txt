Safety
------

- Creating isolation to protect other devices connected to the board


DC-DC-Isolation
----------------

- Isolating the power supply to ADC from the USB Input
- https://www.analog.com/en/products/adum5000.html#part-details
- Videos -> https://www.youtube.com/watch?v=OM6CCaYSZ9A

SPI Isolation
--------------

- Isolating the SPI connection between the ADC and MCU
- Potentially we can remove the 1V2 REF IN to the ADC, as the ADC has its own reference voltage generator. But need to think about the Op-Amp used for the current input.
- https://www.analog.com/en/products/adum3151.html#part-details

