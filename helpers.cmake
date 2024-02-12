
cmake_minimum_required(VERSION 3.20.0)

function(add_component component_name)
  add_subdirectory(${COMPONENTS_DIR}/${component_name} ${CMAKE_CURRENT_BINARY_DIR}/${component_name})
  target_include_directories(app PRIVATE ${COMPONENTS_DIR}/${component_name})
endfunction()

function(add_driver driver_name os_name)
  add_subdirectory(${DRIVERS_DIR}/${driver_name}_${os_name} ${CMAKE_CURRENT_BINARY_DIR}/${driver_name})
  target_include_directories(app PRIVATE ${DRIVERS_DIR}/include)
endfunction()

