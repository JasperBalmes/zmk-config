if(CONFIG_ZMK_DISPLAY AND CONFIG_SKULL_OLED_STATUS)
  zephyr_library_include_directories(${CMAKE_SOURCE_DIR}/include)
  zephyr_library_sources(custom_status_screen.c)

  # The left half is the central: it shows the left half of the skull.
  # The right half (peripheral) shows the right half.
  if(NOT CONFIG_ZMK_SPLIT OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    set(SKULL_HALF left)
  else()
    set(SKULL_HALF right)
  endif()

  if(CONFIG_SKULL_OLED_ORIENTATION_CCW)
    set(SKULL_ROT ccw)
  else()
    set(SKULL_ROT cw)
  endif()

  zephyr_library_sources(skull_${SKULL_HALF}_${SKULL_ROT}.c)
endif()
