find_program(CYHWINFO_DOTNET dotnet REQUIRED)

set(lhm_project_dir "${CMAKE_CURRENT_SOURCE_DIR}/src/lhm_bridge")
set(lhm_build_dir "${CMAKE_CURRENT_BINARY_DIR}/lhm_bridge")
set(lhm_bridge_dir "${lhm_build_dir}/cyhwinfo-lhm")
set(lhm_stamp "${lhm_build_dir}/published.stamp")
set(lhm_license_dir "${lhm_project_dir}/licenses")
file(GLOB lhm_notices CONFIGURE_DEPENDS "${lhm_license_dir}/*.txt")

add_custom_command(
    OUTPUT "${lhm_stamp}"
    BYPRODUCTS "${lhm_bridge_dir}/CyHWInfo.SensorBridge.exe"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${lhm_bridge_dir}"
    COMMAND "${CYHWINFO_DOTNET}" publish "${lhm_project_dir}/CyHWInfo.SensorBridge.csproj"
        --configuration Release --no-self-contained --output "${lhm_bridge_dir}"
        "-p:BaseIntermediateOutputPath=${lhm_build_dir}/obj/"
        "-p:BaseOutputPath=${lhm_build_dir}/bin/"
        -p:RestoreLockedMode=true
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${lhm_license_dir}" "${lhm_bridge_dir}/licenses"
    COMMAND "${CMAKE_COMMAND}" -E touch "${lhm_stamp}"
    DEPENDS
        "${lhm_project_dir}/CyHWInfo.SensorBridge.csproj"
        "${lhm_project_dir}/Program.cs"
        "${lhm_project_dir}/RaphaelSensors.cs"
        "${lhm_project_dir}/packages.lock.json"
        ${lhm_notices}
    COMMENT "Publishing the LibreHardwareMonitor sensor bridge"
    VERBATIM
)

add_custom_target(cyhwinfo_lhm_bridge DEPENDS "${lhm_stamp}")
add_dependencies(cyhwinfo_sensors cyhwinfo_lhm_bridge)
set_property(TARGET cyhwinfo_sensors PROPERTY CYHWINFO_SENSOR_BRIDGE_DIR "${lhm_bridge_dir}")

include("${CMAKE_CURRENT_LIST_DIR}/DeployLibreHardwareMonitor.cmake")

install(DIRECTORY "${lhm_bridge_dir}/" DESTINATION "${CMAKE_INSTALL_BINDIR}/cyhwinfo-lhm")
install(FILES "${CMAKE_CURRENT_LIST_DIR}/DeployLibreHardwareMonitor.cmake"
              "${CMAKE_CURRENT_LIST_DIR}/CopyLibreHardwareMonitor.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/CyHWInfo")
