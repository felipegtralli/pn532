# PN532 Component for ESP-IDF
This project provides an ESP-IDF library for interfacing with the PN532 NFC/RFID controller.  
Note: Currently, this library only supports UART protocol. Support for I2C and SPI may be added in the future.

## How to Use
### Hardware
- **ESP32 Board**: Any ESP32-based development board.
- **PN532 NFC/RFID Controller**: Elechouse's PN532 Module V3 was used for testing and development.

### Connection Diagrams
Connect the PN532 module to the ESP32 board as follows:
#### For UART
| PN532 Pin | ESP32 Pin |
|-----------|-----------|
| VCC       | 3.3V      |
| GND       | GND       |
| TX        | RX (GPIO) |
| RX        | TX (GPIO) |

### Getting Started
1. Install ESP-IDF<br>
 Follow the ESP-IDF installation guide for your operating system.  
2. Clone the Repository
   ```sh
    git clone https://github.com/felipegtralli/pn532.git
    ```
3. Add PN532 as a Component<br>
 Include the PN532 library in your ESP-IDF project by placing it in the components directory or by linking it via an idf_component.yml.
4. Reconfigure
   ```sh
   idf.py reconfigure
   ```
5. Build and Flash
    ```sh
    idf.py build flash
    ```
    
## Testing Component
1. Connect Hardware<br>
 Ensure the ESP32 and PN532 are properly connected.
2. Run Example Code<br>
 Flash and monitor any example provided in the examples folder.
    ```sh
    idf.py build flash monitor
    ```
    
## Contributing
1. Fork the repository.
2. Submit pull requests for bug fixes or feature additions.
