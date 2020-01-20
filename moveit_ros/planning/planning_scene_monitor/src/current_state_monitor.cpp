/*********************************************************************
 * Software License Agreement (BSD License)
 *
 *  Copyright (c) 2011, Willow Garage, Inc.
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

/* Author: Ioan Sucan */

#include <moveit/planning_scene_monitor/current_state_monitor.h>

#include <tf2_eigen/tf2_eigen.h>
#include <tf2_geometry_msgs/tf2_geometry_msgs.h>

#include <limits>

namespace planning_scene_monitor
{
rclcpp::Logger logger = rclcpp::get_logger("planning_scene_monitor");

CurrentStateMonitor::CurrentStateMonitor(const rclcpp::Node::SharedPtr& node,
                                         const robot_model::RobotModelConstPtr& robot_model,
                                         const std::shared_ptr<tf2_ros::Buffer>& tf_buffer)
  : node_(node)
  , tf_buffer_(tf_buffer)
  , robot_model_(robot_model)
  , robot_state_(robot_model)
  , state_monitor_started_(false)
  , copy_dynamics_(true)
  , error_(std::numeric_limits<double>::epsilon())
{
  robot_state_.setToDefaultValues();
}

CurrentStateMonitor::~CurrentStateMonitor()
{
  stopStateMonitor();
}

robot_state::RobotStatePtr CurrentStateMonitor::getCurrentState() const
{
  std::unique_lock<std::mutex> slock(state_update_lock_);
  robot_state::RobotState* result = new robot_state::RobotState(robot_state_);
  return robot_state::RobotStatePtr(result);
}

rclcpp::Time CurrentStateMonitor::getCurrentStateTime() const
{
  std::unique_lock<std::mutex> slock(state_update_lock_);
  return current_state_time_;
}

std::pair<robot_state::RobotStatePtr, rclcpp::Time> CurrentStateMonitor::getCurrentStateAndTime() const
{
  std::unique_lock<std::mutex> slock(state_update_lock_);
  robot_state::RobotState* result = new robot_state::RobotState(robot_state_);
  return std::make_pair(robot_state::RobotStatePtr(result), current_state_time_);
}

std::map<std::string, double> CurrentStateMonitor::getCurrentStateValues() const
{
  std::map<std::string, double> m;
  std::unique_lock<std::mutex> slock(state_update_lock_);
  const double* pos = robot_state_.getVariablePositions();
  const std::vector<std::string>& names = robot_state_.getVariableNames();
  for (std::size_t i = 0; i < names.size(); ++i)
    m[names[i]] = pos[i];
  return m;
}

void CurrentStateMonitor::setToCurrentState(robot_state::RobotState& upd) const
{
  std::unique_lock<std::mutex> slock(state_update_lock_);
  const double* pos = robot_state_.getVariablePositions();
  upd.setVariablePositions(pos);
  if (copy_dynamics_)
  {
    if (robot_state_.hasVelocities())
    {
      const double* vel = robot_state_.getVariableVelocities();
      upd.setVariableVelocities(vel);
    }
    if (robot_state_.hasAccelerations())
    {
      const double* acc = robot_state_.getVariableAccelerations();
      upd.setVariableAccelerations(acc);
    }
    if (robot_state_.hasEffort())
    {
      const double* eff = robot_state_.getVariableEffort();
      upd.setVariableEffort(eff);
    }
  }
}

void CurrentStateMonitor::addUpdateCallback(const JointStateUpdateCallback& fn)
{
  if (fn)
    update_callbacks_.push_back(fn);
}

void CurrentStateMonitor::clearUpdateCallbacks()
{
  update_callbacks_.clear();
}

void CurrentStateMonitor::startStateMonitor(const std::string& joint_states_topic)
{
  rclcpp::Clock ros_clock;
  if (!state_monitor_started_ && robot_model_)
  {
    joint_time_.clear();
    if (joint_states_topic.empty())
    {
      RCLCPP_ERROR(node_->get_logger(), " The joint states topic cannot be an empty string");
    }
    else
      joint_state_subscriber_ = node_->create_subscription<sensor_msgs::msg::JointState>(
          joint_states_topic, std::bind(&CurrentStateMonitor::jointStateCallback, this, std::placeholders::_1),
          rmw_qos_profile_sensor_data);
    if (tf_buffer_ && !robot_model_->getMultiDOFJointModels().empty())
    {
      // TODO (anasarrak): replace this for the appropiate function, there is no similar
      // function in ros2/geometry2.
      // tf_connection_.reset(new TFConnection(
      //     tf_buffer_->_addTransformsChangedListener(std::bind(&CurrentStateMonitor::tfCallback, this))));
    }
    state_monitor_started_ = true;
    monitor_start_time_ = ros_clock.now();
    RCLCPP_INFO(node_->get_logger(), "Listening to joint states on topic '%s'", joint_states_topic.c_str());
  }
}

bool CurrentStateMonitor::isActive() const
{
  return state_monitor_started_;
}

void CurrentStateMonitor::stopStateMonitor()
{
  if (state_monitor_started_)
  {
    joint_state_subscriber_.reset();
    if (tf_buffer_ && tf_connection_)
    {
      // TODO (anasarrak): replace this for the appropiate function, there is no similar
      // function in ros2/geometry2.
      // tf_buffer_->_removeTransformsChangedListener(*tf_connection_);
      tf_connection_.reset();
    }
    RCLCPP_DEBUG(node_->get_logger(), "No longer listening for joint states");
    state_monitor_started_ = false;
  }
}

std::string CurrentStateMonitor::getMonitoredTopic(const std::string& joint_states_topic)
{
  std::string result;
  // joint_state_subscriber_ = node_->create_subscription<sensor_msgs::msg::JointState>(joint_states_topic,
  //                   std::bind(& CurrentStateMonitor::jointStateCallback, this,
  //                   std::placeholders::_1));
  if (joint_state_subscriber_)
    result = joint_state_subscriber_->get_topic_name();
  else
    result = std::string("");
  // joint_state_subscriber_.reset();
  return result;
}

bool CurrentStateMonitor::haveCompleteState() const
{
  bool result = true;
  const std::vector<const moveit::core::JointModel*>& joints = robot_model_->getActiveJointModels();
  std::unique_lock<std::mutex> slock(state_update_lock_);
  for (const moveit::core::JointModel* joint : joints)
    if (joint_time_.find(joint) == joint_time_.end())
    {
      if (!joint->isPassive() && !joint->getMimic())
      {
        RCLCPP_DEBUG(node_->get_logger(), "Joint '%s' has never been updated", joint->getName().c_str());
        result = false;
      }
    }
  return result;
}

bool CurrentStateMonitor::haveCompleteState(std::vector<std::string>& missing_states) const
{
  bool result = true;
  const std::vector<const moveit::core::JointModel*>& joints = robot_model_->getActiveJointModels();
  std::unique_lock<std::mutex> slock(state_update_lock_);
  for (const moveit::core::JointModel* joint : joints)
    if (joint_time_.find(joint) == joint_time_.end())
      if (!joint->isPassive() && !joint->getMimic())
      {
        missing_states.push_back(joint->getName());
        result = false;
      }
  return result;
}

bool CurrentStateMonitor::haveCompleteState(const rclcpp::Duration& age) const
{
  bool result = true;
  const std::vector<const moveit::core::JointModel*>& joints = robot_model_->getActiveJointModels();
  rclcpp::Clock ros_clock;
  rclcpp::Time now = ros_clock.now();
  rclcpp::Time old = now - age;
  std::unique_lock<std::mutex> slock(state_update_lock_);
  for (const moveit::core::JointModel* joint : joints)
  {
    if (joint->isPassive() || joint->getMimic())
      continue;
    std::map<const moveit::core::JointModel*, rclcpp::Time>::const_iterator it = joint_time_.find(joint);
    if (it == joint_time_.end())
    {
      RCLCPP_DEBUG(node_->get_logger(), "Joint '%s' has never been updated", joint->getName().c_str());
      result = false;
    }
    else if (it->second < old)
    {
      RCLCPP_DEBUG(node_->get_logger(),
                   "Joint '%s' was last updated %0.3lf seconds ago (older than the allowed %0.3lf seconds)",
                   joint->getName().c_str(), (now - it->second).seconds(), age.seconds());
      result = false;
    }
  }
  return result;
}

bool CurrentStateMonitor::haveCompleteState(const rclcpp::Duration& age, std::vector<std::string>& missing_states) const
{
  bool result = true;
  const std::vector<const moveit::core::JointModel*>& joints = robot_model_->getActiveJointModels();
  rclcpp::Clock ros_clock;
  rclcpp::Time now = ros_clock.now();
  rclcpp::Time old = now - age;
  std::unique_lock<std::mutex> slock(state_update_lock_);
  for (const moveit::core::JointModel* joint : joints)
  {
    if (joint->isPassive() || joint->getMimic())
      continue;
    std::map<const moveit::core::JointModel*, rclcpp::Time>::const_iterator it = joint_time_.find(joint);
    if (it == joint_time_.end())
    {
      RCLCPP_DEBUG(node_->get_logger(), "Joint '%s' has never been updated", joint->getName().c_str());
      missing_states.push_back(joint->getName());
      result = false;
    }
    else if (it->second < old)
    {
      RCLCPP_DEBUG(node_->get_logger(),
                   "Joint '%s' was last updated %0.3lf seconds ago (older than the allowed %0.3lf seconds)",
                   joint->getName().c_str(), (now - it->second).seconds(), age.seconds());
      missing_states.push_back(joint->getName());
      result = false;
    }
  }
  return result;
}

bool CurrentStateMonitor::waitForCurrentState(rclcpp::Time t, double wait_time) const
{
  rclcpp::Clock ros_clock;
  t = ros_clock.now();

  auto start = std::chrono::system_clock::now();

  auto durElapsed = std::chrono::duration<double>(0.0);
  auto durTimeout = std::chrono::duration<double>(wait_time);

  auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(durElapsed);
  auto timeout = std::chrono::duration_cast<std::chrono::seconds>(durTimeout);

  std::chrono::duration<double> dur;

  std::unique_lock<std::mutex> lock(state_update_lock_);
  // TODO (ahcorde)
  // while (current_state_time_ < t)
  // {
  //   state_update_condition_.wait_for(lock, std::chrono::nanoseconds(elapsed - timeout));
  //   dur = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - start);
  //   if (dur > timeout)
  //   {
  //     RCLCPP_INFO(node_->get_logger(), "Didn't received robot state (joint angles) with recent timestamp within "
  //                     "%f seconds.\n"
  //                     "Check clock synchronization if your are running ROS across multiple machines!", wait_time);
  //     return false;
  //   }
  //   rclcpp::spin_some(node_);
  // }
  return true;
}

bool CurrentStateMonitor::waitForCompleteState(double wait_time) const
{
  double slept_time = 0.0;
  double sleep_step_s = std::min(0.05, wait_time / 10.0);
  rclcpp::Rate sleep_step(sleep_step_s);
  while (!haveCompleteState() && slept_time < wait_time)
  {
    sleep_step.sleep();
    slept_time += sleep_step_s;
  }
  return haveCompleteState();
}

bool CurrentStateMonitor::waitForCompleteState(const std::string& group, double wait_time) const
{
  if (waitForCompleteState(wait_time))
    return true;
  bool ok = true;

  // check to see if we have a fully known state for the joints we want to record
  std::vector<std::string> missing_joints;
  if (!haveCompleteState(missing_joints))
  {
    const moveit::core::JointModelGroup* jmg = robot_model_->getJointModelGroup(group);
    if (jmg)
    {
      std::set<std::string> mj;
      mj.insert(missing_joints.begin(), missing_joints.end());
      const std::vector<std::string>& names = jmg->getJointModelNames();
      bool ok = true;
      for (std::size_t i = 0; ok && i < names.size(); ++i)
        if (mj.find(names[i]) != mj.end())
          ok = false;
    }
    else
      ok = false;
  }
  return ok;
}

void CurrentStateMonitor::jointStateCallback(const sensor_msgs::msg::JointState::ConstSharedPtr joint_state)
{
  if (joint_state->name.size() != joint_state->position.size())
  {
    RCUTILS_LOG_ERROR_THROTTLE(
        RCUTILS_STEADY_TIME, 1,
        "State monitor received invalid joint state (number of joint names does not match number of "
        "positions)");
    return;
  }
  bool update = false;

  // std::unique_lock<std::mutex> _(state_update_lock_);
  // read the received values, and update their time stamps
  std::size_t n = joint_state->name.size();
  current_state_time_ = joint_state->header.stamp;
  for (std::size_t i = 0; i < n; ++i)
  {
    const moveit::core::JointModel* jm = robot_model_->getJointModel(joint_state->name[i]);
    if (!jm)
      continue;
    // ignore fixed joints, multi-dof joints (they should not even be in the message)
    if (jm->getVariableCount() != 1)
      continue;

    joint_time_[jm] = joint_state->header.stamp;

    if (robot_state_.getJointPositions(jm)[0] != joint_state->position[i])
    {
      update = true;
      robot_state_.setJointPositions(jm, &(joint_state->position[i]));

      // optionally copy velocities and effort
      if (copy_dynamics_)
      {
        // check if velocities exist
        if (joint_state->name.size() == joint_state->velocity.size())
        {
          robot_state_.setJointVelocities(jm, &(joint_state->velocity[i]));

          // check if effort exist. assume they are not useful if no velocities were passed in
          if (joint_state->name.size() == joint_state->effort.size())
          {
            robot_state_.setJointEfforts(jm, &(joint_state->effort[i]));
          }
        }
      }

      // continuous joints wrap, so we don't modify them (even if they are outside bounds!)
      if (jm->getType() == moveit::core::JointModel::REVOLUTE)
        if (static_cast<const moveit::core::RevoluteJointModel*>(jm)->isContinuous())
          continue;

      const robot_model::VariableBounds& b =
          jm->getVariableBounds()[0];  // only one variable in the joint, so we get its bounds

      // if the read variable is 'almost' within bounds (up to error_ difference), then consider it to be within
      // bounds
      if (joint_state->position[i] < b.min_position_ && joint_state->position[i] >= b.min_position_ - error_)
        robot_state_.setJointPositions(jm, &b.min_position_);
      else if (joint_state->position[i] > b.max_position_ && joint_state->position[i] <= b.max_position_ + error_)
        robot_state_.setJointPositions(jm, &b.max_position_);
    }
  }

  // callbacks, if needed
  if (update)
    for (JointStateUpdateCallback& update_callback : update_callbacks_)
      update_callback(joint_state);

  // notify waitForCurrentState() *after* potential update callbacks
  state_update_condition_.notify_all();
}

void CurrentStateMonitor::tfCallback()
{
  // read multi-dof joint states from TF, if needed
  const std::vector<const moveit::core::JointModel*>& multi_dof_joints = robot_model_->getMultiDOFJointModels();

  bool update = false;
  bool changes = false;
  {
    std::unique_lock<std::mutex> _(state_update_lock_);
    rclcpp::Clock clock;
    rclcpp::Time rclcpp_time = clock.now();
    tf2::TimePoint tf2_time(std::chrono::nanoseconds(rclcpp_time.nanoseconds()));
    for (size_t i = 0; i < multi_dof_joints.size(); i++)
    {
      const std::string& child_frame = joint->getChildLinkModel()->getName();
      const std::string& parent_frame =
          joint->getParentLinkModel() ? joint->getParentLinkModel()->getName() : robot_model_->getModelFrame();

      rclcpp::Time latest_common_time;
      geometry_msgs::msg::TransformStamped transf;
      try
      {
        transf = tf_buffer_->lookupTransform(parent_frame, child_frame, tf2_time);
        latest_common_time = transf.header.stamp;
      }
      catch (tf2::TransformException& ex)
      {
        RCLCPP_WARN_ONCE(node_->get_logger(), "Unable to update multi-DOF joint '%s':"
                                              "Failure to lookup transform between '%s'"
                                              "and '%s' with TF exception: ",
                         joint->getName().c_str(), parent_frame.c_str(), child_frame.c_str(), ex.what());
        continue;
      }

      // allow update if time is more recent or if it is a static transform (time = 0)
      if (latest_common_time <= joint_time_[joint] && latest_common_time > rclcpp::Time(0))
        continue;
      joint_time_[joint] = latest_common_time;

      std::vector<double> new_values(joint->getStateSpaceDimension());
      const robot_model::LinkModel* link = joint->getChildLinkModel();
      if (link->jointOriginTransformIsIdentity())
        joint->computeVariablePositions(tf2::transformToEigen(transf), new_values.data());
      else
        joint->computeVariablePositions(link->getJointOriginTransform().inverse() * tf2::transformToEigen(transf),
                                        new_values.data());

      if (joint->distance(new_values.data(), robot_state_.getJointPositions(joint)) > 1e-5)
      {
        changes = true;
      }

      robot_state_.setJointPositions(joint, new_values.data());
      update = true;
    }
  }

  // callbacks, if needed
  if (changes)
  {
    // stub joint state: multi-dof joints are not modelled in the message,
    // but we should still trigger the update callbacks
    sensor_msgs::msg::JointState::Ptr joint_state(new sensor_msgs::msg::JointState);
    for (std::size_t i = 0; i < update_callbacks_.size(); ++i)
      update_callbacks_[i](joint_state);
  }

  if (update)
  {
    // notify waitForCurrentState() *after* potential update callbacks
    state_update_condition_.notify_all();
  }
}
}  // namespace planning_scene_monitor
