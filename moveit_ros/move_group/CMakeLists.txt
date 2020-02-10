cmake_minimum_required(VERSION 3.5)
project(moveit_ros_move_group)

# Add support for C++11
if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 11)
endif()

find_package(tf2_geometry_msgs REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_srvs REQUIRED)
find_package(pluginlib REQUIRED)
find_package(rclcpp_action REQUIRED)
find_package(tf2 REQUIRED)
find_package(moveit_ros_planning REQUIRED)
find_package(tf2_ros REQUIRED)
find_package(ament_cmake REQUIRED)
find_package(moveit_core REQUIRED)
find_package(moveit_ros_occupancy_map_monitor)

set(INCLUDE_DIRS include ${tf2_geometry_msgs_INCLUDE_DIRS}
  ${rclcpp_INCLUDE_DIRS} ${std_srvs_INCLUDE_DIRS} ${pluginlib_INCLUDE_DIRS}
  ${rclcpp_action_INCLUDE_DIRS} ${tf2_INCLUDE_DIRS}
  ${moveit_ros_planning_INCLUDE_DIRS} ${tf2_ros_INCLUDE_DIRS}
  ${ament_cmake_INCLUDE_DIRS} ${moveit_core_INCLUDE_DIRS}
  ${moveit_ros_occupancy_map_monitor_INCLUDE_DIRS})
include_directories(${INCLUDE_DIRS})

set(LIBRARY_DIRS ${tf2_geometry_msgs_LIBRARY_DIRS} ${rclcpp_LIBRARY_DIRS}
  ${std_srvs_LIBRARY_DIRS} ${pluginlib_LIBRARY_DIRS} ${rclcpp_action_LIBRARY_DIRS}
  ${tf2_LIBRARY_DIRS} ${moveit_ros_planning_LIBRARY_DIRS} ${tf2_ros_LIBRARY_DIRS}
  ${ament_cmake_LIBRARY_DIRS} ${moveit_core_LIBRARY_DIRS}
  ${moveit_ros_occupancy_map_monitor_LIBRARY_DIRS}
)

link_directories(${LIBRARY_DIRS})

set(LIBS ${tf2_geometry_msgs_LIBRARIES} ${rclcpp_LIBRARIES}
  ${std_srvs_LIBRARIES} ${pluginlib_LIBRARIES} ${rclcpp_action_LIBRARIES}
  ${tf2_LIBRARIES} ${moveit_ros_planning_LIBRARIES} ${tf2_ros_LIBRARIES}
  ${ament_cmake_LIBRARIES} ${moveit_core_LIBRARIES}
  ${moveit_ros_occupancy_map_monitor_LIBRARIES}
)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Boost REQUIRED system filesystem date_time program_options thread)

add_library(moveit_move_group_capabilities_base src/move_group_context.cpp
  src/move_group_capability.cpp)
set_target_properties(moveit_move_group_capabilities_base
  PROPERTIES
  VERSION "${${PROJECT_NAME}_VERSION}")

add_executable(move_group src/move_group.cpp)

add_executable(list_move_group_capabilities src/list_capabilities.cpp)

add_library(moveit_move_group_default_capabilities
  src/default_capabilities/move_action_capability.cpp
  src/default_capabilities/plan_service_capability.cpp
  src/default_capabilities/execute_trajectory_action_capability.cpp
  src/default_capabilities/query_planners_service_capability.cpp
  src/default_capabilities/kinematics_service_capability.cpp
  src/default_capabilities/state_validation_service_capability.cpp
  src/default_capabilities/cartesian_path_service_capability.cpp
  src/default_capabilities/get_planning_scene_service_capability.cpp
  src/default_capabilities/apply_planning_scene_service_capability.cpp
  src/default_capabilities/clear_octomap_service_capability.cpp
  src/default_capabilities/tf_publisher_capability.cpp)
set_target_properties(moveit_move_group_default_capabilities
  PROPERTIES
  VERSION "${${PROJECT_NAME}_VERSION}")

target_link_libraries(moveit_move_group_capabilities_base ${LIBS}
  ${Boost_LIBRARIES})
target_link_libraries(move_group moveit_move_group_capabilities_base ${LIBS}
  ${Boost_LIBRARIES})
target_link_libraries(moveit_move_group_default_capabilities
  moveit_move_group_capabilities_base ${LIBS} ${Boost_LIBRARIES})
target_link_libraries(list_move_group_capabilities ${LIBS} ${Boost_LIBRARIES})

install(TARGETS move_group list_move_group_capabilities
  RUNTIME
  DESTINATION lib/${PROJECT_NAME})

install(TARGETS moveit_move_group_capabilities_base
  moveit_move_group_default_capabilities
  ARCHIVE
  DESTINATION lib
  LIBRARY
  DESTINATION lib
  RUNTIME
  DESTINATION bin)

install(DIRECTORY include/ DESTINATION include)

install(PROGRAMS scripts/load_map scripts/save_map
  DESTINATION lib/${PROJECT_NAME})

install(FILES default_capabilities_plugin_description.xml
  DESTINATION share/${PROJECT_NAME})

if(BUILD_TESTING)
#  find_package(rostest REQUIRED) # rostest under development in ROS2 https://github.com/ros-planning/moveit2/issues/23
  # this test is flaky
  # add_rostest(test/test_cancel_before_plan_execution.test)
endif()

ament_export_dependencies(tf2_geometry_msgs)
ament_export_dependencies(rclcpp)
ament_export_dependencies(std_srvs)
ament_export_dependencies(pluginlib)
ament_export_dependencies(rclcpp_action)
ament_export_dependencies(tf2)
ament_export_dependencies(moveit_ros_planning)
ament_export_dependencies(tf2_ros)
ament_export_dependencies(ament_cmake)
ament_export_dependencies(moveit_core)
ament_export_dependencies(moveit_ros_occupancy_map_monitor)
ament_export_include_directories(${INCLUDE_DIRS})
ament_export_libraries(moveit_move_group_capabilities_base
  moveit_move_group_default_capabilities ${LIBS})

ament_package()
