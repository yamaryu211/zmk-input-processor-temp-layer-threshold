if(CONFIG_ZMK_INPUT_PROCESSOR_TEMP_LAYER_THRESHOLD)
    zephyr_library_named(zmk-input-processor-temp-layer-threshold)
    zephyr_library_sources(src/input_processor_temp_layer_threshold.c)
endif()
