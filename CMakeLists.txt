if(CONFIG_ZMK_INPUT_PROCESSOR_TEMP_LAYER_THRESHOLD)
    target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/input_processor_temp_layer_threshold.c)
endif()
