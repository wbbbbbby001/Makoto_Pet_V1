# get IDF version for comparison
set(idf_version "${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}")

set(priv_requires "")

# set conversion sources
set(srcs
  conversions/yuv.c
  conversions/to_jpg.cpp
  conversions/to_bmp.c
  conversions/jpge.cpp
  )

set(priv_include_dirs
  conversions/private_include
  )

set(include_dirs
  driver/include
  conversions/include
  )

# set driver sources only for supported platforms
if(IDF_TARGET STREQUAL "esp32" OR IDF_TARGET STREQUAL "esp32s2" OR IDF_TARGET STREQUAL "esp32s3")
  list(APPEND srcs
    driver/esp_camera.c
    driver/esp_camera_af.c
    driver/cam_hal.c
    driver/sensor.c
    sensors/ov2640.c
    sensors/ov3660.c
    sensors/ov3640.c
    sensors/ov5640.c
    sensors/ov5640_af.c
    sensors/ov7725.c
    sensors/ov7670.c
    sensors/nt99141.c
    sensors/gc0308.c
    sensors/gc2145.c
    sensors/gc032a.c
    sensors/bf3005.c
    sensors/bf20a6.c
    sensors/sc101iot.c
    sensors/sc030iot.c
    sensors/sc031gs.c
    sensors/mega_ccm.c
    sensors/hm1055.c
    sensors/hm0360.c
    )

  list(APPEND priv_include_dirs
    driver/private_include
    sensors/private_include
    target/private_include
    )

  if(IDF_TARGET STREQUAL "esp32")
    list(APPEND srcs
      target/xclk.c
      target/esp32/ll_cam.c
      )
  endif()

  if(IDF_TARGET STREQUAL "esp32s2")
    list(APPEND srcs
      target/xclk.c
      target/esp32s2/ll_cam.c
      )

    list(APPEND priv_include_dirs
      target/esp32s2/private_include
      )
  endif()

  if(IDF_TARGET STREQUAL "esp32s3")
    list(APPEND srcs
      target/esp32s3/ll_cam.c
      )
  endif()

  list(APPEND priv_requires freertos nvs_flash esp_mm)

  set(min_version_for_esp_timer "4.2")
  if (idf_version VERSION_GREATER_EQUAL min_version_for_esp_timer)
    list(APPEND priv_requires esp_timer)
  endif()

  # include the SCCB I2C driver
  # 本地修改：本工程 BSP 使用新版 i2c_master 驱动，而 IDF 5.3 没有
  # i2c_master_get_bus_handle()，所以这里强制用 sccb-ng.c，并通过
  # g_sccb_bus_handle 接收主工程已经创建好的 I2C 总线句柄。
  # 这样不会再链接旧版 driver/i2c.h，避免启动时 check_i2c_driver_conflict() abort。
  if (NOT CONFIG_SCCB_HARDWARE_I2C_DRIVER_LEGACY)
    list(APPEND srcs driver/sccb-ng.c)
  else()
    list(APPEND srcs driver/sccb.c)
  endif()

endif()

set(req driver)
if (idf_version VERSION_GREATER_EQUAL "6.0")
  list(APPEND priv_requires esp_driver_gpio esp_driver_spi esp_driver_i2c)
  list(APPEND req esp_driver_ledc esp_hal_clock)
endif()

idf_component_register(
  SRCS ${srcs}
  INCLUDE_DIRS ${include_dirs}
  PRIV_INCLUDE_DIRS ${priv_include_dirs}
  REQUIRES ${req}
  PRIV_REQUIRES ${priv_requires}
)
