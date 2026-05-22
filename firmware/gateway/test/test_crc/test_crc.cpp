#include <Arduino.h>
#include <unity.h>

#include "../../src/crc.h"
#include "../../src/crc.cpp"

void test_crc16_modbus_known_vector()
{
  const uint8_t payload[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  ModbusCRC crc;

  TEST_ASSERT_EQUAL_HEX16(0x4B37, crc.crc16_modbus(payload, sizeof(payload)));
}

void test_crc16_modbus_incremental_update_matches_buffer_update()
{
  const uint8_t request[] = {0x01, 0x03, 0x13, 0x02, 0x00, 0x0A};
  ModbusCRC bulkCrc;
  ModbusCRC incrementalCrc;

  uint16_t bulk = bulkCrc.crc16_modbus(request, sizeof(request));

  incrementalCrc.init();
  for (size_t i = 0; i < sizeof(request); i++) {
    incrementalCrc.update(request[i]);
  }

  TEST_ASSERT_EQUAL_HEX16(bulk, incrementalCrc.getcrc());
}

void setup()
{
  delay(2000);
  UNITY_BEGIN();
  RUN_TEST(test_crc16_modbus_known_vector);
  RUN_TEST(test_crc16_modbus_incremental_update_matches_buffer_update);
  UNITY_END();
}

void loop()
{
}
