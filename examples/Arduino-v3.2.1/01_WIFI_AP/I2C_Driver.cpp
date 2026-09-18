#include <Arduino.h>
#include "I2C_Driver.h"


esp_err_t I2C_Init(void) {
  if (!Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN)) {
    Serial.printf("I2C init failed on SDA:%d SCL:%d\r\n", I2C_SDA_PIN, I2C_SCL_PIN);
    return ESP_FAIL;
  }
  return ESP_OK;
}


esp_err_t I2C_Read(uint8_t Driver_addr, uint8_t Reg_addr, uint8_t *Reg_data, uint32_t Length)
{
  if (Reg_data == NULL && Length > 0) {
    return ESP_ERR_INVALID_ARG;
  }
  Wire.beginTransmission(Driver_addr);
  Wire.write(Reg_addr); 
  if (Wire.endTransmission(true)) {
    Serial.printf("The I2C transmission fails. - I2C Read\r\n");
    return ESP_FAIL;
  }
  size_t read_len = Wire.requestFrom(Driver_addr, Length);
  if (read_len != Length) {
    Serial.printf("The I2C read length is invalid. - I2C Read\r\n");
    return ESP_ERR_INVALID_SIZE;
  }
  for (uint32_t i = 0; i < Length; i++) {
    *Reg_data++ = Wire.read();
  }
  return ESP_OK;
}
esp_err_t I2C_Write(uint8_t Driver_addr, uint8_t Reg_addr, const uint8_t *Reg_data, uint32_t Length)
{
  if (Reg_data == NULL && Length > 0) {
    return ESP_ERR_INVALID_ARG;
  }
  Wire.beginTransmission(Driver_addr);
  Wire.write(Reg_addr);       
  for (uint32_t i = 0; i < Length; i++) {
    Wire.write(*Reg_data++);
  }
  if (Wire.endTransmission(true)) {
    Serial.printf("The I2C transmission fails. - I2C Write\r\n");
    return ESP_FAIL;
  }
  return ESP_OK;
}
