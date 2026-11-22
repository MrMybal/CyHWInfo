function(cyhwinfo_deploy_libre_hardware_monitor target)
    if(TARGET cyhwinfo_sensors)
        set(provider cyhwinfo_sensors)
    else()
        set(provider CyHWInfo::LibreHardwareMonitor)
    endif()
    get_target_property(bridge "${provider}" CYHWINFO_SENSOR_BRIDGE_DIR)
    set(deploy_target "${target}_cyhwinfo_lhm_runtime")
    add_custom_target("${deploy_target}"
        COMMAND "${CMAKE_COMMAND}" "-Dbridge_source=${bridge}"
            "-Dbridge_destination=$<TARGET_FILE_DIR:${target}>/cyhwinfo-lhm"
            -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/CopyLibreHardwareMonitor.cmake"
        VERBATIM
    )
    if(TARGET cyhwinfo_lhm_bridge)
        add_dependencies("${deploy_target}" cyhwinfo_lhm_bridge)
    endif()
    add_dependencies("${target}" "${deploy_target}")
endfunction()
