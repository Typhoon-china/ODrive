/*
* ODrive I2C communication library
* This file implements I2C communication with the ODrive.
*
*   - Implement the C function I2C_transaction to provide low level I2C access.
*   - Use read_property<PropertyId>() to read properties from the ODrive.
*   - Use write_property<PropertyId>() to modify properties on the ODrive.
*   - Use endpoint_type_t<PropertyId> to retrieve the underlying type
*     of a given property.
*
* To regenerate the interface definitions, flash an ODrive with the new
* firmware, then run
*   ../tools/odrivetool generate-code --output odrive_endpoints.h
*/


#include <limits.h>
#include <stdint.h>

#include "odrive_endpoints.h"

#ifdef __AVR__
// AVR-GCC doesn't ship with the STL, so we use our own little excerpt
#include "type_traits.h"
#else
#include <type_traits>
#endif


extern "C" {

/* @brief Send and receive data to/from an I2C slave
*
* This function carries out the following sequence:
* 1. generate a START condition
* 2. if the tx_buffer is not null:
*    a. send 7-bit slave address (with the LSB 0)
*    b. send all bytes in the tx_buffer
* 3. if both tx_buffer and rx_buffer are not null, generate a REPEATED START condition
* 4. if the rx_buffer is not null:
*    a. send 7-bit slave address (with the LSB 1)
*    b. read rx_length bytes into rx_buffer
* 5. send STOP condition
*
* @param slave_addr: 7-bit slave address (the MSB is ignored)
* @return true if all data was transmitted and received as requested by the caller, false otherwise
*/
bool I2C_transaction(uint8_t slave_addr, const uint8_t * tx_buffer, size_t tx_length, uint8_t * rx_buffer, size_t rx_length);

}


namespace odrive {
    static constexpr const uint8_t i2c_addr = (0xD << 3); // write: 1101xxx0, read: 1101xxx1

    template<typename T>
    using bit_width = std::integral_constant<unsigned int, CHAR_BIT * sizeof(T)>;

    template<typename T>
    using byte_width = std::integral_constant<unsigned int, (bit_width<T>::value + 7) / 8>;


    template<unsigned int IBitSize>
    struct unsigned_int_of_size;

    template<> struct unsigned_int_of_size<32> { typedef uint32_t type; };


    template<typename T>
    typename std::enable_if<std::is_integral<T>::value, T>::type
    read_le(const uint8_t buffer[byte_width<T>::value]) {
        T value = 0;
        for (size_t i = 0; i < byte_width<T>::value; ++i)
            value |= (static_cast<T>(buffer[i]) << (i << 3));
        return value;
    }

    template<typename T>
    typename std::enable_if<std::is_floating_point<T>::value, T>::type
    read_le(const uint8_t buffer[]) {
        using T_Int = typename unsigned_int_of_size<bit_width<T>::value>::type;
        T_Int value = read_le<T_Int>(buffer);
        return *reinterpret_cast<T*>(&value);
    }

    template<typename T>
    typename std::enable_if<std::is_integral<T>::value, void>::type
    write_le(uint8_t buffer[byte_width<T>::value], T value) {
        for (size_t i = 0; i < byte_width<T>::value; ++i)
            buffer[i] = (value >> (i << 3)) & 0xff;
    }

    template<typename T>
    typename std::enable_if<std::is_floating_point<T>::value, T>::type
    write_le(uint8_t buffer[byte_width<T>::value], T value) {
        using T_Int = typename unsigned_int_of_size<bit_width<T>::value>::type;
        write_le<T_Int>(buffer, *reinterpret_cast<T_Int*>(&value));
    }

    /* @brief Read from an endpoint on the ODrive
    *
    * Usage example:
    *   float val;
    *   success = odrive::read_property<odrive::VBUS_VOLTAGE>(0, &val);
    *
    * @param num Selects the ODrive. For instance the value 4 selects
    * the ODrive that has [A2, A1, A0] connected to [VCC, GND, GND].
    * @return true if the I2C transaction succeeded, false otherwise
    */
    template<int IPropertyId>
    bool read_property(uint8_t num, endpoint_type_t<IPropertyId>* value) {
        uint8_t i2c_tx_buffer[4];
        write_le<uint16_t>(i2c_tx_buffer, IPropertyId);
        write_le<uint16_t>(i2c_tx_buffer + sizeof(i2c_tx_buffer) - 2, json_crc);
        uint8_t i2c_rx_buffer[byte_width<endpoint_type_t<IPropertyId>>::value];
        if (!I2C_transaction(i2c_addr + num,
            i2c_tx_buffer, sizeof(i2c_tx_buffer),
            i2c_rx_buffer, sizeof(i2c_rx_buffer)))
            return false;
        if (value)
            *value = read_le<endpoint_type_t<IPropertyId>>(i2c_rx_buffer);
        return true;
    }

    /* @brief Write to an endpoint on the ODrive
    *
    * Usage example:
    *   success = odrive::write_property<odrive::AXIS0__CONTROLLER__VEL_SETPOINT>(0, 10000);
    *
    * @param num Selects the ODrive. For instance the value 4 selects
    * the ODrive that has [A2, A1, A0] connected to [VCC, GND, GND].
    * @return true if the I2C transaction succeeded, false otherwise
    */
    template<int IPropertyId>
    bool write_property(uint8_t num, endpoint_type_t<IPropertyId> value) {
        uint8_t i2c_tx_buffer[4 + byte_width<endpoint_type_t<IPropertyId>>::value];
        write_le<uint16_t>(i2c_tx_buffer, IPropertyId);
        write_le<endpoint_type_t<IPropertyId>>(i2c_tx_buffer + 2, value);
        write_le<uint16_t>(i2c_tx_buffer + sizeof(i2c_tx_buffer) - 2, json_crc);
        return I2C_transaction(i2c_addr + num, i2c_tx_buffer, sizeof(i2c_tx_buffer), nullptr, 0);
    }

}