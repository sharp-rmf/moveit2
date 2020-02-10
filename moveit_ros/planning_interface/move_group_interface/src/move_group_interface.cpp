/*********************************************************************
 * Software License Agreement (BSD License)
 *
 *  Copyright (c) 2014, SRI International
 *  Copyright (c) 2013, Ioan A. Sucan
 *  Copyright (c) 2012, Willow Garage, Inc.
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of Willow Garage nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *********************************************************************/

/* Author: Ioan Sucan, Sachin Chitta */

#include <stdexcept>
#include <sstream>
#include <memory>
#if 0 //@todo
#include <moveit/warehouse/constraints_storage.h>
#endif
#include <moveit/kinematic_constraints/utils.h>
#include <moveit/move_group/capability_names.h>
#include <moveit/move_group_pick_place_capability/capability_names.h>
#include <moveit/move_group_interface/move_group_interface.h>
#include <moveit/planning_scene_monitor/current_state_monitor.h>
#include <moveit/planning_scene_monitor/planning_scene_monitor.h>
#include <moveit/planning_scene_interface/planning_scene_interface.h>
#include <moveit/trajectory_execution_manager/trajectory_execution_manager.h>
#include <moveit/common_planning_interface_objects/common_objects.h>
#include <moveit/robot_state/conversions.h>
#if 0 //@todo
#include <moveit_msgs/action/pickup_action.h>
#include <moveit_msgs/action/execute_trajectory_action.h>
#include <moveit_msgs/action/place_action.h>
#endif
#include <moveit_msgs/srv/execute_known_trajectory.hpp>
#include <moveit_msgs/srv/query_planner_interfaces.hpp>
#include <moveit_msgs/srv/get_cartesian_path.hpp>
#include <moveit_msgs/srv/grasp_planning.hpp>
#include <moveit_msgs/srv/get_planner_params.hpp>
#include <moveit_msgs/srv/set_planner_params.hpp>

#include <std_msgs/msg/string.hpp>
#include <geometry_msgs/msg/transform_stamped.h>
#include <tf2/utils.h>
#include <tf2_eigen/tf2_eigen.h>
#include <tf2_ros/transform_listener.h>
#if 0 //@todo
#include <ros/console.h>
#include <ros/ros.h>
#endif

namespace moveit
{
namespace planning_interface
{
const std::string MoveGroupInterface::ROBOT_DESCRIPTION =
    "robot_description";  // name of the robot description (a param name, so it can be changed externally)

const std::string GRASP_PLANNING_SERVICE_NAME = "plan_grasps";  // name of the service that can be used to plan grasps

namespace
{
enum ActiveTargetType
{
  JOINT,
  POSE,
  POSITION,
  ORIENTATION
};
}

class MoveGroupInterface::MoveGroupInterfaceImpl
{
public:
  MoveGroupInterfaceImpl(const Options& opt, const std::shared_ptr<tf2_ros::Buffer>& tf_buffer,
                         const rclcpp::Duration& wait_for_servers)
    : opt_(opt), node_(opt.node_), tf_buffer_(tf_buffer)
  {
#if 0 //@todo
    robot_model_ = opt.robot_model_ ? opt.robot_model_ : getSharedRobotModel(opt.robot_description_);
#else
    robot_model_ = opt.robot_model_ ? opt.robot_model_ : nullptr;
#endif
    if (!getRobotModel())
    {
      std::string error = "Unable to construct robot model. Please make sure all needed information is on the "
                          "parameter server.";
      RCLCPP_FATAL_STREAM(rclcpp::get_logger("move_group_interface"), error);
      throw std::runtime_error(error);
    }

    if (!getRobotModel()->hasJointModelGroup(opt.group_name_))
    {
      std::string error = "Group '" + opt.group_name_ + "' was not found.";
      RCLCPP_FATAL_STREAM(rclcpp::get_logger("move_group_interface"), error);
      throw std::runtime_error(error);
    }

    joint_model_group_ = getRobotModel()->getJointModelGroup(opt.group_name_);

    joint_state_target_.reset(new robot_state::RobotState(getRobotModel()));
    joint_state_target_->setToDefaultValues();
    active_target_ = JOINT;
    can_look_ = false;
    can_replan_ = false;
    replan_delay_ = 2.0;
    goal_joint_tolerance_ = 1e-4;
    goal_position_tolerance_ = 1e-4;     // 0.1 mm
    goal_orientation_tolerance_ = 1e-3;  // ~0.1 deg
    allowed_planning_time_ = 5.0;
    num_planning_attempts_ = 1;
    max_velocity_scaling_factor_ = 1.0;
    max_acceleration_scaling_factor_ = 1.0;
    initializing_constraints_ = false;

    if (joint_model_group_->isChain())
      end_effector_link_ = joint_model_group_->getLinkModelNames().back();
    pose_reference_frame_ = getRobotModel()->getModelFrame();

    trajectory_event_publisher_ = node_->create_publisher<std_msgs::msg::String>(
        trajectory_execution_manager::TrajectoryExecutionManager::EXECUTION_EVENT_TOPIC(), 1);
    attached_object_publisher_ = node_->create_publisher<moveit_msgs::msg::AttachedCollisionObject>(
        planning_scene_monitor::PlanningSceneMonitor::DEFAULT_ATTACHED_COLLISION_OBJECT_TOPIC, 1);

#if 0 //@todo
    current_state_monitor_ = getSharedStateMonitor(robot_model_, tf_buffer_, node_);
#endif
    rclcpp::Time timeout_for_servers = node_->get_clock()->now() + wait_for_servers;
    if (wait_for_servers == rclcpp::Duration(std::chrono::duration_values<double>::max())) //@todo ; no rclcpp::Duration() representation for infinite time
      timeout_for_servers = rclcpp::Time();  // wait for ever
    double allotted_time = wait_for_servers.seconds();
#if 0 //@todo
    move_action_client_.reset(new actionlib::SimpleActionClient<moveit_msgs::action::MoveGroupAction>(
        node_, move_group::MOVE_ACTION, false));
    waitForAction(move_action_client_, move_group::MOVE_ACTION, timeout_for_servers, allotted_time);

    pick_action_client_.reset(new actionlib::SimpleActionClient<moveit_msgs::action::PickupAction>(
        node_, move_group::PICKUP_ACTION, false));
    waitForAction(pick_action_client_, move_group::PICKUP_ACTION, timeout_for_servers, allotted_time);

    place_action_client_.reset(new actionlib::SimpleActionClient<moveit_msgs::action::PlaceAction>(
        node_, move_group::PLACE_ACTION, false));
    waitForAction(place_action_client_, move_group::PLACE_ACTION, timeout_for_servers, allotted_time);

    execute_action_client_.reset(new actionlib::SimpleActionClient<moveit_msgs::action::ExecuteTrajectoryAction>(
        node_, move_group::EXECUTE_ACTION_NAME, false));
    waitForAction(execute_action_client_, move_group::EXECUTE_ACTION_NAME, timeout_for_servers, allotted_time);
#endif
    query_service_ =
        node_->create_client<moveit_msgs::srv::QueryPlannerInterfaces>(move_group::QUERY_PLANNERS_SERVICE_NAME);
    get_params_service_ =
        node_->create_client<moveit_msgs::srv::GetPlannerParams>(move_group::GET_PLANNER_PARAMS_SERVICE_NAME);
    set_params_service_ =
        node_->create_client<moveit_msgs::srv::SetPlannerParams>(move_group::SET_PLANNER_PARAMS_SERVICE_NAME);

    cartesian_path_service_ =
        node_->create_client<moveit_msgs::srv::GetCartesianPath>(move_group::CARTESIAN_PATH_SERVICE_NAME);

    plan_grasps_service_ = node_->create_client<moveit_msgs::srv::GraspPlanning>(GRASP_PLANNING_SERVICE_NAME);

    RCLCPP_INFO_STREAM(rclcpp::get_logger("move_group_interface"), "Ready to take commands for planning group " << opt.group_name_
                                                                                               << ".");
  }

  template <typename T>
  void waitForAction(const T& action, const std::string& name, const rclcpp::Time& timeout, double allotted_time) const
  {
    RCLCPP_DEBUG(rclcpp::get_logger("move_group_interface"), "Waiting for move_group action server (%s)...", name.c_str());
#if 0 //@todo
    // wait for the server (and spin as needed)
    if (timeout == rclcpp::WallTime())  // wait forever
    {
      while (node_->ok() && !action->isServerConnected())
      {
        rclcpp::Duration(0.001).sleep();
        // explicit ros::spinOnce on the callback queue used by NodeHandle that manages the action client
        ros::CallbackQueue* queue = dynamic_cast<ros::CallbackQueue*>(node_->getCallbackQueue());
        if (queue)
        {
          queue->callAvailable();
        }
        else  // in case of nodelets and specific callback queue implementations
        {
          RCLCPP_WARN_ONCE(rclcpp::get_logger("move_group_interface"), "Non-default CallbackQueue: Waiting for external queue "
                                                      "handling.");
        }
      }
    }
    else  // wait with timeout
    {
      while (node_->ok() && !action->isServerConnected() && timeout > rclcpp::WallTime::now())
      {
        std::chrono::duration<double> d(0.001);
        rclcpp::sleep_for(std::chrono::duration_cast<std::chrono::nanoseconds>(d), rclcpp::Context::SharedPtr(nullptr));

        // explicit ros::spinOnce on the callback queue used by NodeHandle that manages the action client
        auto groups = node_->get_callback_groups();
        //rclcpp::CallbackQueue* queue = dynamic_cast<rclcpp::CallbackQueue*>();
        if (groups.size())
        {
          auto l = groups[0].lock();
          //->get()->callAvailable();
        }
        else  // in case of nodelets and specific callback queue implementations
        {
          RCLCPP_WARN_ONCE(rclcpp::get_logger("move_group_interface"), "Non-default CallbackQueue: Waiting for external queue "
                                                      "handling.");
        }
      }
    }
#else
    std::chrono::duration<double> d(0.001);
    rclcpp::sleep_for(std::chrono::duration_cast<std::chrono::nanoseconds>(d), rclcpp::Context::SharedPtr(nullptr));
#endif
    if (!action->isServerConnected())
    {
      std::stringstream error;
      error << "Unable to connect to move_group action server '" << name << "' within allotted time (" << allotted_time
            << "s)";
      throw std::runtime_error(error.str());
    }
    else
    {
      RCLCPP_DEBUG(rclcpp::get_logger("move_group_interface"), "Connected to '%s'", name.c_str());
    }
  }

  ~MoveGroupInterfaceImpl()
  {
    if (constraints_init_thread_)
      constraints_init_thread_->join();
  }

  const std::shared_ptr<tf2_ros::Buffer>& getTF() const
  {
    return tf_buffer_;
  }

  const Options& getOptions() const
  {
    return opt_;
  }

  const robot_model::RobotModelConstPtr& getRobotModel() const
  {
    return robot_model_;
  }

  const robot_model::JointModelGroup* getJointModelGroup() const
  {
    return joint_model_group_;
  }
#if 0 //@todo
  actionlib::SimpleActionClient<moveit_msgs::action::MoveGroupAction>& getMoveGroupClient() const
  {
    return *move_action_client_;
  }
#endif
  bool getInterfaceDescription(moveit_msgs::msg::PlannerInterfaceDescription& desc)
  {
    auto req = std::make_shared<moveit_msgs::srv::QueryPlannerInterfaces::Request>();
    moveit_msgs::srv::QueryPlannerInterfaces::Response::SharedPtr response;
    
    auto res = query_service_->async_send_request(req);

    //if (query_service_->call(req, res))
    if (rclcpp::spin_until_future_complete(node_, res) == rclcpp::executor::FutureReturnCode::SUCCESS)
    {
      response = res.get();
      if (!response->planner_interfaces.empty())
      {
        desc = response->planner_interfaces.front();
        return true;
      }
    }
    return false;
  }

  std::map<std::string, std::string> getPlannerParams(const std::string& planner_id, const std::string& group = "")
  {
    auto req = std::make_shared<moveit_msgs::srv::GetPlannerParams::Request>();
    moveit_msgs::srv::GetPlannerParams::Response::SharedPtr response;
    req->planner_config = planner_id;
    req->group = group;
    std::map<std::string, std::string> result;

    auto res = get_params_service_->async_send_request(req);
    //if (get_params_service_.call(req, res))
    if (rclcpp::spin_until_future_complete(node_, res) ==
      rclcpp::executor::FutureReturnCode::SUCCESS)
    {
      response = res.get();
      for (unsigned int i = 0, end = response->params.keys.size(); i < end; ++i)
        result[response->params.keys[i]] = response->params.values[i];
    }
    return result;
  }

  void setPlannerParams(const std::string& planner_id, const std::string& group,
                        const std::map<std::string, std::string>& params, bool replace = false)
  {
    auto req = std::make_shared<moveit_msgs::srv::SetPlannerParams::Request>();
    req->planner_config = planner_id;
    req->group = group;
    req->replace = replace;
    for (const std::pair<const std::string, std::string>& param : params)
    {
      req->params.keys.push_back(param.first);
      req->params.values.push_back(param.second);
    }
    set_params_service_->async_send_request(req);
  }

  std::string getDefaultPlannerId(const std::string& group) const
  {
    std::stringstream param_name;
    param_name << "move_group";
    if (!group.empty())
      param_name << "/" << group;
    param_name << "/default_planner_config";

    std::string default_planner_config;
    node_->get_parameter(param_name.str(), default_planner_config);
    return default_planner_config;
  }

  void setPlannerId(const std::string& planner_id)
  {
    planner_id_ = planner_id;
  }

  const std::string& getPlannerId() const
  {
    return planner_id_;
  }

  void setNumPlanningAttempts(unsigned int num_planning_attempts)
  {
    num_planning_attempts_ = num_planning_attempts;
  }

  void setMaxVelocityScalingFactor(double max_velocity_scaling_factor)
  {
    max_velocity_scaling_factor_ = max_velocity_scaling_factor;
  }

  void setMaxAccelerationScalingFactor(double max_acceleration_scaling_factor)
  {
    max_acceleration_scaling_factor_ = max_acceleration_scaling_factor;
  }

  robot_state::RobotState& getTargetRobotState()
  {
    return *joint_state_target_;
  }

  const robot_state::RobotState& getTargetRobotState() const
  {
    return *joint_state_target_;
  }

  void setStartState(const robot_state::RobotState& start_state)
  {
    considered_start_state_.reset(new robot_state::RobotState(start_state));
  }

  void setStartStateToCurrentState()
  {
    considered_start_state_.reset();
  }

  robot_state::RobotStatePtr getStartState()
  {
    if (considered_start_state_)
      return considered_start_state_;
    else
    {
      robot_state::RobotStatePtr s;
      getCurrentState(s);
      return s;
    }
  }

  bool setJointValueTarget(const geometry_msgs::msg::Pose& eef_pose, const std::string& end_effector_link,
                           const std::string& frame, bool approx)
  {
    const std::string& eef = end_effector_link.empty() ? getEndEffectorLink() : end_effector_link;
    // this only works if we have an end-effector
    if (!eef.empty())
    {
      // first we set the goal to be the same as the start state
      moveit::core::RobotStatePtr c = getStartState();
      if (c)
      {
        setTargetType(JOINT);
        c->enforceBounds();
        getTargetRobotState() = *c;
        if (!getTargetRobotState().satisfiesBounds(getGoalJointTolerance()))
          return false;
      }
      else
        return false;

      // we may need to do approximate IK
      kinematics::KinematicsQueryOptions o;
      o.return_approximate_solution = approx;

      // if no frame transforms are needed, call IK directly
      if (frame.empty() || moveit::core::Transforms::sameFrame(frame, getRobotModel()->getModelFrame()))
        return getTargetRobotState().setFromIK(getJointModelGroup(), eef_pose, eef, 0.0,
                                               moveit::core::GroupStateValidityCallbackFn(), o);
      else
      {
        // transform the pose into the model frame, then do IK
        bool frame_found;
        const Eigen::Isometry3d& t = getTargetRobotState().getFrameTransform(frame, &frame_found);
        if (frame_found)
        {
          Eigen::Isometry3d p;
          tf2::fromMsg(eef_pose, p);
          return getTargetRobotState().setFromIK(getJointModelGroup(), t * p, eef, 0.0,
                                                 moveit::core::GroupStateValidityCallbackFn(), o);
        }
        else
        {
          RCLCPP_ERROR(rclcpp::get_logger("move_group_interface"), "Unable to transform from frame '%s' to frame '%s'", frame.c_str(),
                          getRobotModel()->getModelFrame().c_str());
          return false;
        }
      }
    }
    else
      return false;
  }

  void setEndEffectorLink(const std::string& end_effector)
  {
    end_effector_link_ = end_effector;
  }

  void clearPoseTarget(const std::string& end_effector_link)
  {
    pose_targets_.erase(end_effector_link);
  }

  void clearPoseTargets()
  {
    pose_targets_.clear();
  }

  const std::string& getEndEffectorLink() const
  {
    return end_effector_link_;
  }

  const std::string& getEndEffector() const
  {
    if (!end_effector_link_.empty())
    {
      const std::vector<std::string>& possible_eefs =
          getRobotModel()->getJointModelGroup(opt_.group_name_)->getAttachedEndEffectorNames();
      for (const std::string& possible_eef : possible_eefs)
        if (getRobotModel()->getEndEffector(possible_eef)->hasLinkModel(end_effector_link_))
          return possible_eef;
    }
    static std::string empty;
    return empty;
  }

  bool setPoseTargets(const std::vector<geometry_msgs::msg::PoseStamped>& poses, const std::string& end_effector_link)
  {
    const std::string& eef = end_effector_link.empty() ? end_effector_link_ : end_effector_link;
    if (eef.empty())
    {
      RCLCPP_ERROR(rclcpp::get_logger("move_group_interface"), "No end-effector to set the pose for");
      return false;
    }
    else
    {
      pose_targets_[eef] = poses;
      // make sure we don't store an actual stamp, since that will become stale can potentially cause tf errors
      std::vector<geometry_msgs::msg::PoseStamped>& stored_poses = pose_targets_[eef];
      for (geometry_msgs::msg::PoseStamped& stored_pose : stored_poses)
        stored_pose.header.stamp = rclcpp::Time(0);
    }
    return true;
  }

  bool hasPoseTarget(const std::string& end_effector_link) const
  {
    const std::string& eef = end_effector_link.empty() ? end_effector_link_ : end_effector_link;
    return pose_targets_.find(eef) != pose_targets_.end();
  }

  const geometry_msgs::msg::PoseStamped& getPoseTarget(const std::string& end_effector_link) const
  {
    const std::string& eef = end_effector_link.empty() ? end_effector_link_ : end_effector_link;

    // if multiple pose targets are set, return the first one
    std::map<std::string, std::vector<geometry_msgs::msg::PoseStamped> >::const_iterator jt = pose_targets_.find(eef);
    if (jt != pose_targets_.end())
      if (!jt->second.empty())
        return jt->second.at(0);

    // or return an error
    static const geometry_msgs::msg::PoseStamped UNKNOWN;
    RCLCPP_ERROR(rclcpp::get_logger("move_group_interface"), "Pose for end-effector '%s' not known.", eef.c_str());
    return UNKNOWN;
  }

  const std::vector<geometry_msgs::msg::PoseStamped>& getPoseTargets(const std::string& end_effector_link) const
  {
    const std::string& eef = end_effector_link.empty() ? end_effector_link_ : end_effector_link;

    std::map<std::string, std::vector<geometry_msgs::msg::PoseStamped> >::const_iterator jt = pose_targets_.find(eef);
    if (jt != pose_targets_.end())
      if (!jt->second.empty())
        return jt->second;

    // or return an error
    static const std::vector<geometry_msgs::msg::PoseStamped> EMPTY;
    RCLCPP_ERROR(rclcpp::get_logger("move_group_interface"), "Poses for end-effector '%s' are not known.", eef.c_str());
    return EMPTY;
  }

  void setPoseReferenceFrame(const std::string& pose_reference_frame)
  {
    pose_reference_frame_ = pose_reference_frame;
  }

  void setSupportSurfaceName(const std::string& support_surface)
  {
    support_surface_ = support_surface;
  }

  const std::string& getPoseReferenceFrame() const
  {
    return pose_reference_frame_;
  }

  void setTargetType(ActiveTargetType type)
  {
    active_target_ = type;
  }

  ActiveTargetType getTargetType() const
  {
    return active_target_;
  }

  bool startStateMonitor(double wait)
  {
    if (!current_state_monitor_)
    {
      RCLCPP_ERROR(rclcpp::get_logger("move_group_interface"), "Unable to monitor current robot state");
      return false;
    }

    // if needed, start the monitor and wait up to 1 second for a full robot state
    if (!current_state_monitor_->isActive())
      current_state_monitor_->startStateMonitor();

    current_state_monitor_->waitForCompleteState(opt_.group_name_, wait);
    return true;
  }

  bool getCurrentState(robot_state::RobotStatePtr& current_state, double wait_seconds = 1.0)
  {
    if (!current_state_monitor_)
    {
      RCLCPP_ERROR(rclcpp::get_logger("move_group_interface"), "Unable to get current robot state");
      return false;
    }

    // if needed, start the monitor and wait up to 1 second for a full robot state
    if (!current_state_monitor_->isActive())
      current_state_monitor_->startStateMonitor();

    if (!current_state_monitor_->waitForCurrentState(node_->get_clock()->now(), wait_seconds))
    {
      RCLCPP_ERROR(rclcpp::get_logger("move_group_interface"), "Failed to fetch current robot state");
      return false;
    }

    current_state = current_state_monitor_->getCurrentState();
    return true;
  }

  /** \brief Convert a vector of PoseStamped to a vector of PlaceLocation */
  std::vector<moveit_msgs::msg::PlaceLocation>
  posesToPlaceLocations(const std::vector<geometry_msgs::msg::PoseStamped>& poses) const
  {
    std::vector<moveit_msgs::msg::PlaceLocation> locations;
    for (const geometry_msgs::msg::PoseStamped& pose : poses)
    {
#if 0 //@todo
      moveit_msgs::action::PlaceLocation location;
      location.pre_place_approach.direction.vector.z = -1.0;
      location.post_place_retreat.direction.vector.x = -1.0;
      location.pre_place_approach.direction.header.frame_id = getRobotModel()->getModelFrame();
      location.post_place_retreat.direction.header.frame_id = end_effector_link_;

      location.pre_place_approach.min_distance = 0.1;
      location.pre_place_approach.desired_distance = 0.2;
      location.post_place_retreat.min_distance = 0.0;
      location.post_place_retreat.desired_distance = 0.2;
      // location.post_place_posture is filled by the pick&place lib with the getDetachPosture from the AttachedBody

      location.place_pose = pose;
      locations.push_back(location);
#endif
    }
    RCLCPP_DEBUG(rclcpp::get_logger("move_group_interface"), "Move group interface has %u place locations",
                    (unsigned int)locations.size());
    return locations;
  }
#if 0 //@todo
  MoveItErrorCode place(const moveit_msgs::action::PlaceGoal& goal)
  {
#if 0 //@todo
    if (!place_action_client_)
    {
      RCLCPP_ERROR_STREAM(rclcpp::get_logger("move_group_interface"), "Place action client not found");
      return MoveItErrorCode(moveit_msgs::msg::MoveItErrorCodes::FAILURE);
    }
    if (!place_action_client_->isServerConnected())
    {
      RCLCPP_ERROR_STREAM(rclcpp::get_logger("move_group_interface"), "Place action server not connected");
      return MoveItErrorCode(moveit_msgs::msg::MoveItErrorCodes::FAILURE);
    }

    place_action_client_->sendGoal(goal);
    RCLCPP_DEBUG(rclcpp::get_logger("move_group_interface"), "Sent place goal with %d locations", (int)goal.place_locations.size());
    if (!place_action_client_->waitForResult())
    {
      RCLCPP_INFO_STREAM(rclcpp::get_logger("move_group_interface"), "Place action returned early");
    }
    if (place_action_client_->getState() == actionlib::SimpleClientGoalState::SUCCEEDED)
    {
      return MoveItErrorCode(place_action_client_->getResult()->error_code);
    }
    else
    {
      RCLCPP_WARN_STREAM(rclcpp::get_logger("move_group_interface"), "Fail: " << place_action_client_->getState().toString() << ": "
                                                             << place_action_client_->getState().getText());
      return MoveItErrorCode(place_action_client_->getResult()->error_code);
    }
#else
    return MoveItErrorCode(moveit_msgs::msg::MoveItErrorCodes::FAILURE);
#endif
  }
#endif

#if 0 //@todo
  MoveItErrorCode pick(const moveit_msgs::action::PickupGoal& goal)
  {
#if 0 //@todo
    if (!pick_action_client_)
    {
      RCLCPP_ERROR_STREAM(rclcpp::get_logger("move_group_interface"), "Pick action client not found");
      return MoveItErrorCode(moveit_msgs::msg::MoveItErrorCodes::FAILURE);
    }
    if (!pick_action_client_->isServerConnected())
    {
      RCLCPP_ERROR_STREAM(rclcpp::get_logger("move_group_interface"), "Pick action server not connected");
      return MoveItErrorCode(moveit_msgs::msg::MoveItErrorCodes::FAILURE);
    }

    pick_action_client_->sendGoal(goal);
    if (!pick_action_client_->waitForResult())
    {
      RCLCPP_INFO_STREAM(rclcpp::get_logger("move_group_interface"), "Pickup action returned early");
    }
    if (pick_action_client_->getState() == actionlib::SimpleClientGoalState::SUCCEEDED)
    {
      return MoveItErrorCode(pick_action_client_->getResult()->error_code);
    }
    else
    {
      RCLCPP_WARN_STREAM(rclcpp::get_logger("move_group_interface"), "Fail: " << pick_action_client_->getState().toString() << ": "
                                                             << pick_action_client_->getState().getText());
      return MoveItErrorCode(pick_action_client_->getResult()->error_code);
    }
#else
    return MoveItErrorCode(moveit_msgs::msg::MoveItErrorCodes::FAILURE);
#endif
  }
#endif

  MoveItErrorCode planGraspsAndPick(const std::string& object, bool plan_only = false)
  {
    if (object.empty())
    {
      return planGraspsAndPick(moveit_msgs::msg::CollisionObject());
    }

    PlanningSceneInterface psi;
    std::map<std::string, moveit_msgs::msg::CollisionObject> objects =
        psi.getObjects(std::vector<std::string>(1, object));

    if (objects.empty())
    {
      RCLCPP_ERROR_STREAM(rclcpp::get_logger("move_group_interface"), "Asked for grasps for the object '"
                                                         << object << "', but the object could not be found");
      return MoveItErrorCode(moveit_msgs::msg::MoveItErrorCodes::INVALID_OBJECT_NAME);
    }

    return planGraspsAndPick(objects[object], plan_only);
  }

  MoveItErrorCode planGraspsAndPick(const moveit_msgs::msg::CollisionObject& object, bool plan_only = false)
  {
    if (!plan_grasps_service_)
    {
      RCLCPP_ERROR_STREAM(rclcpp::get_logger("move_group_interface"), "Grasp planning service '"
                                                         << GRASP_PLANNING_SERVICE_NAME
                                                         << "' is not available."
                                                            " This has to be implemented and started separately.");
      return MoveItErrorCode(moveit_msgs::msg::MoveItErrorCodes::FAILURE);
    }

    auto request = std::make_shared<moveit_msgs::srv::GraspPlanning::Request>();
    moveit_msgs::srv::GraspPlanning::Response::SharedPtr response;

    request->group_name = opt_.group_name_;
    request->target = object;
    request->support_surfaces.push_back(support_surface_);

    RCLCPP_DEBUG(rclcpp::get_logger("move_group_interface"), "Calling grasp planner...");

    auto res = plan_grasps_service_->async_send_request(request);
    if (rclcpp::spin_until_future_complete(node_, res) !=
          rclcpp::executor::FutureReturnCode::SUCCESS)
    {
      RCLCPP_ERROR(rclcpp::get_logger("move_group_interface"), "Grasp planning failed. Unable to pick.");
      return MoveItErrorCode(moveit_msgs::msg::MoveItErrorCodes::FAILURE);
    }
    response = res.get();
    if (response->error_code.val != moveit_msgs::msg::MoveItErrorCodes::SUCCESS)
    {
      RCLCPP_ERROR(rclcpp::get_logger("move_group_interface"), "Grasp planning failed. Unable to pick.");
      return MoveItErrorCode(moveit_msgs::msg::MoveItErrorCodes::FAILURE);
    }
#if 0 //@todo
    return pick(constructPickupGoal(object.id, std::move(response.grasps), plan_only));
#else
    return MoveItErrorCode(moveit_msgs::msg::MoveItErrorCodes::FAILURE);
#endif
  }

  MoveItErrorCode plan(Plan& plan)
  {
#if 0 //@todo
    if (!move_action_client_)
    {
      return MoveItErrorCode(moveit_msgs::msg::MoveItErrorCodes::FAILURE);
    }
    if (!move_action_client_->isServerConnected())
    {
      return MoveItErrorCode(moveit_msgs::msg::MoveItErrorCodes::FAILURE);
    }

    moveit_msgs::action::MoveGroupGoal goal;
    constructGoal(goal);
    goal.planning_options.plan_only = true;
    goal.planning_options.look_around = false;
    goal.planning_options.replan = false;
    goal.planning_options.planning_scene_diff.is_diff = true;
    goal.planning_options.planning_scene_diff.robot_state.is_diff = true;

    move_action_client_->sendGoal(goal);
    if (!move_action_client_->waitForResult())
    {
      RCLCPP_INFO_STREAM(rclcpp::get_logger("move_group_interface"), "MoveGroup action returned early");
    }
    if (move_action_client_->getState() == actionlib::SimpleClientGoalState::SUCCEEDED)
    {
      plan.trajectory_ = move_action_client_->getResult()->planned_trajectory;
      plan.start_state_ = move_action_client_->getResult()->trajectory_start;
      plan.planning_time_ = move_action_client_->getResult()->planning_time;
      return MoveItErrorCode(move_action_client_->getResult()->error_code);
    }
    else
    {
      RCLCPP_WARN_STREAM(rclcpp::get_logger("move_group_interface"), "Fail: " << move_action_client_->getState().toString() << ": "
                                                             << move_action_client_->getState().getText());
      return MoveItErrorCode(move_action_client_->getResult()->error_code);
    }
#else
    return MoveItErrorCode(moveit_msgs::msg::MoveItErrorCodes::FAILURE);
#endif
  }

  MoveItErrorCode move(bool wait)
  {
#if 0 //@todo
    if (!move_action_client_)
    {
      return MoveItErrorCode(moveit_msgs::msg::MoveItErrorCodes::FAILURE);
    }
    if (!move_action_client_->isServerConnected())
    {
      return MoveItErrorCode(moveit_msgs::msg::MoveItErrorCodes::FAILURE);
    }

    moveit_msgs::action::MoveGroupGoal goal;
    constructGoal(goal);
    goal.planning_options.plan_only = false;
    goal.planning_options.look_around = can_look_;
    goal.planning_options.replan = can_replan_;
    goal.planning_options.replan_delay = replan_delay_;
    goal.planning_options.planning_scene_diff.is_diff = true;
    goal.planning_options.planning_scene_diff.robot_state.is_diff = true;

    move_action_client_->sendGoal(goal);
    if (!wait)
    {
      return MoveItErrorCode(moveit_msgs::msg::MoveItErrorCodes::SUCCESS);
    }

    if (!move_action_client_->waitForResult())
    {
      RCLCPP_INFO_STREAM(rclcpp::get_logger("move_group_interface"), "MoveGroup action returned early");
    }

    if (move_action_client_->getState() == actionlib::SimpleClientGoalState::SUCCEEDED)
    {
      return MoveItErrorCode(move_action_client_->getResult()->error_code);
    }
    else
    {
      RCLCPP_INFO_STREAM(rclcpp::get_logger("move_group_interface"), move_action_client_->getState().toString()
                                                        << ": " << move_action_client_->getState().getText());
      return MoveItErrorCode(move_action_client_->getResult()->error_code);
    }
#else
  return MoveItErrorCode(moveit_msgs::msg::MoveItErrorCodes::FAILURE);
#endif
  }

  MoveItErrorCode execute(const Plan& plan, bool wait)
  {
#if 0 //@todo
    if (!execute_action_client_->isServerConnected())
    {
      return MoveItErrorCode(moveit_msgs::msg::MoveItErrorCodes::FAILURE);
    }

    moveit_msgs::action::ExecuteTrajectoryGoal goal;
    goal.trajectory = plan.trajectory_;

    execute_action_client_->sendGoal(goal);
    if (!wait)
    {
      return MoveItErrorCode(moveit_msgs::msg::MoveItErrorCodes::SUCCESS);
    }

    if (!execute_action_client_->waitForResult())
    {
      RCLCPP_INFO_STREAM(rclcpp::get_logger("move_group_interface"), "ExecuteTrajectory action returned early");
    }

    if (execute_action_client_->getState() == actionlib::SimpleClientGoalState::SUCCEEDED)
    {
      return MoveItErrorCode(execute_action_client_->getResult()->error_code);
    }
    else
    {
      RCLCPP_INFO_STREAM(rclcpp::get_logger("move_group_interface"), execute_action_client_->getState().toString()
                                                        << ": " << execute_action_client_->getState().getText());
      return MoveItErrorCode(execute_action_client_->getResult()->error_code);
    }
#else
    return MoveItErrorCode(moveit_msgs::msg::MoveItErrorCodes::FAILURE);
#endif
  }

  double computeCartesianPath(const std::vector<geometry_msgs::msg::Pose>& waypoints, double step,
                              double jump_threshold, moveit_msgs::msg::RobotTrajectory& msg,
                              const moveit_msgs::msg::Constraints& path_constraints, bool avoid_collisions,
                              moveit_msgs::msg::MoveItErrorCodes& error_code)
  {
    auto req = std::make_shared<moveit_msgs::srv::GetCartesianPath::Request>();
    moveit_msgs::srv::GetCartesianPath::Response::SharedPtr response;

    if (considered_start_state_)
      robot_state::robotStateToRobotStateMsg(*considered_start_state_, req->start_state);
    else
      req->start_state.is_diff = true;

    req->group_name = opt_.group_name_;
    req->header.frame_id = getPoseReferenceFrame();
    req->header.stamp = getClock()->now();
    req->waypoints = waypoints;
    req->max_step = step;
    req->jump_threshold = jump_threshold;
    req->path_constraints = path_constraints;
    req->avoid_collisions = avoid_collisions;
    req->link_name = getEndEffectorLink();

    auto res = cartesian_path_service_->async_send_request(req);
    if (rclcpp::spin_until_future_complete(node_, res) == rclcpp::executor::FutureReturnCode::SUCCESS)
    {
      response = res.get();
      error_code = response->error_code;
      if (response->error_code.val == moveit_msgs::msg::MoveItErrorCodes::SUCCESS)
      {
        msg = response->solution;
        return response->fraction;
      }
      else
        return -1.0;
    }
    else
    {
      error_code.val = error_code.FAILURE;
      return -1.0;
    }
  }

  void stop()
  {
    if (trajectory_event_publisher_)
    {
      std_msgs::msg::String event;
      event.data = "stop";
      trajectory_event_publisher_->publish(event);
    }
  }

  bool attachObject(const std::string& object, const std::string& link, const std::vector<std::string>& touch_links)
  {
    std::string l = link.empty() ? getEndEffectorLink() : link;
    if (l.empty())
    {
      const std::vector<std::string>& links = joint_model_group_->getLinkModelNames();
      if (!links.empty())
        l = links[0];
    }
    if (l.empty())
    {
      RCLCPP_ERROR(rclcpp::get_logger("move_group_interface"), "No known link to attach object '%s' to", object.c_str());
      return false;
    }
    moveit_msgs::msg::AttachedCollisionObject aco;
    aco.object.id = object;
    aco.link_name.swap(l);
    if (touch_links.empty())
      aco.touch_links.push_back(aco.link_name);
    else
      aco.touch_links = touch_links;
    aco.object.operation = moveit_msgs::msg::CollisionObject::ADD;
    attached_object_publisher_->publish(aco);
    return true;
  }

  bool detachObject(const std::string& name)
  {
    moveit_msgs::msg::AttachedCollisionObject aco;
    // if name is a link
    if (!name.empty() && joint_model_group_->hasLinkModel(name))
      aco.link_name = name;
    else
      aco.object.id = name;
    aco.object.operation = moveit_msgs::msg::CollisionObject::REMOVE;
    if (aco.link_name.empty() && aco.object.id.empty())
    {
      // we only want to detach objects for this group
      const std::vector<std::string>& lnames = joint_model_group_->getLinkModelNames();
      for (const std::string& lname : lnames)
      {
        aco.link_name = lname;
        attached_object_publisher_->publish(aco);
      }
    }
    else
      attached_object_publisher_->publish(aco);
    return true;
  }

  double getGoalPositionTolerance() const
  {
    return goal_position_tolerance_;
  }

  double getGoalOrientationTolerance() const
  {
    return goal_orientation_tolerance_;
  }

  double getGoalJointTolerance() const
  {
    return goal_joint_tolerance_;
  }

  void setGoalJointTolerance(double tolerance)
  {
    goal_joint_tolerance_ = tolerance;
  }

  void setGoalPositionTolerance(double tolerance)
  {
    goal_position_tolerance_ = tolerance;
  }

  void setGoalOrientationTolerance(double tolerance)
  {
    goal_orientation_tolerance_ = tolerance;
  }

  void setPlanningTime(double seconds)
  {
    if (seconds > 0.0)
      allowed_planning_time_ = seconds;
  }

  double getPlanningTime() const
  {
    return allowed_planning_time_;
  }

  void allowLooking(bool flag)
  {
    can_look_ = flag;
    RCLCPP_INFO(rclcpp::get_logger("move_group_interface"),"Looking around: %s", can_look_ ? "yes" : "no");
  }

  void allowReplanning(bool flag)
  {
    can_replan_ = flag;
    RCLCPP_INFO(rclcpp::get_logger("move_group_interface"),"Replanning: %s", can_replan_ ? "yes" : "no");
  }

  void setReplanningDelay(double delay)
  {
    if (delay >= 0.0)
      replan_delay_ = delay;
  }

  double getReplanningDelay() const
  {
    return replan_delay_;
  }

  void constructMotionPlanRequest(moveit_msgs::msg::MotionPlanRequest& request) const
  {
    request.group_name = opt_.group_name_;
    request.num_planning_attempts = num_planning_attempts_;
    request.max_velocity_scaling_factor = max_velocity_scaling_factor_;
    request.max_acceleration_scaling_factor = max_acceleration_scaling_factor_;
    request.allowed_planning_time = allowed_planning_time_;
    request.planner_id = planner_id_;
    request.workspace_parameters = workspace_parameters_;

    if (considered_start_state_)
      robot_state::robotStateToRobotStateMsg(*considered_start_state_, request.start_state);
    else
      request.start_state.is_diff = true;

    if (active_target_ == JOINT)
    {
      request.goal_constraints.resize(1);
      request.goal_constraints[0] = kinematic_constraints::constructGoalConstraints(
          getTargetRobotState(), joint_model_group_, goal_joint_tolerance_);
    }
    else if (active_target_ == POSE || active_target_ == POSITION || active_target_ == ORIENTATION)
    {
      // find out how many goals are specified
      std::size_t goal_count = 0;
      for (const auto& pose_target : pose_targets_)
        goal_count = std::max(goal_count, pose_target.second.size());

      // start filling the goals;
      // each end effector has a number of possible poses (K) as valid goals
      // but there could be multiple end effectors specified, so we want each end effector
      // to reach the goal that corresponds to the goals of the other end effectors
      request.goal_constraints.resize(goal_count);

      for (const auto& pose_target : pose_targets_)
      {
        for (std::size_t i = 0; i < pose_target.second.size(); ++i)
        {
          moveit_msgs::msg::Constraints c = kinematic_constraints::constructGoalConstraints(
              pose_target.first, pose_target.second[i], goal_position_tolerance_, goal_orientation_tolerance_);
          if (active_target_ == ORIENTATION)
            c.position_constraints.clear();
          if (active_target_ == POSITION)
            c.orientation_constraints.clear();
          request.goal_constraints[i] = kinematic_constraints::mergeConstraints(request.goal_constraints[i], c);
        }
      }
    }
    else
      RCLCPP_ERROR(rclcpp::get_logger("move_group_interface"), "Unable to construct MotionPlanRequest representation");

    if (path_constraints_)
      request.path_constraints = *path_constraints_;
    if (trajectory_constraints_)
      request.trajectory_constraints = *trajectory_constraints_;
  }
#if 0 //@todo
  void constructGoal(moveit_msgs::action::MoveGroupGoal& goal) const
  {
    constructMotionPlanRequest(goal.request);
  }
#endif

#if 0 //@todo
  moveit_msgs::action::PickupGoal constructPickupGoal(const std::string& object,
                                                      std::vector<moveit_msgs::msg::Grasp>&& grasps,
                                                      bool plan_only = false) const
  {
    moveit_msgs::action::PickupGoal goal;
    goal.target_name = object;
    goal.group_name = opt_.group_name_;
    goal.end_effector = getEndEffector();
    goal.support_surface_name = support_surface_;
    goal.possible_grasps = std::move(grasps);
    if (!support_surface_.empty())
      goal.allow_gripper_support_collision = true;

    if (path_constraints_)
      goal.path_constraints = *path_constraints_;

    goal.planner_id = planner_id_;
    goal.allowed_planning_time = allowed_planning_time_;

    goal.planning_options.plan_only = plan_only;
    goal.planning_options.look_around = can_look_;
    goal.planning_options.replan = can_replan_;
    goal.planning_options.replan_delay = replan_delay_;
    goal.planning_options.planning_scene_diff.is_diff = true;
    goal.planning_options.planning_scene_diff.robot_state.is_diff = true;
    return goal;
  }

  moveit_msgs::action::PlaceGoal constructPlaceGoal(const std::string& object,
                                                    std::vector<moveit_msgs::msg::PlaceLocation>&& locations,
                                                    bool plan_only = false) const
  {
    moveit_msgs::action::PlaceGoal goal;
    goal.group_name = opt_.group_name_;
    goal.attached_object_name = object;
    goal.support_surface_name = support_surface_;
    goal.place_locations = std::move(locations);
    if (!support_surface_.empty())
      goal.allow_gripper_support_collision = true;

    if (path_constraints_)
      goal.path_constraints = *path_constraints_;

    goal.planner_id = planner_id_;
    goal.allowed_planning_time = allowed_planning_time_;

    goal.planning_options.plan_only = plan_only;
    goal.planning_options.look_around = can_look_;
    goal.planning_options.replan = can_replan_;
    goal.planning_options.replan_delay = replan_delay_;
    goal.planning_options.planning_scene_diff.is_diff = true;
    goal.planning_options.planning_scene_diff.robot_state.is_diff = true;
    return goal;
  }
#endif
  void setPathConstraints(const moveit_msgs::msg::Constraints& constraint)
  {
    path_constraints_.reset(new moveit_msgs::msg::Constraints(constraint));
  }

  bool setPathConstraints(const std::string& constraint)
  {
#if 0 //@todo
    if (constraints_storage_)
    {

      moveit_warehouse::ConstraintsWithMetadata msg_m;
      if (constraints_storage_->getConstraints(msg_m, constraint, robot_model_->getName(), opt_.group_name_))
      {
        path_constraints_.reset(new moveit_msgs::msg::Constraints(static_cast<moveit_msgs::msg::Constraints>(*msg_m)));
        return true;
      }
      else
        return false;
    }
    else
#endif
      return false;
  }

  void clearPathConstraints()
  {
    path_constraints_.reset();
  }

  void setTrajectoryConstraints(const moveit_msgs::msg::TrajectoryConstraints& constraint)
  {
    trajectory_constraints_.reset(new moveit_msgs::msg::TrajectoryConstraints(constraint));
  }

  void clearTrajectoryConstraints()
  {
    trajectory_constraints_.reset();
  }

  std::vector<std::string> getKnownConstraints() const
  {
    while (initializing_constraints_)
    {
      std::chrono::duration<double> d(0.01);
      rclcpp::sleep_for(std::chrono::duration_cast<std::chrono::nanoseconds>(d), rclcpp::Context::SharedPtr(nullptr));
    }

    std::vector<std::string> c;
#if 0 //@todo
    if (constraints_storage_)
      constraints_storage_->getKnownConstraints(c, robot_model_->getName(), opt_.group_name_);
#endif

    return c;
  }

  moveit_msgs::msg::Constraints getPathConstraints() const
  {
    if (path_constraints_)
      return *path_constraints_;
    else
      return moveit_msgs::msg::Constraints();
  }

  moveit_msgs::msg::TrajectoryConstraints getTrajectoryConstraints() const
  {
    if (trajectory_constraints_)
      return *trajectory_constraints_;
    else
      return moveit_msgs::msg::TrajectoryConstraints();
  }

  void initializeConstraintsStorage(const std::string& host, unsigned int port)
  {
    initializing_constraints_ = true;
    if (constraints_init_thread_)
      constraints_init_thread_->join();
    constraints_init_thread_.reset(
        new boost::thread(boost::bind(&MoveGroupInterfaceImpl::initializeConstraintsStorageThread, this, host, port)));
  }

  void setWorkspace(double minx, double miny, double minz, double maxx, double maxy, double maxz)
  {
    workspace_parameters_.header.frame_id = getRobotModel()->getModelFrame();
    workspace_parameters_.header.stamp = getClock()->now();
    workspace_parameters_.min_corner.x = minx;
    workspace_parameters_.min_corner.y = miny;
    workspace_parameters_.min_corner.z = minz;
    workspace_parameters_.max_corner.x = maxx;
    workspace_parameters_.max_corner.y = maxy;
    workspace_parameters_.max_corner.z = maxz;
  }

  rclcpp::Clock::SharedPtr getClock() 
  {
    return node_->get_clock();
  }
private:
  void initializeConstraintsStorageThread(const std::string& host, unsigned int port)
  {
    // Set up db
    try
    {
#if 0 //@todo
      warehouse_ros::DatabaseConnection::Ptr conn = moveit_warehouse::loadDatabase();
      conn->setParams(host, port);
      if (conn->connect())
      {
        constraints_storage_.reset(new moveit_warehouse::ConstraintsStorage(conn));
      }
#endif
    }
    catch (std::exception& ex)
    {
      RCLCPP_ERROR(rclcpp::get_logger("move_group_interface"), "%s", ex.what());
    }
    initializing_constraints_ = false;
  }

  Options opt_;
  rclcpp::Node::SharedPtr node_;
  std::shared_ptr<tf2_ros::Buffer> tf_buffer_;
  robot_model::RobotModelConstPtr robot_model_;
  planning_scene_monitor::CurrentStateMonitorPtr current_state_monitor_;
#if 0 //@todo
  std::unique_ptr<actionlib::SimpleActionClient<moveit_msgs::action::MoveGroupAction> > move_action_client_;
  std::unique_ptr<actionlib::SimpleActionClient<moveit_msgs::action::ExecuteTrajectoryAction> > execute_action_client_;
  std::unique_ptr<actionlib::SimpleActionClient<moveit_msgs::action::PickupAction> > pick_action_client_;
  std::unique_ptr<actionlib::SimpleActionClient<moveit_msgs::action::PlaceAction> > place_action_client_;
#endif
  // general planning params
  robot_state::RobotStatePtr considered_start_state_;
  moveit_msgs::msg::WorkspaceParameters workspace_parameters_;
  double allowed_planning_time_;
  std::string planner_id_;
  unsigned int num_planning_attempts_;
  double max_velocity_scaling_factor_;
  double max_acceleration_scaling_factor_;
  double goal_joint_tolerance_;
  double goal_position_tolerance_;
  double goal_orientation_tolerance_;
  bool can_look_;
  bool can_replan_;
  double replan_delay_;

  // joint state goal
  robot_state::RobotStatePtr joint_state_target_;
  const robot_model::JointModelGroup* joint_model_group_;

  // pose goal;
  // for each link we have a set of possible goal locations;
  std::map<std::string, std::vector<geometry_msgs::msg::PoseStamped> > pose_targets_;

  // common properties for goals
  ActiveTargetType active_target_;
  std::unique_ptr<moveit_msgs::msg::Constraints> path_constraints_;
  std::unique_ptr<moveit_msgs::msg::TrajectoryConstraints> trajectory_constraints_;
  std::string end_effector_link_;
  std::string pose_reference_frame_;
  std::string support_surface_;

  // ROS communication
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr trajectory_event_publisher_;
  rclcpp::Publisher<moveit_msgs::msg::AttachedCollisionObject>::SharedPtr attached_object_publisher_;
  rclcpp::Client<moveit_msgs::srv::QueryPlannerInterfaces>::SharedPtr query_service_;
  rclcpp::Client<moveit_msgs::srv::GetPlannerParams>::SharedPtr get_params_service_;
  rclcpp::Client<moveit_msgs::srv::SetPlannerParams>::SharedPtr set_params_service_;
  rclcpp::Client<moveit_msgs::srv::GetCartesianPath>::SharedPtr cartesian_path_service_;
  rclcpp::Client<moveit_msgs::srv::GraspPlanning>::SharedPtr plan_grasps_service_;
#if 0 //@todo
  std::unique_ptr<moveit_warehouse::ConstraintsStorage> constraints_storage_;
#endif
  std::unique_ptr<boost::thread> constraints_init_thread_;
  bool initializing_constraints_;
};

MoveGroupInterface::MoveGroupInterface(const std::string& group_name, const std::shared_ptr<tf2_ros::Buffer>& tf_buffer,
                                       const rclcpp::Duration& wait_for_servers)
{
  if (!rclcpp::ok())
    throw std::runtime_error("ROS does not seem to be running");
  impl_ = new MoveGroupInterfaceImpl(Options(group_name), tf_buffer ? tf_buffer : getSharedTF(), wait_for_servers);
}
#if 0
MoveGroupInterface::MoveGroupInterface(const std::string& group, const std::shared_ptr<tf2_ros::Buffer>& tf_buffer,
                                       const rclcpp::Duration& wait_for_servers)
  : MoveGroupInterface(group, tf_buffer, rclcpp::Duration(wait_for_servers.toSec()))
{
}
#endif
MoveGroupInterface::MoveGroupInterface(const Options& opt, const std::shared_ptr<tf2_ros::Buffer>& tf_buffer,
                                       const rclcpp::Duration& wait_for_servers)
{
  impl_ = new MoveGroupInterfaceImpl(opt, tf_buffer ? tf_buffer : getSharedTF(), wait_for_servers);
}

#if 0 //@todo
MoveGroupInterface::MoveGroupInterface(const MoveGroupInterface::Options& opt,
                                       const std::shared_ptr<tf2_ros::Buffer>& tf_buffer,
                                       const rclcpp::Duration& wait_for_servers)
  : MoveGroupInterface(opt, tf_buffer, rclcpp::Duration(wait_for_servers.toSec()))
{
}
#endif

MoveGroupInterface::~MoveGroupInterface()
{
  delete impl_;
}

MoveGroupInterface::MoveGroupInterface(MoveGroupInterface&& other) noexcept
  : remembered_joint_values_(std::move(other.remembered_joint_values_)), impl_(other.impl_)
{
  other.impl_ = nullptr;
}

MoveGroupInterface& MoveGroupInterface::operator=(MoveGroupInterface&& other) noexcept
{
  if (this != &other)
  {
    delete impl_;
    impl_ = other.impl_;
    remembered_joint_values_ = std::move(other.remembered_joint_values_);
    other.impl_ = nullptr;
  }

  return *this;
}

const std::string& MoveGroupInterface::getName() const
{
  return impl_->getOptions().group_name_;
}

const std::vector<std::string>& MoveGroupInterface::getNamedTargets() const
{
  // The pointer returned by getJointModelGroup is guaranteed by the class
  // constructor to always be non-null
  return impl_->getJointModelGroup()->getDefaultStateNames();
}

robot_model::RobotModelConstPtr MoveGroupInterface::getRobotModel() const
{
  return impl_->getRobotModel();
}

rclcpp::Node::SharedPtr MoveGroupInterface::getNodeHandle() 
{
  return impl_->getOptions().node_;
}

bool MoveGroupInterface::getInterfaceDescription(moveit_msgs::msg::PlannerInterfaceDescription& desc) const
{
  return impl_->getInterfaceDescription(desc);
}

std::map<std::string, std::string> MoveGroupInterface::getPlannerParams(const std::string& planner_id,
                                                                        const std::string& group) const
{
  return impl_->getPlannerParams(planner_id, group);
}

void MoveGroupInterface::setPlannerParams(const std::string& planner_id, const std::string& group,
                                          const std::map<std::string, std::string>& params, bool replace)
{
  impl_->setPlannerParams(planner_id, group, params, replace);
}

std::string MoveGroupInterface::getDefaultPlannerId(const std::string& group) const
{
  return impl_->getDefaultPlannerId(group);
}

void MoveGroupInterface::setPlannerId(const std::string& planner_id)
{
  impl_->setPlannerId(planner_id);
}

const std::string& MoveGroupInterface::getPlannerId() const
{
  return impl_->getPlannerId();
}

void MoveGroupInterface::setNumPlanningAttempts(unsigned int num_planning_attempts)
{
  impl_->setNumPlanningAttempts(num_planning_attempts);
}

void MoveGroupInterface::setMaxVelocityScalingFactor(double max_velocity_scaling_factor)
{
  impl_->setMaxVelocityScalingFactor(max_velocity_scaling_factor);
}

void MoveGroupInterface::setMaxAccelerationScalingFactor(double max_acceleration_scaling_factor)
{
  impl_->setMaxAccelerationScalingFactor(max_acceleration_scaling_factor);
}

MoveItErrorCode MoveGroupInterface::asyncMove()
{
  return impl_->move(false);
}
#if 0 //@todo
actionlib::SimpleActionClient<moveit_msgs::action::MoveGroupAction>& MoveGroupInterface::getMoveGroupClient() const
{
  return impl_->getMoveGroupClient();
}
#endif
MoveItErrorCode MoveGroupInterface::move()
{
  return impl_->move(true);
}

MoveItErrorCode MoveGroupInterface::asyncExecute(const Plan& plan)
{
  return impl_->execute(plan, false);
}

MoveItErrorCode MoveGroupInterface::execute(const Plan& plan)
{
  return impl_->execute(plan, true);
}

MoveItErrorCode MoveGroupInterface::plan(Plan& plan)
{
  return impl_->plan(plan);
}
#if 0 //@todo
moveit_msgs::action::PickupGoal MoveGroupInterface::constructPickupGoal(const std::string& object,
                                                                        std::vector<moveit_msgs::msg::Grasp> grasps,
                                                                        bool plan_only = false) const
{
  return impl_->constructPickupGoal(object, std::move(grasps), plan_only);
}

moveit_msgs::action::PlaceGoal MoveGroupInterface::constructPlaceGoal(
    const std::string& object, std::vector<moveit_msgs::msg::PlaceLocation> locations, bool plan_only = false) const
{
  return impl_->constructPlaceGoal(object, std::move(locations), plan_only);
}
#endif
std::vector<moveit_msgs::msg::PlaceLocation>
MoveGroupInterface::posesToPlaceLocations(const std::vector<geometry_msgs::msg::PoseStamped>& poses) const
{
  return impl_->posesToPlaceLocations(poses);
}
#if 0 //@todo
MoveItErrorCode MoveGroupInterface::pick(const moveit_msgs::action::PickupGoal& goal)
{
  return impl_->pick(goal);
}
#endif
MoveItErrorCode MoveGroupInterface::planGraspsAndPick(const std::string& object, bool plan_only)
{
  return impl_->planGraspsAndPick(object, plan_only);
}

MoveItErrorCode MoveGroupInterface::planGraspsAndPick(const moveit_msgs::msg::CollisionObject& object, bool plan_only)
{
  return impl_->planGraspsAndPick(object, plan_only);
}
#if 0 //@todo
MoveItErrorCode MoveGroupInterface::place(const moveit_msgs::action::PlaceGoal& goal)
{
  return impl_->place(goal);
}
#endif
double MoveGroupInterface::computeCartesianPath(const std::vector<geometry_msgs::msg::Pose>& waypoints, double eef_step,
                                                double jump_threshold, moveit_msgs::msg::RobotTrajectory& trajectory,
                                                bool avoid_collisions, moveit_msgs::msg::MoveItErrorCodes* error_code)
{
  moveit_msgs::msg::Constraints path_constraints_tmp;
  return computeCartesianPath(waypoints, eef_step, jump_threshold, trajectory, path_constraints_tmp, avoid_collisions,
                              error_code);
}

double MoveGroupInterface::computeCartesianPath(const std::vector<geometry_msgs::msg::Pose>& waypoints, double eef_step,
                                                double jump_threshold, moveit_msgs::msg::RobotTrajectory& trajectory,
                                                const moveit_msgs::msg::Constraints& path_constraints,
                                                bool avoid_collisions, moveit_msgs::msg::MoveItErrorCodes* error_code)
{
  if (error_code)
  {
    return impl_->computeCartesianPath(waypoints, eef_step, jump_threshold, trajectory, path_constraints,
                                       avoid_collisions, *error_code);
  }
  else
  {
    moveit_msgs::msg::MoveItErrorCodes error_code_tmp;
    return impl_->computeCartesianPath(waypoints, eef_step, jump_threshold, trajectory, path_constraints,
                                       avoid_collisions, error_code_tmp);
  }
}

void MoveGroupInterface::stop()
{
  impl_->stop();
}

void MoveGroupInterface::setStartState(const moveit_msgs::msg::RobotState& start_state)
{
  robot_state::RobotStatePtr rs;
  impl_->getCurrentState(rs);
  robot_state::robotStateMsgToRobotState(start_state, *rs);
  setStartState(*rs);
}

void MoveGroupInterface::setStartState(const robot_state::RobotState& start_state)
{
  impl_->setStartState(start_state);
}

void MoveGroupInterface::setStartStateToCurrentState()
{
  impl_->setStartStateToCurrentState();
}

void MoveGroupInterface::setRandomTarget()
{
  impl_->getTargetRobotState().setToRandomPositions();
  impl_->setTargetType(JOINT);
}

const std::vector<std::string>& MoveGroupInterface::getJointNames() const
{
  return impl_->getJointModelGroup()->getVariableNames();
}

const std::vector<std::string>& MoveGroupInterface::getLinkNames() const
{
  return impl_->getJointModelGroup()->getLinkModelNames();
}

std::map<std::string, double> MoveGroupInterface::getNamedTargetValues(const std::string& name) const
{
  std::map<std::string, std::vector<double> >::const_iterator it = remembered_joint_values_.find(name);
  std::map<std::string, double> positions;

  if (it != remembered_joint_values_.cend())
  {
    std::vector<std::string> names = impl_->getJointModelGroup()->getVariableNames();
    for (size_t x = 0; x < names.size(); ++x)
    {
      positions[names[x]] = it->second[x];
    }
  }
  else
  {
    impl_->getJointModelGroup()->getVariableDefaultPositions(name, positions);
  }
  return positions;
}

bool MoveGroupInterface::setNamedTarget(const std::string& name)
{
  std::map<std::string, std::vector<double> >::const_iterator it = remembered_joint_values_.find(name);
  if (it != remembered_joint_values_.end())
  {
    return setJointValueTarget(it->second);
  }
  else
  {
    if (impl_->getTargetRobotState().setToDefaultValues(impl_->getJointModelGroup(), name))
    {
      impl_->setTargetType(JOINT);
      return true;
    }
    RCLCPP_ERROR(rclcpp::get_logger("move_group_interface"), "The requested named target '%s' does not exist", name.c_str());
    return false;
  }
}

void MoveGroupInterface::getJointValueTarget(std::vector<double>& group_variable_values) const
{
  impl_->getTargetRobotState().copyJointGroupPositions(impl_->getJointModelGroup(), group_variable_values);
}

bool MoveGroupInterface::setJointValueTarget(const std::vector<double>& joint_values)
{
  if (joint_values.size() != impl_->getJointModelGroup()->getVariableCount())
    return false;
  impl_->setTargetType(JOINT);
  impl_->getTargetRobotState().setJointGroupPositions(impl_->getJointModelGroup(), joint_values);
  return impl_->getTargetRobotState().satisfiesBounds(impl_->getJointModelGroup(), impl_->getGoalJointTolerance());
}

bool MoveGroupInterface::setJointValueTarget(const std::map<std::string, double>& variable_values)
{
  const auto& allowed = impl_->getJointModelGroup()->getVariableNames();
  for (const auto& pair : variable_values)
  {
    if (std::find(allowed.begin(), allowed.end(), pair.first) == allowed.end())
    {
      RCLCPP_ERROR_STREAM(rclcpp::get_logger("move_group_interface"), "joint variable " << pair.first << " is not part of group "
                                         << impl_->getJointModelGroup()->getName());
      return false;
    }
  }

  impl_->setTargetType(JOINT);
  impl_->getTargetRobotState().setVariablePositions(variable_values);
  return impl_->getTargetRobotState().satisfiesBounds(impl_->getGoalJointTolerance());
}

bool MoveGroupInterface::setJointValueTarget(const std::vector<std::string>& variable_names,
                                             const std::vector<double>& variable_values)
{
  const auto& allowed = impl_->getJointModelGroup()->getVariableNames();
  for (const auto& variable_name : variable_names)
  {
    if (std::find(allowed.begin(), allowed.end(), variable_name) == allowed.end())
    {
      RCLCPP_ERROR_STREAM(rclcpp::get_logger("move_group_interface"), "joint variable " << variable_name << " is not part of group "
                                         << impl_->getJointModelGroup()->getName());
      return false;
    }
  }

  impl_->setTargetType(JOINT);
  impl_->getTargetRobotState().setVariablePositions(variable_names, variable_values);
  return impl_->getTargetRobotState().satisfiesBounds(impl_->getGoalJointTolerance());
}

bool MoveGroupInterface::setJointValueTarget(const robot_state::RobotState& rstate)
{
  impl_->setTargetType(JOINT);
  impl_->getTargetRobotState() = rstate;
  return impl_->getTargetRobotState().satisfiesBounds(impl_->getGoalJointTolerance());
}

bool MoveGroupInterface::setJointValueTarget(const std::string& joint_name, double value)
{
  std::vector<double> values(1, value);
  return setJointValueTarget(joint_name, values);
}

bool MoveGroupInterface::setJointValueTarget(const std::string& joint_name, const std::vector<double>& values)
{
  impl_->setTargetType(JOINT);
  const robot_model::JointModel* jm = impl_->getJointModelGroup()->getJointModel(joint_name);
  if (jm && jm->getVariableCount() == values.size())
  {
    impl_->getTargetRobotState().setJointPositions(jm, values);
    return impl_->getTargetRobotState().satisfiesBounds(jm, impl_->getGoalJointTolerance());
  }

  RCLCPP_ERROR_STREAM(rclcpp::get_logger("move_group_interface"), "joint " << joint_name << " is not part of group " << impl_->getJointModelGroup()->getName());
  return false;
}

bool MoveGroupInterface::setJointValueTarget(const sensor_msgs::msg::JointState& state)
{
  return setJointValueTarget(state.name, state.position);
}

bool MoveGroupInterface::setJointValueTarget(const geometry_msgs::msg::Pose& eef_pose,
                                             const std::string& end_effector_link)
{
  return impl_->setJointValueTarget(eef_pose, end_effector_link, "", false);
}

bool MoveGroupInterface::setJointValueTarget(const geometry_msgs::msg::PoseStamped& eef_pose,
                                             const std::string& end_effector_link)
{
  return impl_->setJointValueTarget(eef_pose.pose, end_effector_link, eef_pose.header.frame_id, false);
}

bool MoveGroupInterface::setJointValueTarget(const Eigen::Isometry3d& eef_pose, const std::string& end_effector_link)
{
  geometry_msgs::msg::Pose msg = tf2::toMsg(eef_pose);
  return setJointValueTarget(msg, end_effector_link);
}

bool MoveGroupInterface::setApproximateJointValueTarget(const geometry_msgs::msg::Pose& eef_pose,
                                                        const std::string& end_effector_link)
{
  return impl_->setJointValueTarget(eef_pose, end_effector_link, "", true);
}

bool MoveGroupInterface::setApproximateJointValueTarget(const geometry_msgs::msg::PoseStamped& eef_pose,
                                                        const std::string& end_effector_link)
{
  return impl_->setJointValueTarget(eef_pose.pose, end_effector_link, eef_pose.header.frame_id, true);
}

bool MoveGroupInterface::setApproximateJointValueTarget(const Eigen::Isometry3d& eef_pose,
                                                        const std::string& end_effector_link)
{
  geometry_msgs::msg::Pose msg = tf2::toMsg(eef_pose);
  return setApproximateJointValueTarget(msg, end_effector_link);
}

const robot_state::RobotState& MoveGroupInterface::getJointValueTarget() const
{
  return impl_->getTargetRobotState();
}

const robot_state::RobotState& MoveGroupInterface::getTargetRobotState() const
{
  return impl_->getTargetRobotState();
}

const std::string& MoveGroupInterface::getEndEffectorLink() const
{
  return impl_->getEndEffectorLink();
}

const std::string& MoveGroupInterface::getEndEffector() const
{
  return impl_->getEndEffector();
}

bool MoveGroupInterface::setEndEffectorLink(const std::string& link_name)
{
  if (impl_->getEndEffectorLink().empty() || link_name.empty())
    return false;
  impl_->setEndEffectorLink(link_name);
  impl_->setTargetType(POSE);
  return true;
}

bool MoveGroupInterface::setEndEffector(const std::string& eef_name)
{
  const robot_model::JointModelGroup* jmg = impl_->getRobotModel()->getEndEffector(eef_name);
  if (jmg)
    return setEndEffectorLink(jmg->getEndEffectorParentGroup().second);
  return false;
}

void MoveGroupInterface::clearPoseTarget(const std::string& end_effector_link)
{
  impl_->clearPoseTarget(end_effector_link);
}

void MoveGroupInterface::clearPoseTargets()
{
  impl_->clearPoseTargets();
}

bool MoveGroupInterface::setPoseTarget(const Eigen::Isometry3d& pose, const std::string& end_effector_link)
{
  std::vector<geometry_msgs::msg::PoseStamped> pose_msg(1);
  pose_msg[0].pose = tf2::toMsg(pose);
  pose_msg[0].header.frame_id = getPoseReferenceFrame();
  pose_msg[0].header.stamp = impl_->getClock()->now();
  return setPoseTargets(pose_msg, end_effector_link);
}

bool MoveGroupInterface::setPoseTarget(const geometry_msgs::msg::Pose& target, const std::string& end_effector_link)
{
  std::vector<geometry_msgs::msg::PoseStamped> pose_msg(1);
  pose_msg[0].pose = target;
  pose_msg[0].header.frame_id = getPoseReferenceFrame();
  pose_msg[0].header.stamp = impl_->getClock()->now();
  return setPoseTargets(pose_msg, end_effector_link);
}

bool MoveGroupInterface::setPoseTarget(const geometry_msgs::msg::PoseStamped& target,
                                       const std::string& end_effector_link)
{
  std::vector<geometry_msgs::msg::PoseStamped> targets(1, target);
  return setPoseTargets(targets, end_effector_link);
}

bool MoveGroupInterface::setPoseTargets(const EigenSTL::vector_Isometry3d& target, const std::string& end_effector_link)
{
  std::vector<geometry_msgs::msg::PoseStamped> pose_out(target.size());
  rclcpp::Time tm = impl_->getClock()->now();
  const std::string& frame_id = getPoseReferenceFrame();
  for (std::size_t i = 0; i < target.size(); ++i)
  {
    pose_out[i].pose = tf2::toMsg(target[i]);
    pose_out[i].header.stamp = tm;
    pose_out[i].header.frame_id = frame_id;
  }
  return setPoseTargets(pose_out, end_effector_link);
}

bool MoveGroupInterface::setPoseTargets(const std::vector<geometry_msgs::msg::Pose>& target,
                                        const std::string& end_effector_link)
{
  std::vector<geometry_msgs::msg::PoseStamped> target_stamped(target.size());
  rclcpp::Time tm = impl_->getClock()->now();
  const std::string& frame_id = getPoseReferenceFrame();
  for (std::size_t i = 0; i < target.size(); ++i)
  {
    target_stamped[i].pose = target[i];
    target_stamped[i].header.stamp = tm;
    target_stamped[i].header.frame_id = frame_id;
  }
  return setPoseTargets(target_stamped, end_effector_link);
}

bool MoveGroupInterface::setPoseTargets(const std::vector<geometry_msgs::msg::PoseStamped>& target,
                                        const std::string& end_effector_link)
{
  if (target.empty())
  {
    RCLCPP_ERROR(rclcpp::get_logger("move_group_interface"), "No pose specified as goal target");
    return false;
  }
  else
  {
    impl_->setTargetType(POSE);
    return impl_->setPoseTargets(target, end_effector_link);
  }
}

const geometry_msgs::msg::PoseStamped& MoveGroupInterface::getPoseTarget(const std::string& end_effector_link) const
{
  return impl_->getPoseTarget(end_effector_link);
}

const std::vector<geometry_msgs::msg::PoseStamped>&
MoveGroupInterface::getPoseTargets(const std::string& end_effector_link) const
{
  return impl_->getPoseTargets(end_effector_link);
}

namespace
{
inline void transformPose(const tf2_ros::Buffer& tf_buffer, const std::string& desired_frame,
                          geometry_msgs::msg::PoseStamped& target)
{
  if (desired_frame != target.header.frame_id)
  {
    geometry_msgs::msg::PoseStamped target_in(target);
    tf_buffer.transform(target_in, target, desired_frame);
    // we leave the stamp to ros::Time(0) on purpose
    target.header.stamp = rclcpp::Time(0);
  }
}
}  // namespace

bool MoveGroupInterface::setPositionTarget(double x, double y, double z, const std::string& end_effector_link)
{
  geometry_msgs::msg::PoseStamped target;
  if (impl_->hasPoseTarget(end_effector_link))
  {
    target = getPoseTarget(end_effector_link);
    transformPose(*impl_->getTF(), impl_->getPoseReferenceFrame(), target);
  }
  else
  {
    target.pose.orientation.x = 0.0;
    target.pose.orientation.y = 0.0;
    target.pose.orientation.z = 0.0;
    target.pose.orientation.w = 1.0;
    target.header.frame_id = impl_->getPoseReferenceFrame();
  }

  target.pose.position.x = x;
  target.pose.position.y = y;
  target.pose.position.z = z;
  bool result = setPoseTarget(target, end_effector_link);
  impl_->setTargetType(POSITION);
  return result;
}

bool MoveGroupInterface::setRPYTarget(double r, double p, double y, const std::string& end_effector_link)
{
  geometry_msgs::msg::PoseStamped target;
  if (impl_->hasPoseTarget(end_effector_link))
  {
    target = getPoseTarget(end_effector_link);
    transformPose(*impl_->getTF(), impl_->getPoseReferenceFrame(), target);
  }
  else
  {
    target.pose.position.x = 0.0;
    target.pose.position.y = 0.0;
    target.pose.position.z = 0.0;
    target.header.frame_id = impl_->getPoseReferenceFrame();
  }
  tf2::Quaternion q;
  q.setRPY(r, p, y);
  target.pose.orientation = tf2::toMsg(q);
  bool result = setPoseTarget(target, end_effector_link);
  impl_->setTargetType(ORIENTATION);
  return result;
}

bool MoveGroupInterface::setOrientationTarget(double x, double y, double z, double w,
                                              const std::string& end_effector_link)
{
  geometry_msgs::msg::PoseStamped target;
  if (impl_->hasPoseTarget(end_effector_link))
  {
    target = getPoseTarget(end_effector_link);
    transformPose(*impl_->getTF(), impl_->getPoseReferenceFrame(), target);
  }
  else
  {
    target.pose.position.x = 0.0;
    target.pose.position.y = 0.0;
    target.pose.position.z = 0.0;
    target.header.frame_id = impl_->getPoseReferenceFrame();
  }

  target.pose.orientation.x = x;
  target.pose.orientation.y = y;
  target.pose.orientation.z = z;
  target.pose.orientation.w = w;
  bool result = setPoseTarget(target, end_effector_link);
  impl_->setTargetType(ORIENTATION);
  return result;
}

void MoveGroupInterface::setPoseReferenceFrame(const std::string& pose_reference_frame)
{
  impl_->setPoseReferenceFrame(pose_reference_frame);
}

const std::string& MoveGroupInterface::getPoseReferenceFrame() const
{
  return impl_->getPoseReferenceFrame();
}

double MoveGroupInterface::getGoalJointTolerance() const
{
  return impl_->getGoalJointTolerance();
}

double MoveGroupInterface::getGoalPositionTolerance() const
{
  return impl_->getGoalPositionTolerance();
}

double MoveGroupInterface::getGoalOrientationTolerance() const
{
  return impl_->getGoalOrientationTolerance();
}

void MoveGroupInterface::setGoalTolerance(double tolerance)
{
  setGoalJointTolerance(tolerance);
  setGoalPositionTolerance(tolerance);
  setGoalOrientationTolerance(tolerance);
}

void MoveGroupInterface::setGoalJointTolerance(double tolerance)
{
  impl_->setGoalJointTolerance(tolerance);
}

void MoveGroupInterface::setGoalPositionTolerance(double tolerance)
{
  impl_->setGoalPositionTolerance(tolerance);
}

void MoveGroupInterface::setGoalOrientationTolerance(double tolerance)
{
  impl_->setGoalOrientationTolerance(tolerance);
}

void MoveGroupInterface::rememberJointValues(const std::string& name)
{
  rememberJointValues(name, getCurrentJointValues());
}

bool MoveGroupInterface::startStateMonitor(double wait)
{
  return impl_->startStateMonitor(wait);
}

std::vector<double> MoveGroupInterface::getCurrentJointValues() const
{
  robot_state::RobotStatePtr current_state;
  std::vector<double> values;
  if (impl_->getCurrentState(current_state))
    current_state->copyJointGroupPositions(getName(), values);
  return values;
}

std::vector<double> MoveGroupInterface::getRandomJointValues() const
{
  std::vector<double> r;
  impl_->getJointModelGroup()->getVariableRandomPositions(impl_->getTargetRobotState().getRandomNumberGenerator(), r);
  return r;
}

geometry_msgs::msg::PoseStamped MoveGroupInterface::getRandomPose(const std::string& end_effector_link) const
{
  const std::string& eef = end_effector_link.empty() ? getEndEffectorLink() : end_effector_link;
  Eigen::Isometry3d pose;
  pose.setIdentity();
  if (eef.empty())
    RCLCPP_ERROR(rclcpp::get_logger("move_group_interface"), "No end-effector specified");
  else
  {
    robot_state::RobotStatePtr current_state;
    if (impl_->getCurrentState(current_state))
    {
      current_state->setToRandomPositions(impl_->getJointModelGroup());
      const robot_model::LinkModel* lm = current_state->getLinkModel(eef);
      if (lm)
        pose = current_state->getGlobalLinkTransform(lm);
    }
  }
  geometry_msgs::msg::PoseStamped pose_msg;
  pose_msg.header.stamp = impl_->getClock()->now();
  pose_msg.header.frame_id = impl_->getRobotModel()->getModelFrame();
  pose_msg.pose = tf2::toMsg(pose);
  return pose_msg;
}

geometry_msgs::msg::PoseStamped MoveGroupInterface::getCurrentPose(const std::string& end_effector_link) const
{
  const std::string& eef = end_effector_link.empty() ? getEndEffectorLink() : end_effector_link;
  Eigen::Isometry3d pose;
  pose.setIdentity();
  if (eef.empty())
    RCLCPP_ERROR(rclcpp::get_logger("move_group_interface"), "No end-effector specified");
  else
  {
    robot_state::RobotStatePtr current_state;
    if (impl_->getCurrentState(current_state))
    {
      const robot_model::LinkModel* lm = current_state->getLinkModel(eef);
      if (lm)
        pose = current_state->getGlobalLinkTransform(lm);
    }
  }
  geometry_msgs::msg::PoseStamped pose_msg;
  pose_msg.header.stamp = impl_->getClock()->now();
  pose_msg.header.frame_id = impl_->getRobotModel()->getModelFrame();
  pose_msg.pose = tf2::toMsg(pose);
  return pose_msg;
}

std::vector<double> MoveGroupInterface::getCurrentRPY(const std::string& end_effector_link) const
{
  std::vector<double> result;
  const std::string& eef = end_effector_link.empty() ? getEndEffectorLink() : end_effector_link;
  if (eef.empty())
    RCLCPP_ERROR(rclcpp::get_logger("move_group_interface"), "No end-effector specified");
  else
  {
    robot_state::RobotStatePtr current_state;
    if (impl_->getCurrentState(current_state))
    {
      const robot_model::LinkModel* lm = current_state->getLinkModel(eef);
      if (lm)
      {
        result.resize(3);
        geometry_msgs::msg::TransformStamped tfs = tf2::eigenToTransform(current_state->getGlobalLinkTransform(lm));
        double pitch, roll, yaw;
        tf2::getEulerYPR<geometry_msgs::msg::Quaternion>(tfs.transform.rotation, yaw, pitch, roll);
        result[0] = roll;
        result[1] = pitch;
        result[2] = yaw;
      }
    }
  }
  return result;
}

const std::vector<std::string>& MoveGroupInterface::getActiveJoints() const
{
  return impl_->getJointModelGroup()->getActiveJointModelNames();
}

const std::vector<std::string>& MoveGroupInterface::getJoints() const
{
  return impl_->getJointModelGroup()->getJointModelNames();
}

unsigned int MoveGroupInterface::getVariableCount() const
{
  return impl_->getJointModelGroup()->getVariableCount();
}

robot_state::RobotStatePtr MoveGroupInterface::getCurrentState(double wait) const
{
  robot_state::RobotStatePtr current_state;
  impl_->getCurrentState(current_state, wait);
  return current_state;
}

void MoveGroupInterface::rememberJointValues(const std::string& name, const std::vector<double>& values)
{
  remembered_joint_values_[name] = values;
}

void MoveGroupInterface::forgetJointValues(const std::string& name)
{
  remembered_joint_values_.erase(name);
}

void MoveGroupInterface::allowLooking(bool flag)
{
  impl_->allowLooking(flag);
}

void MoveGroupInterface::allowReplanning(bool flag)
{
  impl_->allowReplanning(flag);
}

std::vector<std::string> MoveGroupInterface::getKnownConstraints() const
{
  return impl_->getKnownConstraints();
}

moveit_msgs::msg::Constraints MoveGroupInterface::getPathConstraints() const
{
  return impl_->getPathConstraints();
}

bool MoveGroupInterface::setPathConstraints(const std::string& constraint)
{
  return impl_->setPathConstraints(constraint);
}

void MoveGroupInterface::setPathConstraints(const moveit_msgs::msg::Constraints& constraint)
{
  impl_->setPathConstraints(constraint);
}

void MoveGroupInterface::clearPathConstraints()
{
  impl_->clearPathConstraints();
}

moveit_msgs::msg::TrajectoryConstraints MoveGroupInterface::getTrajectoryConstraints() const
{
  return impl_->getTrajectoryConstraints();
}

void MoveGroupInterface::setTrajectoryConstraints(const moveit_msgs::msg::TrajectoryConstraints& constraint)
{
  impl_->setTrajectoryConstraints(constraint);
}

void MoveGroupInterface::clearTrajectoryConstraints()
{
  impl_->clearTrajectoryConstraints();
}

void MoveGroupInterface::setConstraintsDatabase(const std::string& host, unsigned int port)
{
  impl_->initializeConstraintsStorage(host, port);
}

void MoveGroupInterface::setWorkspace(double minx, double miny, double minz, double maxx, double maxy, double maxz)
{
  impl_->setWorkspace(minx, miny, minz, maxx, maxy, maxz);
}

/** \brief Set time allowed to planner to solve problem before aborting */
void MoveGroupInterface::setPlanningTime(double seconds)
{
  impl_->setPlanningTime(seconds);
}

/** \brief Get time allowed to planner to solve problem before aborting */
double MoveGroupInterface::getPlanningTime() const
{
  return impl_->getPlanningTime();
}

void MoveGroupInterface::setSupportSurfaceName(const std::string& name)
{
  impl_->setSupportSurfaceName(name);
}

const std::string& MoveGroupInterface::getPlanningFrame() const
{
  return impl_->getRobotModel()->getModelFrame();
}

const std::vector<std::string>& MoveGroupInterface::getJointModelGroupNames() const
{
  return impl_->getRobotModel()->getJointModelGroupNames();
}

bool MoveGroupInterface::attachObject(const std::string& object, const std::string& link)
{
  return attachObject(object, link, std::vector<std::string>());
}

bool MoveGroupInterface::attachObject(const std::string& object, const std::string& link,
                                      const std::vector<std::string>& touch_links)
{
  return impl_->attachObject(object, link, touch_links);
}

bool MoveGroupInterface::detachObject(const std::string& name)
{
  return impl_->detachObject(name);
}

void MoveGroupInterface::constructMotionPlanRequest(moveit_msgs::msg::MotionPlanRequest& goal_out)
{
  impl_->constructMotionPlanRequest(goal_out);
}

}  // namespace planning_interface
}  // namespace moveit
