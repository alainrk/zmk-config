if(CONFIG_ZMK_DISPLAY AND CONFIG_NICE_VIEW_DISP_WIDGET_STATUS)
  zephyr_library_include_directories(${CMAKE_SOURCE_DIR}/include)
  zephyr_library_sources(custom_status_screen.c)
  zephyr_library_sources(widgets/bolt.c)
  zephyr_library_sources(widgets/util.c)
  # Caps Lock -> red underglow. Compiled on BOTH halves: each reacts to its own
  # HID-indicator event and drives its own strip (underglow state is not synced
  # across the split in ZMK v0.3).
  zephyr_library_sources(widgets/caps_underglow.c)

  if(NOT CONFIG_ZMK_SPLIT OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    zephyr_library_sources(widgets/status.c)
  else()
    zephyr_library_sources(widgets/art.c)
    zephyr_library_sources(widgets/peripheral_status.c)
  endif()
endif()
