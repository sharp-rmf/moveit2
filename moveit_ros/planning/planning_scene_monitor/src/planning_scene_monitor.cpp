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

#include <moveit/planning_scene_monitor/planning_scene_monitor.h>
#include <moveit/robot_model_loader/robot_model_loader.h>
#include <moveit/utils/message_checks.h>
#include <moveit/exceptions/exceptions.h>
#include <moveit_msgs/srv/get_planning_scene.hpp>
// TODO(anasarrak): Generate a configuration for ROS2.
// #include <moveit_ros_planning/PlanningSceneMonitorDynamicReconfigureConfig.h>
#include <tf2/exceptions.h>
#include <tf2/LinearMath/Transform.h>
#include <tf2_eigen/tf2_eigen.h>
#include <tf2_geometry_msgs/tf2_geometry_msgs.h>
#include <moveit/profiler/profiler.h>
#include <rcutils/logging_macros.h>

#include <boost/algorithm/string/join.hpp>
#include <memory>

rclcpp::Logger LOGGER_PLANNING_SCENE_MONITOR = rclcpp::get_logger("planning_scene_monitor");

namespace planning_scene_monitor
{
// This namespace is used for the dynamic_reconfigure
// using namespace moveit_ros_planning;

static const std::string LOGNAME = "planning_scene_monitor";

class PlanningSceneMonitor::DynamicReconfigureImpl
{
public:
  DynamicReconfigureImpl(PlanningSceneMonitor* owner)
    : owner_(owner) /*, dynamic_reconfigure_server_(ros::NodeHandle(decideNamespace(owner->getName())))*/
  {
    // TODO (anasarrak): re-add when starting with the parameters for ros2
    // dynamic_reconfigure_server_.setCallback(
    //     boost::bind(&DynamicReconfigureImpl::dynamicReconfigureCallback, this, _1, _2));
  }

private:
  // make sure we do not advertise the same service multiple times, in case we use multiple PlanningSceneMonitor
  // instances in a process
  // TODO (anasarrak): Update advertise for ros2
  // static std::string decideNamespace(const std::string& name)
  // {
  //   std::string ns = "~/" + name;
  //   std::replace(ns.begin(), ns.end(), ' ', '_');
  //   std::transform(ns.begin(), ns.end(), ns.begin(), ::tolower);
  //   if (ros::service::exists(ns + "/set_parameters", false))
  //   {
  //     unsigned int c = 1;
  //     while (ros::service::exists(ns + boost::lexical_cast<std::string>(c) + "/set_parameters", false))
  //       c++;
  //     ns += boost::lexical_cast<std::string>(c);
  //   }
  //   return ns;
  // }
  // TODO(anasarrak): uncomment this once the config for ROS2 is generated
  // void dynamicReconfigureCallback(PlanningSceneMonitorDynamicReconfigureConfig& config, uint32_t level)
  // {
  //   PlanningSceneMonitor::SceneUpdateType event = PlanningSceneMonitor::UPDATE_NONE;
  //   if (config.publish_geometry_updates)
  //     event = (PlanningSceneMonitor::SceneUpdateType)((int)event | (int)PlanningSceneMonitor::UPDATE_GEOMETRY);
  //   if (config.publish_state_updates)
  //     event = (PlanningSceneMonitor::SceneUpdateType)((int)event | (int)PlanningSceneMonitor::UPDATE_STATE);
  //   if (config.publish_transforms_updates)
  //     event = (PlanningSceneMonitor::SceneUpdateType)((int)event | (int)PlanningSceneMonitor::UPDATE_TRANSFORMS);
  //   if (config.publish_planning_scene)
  //   {
  //     owner_->setPlanningScenePublishingFrequency(config.publish_planning_scene_hz);
  //     owner_->startPublishingPlanningScene(event);
  //   }
  //   else
  //     owner_->stopPublishingPlanningScene();
  // }

  PlanningSceneMonitor* owner_;
  // dynamic_reconfigure::Server<PlanningSceneMonitorDynamicReconfigureConfig> dynamic_reconfigure_server_;
};

const std::string PlanningSceneMonitor::DEFAULT_JOINT_STATES_TOPIC = "joint_states";
const std::string PlanningSceneMonitor::DEFAULT_ATTACHED_COLLISION_OBJECT_TOPIC = "attached_collision_object";
const std::string PlanningSceneMonitor::DEFAULT_COLLISION_OBJECT_TOPIC = "collision_object";
const std::string PlanningSceneMonitor::DEFAULT_PLANNING_SCENE_WORLD_TOPIC = "planning_scene_world";
const std::string PlanningSceneMonitor::DEFAULT_PLANNING_SCENE_TOPIC = "planning_scene";
const std::string PlanningSceneMonitor::DEFAULT_PLANNING_SCENE_SERVICE = "get_planning_scene";
const std::string PlanningSceneMonitor::MONITORED_PLANNING_SCENE_TOPIC = "monitored_planning_scene";

PlanningSceneMonitor::PlanningSceneMonitor(const std::string& robot_description,
                                           std::shared_ptr<rclcpp::Node>& node,
                                           const std::shared_ptr<tf2_ros::Buffer>& tf_buffer, const std::string& name)
  : PlanningSceneMonitor(planning_scene::PlanningScenePtr(), robot_description, node, tf_buffer, name)
{
}

PlanningSceneMonitor::PlanningSceneMonitor(const planning_scene::PlanningScenePtr& scene,
                                           const std::string& robot_description,
                                           std::shared_ptr<rclcpp::Node>& node,
                                           const std::shared_ptr<tf2_ros::Buffer>& tf_buffer, const std::string& name)
  : PlanningSceneMonitor(scene, std::make_shared<robot_model_loader::RobotModelLoader>(robot_description, node), node, tf_buffer,
                         name)
{
}

PlanningSceneMonitor::PlanningSceneMonitor(const robot_model_loader::RobotModelLoaderPtr& rm_loader,
                                           std::shared_ptr<rclcpp::Node>& node,
                                           const std::shared_ptr<tf2_ros::Buffer>& tf_buffer, const std::string& name)
  : PlanningSceneMonitor(planning_scene::PlanningScenePtr(), rm_loader, node, tf_buffer, name)
{
}

PlanningSceneMonitor::PlanningSceneMonitor(const planning_scene::PlanningScenePtr& scene,
                                           const robot_model_loader::RobotModelLoaderPtr& rm_loader,
                                           std::shared_ptr<rclcpp::Node>& node,
                                           const std::shared_ptr<tf2_ros::Buffer>& tf_buffer,
                                           const std::string& name)
  : monitor_name_(name), node_(node), tf_buffer_(tf_buffer), rm_loader_(rm_loader), shape_transform_cache_lookup_wait_time_(rclcpp::Duration(0,0))
{
  // use same callback queue as root_nh_
  // nh_.setCallbackQueue(root_nh_.getCallbackQueue());
  initialize(scene);
}

PlanningSceneMonitor::~PlanningSceneMonitor()
{
  if (scene_)
  {
    scene_->setCollisionObjectUpdateCallback(collision_detection::World::ObserverCallbackFn());
    scene_->setAttachedBodyUpdateCallback(robot_state::AttachedBodyCallback());
  }
  stopPublishingPlanningScene();
  stopStateMonitor();
  stopWorldGeometryMonitor();
  stopSceneMonitor();

  spinner_.reset();
  delete reconfigure_impl_;
  current_state_monitor_.reset();
  scene_const_.reset();
  scene_.reset();
  parent_scene_.reset();
  robot_model_.reset();
  rm_loader_.reset();
}

void PlanningSceneMonitor::initialize(const planning_scene::PlanningScenePtr& scene)
{
  moveit::tools::Profiler::ScopedStart prof_start;
  moveit::tools::Profiler::ScopedBlock prof_block("PlanningSceneMonitor::initialize");
  if (monitor_name_.empty())
    monitor_name_ = "planning_scene_monitor";
  robot_description_ = rm_loader_->getRobotDescription();
  if (rm_loader_->getModel())
  {
    robot_model_ = rm_loader_->getModel();
    scene_ = scene;
    collision_loader_.setupScene(node_, scene_);
    scene_const_ = scene_;
    if (!scene_)
    {
      try
      {
        scene_.reset(new planning_scene::PlanningScene(rm_loader_->getModel()));
        collision_loader_.setupScene(node_, scene_);
        scene_const_ = scene_;
        configureCollisionMatrix(scene_);
        configureDefaultPadding();

        scene_->getCollisionEnvNonConst()->setPadding(default_robot_padd_);
        scene_->getCollisionEnvNonConst()->setScale(default_robot_scale_);
        for (const std::pair<const std::string, double>& it : default_robot_link_padd_)
        {
          scene_->getCollisionEnvNonConst()->setLinkPadding(it.first, it.second);
        }
        for (const std::pair<const std::string, double>& it : default_robot_link_scale_)
        {
          scene_->getCollisionEnvNonConst()->setLinkScale(it.first, it.second);
        }
        scene_->propogateRobotPadding();
      }
      catch (moveit::ConstructException& e)
      {
        RCLCPP_ERROR(node_->get_logger(), "Configuration of planning scene failed");
        scene_.reset();
        scene_const_ = scene_;
      }
    }
    if (scene_)
    {
      scene_->setAttachedBodyUpdateCallback(
          boost::bind(&PlanningSceneMonitor::currentStateAttachedBodyUpdateCallback, this, _1, _2));
      scene_->setCollisionObjectUpdateCallback(
          boost::bind(&PlanningSceneMonitor::currentWorldObjectUpdateCallback, this, _1, _2));
    }
  }
  else
  {
    RCLCPP_ERROR(node_->get_logger(), "Robot model not loaded");
  }

  publish_planning_scene_frequency_ = 2.0;
  new_scene_update_ = UPDATE_NONE;

  last_update_time_ = last_robot_motion_time_ = clock_.now();
  last_robot_state_update_wall_time_ = std::chrono::system_clock::now();

  // auto dur = std::chrono::duration<double>(0.1);
  // auto timeout = std::chrono::duration_cast<std::chrono::seconds>(dur).count();
  // int sec = (int32_t) floor(timeout);
  // int nSec = (int32_t)((timeout - (double)timeout)*1000000000);
  // rclcpp::Time t(sec,nSec);
  dt_state_update_ = std::chrono::duration<double>(0.1);

  double temp_wait_time = 0.05;

  auto parameters_robot_description = std::make_shared<rclcpp::SyncParametersClient>(node_);

  std::string robot_des = robot_description_ + "_planning/shape_transform_cache_lookup_wait_time";
  if (parameters_robot_description->has_parameter({ robot_des }))
  {
    temp_wait_time = node_->get_parameter(robot_des).get_value<double>();
  }

  int seconds = (int)temp_wait_time;
  shape_transform_cache_lookup_wait_time_ =
      rclcpp::Duration((int32_t)seconds, (int32_t)(temp_wait_time - seconds) * 1.0e+9);

  state_update_pending_ = false;
  // Period for 0.1 sec
  auto period = std::chrono::milliseconds(100);
  state_update_timer_ =
      node_->create_wall_timer(period, std::bind(&PlanningSceneMonitor::stateUpdateTimerCallback, this));

  reconfigure_impl_ = new DynamicReconfigureImpl(this);
}

void PlanningSceneMonitor::monitorDiffs(bool flag)
{
  if (scene_)
  {
    if (flag)
    {
      boost::unique_lock<boost::shared_mutex> ulock(scene_update_mutex_);
      if (scene_)
      {
        scene_->setAttachedBodyUpdateCallback(robot_state::AttachedBodyCallback());
        scene_->setCollisionObjectUpdateCallback(collision_detection::World::ObserverCallbackFn());
        scene_->decoupleParent();
        parent_scene_ = scene_;
        scene_ = parent_scene_->diff();
        scene_const_ = scene_;
        scene_->setAttachedBodyUpdateCallback(
            boost::bind(&PlanningSceneMonitor::currentStateAttachedBodyUpdateCallback, this, _1, _2));
        scene_->setCollisionObjectUpdateCallback(
            boost::bind(&PlanningSceneMonitor::currentWorldObjectUpdateCallback, this, _1, _2));
      }
    }
    else
    {
      if (publish_planning_scene_)
      {
        RCLCPP_WARN(node_->get_logger(), "Diff monitoring was stopped while publishing planning scene diffs. "
                                         "Stopping planning scene diff publisher");
        stopPublishingPlanningScene();
      }
      {
        boost::unique_lock<boost::shared_mutex> ulock(scene_update_mutex_);
        if (scene_)
        {
          scene_->decoupleParent();
          parent_scene_.reset();
          // remove the '+' added by .diff() at the end of the scene name
          if (!scene_->getName().empty())
          {
            if (scene_->getName()[scene_->getName().length() - 1] == '+')
              scene_->setName(scene_->getName().substr(0, scene_->getName().length() - 1));
          }
        }
      }
    }
  }
}

void PlanningSceneMonitor::stopPublishingPlanningScene()
{
  if (publish_planning_scene_)
  {
    std::unique_ptr<boost::thread> copy;
    copy.swap(publish_planning_scene_);
    new_scene_update_condition_.notify_all();
    copy->join();
    monitorDiffs(false);
    planning_scene_publisher_.reset();
    RCLCPP_INFO(node_->get_logger(), "Stopped publishing maintained planning scene.");
  }
}

void PlanningSceneMonitor::startPublishingPlanningScene(SceneUpdateType update_type,
                                                        const std::string& planning_scene_topic)
{
  publish_update_types_ = update_type;
  if (!publish_planning_scene_ && scene_)
  {
    planning_scene_publisher_ =
        node_->create_publisher<moveit_msgs::msg::PlanningScene>(planning_scene_topic, rmw_qos_profile_default);
    RCLCPP_INFO(node_->get_logger(), "Publishing maintained planning scene on '%s'", planning_scene_topic.c_str());
    monitorDiffs(true);
    publish_planning_scene_.reset(new boost::thread(boost::bind(&PlanningSceneMonitor::scenePublishingThread, this)));
  }
}

void PlanningSceneMonitor::scenePublishingThread()
{
  RCLCPP_DEBUG(node_->get_logger(), "Started scene publishing thread ...");

  // publish the full planning scene once
  {
    moveit_msgs::msg::PlanningScene msg;
    {
      occupancy_map_monitor::OccMapTree::ReadLock lock;
      if (octomap_monitor_)
        lock = octomap_monitor_->getOcTreePtr()->reading();
      scene_->getPlanningSceneMsg(msg);
    }
    planning_scene_publisher_->publish(msg);
    RCLCPP_DEBUG(node_->get_logger(), "Published the full planning scene: '%s'", msg.name.c_str());
  }

  do
  {
    moveit_msgs::msg::PlanningScene msg;
    bool publish_msg = false;
    bool is_full = false;
    rclcpp::Rate rate(publish_planning_scene_frequency_);
    {
      boost::unique_lock<boost::shared_mutex> ulock(scene_update_mutex_);
      while (new_scene_update_ == UPDATE_NONE && publish_planning_scene_)
        new_scene_update_condition_.wait(ulock);
      if (new_scene_update_ != UPDATE_NONE)
      {
        if ((publish_update_types_ & new_scene_update_) || new_scene_update_ == UPDATE_SCENE)
        {
          if (new_scene_update_ == UPDATE_SCENE)
            is_full = true;
          else
          {
            occupancy_map_monitor::OccMapTree::ReadLock lock;
            if (octomap_monitor_)
              lock = octomap_monitor_->getOcTreePtr()->reading();
            scene_->getPlanningSceneDiffMsg(msg);
          }
          boost::recursive_mutex::scoped_lock prevent_shape_cache_updates(shape_handles_lock_);  // we don't want the
                                                                                                 // transform cache to
                                                                                                 // update while we are
                                                                                                 // potentially changing
                                                                                                 // attached bodies
          scene_->setAttachedBodyUpdateCallback(robot_state::AttachedBodyCallback());
          scene_->setCollisionObjectUpdateCallback(collision_detection::World::ObserverCallbackFn());
          scene_->pushDiffs(parent_scene_);
          scene_->clearDiffs();
          scene_->setAttachedBodyUpdateCallback(
              boost::bind(&PlanningSceneMonitor::currentStateAttachedBodyUpdateCallback, this, _1, _2));
          scene_->setCollisionObjectUpdateCallback(
              boost::bind(&PlanningSceneMonitor::currentWorldObjectUpdateCallback, this, _1, _2));
          if (octomap_monitor_)
          {
            excludeAttachedBodiesFromOctree();  // in case updates have happened to the attached bodies, put them in
            excludeWorldObjectsFromOctree();    // in case updates have happened to the attached bodies, put them in
          }
          if (is_full)
          {
            occupancy_map_monitor::OccMapTree::ReadLock lock;
            if (octomap_monitor_)
              lock = octomap_monitor_->getOcTreePtr()->reading();
            scene_->getPlanningSceneMsg(msg);
          }
          // also publish timestamp of this robot_state
          msg.robot_state.joint_state.header.stamp = last_robot_motion_time_;
          publish_msg = true;
        }
        new_scene_update_ = UPDATE_NONE;
      }
    }
    if (publish_msg)
    {
      rate.reset();
      planning_scene_publisher_->publish(msg);
      if (is_full)
        RCLCPP_DEBUG(node_->get_logger(), "Published full planning scene: '%s'", msg.name.c_str());
      rate.sleep();
    }
  } while (publish_planning_scene_);
}

void PlanningSceneMonitor::getMonitoredTopics(std::vector<std::string>& topics) const
{
  // TODO(anasarrak): Do we need this for ROS2?
  topics.clear();
  if (current_state_monitor_)
  {
    const std::string& t = current_state_monitor_->getMonitoredTopic();
    if (!t.empty())
      topics.push_back(t);
  }
  if (planning_scene_subscriber_)
    topics.push_back(planning_scene_subscriber_->get_topic_name());
  if (collision_object_subscriber_)
    // TODO (anasarrak) This has been changed to subscriber on Moveit, look at
    // https://github.com/ros-planning/moveit/pull/1406/files/cb9488312c00e9c8949d89b363766f092330954d#diff-fb6e26ecc9a73d59dbdae3f3e08145e6
    topics.push_back(collision_object_subscriber_->getTopic());
  if (planning_scene_world_subscriber_)
    topics.push_back(planning_scene_world_subscriber_->get_topic_name());
}

namespace
{
bool sceneIsParentOf(const planning_scene::PlanningSceneConstPtr& scene,
                     const planning_scene::PlanningScene* possible_parent)
{
  if (scene && scene.get() == possible_parent)
    return true;
  if (scene)
    return sceneIsParentOf(scene->getParent(), possible_parent);
  return false;
}
}

bool PlanningSceneMonitor::updatesScene(const planning_scene::PlanningScenePtr& scene) const
{
  return sceneIsParentOf(scene_const_, scene.get());
}

bool PlanningSceneMonitor::updatesScene(const planning_scene::PlanningSceneConstPtr& scene) const
{
  return sceneIsParentOf(scene_const_, scene.get());
}

void PlanningSceneMonitor::triggerSceneUpdateEvent(SceneUpdateType update_type)
{
  // do not modify update functions while we are calling them
  boost::recursive_mutex::scoped_lock lock(update_lock_);

  for (boost::function<void(SceneUpdateType)>& update_callback : update_callbacks_)
    update_callback(update_type);
  new_scene_update_ = (SceneUpdateType)((int)new_scene_update_ | (int)update_type);
  new_scene_update_condition_.notify_all();
}

bool PlanningSceneMonitor::requestPlanningSceneState(const std::string& service_name)
{
  // use global namespace for service
  auto client = node_->create_client<moveit_msgs::srv::GetPlanningScene>(service_name);
  auto srv = std::make_shared<moveit_msgs::srv::GetPlanningScene::Request>();
  srv->components.components = srv->components.SCENE_SETTINGS | srv->components.ROBOT_STATE |
                               srv->components.ROBOT_STATE_ATTACHED_OBJECTS | srv->components.WORLD_OBJECT_NAMES |
                               srv->components.WORLD_OBJECT_GEOMETRY | srv->components.OCTOMAP |
                               srv->components.TRANSFORMS | srv->components.ALLOWED_COLLISION_MATRIX |
                               srv->components.LINK_PADDING_AND_SCALING | srv->components.OBJECT_COLORS;

  // Make sure client is connected to server
  while (!client->wait_for_service(std::chrono::seconds(5)))
  {
    RCLCPP_DEBUG(node_->get_logger(), "Waiting for service `%s` to exist.", service_name.c_str());
  }

  auto result = client->async_send_request(srv);

  if (rclcpp::spin_until_future_complete(node_, result) != rclcpp::executor::FutureReturnCode::SUCCESS)
  {
    RCLCPP_INFO(node_->get_logger(), "Failed to call service %s, have you launched move_group? at %s:%d",
                service_name.c_str(), __FILE__, __LINE__);
    return false;
  }
  else
  {
    newPlanningSceneMessage(result.get()->scene);
  }
  return true;
}

void PlanningSceneMonitor::newPlanningSceneCallback(const moveit_msgs::msg::PlanningScene::SharedPtr scene)
{
  newPlanningSceneMessage(*scene);
}

void PlanningSceneMonitor::clearOctomap()
{
  octomap_monitor_->getOcTreePtr()->lockWrite();
  octomap_monitor_->getOcTreePtr()->clear();
  octomap_monitor_->getOcTreePtr()->unlockWrite();
}

bool PlanningSceneMonitor::newPlanningSceneMessage(const moveit_msgs::msg::PlanningScene& scene)
{
  if (!scene_)
    return false;

  bool result;

  SceneUpdateType upd = UPDATE_SCENE;
  std::string old_scene_name;
  {
    boost::unique_lock<boost::shared_mutex> ulock(scene_update_mutex_);
    // we don't want the transform cache to update while we are potentially changing attached bodies
    boost::recursive_mutex::scoped_lock prevent_shape_cache_updates(shape_handles_lock_);

    last_update_time_ = clock_.now();
    last_robot_motion_time_ = scene.robot_state.joint_state.header.stamp;
    RCLCPP_DEBUG(node_->get_logger(), "scene update %f robot stamp: %f", fmod(last_update_time_.seconds(), 10.),
                 fmod(last_robot_motion_time_.seconds(), 10.));
    old_scene_name = scene_->getName();
    result = scene_->usePlanningSceneMsg(scene);
    if (octomap_monitor_)
    {
      if (!scene.is_diff && scene.world.octomap.octomap.data.empty())
      {
        octomap_monitor_->getOcTreePtr()->lockWrite();
        octomap_monitor_->getOcTreePtr()->clear();
        octomap_monitor_->getOcTreePtr()->unlockWrite();
      }
    }
    robot_model_ = scene_->getRobotModel();

    // if we just reset the scene completely but we were maintaining diffs, we need to fix that
    if (!scene.is_diff && parent_scene_)
    {
      // the scene is now decoupled from the parent, since we just reset it
      scene_->setAttachedBodyUpdateCallback(robot_state::AttachedBodyCallback());
      scene_->setCollisionObjectUpdateCallback(collision_detection::World::ObserverCallbackFn());
      parent_scene_ = scene_;
      scene_ = parent_scene_->diff();
      scene_const_ = scene_;
      scene_->setAttachedBodyUpdateCallback(
          boost::bind(&PlanningSceneMonitor::currentStateAttachedBodyUpdateCallback, this, _1, _2));
      scene_->setCollisionObjectUpdateCallback(
          boost::bind(&PlanningSceneMonitor::currentWorldObjectUpdateCallback, this, _1, _2));
    }
    if (octomap_monitor_)
    {
      excludeAttachedBodiesFromOctree();  // in case updates have happened to the attached bodies, put them in
      excludeWorldObjectsFromOctree();    // in case updates have happened to the attached bodies, put them in
    }
  }

  // if we have a diff, try to more accuratelly determine the update type
  if (scene.is_diff)
  {
    bool no_other_scene_upd = (scene.name.empty() || scene.name == old_scene_name) &&
                              scene.allowed_collision_matrix.entry_names.empty() && scene.link_padding.empty() &&
                              scene.link_scale.empty();
    if (no_other_scene_upd)
    {
      upd = UPDATE_NONE;
      if (!moveit::core::isEmpty(scene.world))
        upd = (SceneUpdateType)((int)upd | (int)UPDATE_GEOMETRY);

      if (!scene.fixed_frame_transforms.empty())
        upd = (SceneUpdateType)((int)upd | (int)UPDATE_TRANSFORMS);

      if (!moveit::core::isEmpty(scene.robot_state))
      {
        upd = (SceneUpdateType)((int)upd | (int)UPDATE_STATE);
        if (!scene.robot_state.attached_collision_objects.empty() || scene.robot_state.is_diff == false)
          upd = (SceneUpdateType)((int)upd | (int)UPDATE_GEOMETRY);
      }
    }
  }
  triggerSceneUpdateEvent(upd);
  return result;
}

void PlanningSceneMonitor::newPlanningSceneWorldCallback(const moveit_msgs::msg::PlanningSceneWorld::SharedPtr world)
{
  if (scene_)
  {
    updateFrameTransforms();
    {
      boost::unique_lock<boost::shared_mutex> ulock(scene_update_mutex_);
      last_update_time_ = clock_.now();
      scene_->getWorldNonConst()->clearObjects();
      scene_->processPlanningSceneWorldMsg(*world);
      if (octomap_monitor_)
      {
        if (world->octomap.octomap.data.empty())
        {
          octomap_monitor_->getOcTreePtr()->lockWrite();
          octomap_monitor_->getOcTreePtr()->clear();
          octomap_monitor_->getOcTreePtr()->unlockWrite();
        }
      }
    }
    triggerSceneUpdateEvent(UPDATE_SCENE);
  }
}

void PlanningSceneMonitor::collisionObjectFailTFCallback(const moveit_msgs::msg::CollisionObject::SharedPtr& obj,
                                                         tf2_ros::filter_failure_reasons::FilterFailureReason reason)
{
  // if we just want to remove objects, the frame does not matter
  if (reason == tf2_ros::filter_failure_reasons::EmptyFrameID &&
      obj->operation == moveit_msgs::msg::CollisionObject::REMOVE)
    collisionObjectCallback(obj);
}

void PlanningSceneMonitor::collisionObjectCallback(const moveit_msgs::msg::CollisionObject::SharedPtr& obj)
{
  if (!scene_)
    return;

  updateFrameTransforms();
  {
    boost::unique_lock<boost::shared_mutex> ulock(scene_update_mutex_);
    last_update_time_ = clock_.now();
    scene_->processCollisionObjectMsg(*obj);
  }
  triggerSceneUpdateEvent(UPDATE_GEOMETRY);
}

void PlanningSceneMonitor::attachObjectCallback(const moveit_msgs::msg::AttachedCollisionObject::SharedPtr obj)
{
  if (scene_)
  {
    updateFrameTransforms();
    {
      boost::unique_lock<boost::shared_mutex> ulock(scene_update_mutex_);
      last_update_time_ = clock_.now();
      scene_->processAttachedCollisionObjectMsg(*obj);
    }
    triggerSceneUpdateEvent(UPDATE_GEOMETRY);
  }
}

void PlanningSceneMonitor::excludeRobotLinksFromOctree()
{
  if (!octomap_monitor_)
    return;

  boost::recursive_mutex::scoped_lock _(shape_handles_lock_);

  includeRobotLinksInOctree();
  const std::vector<const robot_model::LinkModel*>& links = getRobotModel()->getLinkModelsWithCollisionGeometry();
  auto start = std::chrono::system_clock::now();
  bool warned = false;
  for (const moveit::core::LinkModel* link : links)
  {
    std::vector<shapes::ShapeConstPtr> shapes = link->getShapes();  // copy shared ptrs on purpuse
    for (std::size_t j = 0; j < shapes.size(); ++j)
    {
      // merge mesh vertices up to 0.1 mm apart
      if (shapes[j]->type == shapes::MESH)
      {
        shapes::Mesh* m = static_cast<shapes::Mesh*>(shapes[j]->clone());
        m->mergeVertices(1e-4);
        shapes[j].reset(m);
      }

      occupancy_map_monitor::ShapeHandle h = octomap_monitor_->excludeShape(shapes[j]);
      if (h)
        link_shape_handles_[link].push_back(std::make_pair(h, j));
    }

    if (!warned && ((std::chrono::system_clock::now() - start) > std::chrono::seconds(30)))
    {
      RCLCPP_WARN(node_->get_logger(), "It is likely there are too many vertices in collision geometry");
      warned = true;
    }
  }
}

void PlanningSceneMonitor::includeRobotLinksInOctree()
{
  if (!octomap_monitor_)
    return;

  boost::recursive_mutex::scoped_lock _(shape_handles_lock_);

  for (std::pair<const robot_model::LinkModel* const,
                 std::vector<std::pair<occupancy_map_monitor::ShapeHandle, std::size_t>>>& link_shape_handle :
       link_shape_handles_)
    for (std::pair<occupancy_map_monitor::ShapeHandle, std::size_t>& it : link_shape_handle.second)
      octomap_monitor_->forgetShape(it.first);
  link_shape_handles_.clear();
}

void PlanningSceneMonitor::includeAttachedBodiesInOctree()
{
  if (!octomap_monitor_)
    return;

  boost::recursive_mutex::scoped_lock _(shape_handles_lock_);

  // clear information about any attached body, without refering to the AttachedBody* ptr (could be invalid)
  for (std::pair<const robot_state::AttachedBody* const,
                 std::vector<std::pair<occupancy_map_monitor::ShapeHandle, std::size_t>>>& attached_body_shape_handle :
       attached_body_shape_handles_)
    for (std::pair<occupancy_map_monitor::ShapeHandle, std::size_t>& it : attached_body_shape_handle.second)
      octomap_monitor_->forgetShape(it.first);
  attached_body_shape_handles_.clear();
}

void PlanningSceneMonitor::excludeAttachedBodiesFromOctree()
{
  boost::recursive_mutex::scoped_lock _(shape_handles_lock_);

  includeAttachedBodiesInOctree();
  // add attached objects again
  std::vector<const robot_state::AttachedBody*> ab;
  scene_->getCurrentState().getAttachedBodies(ab);
  for (const moveit::core::AttachedBody* body : ab)
    excludeAttachedBodyFromOctree(body);
}

void PlanningSceneMonitor::includeWorldObjectsInOctree()
{
  if (!octomap_monitor_)
    return;

  boost::recursive_mutex::scoped_lock _(shape_handles_lock_);

  // clear information about any attached object
  for (std::pair<const std::string,
                 std::vector<std::pair<occupancy_map_monitor::ShapeHandle, const Eigen::Isometry3d*>>>&
           collision_body_shape_handle : collision_body_shape_handles_)
    for (std::pair<occupancy_map_monitor::ShapeHandle, const Eigen::Isometry3d*>& it :
         collision_body_shape_handle.second)
      octomap_monitor_->forgetShape(it.first);
  collision_body_shape_handles_.clear();
}

void PlanningSceneMonitor::excludeWorldObjectsFromOctree()
{
  boost::recursive_mutex::scoped_lock _(shape_handles_lock_);

  includeWorldObjectsInOctree();
  for (const std::pair<const std::string, collision_detection::World::ObjectPtr>& it : *scene_->getWorld())
    excludeWorldObjectFromOctree(it.second);
}

void PlanningSceneMonitor::excludeAttachedBodyFromOctree(const robot_state::AttachedBody* attached_body)
{
  if (!octomap_monitor_)
    return;

  boost::recursive_mutex::scoped_lock _(shape_handles_lock_);
  bool found = false;
  for (std::size_t i = 0; i < attached_body->getShapes().size(); ++i)
  {
    if (attached_body->getShapes()[i]->type == shapes::PLANE || attached_body->getShapes()[i]->type == shapes::OCTREE)
      continue;
    occupancy_map_monitor::ShapeHandle h = octomap_monitor_->excludeShape(attached_body->getShapes()[i]);
    if (h)
    {
      found = true;
      attached_body_shape_handles_[attached_body].push_back(std::make_pair(h, i));
    }
  }
  if (found)
    RCLCPP_DEBUG(node_->get_logger(), "Excluding attached body '%s' from monitored octomap",
                 attached_body->getName().c_str());
}

void PlanningSceneMonitor::includeAttachedBodyInOctree(const robot_state::AttachedBody* attached_body)
{
  if (!octomap_monitor_)
    return;

  boost::recursive_mutex::scoped_lock _(shape_handles_lock_);

  AttachedBodyShapeHandles::iterator it = attached_body_shape_handles_.find(attached_body);
  if (it != attached_body_shape_handles_.end())
  {
    for (std::size_t k = 0; k < it->second.size(); ++k)
      octomap_monitor_->forgetShape(it->second[k].first);
    RCLCPP_DEBUG(node_->get_logger(), "Including attached body '%s' in monitored octomap",
                 attached_body->getName().c_str());
    attached_body_shape_handles_.erase(it);
  }
}

void PlanningSceneMonitor::excludeWorldObjectFromOctree(const collision_detection::World::ObjectConstPtr& obj)
{
  if (!octomap_monitor_)
    return;

  boost::recursive_mutex::scoped_lock _(shape_handles_lock_);

  bool found = false;
  for (std::size_t i = 0; i < obj->shapes_.size(); ++i)
  {
    if (obj->shapes_[i]->type == shapes::PLANE || obj->shapes_[i]->type == shapes::OCTREE)
      continue;
    occupancy_map_monitor::ShapeHandle h = octomap_monitor_->excludeShape(obj->shapes_[i]);
    if (h)
    {
      collision_body_shape_handles_[obj->id_].push_back(std::make_pair(h, &obj->shape_poses_[i]));
      found = true;
    }
  }
  if (found)
    RCLCPP_DEBUG(node_->get_logger(), "Excluding collision object '%s' from monitored octomap", obj->id_.c_str());
}

void PlanningSceneMonitor::includeWorldObjectInOctree(const collision_detection::World::ObjectConstPtr& obj)
{
  if (!octomap_monitor_)
    return;

  boost::recursive_mutex::scoped_lock _(shape_handles_lock_);

  CollisionBodyShapeHandles::iterator it = collision_body_shape_handles_.find(obj->id_);
  if (it != collision_body_shape_handles_.end())
  {
    for (std::size_t k = 0; k < it->second.size(); ++k)
      octomap_monitor_->forgetShape(it->second[k].first);
    RCLCPP_DEBUG(node_->get_logger(), "Including collision object '%s' in monitored octomap", obj->id_.c_str());
    collision_body_shape_handles_.erase(it);
  }
}

void PlanningSceneMonitor::currentStateAttachedBodyUpdateCallback(robot_state::AttachedBody* attached_body,
                                                                  bool just_attached)
{
  if (!octomap_monitor_)
    return;

  if (just_attached)
    excludeAttachedBodyFromOctree(attached_body);
  else
    includeAttachedBodyInOctree(attached_body);
}

void PlanningSceneMonitor::currentWorldObjectUpdateCallback(const collision_detection::World::ObjectConstPtr& obj,
                                                            collision_detection::World::Action action)
{
  if (!octomap_monitor_)
    return;
  if (obj->id_ == planning_scene::PlanningScene::OCTOMAP_NS)
    return;

  if (action & collision_detection::World::CREATE)
    excludeWorldObjectFromOctree(obj);
  else if (action & collision_detection::World::DESTROY)
    includeWorldObjectInOctree(obj);
  else
  {
    excludeWorldObjectFromOctree(obj);
    includeWorldObjectInOctree(obj);
  }
}

bool PlanningSceneMonitor::waitForCurrentRobotState(const rclcpp::Time& t, double wait_time)
{
  if (t.seconds() == 0 && t.nanoseconds() == 0)
    return false;
  auto start = std::chrono::system_clock::now();
  // rclcpp::Duration timeout(wait_time);
  auto dur = std::chrono::duration<double>(wait_time);
  auto timeout = std::chrono::duration_cast<std::chrono::seconds>(dur);
  RCLCPP_DEBUG(node_->get_logger(), "sync robot state to: %.3fs", fmod(t.seconds(), 10.));

  if (current_state_monitor_)
  {
    // Wait for next robot update in state monitor.
    bool success = current_state_monitor_->waitForCurrentState(t, wait_time);

    /* As robot updates are passed to the planning scene only in throttled fashion, there might
       be still an update pending. If so, explicitly update the planning scene here.
       If waitForCurrentState failed, we didn't get any new state updates within wait_time. */
    if (success)
    {
      boost::mutex::scoped_lock lock(state_pending_mutex_);
      if (state_update_pending_)  // enforce state update
      {
        state_update_pending_ = false;
        last_robot_state_update_wall_time_ = std::chrono::system_clock::now();
        lock.unlock();
        updateSceneWithCurrentState();
      }
      return true;
    }

    RCLCPP_WARN(node_->get_logger(), "Failed to fetch current robot state.");
    return false;
  }

  // Sometimes there is no state monitor. In this case state updates are received as part of scene updates only.
  // However, scene updates are only published if the robot actually moves. Hence we need a timeout!
  // As publishing planning scene updates is throttled (2Hz by default), a 1s timeout is a suitable default.
  boost::shared_lock<boost::shared_mutex> lock(scene_update_mutex_);
  rclcpp::Time prev_robot_motion_time = last_robot_motion_time_;
  auto wallDur = std::chrono::seconds();
  while (last_robot_motion_time_ < t &&  // Wait until the state update actually reaches the scene.
         timeout > wallDur)
  {
    RCLCPP_DEBUG(node_->get_logger(), "last robot motion: %i ago", (t - last_robot_motion_time_).nanoseconds());
    new_scene_update_condition_.wait_for(lock, boost::chrono::nanoseconds(timeout.count()));
    // TODO (anasarrak): look into this if the remaining wait_time its well calculated
    dur -= std::chrono::system_clock::now() - start;  // compute remaining wait_time
  }
  bool success = last_robot_motion_time_ >= t;
  // suppress warning if we received an update at all
  if (!success && prev_robot_motion_time != last_robot_motion_time_)
    RCLCPP_WARN(node_->get_logger(), "Maybe failed to update robot state, time diff: %.3fs",
                (t - last_robot_motion_time_).seconds());

  RCLCPP_DEBUG(node_->get_logger(), "sync done: robot motion: %i scene update: %i",
               (t - last_robot_motion_time_).seconds(), (t - last_update_time_).seconds());
  return success;
}

void PlanningSceneMonitor::lockSceneRead()
{
  scene_update_mutex_.lock_shared();
  if (octomap_monitor_)
    octomap_monitor_->getOcTreePtr()->lockRead();
}

void PlanningSceneMonitor::unlockSceneRead()
{
  if (octomap_monitor_)
    octomap_monitor_->getOcTreePtr()->unlockRead();
  scene_update_mutex_.unlock_shared();
}

void PlanningSceneMonitor::lockSceneWrite()
{
  scene_update_mutex_.lock();
  if (octomap_monitor_)
    octomap_monitor_->getOcTreePtr()->lockWrite();
}

void PlanningSceneMonitor::unlockSceneWrite()
{
  if (octomap_monitor_)
    octomap_monitor_->getOcTreePtr()->unlockWrite();
  scene_update_mutex_.unlock();
}

void PlanningSceneMonitor::startSceneMonitor(const std::string& scene_topic)
{
  stopSceneMonitor();

  RCLCPP_INFO(node_->get_logger(), "Starting planning scene monitor");
  // listen for planning scene updates; these messages include transforms, so no need for filters
  if (!scene_topic.empty())
  {
    planning_scene_subscriber_ = node_->create_subscription<moveit_msgs::msg::PlanningScene>(
        scene_topic, std::bind(&PlanningSceneMonitor::newPlanningSceneCallback, this, std::placeholders::_1));
    RCLCPP_INFO(node_->get_logger(), "Listening to '%s'", planning_scene_subscriber_->get_topic_name());
  }
}

void PlanningSceneMonitor::stopSceneMonitor()
{
  if (planning_scene_subscriber_)
  {
    RCLCPP_INFO(node_->get_logger(), "Stopping planning scene monitor");
    planning_scene_subscriber_.reset();
  }
}

bool PlanningSceneMonitor::getShapeTransformCache(const std::string& target_frame, const rclcpp::Time& target_time,
                                                  occupancy_map_monitor::ShapeTransformCache& cache) const
{
  if (!tf_buffer_)
    return false;
  try
  {
    boost::recursive_mutex::scoped_lock _(shape_handles_lock_);

    tf2::TimePoint tf2_time(std::chrono::nanoseconds(target_time.nanoseconds()));

    for (LinkShapeHandles::const_iterator it = link_shape_handles_.begin(); it != link_shape_handles_.end(); ++it)
    {
      tf_buffer_->canTransform(target_frame, it->first->getName(), tf2_time,
                               tf2::durationFromSec(shape_transform_cache_lookup_wait_time_.seconds()));
      Eigen::Isometry3d ttr =
          tf2::transformToEigen(tf_buffer_->lookupTransform(target_frame, it->first->getName(), tf2_time));
      for (std::size_t j = 0; j < it->second.size(); ++j)
        cache[it->second[j].first] = ttr * it->first->getCollisionOriginTransforms()[it->second[j].second];
    }
    for (const std::pair<const robot_state::AttachedBody* const,
                         std::vector<std::pair<occupancy_map_monitor::ShapeHandle, std::size_t>>>&
             attached_body_shape_handle : attached_body_shape_handles_)
    {
      tf_buffer_->canTransform(target_frame, it->first->getAttachedLinkName(), tf2_time,
                               tf2::durationFromSec(shape_transform_cache_lookup_wait_time_.seconds()));
      Eigen::Isometry3d transform =
          tf2::transformToEigen(tf_buffer_->lookupTransform(target_frame, it->first->getAttachedLinkName(), tf2_time));
      for (std::size_t k = 0; k < it->second.size(); ++k)
        cache[it->second[k].first] = transform * it->first->getFixedTransforms()[it->second[k].second];
    }
    {
      tf_buffer_->canTransform(target_frame, scene_->getPlanningFrame(), tf2_time,
                               tf2::durationFromSec(shape_transform_cache_lookup_wait_time_.seconds()));
      Eigen::Isometry3d transform =
          tf2::transformToEigen(tf_buffer_->lookupTransform(target_frame, scene_->getPlanningFrame(), tf2_time));
      for (CollisionBodyShapeHandles::const_iterator it = collision_body_shape_handles_.begin();
           it != collision_body_shape_handles_.end(); ++it)
        for (std::size_t k = 0; k < it->second.size(); ++k)
          cache[it->second[k].first] = transform * (*it->second[k].second);
    }
  }
  catch (tf2::TransformException& ex)
  {
    RCUTILS_LOG_ERROR_THROTTLE_NAMED(RCUTILS_STEADY_TIME, 1, "Transform error: %s", ex.what());
    return false;
  }
  return true;
}

void PlanningSceneMonitor::startWorldGeometryMonitor(const std::string& collision_objects_topic,
                                                     const std::string& planning_scene_world_topic,
                                                     const bool load_octomap_monitor)
{
  stopWorldGeometryMonitor();
  RCLCPP_INFO(node_->get_logger(),
              "Starting world geometry update monitor for collision objects, attached objects, octomap "
              "updates.");

  // Listen to the /collision_objects topic to detect requests to add/remove/update collision objects to/from the world
  // TODO (ahcorde)
  // if (!collision_objects_topic.empty())
  // {
  //   collision_object_subscriber_.reset();
  //   if (tf_buffer_)
  //   {
  //     collision_object_filter_.reset(new tf2_ros::MessageFilter<moveit_msgs::msg::CollisionObject>(
  //         *collision_object_subscriber_, *tf_buffer_, scene_->getPlanningFrame(), 1024, node_));
  //     // collision_object_filter_->connectInput(*collision_object_subscriber_);
  //     collision_object_filter_->registerCallback(&PlanningSceneMonitor::collisionObjectCallback, this);
  //     // TODO (anasarrak): No registerCallback implementation
  //     // collision_object_filter_->registerFailureCallback(
  //     //     std::bind(&PlanningSceneMonitor::collisionObjectFailTFCallback, this, _1, _2));
  //     RCLCPP_INFO(node_->get_logger(), "Listening to '%s' using message notifier with target frame '%s'",
  //                 collision_object_subscriber_->getTopic().c_str(),
  //                 collision_object_filter_->getTargetFramesString().c_str());
  //   }
  //   else
  //   {
  //     collision_object_subscriber_->registerCallback(&PlanningSceneMonitor::collisionObjectCallback, this);
  //     RCLCPP_INFO(node_->get_logger(), "Listening to '%s'", collision_objects_topic.c_str());
  //   }
  // }

  if (!planning_scene_world_topic.empty())
  {
    planning_scene_world_subscriber_ = node_->create_subscription<moveit_msgs::msg::PlanningSceneWorld>(
        planning_scene_world_topic,
        std::bind(&PlanningSceneMonitor::newPlanningSceneWorldCallback, this, std::placeholders::_1));
    RCLCPP_INFO(node_->get_logger(), "Listening to '%s' for planning scene world geometry",
                planning_scene_world_topic.c_str());
  }

  // Ocotomap monitor is optional
  // if (load_octomap_monitor)
  // {
  //   if (!octomap_monitor_)
  //   {
  //     octomap_monitor_.reset(new occupancy_map_monitor::OccupancyMapMonitor(tf_buffer_, scene_->getPlanningFrame()));
  //     excludeRobotLinksFromOctree();
  //     excludeAttachedBodiesFromOctree();
  //     excludeWorldObjectsFromOctree();
  //
  //     octomap_monitor_->setTransformCacheCallback(
  //         boost::bind(&PlanningSceneMonitor::getShapeTransformCache, this, _1, _2, _3));
  //     octomap_monitor_->setUpdateCallback(boost::bind(&PlanningSceneMonitor::octomapUpdateCallback, this));
  //   }
  //   octomap_monitor_->startMonitor();
  // }
}

void PlanningSceneMonitor::stopWorldGeometryMonitor()
{
  if (collision_object_subscriber_)
  {
    RCLCPP_INFO(node_->get_logger(), "Stopping world geometry monitor");
    collision_object_filter_.reset();
    collision_object_subscriber_.reset();
    planning_scene_world_subscriber_.reset();
  }
  else if (planning_scene_world_subscriber_)
  {
    RCLCPP_INFO(node_->get_logger(), "Stopping world geometry monitor");
    planning_scene_world_subscriber_.reset();
  }
  if (octomap_monitor_)
    octomap_monitor_->stopMonitor();
}

void PlanningSceneMonitor::startStateMonitor(const std::string& joint_states_topic,
                                             const std::string& attached_objects_topic)
{
  stopStateMonitor();
  if (scene_)
  {
    if (!current_state_monitor_)
      current_state_monitor_.reset(new CurrentStateMonitor(getRobotModel(), tf_buffer_, node_));
    current_state_monitor_->addUpdateCallback(boost::bind(&PlanningSceneMonitor::onStateUpdate, this, _1));
    current_state_monitor_->startStateMonitor(joint_states_topic);

    {
      boost::mutex::scoped_lock lock(state_pending_mutex_);
      auto period = std::chrono::milliseconds(int(dt_state_update_.count() * 1000));
      if (dt_state_update_.count() > 0)
        // Internal implementation to instanciate the start walltimer
        // http://docs.ros.org/indigo/api/roscpp/html/wall__timer_8cpp_source.html
        // state_update_timer_.start();
        state_update_timer_ =
            node_->create_wall_timer(period, std::bind(&PlanningSceneMonitor::stateUpdateTimerCallback, this));
    }

    if (!attached_objects_topic.empty())
    {
      // using regular message filter as there's no header
      attached_collision_object_subscriber_ = node_->create_subscription<moveit_msgs::msg::AttachedCollisionObject>(
          attached_objects_topic, std::bind(&PlanningSceneMonitor::attachObjectCallback, this, std::placeholders::_1));
      RCLCPP_INFO(node_->get_logger(), "Listening to '%s' for attached collision objects",
                  attached_collision_object_subscriber_->get_topic_name());
    }
  }
  else
    RCLCPP_ERROR(node_->get_logger(), "Cannot monitor robot state because planning scene is not configured");
}

void PlanningSceneMonitor::stopStateMonitor()
{
  if (current_state_monitor_)
    current_state_monitor_->stopStateMonitor();
  //TODO (ahcorde):
  // if (attached_collision_object_subscriber_)
  //   attached_collision_object_subscriber_.reset();

  // stop must be called with state_pending_mutex_ unlocked to avoid deadlock
  // Internal implementation to stop the walltimer ros 1
  // http://docs.ros.org/indigo/api/roscpp/html/classros_1_1WallTimer.html#ac3f697bdf6f0d86150f0bc9ac106d9aa
  // TODO (anasarrak): review these changes
  // delete &state_update_timer_;
  // {
  //   boost::mutex::scoped_lock lock(state_pending_mutex_);
  //   state_update_pending_ = false;
  // }
}

void PlanningSceneMonitor::onStateUpdate(const sensor_msgs::msg::JointState::ConstPtr& /*joint_state */)
{
  const std::chrono::system_clock::time_point& n = std::chrono::system_clock::now();
  std::chrono::duration<double> dt = n - last_robot_state_update_wall_time_;

  bool update = false;
  {
    boost::mutex::scoped_lock lock(state_pending_mutex_);

    if (dt.count() < dt_state_update_.count())
    {
      state_update_pending_ = true;
    }
    else
    {
      state_update_pending_ = false;
      last_robot_state_update_wall_time_ = n;
      update = true;
    }
  }
  // run the state update with state_pending_mutex_ unlocked
  if (update)
    updateSceneWithCurrentState();
}

void PlanningSceneMonitor::stateUpdateTimerCallback(/*const ros::WallTimerEvent& event*/)
{
  if (state_update_pending_)
  {
    bool update = false;

    std::chrono::system_clock::time_point n = std::chrono::system_clock::now();
    std::chrono::duration<double> dt = n - last_robot_state_update_wall_time_;

    {
      // lock for access to dt_state_update_ and state_update_pending_
      boost::mutex::scoped_lock lock(state_pending_mutex_);
      if (state_update_pending_ && dt >= dt_state_update_)
      {
        state_update_pending_ = false;
        last_robot_state_update_wall_time_ = std::chrono::system_clock::now();
        auto sec = std::chrono::duration<double>(last_robot_state_update_wall_time_.time_since_epoch()).count();
        update = true;
        RCLCPP_DEBUG(node_->get_logger(), "performPendingStateUpdate: %f", fmod(sec, 10));
      }
    }

    // run the state update with state_pending_mutex_ unlocked
    if (update)
    {
      updateSceneWithCurrentState();
      RCLCPP_DEBUG(node_->get_logger(), "performPendingStateUpdate done");
    }
  }
}

void PlanningSceneMonitor::octomapUpdateCallback()
{
  if (!octomap_monitor_)
    return;

  updateFrameTransforms();
  {
    boost::unique_lock<boost::shared_mutex> ulock(scene_update_mutex_);
    last_update_time_ = clock_.now();
    octomap_monitor_->getOcTreePtr()->lockRead();
    try
    {
      scene_->processOctomapPtr(octomap_monitor_->getOcTreePtr(), Eigen::Isometry3d::Identity());
      octomap_monitor_->getOcTreePtr()->unlockRead();
    }
    catch (...)
    {
      octomap_monitor_->getOcTreePtr()->unlockRead();  // unlock and rethrow
      throw;
    }
  }
  triggerSceneUpdateEvent(UPDATE_GEOMETRY);
}

void PlanningSceneMonitor::setStateUpdateFrequency(double hz)
{
  bool update = false;
  if (hz > std::numeric_limits<double>::epsilon())
  {
    boost::mutex::scoped_lock lock(state_pending_mutex_);
    dt_state_update_ = std::chrono::duration<double>(1.0 / hz);
    // state_update_timer_.setPeriod(dt_state_update_.count());
    // state_update_timer_.start();
    // TODO(anasarrak): review these walltimer changes
    auto period = std::chrono::milliseconds(int(dt_state_update_.count() * 1000));
    state_update_timer_ =
        node_->create_wall_timer(period, std::bind(&PlanningSceneMonitor::stateUpdateTimerCallback, this));
  }
  else
  {
    // stop must be called with state_pending_mutex_ unlocked to avoid deadlock
    // TODO (anasarrak): fix wallTime
    // state_update_timer_.stop();
    delete &state_update_timer_;
    boost::mutex::scoped_lock lock(state_pending_mutex_);
    dt_state_update_ = std::chrono::duration<double>(0.0);
    if (state_update_pending_)
      update = true;
  }
  RCLCPP_INFO(node_->get_logger(), "Updating internal planning scene state at most every %lf seconds",
              dt_state_update_.count());

  if (update)
    updateSceneWithCurrentState();
}

void PlanningSceneMonitor::updateSceneWithCurrentState()
{
  rclcpp::Time time = rclcpp::Clock().now();
  if (current_state_monitor_)
  {
    std::vector<std::string> missing;
    if (!current_state_monitor_->haveCompleteState(missing) &&
        (time - current_state_monitor_->getMonitorStartTime()).seconds() > 1.0)
    {
      std::string missing_str = boost::algorithm::join(missing, ", ");
      RCUTILS_LOG_WARN_THROTTLE_NAMED(
          RCUTILS_STEADY_TIME, 1, "The complete state of the robot is not yet known.  Missing %s", missing_str.c_str());
    }

    {
      boost::unique_lock<boost::shared_mutex> ulock(scene_update_mutex_);
      last_update_time_ = last_robot_motion_time_ = current_state_monitor_->getCurrentStateTime();
      RCLCPP_DEBUG(node_->get_logger(), "robot state update %f", fmod(last_robot_motion_time_.seconds(), 10.));
      current_state_monitor_->setToCurrentState(scene_->getCurrentStateNonConst());
      scene_->getCurrentStateNonConst().update();  // compute all transforms
    }
    triggerSceneUpdateEvent(UPDATE_STATE);
  }
  else
  {
    RCUTILS_LOG_ERROR_THROTTLE(RCUTILS_STEADY_TIME, 1,
                               "State monitor is not active. Unable to set the planning scene state");
  }
}

void PlanningSceneMonitor::addUpdateCallback(const boost::function<void(SceneUpdateType)>& fn)
{
  boost::recursive_mutex::scoped_lock lock(update_lock_);
  if (fn)
    update_callbacks_.push_back(fn);
}

void PlanningSceneMonitor::clearUpdateCallbacks()
{
  boost::recursive_mutex::scoped_lock lock(update_lock_);
  update_callbacks_.clear();
}

void PlanningSceneMonitor::setPlanningScenePublishingFrequency(double hz)
{
  publish_planning_scene_frequency_ = hz;
  RCLCPP_DEBUG(node_->get_logger(), "Maximum frquency for publishing a planning scene is now %lf Hz",
               publish_planning_scene_frequency_);
}

void PlanningSceneMonitor::getUpdatedFrameTransforms(std::vector<geometry_msgs::msg::TransformStamped>& transforms)
{
  const std::string& target = getRobotModel()->getModelFrame();

  std::vector<std::string> all_frame_names;
  tf_buffer_->_getFrameStrings(all_frame_names);
  for (const std::string& all_frame_name : all_frame_names)
  {
    if (all_frame_name == target || getRobotModel()->hasLinkModel(all_frame_name))
      continue;

    geometry_msgs::msg::TransformStamped f;
    try
    {
      rclcpp::Time t(0.0);
      tf2::TimePoint tf2_time(std::chrono::nanoseconds(t.nanoseconds()));
      f = tf_buffer_->lookupTransform(target, all_frame_names[i], tf2_time);
    }
    catch (tf2::TransformException& ex)
    {
      RCLCPP_WARN(node_->get_logger(), "Unable to transform object from frame '%s' to planning frame'%s' (%s)",
                  all_frame_names[i].c_str(), target.c_str(), ex.what());
      continue;
    }
    f.header.frame_id = all_frame_name;
    f.child_frame_id = target;
    transforms.push_back(f);
  }
}

void PlanningSceneMonitor::updateFrameTransforms()
{
  if (!tf_buffer_)
    return;

  if (scene_)
  {
    std::vector<geometry_msgs::msg::TransformStamped> transforms;
    getUpdatedFrameTransforms(transforms);
    {
      boost::unique_lock<boost::shared_mutex> ulock(scene_update_mutex_);
      scene_->getTransformsNonConst().setTransforms(transforms);
      last_update_time_ = clock_.now();
    }
    triggerSceneUpdateEvent(UPDATE_TRANSFORMS);
  }
}

void PlanningSceneMonitor::publishDebugInformation(bool flag)
{
  if (octomap_monitor_)
    octomap_monitor_->publishDebugInformation(flag);
}

void PlanningSceneMonitor::configureCollisionMatrix(const planning_scene::PlanningScenePtr& scene)
{
  if (!scene || robot_description_.empty())
    return;
  collision_detection::AllowedCollisionMatrix& acm = scene->getAllowedCollisionMatrixNonConst();

  auto parameter_robot_description = std::make_shared<rclcpp::SyncParametersClient>(node_);
  // read overriding values from the param server

  // first we do default collision operations
  for (auto& parameter :
       parameter_robot_description->get_parameters({ robot_description_ + "_planning/default_collision_operations" }))
  {
    if (parameter.get_name().compare(robot_description_ + "_planning/default_collision_operations"))
    {
      RCLCPP_DEBUG(node_->get_logger(), "No additional default collision operations specified");
    }
    else
    {
      RCLCPP_DEBUG(node_->get_logger(), "Reading additional default collision operations");

      // TODO (anasarrak): Review these changes, better use a struct?

      std::vector<std::string> object1;
      std::vector<std::string> object2;
      bool operation;

      auto parameters_coll_ops = std::make_shared<rclcpp::SyncParametersClient>(node_);

      for (auto& parameter : parameters_coll_ops->get_parameters({ "coll_ops/object1", "coll_ops/operation" }))
      {
        if (!parameter.get_type_name().find("array"))
        {
          RCLCPP_WARN(node_->get_logger(), "default_collision_operations is not an array");
          return;
        }
        else
        {
          object1 = parameter.as_string_array();
        }
        if (parameter.get_type_name().compare("coll_ops/operation"))
        {
          operation = parameter.as_bool();
        }
      }

      for (auto& parameter : parameters_coll_ops->get_parameters({ "coll_ops/object2" }))
      {
        if (!parameter.get_type_name().find("array"))
        {
          RCLCPP_WARN(node_->get_logger(), "default_collision_operations is not an array");
          return;
        }
        else
        {
          object2 = parameter.as_string_array();
        }
      }

      if (object1.size() == 0 && object2.size() == 0)
      {
        RCLCPP_WARN(node_->get_logger(), "No collision operations in default collision operations");
        return;
      }
      else
      {
        RCLCPP_WARN(node_->get_logger(), "All collision operations must have two objects and an operation");
        // TODO (anasarrak): Look at a better way to do this
        for (int x = 0; x < object1.size(); x++)
        {
          if (object1[x].compare("") || object2[x].compare("") || !operation)
          {
            RCLCPP_WARN(node_->get_logger(), "All collision operations must have two objects and an operation");
            continue;
          }
          acm.setEntry(object1[x], object2[x], operation);
        }
      }
    }
  }
}

void PlanningSceneMonitor::configureDefaultPadding()
{
  if (robot_description_.empty())
  {
    default_robot_padd_ = 0.0;
    default_robot_scale_ = 1.0;
    default_object_padd_ = 0.0;
    default_attached_padd_ = 0.0;
    return;
  }

  // Ensure no leading slash creates a bad param server address
  static const std::string robot_description =
      (robot_description_[0] == '/') ? robot_description_.substr(1) : robot_description_;

  auto parameters_robot_description = std::make_shared<rclcpp::SyncParametersClient>(node_);

  std::string robot_des = robot_description + "_planning/default_robot_padding";

  if (parameters_robot_description->has_parameter({ robot_des }))
    default_robot_padd_ = node_->get_parameter(robot_des).get_value<double>();
  else
    default_robot_padd_ = 0.0;

  robot_des = robot_description + "_planning/default_robot_scale";

  if (parameters_robot_description->has_parameter({ robot_des }))
    default_robot_scale_ = node_->get_parameter(robot_des).get_value<double>();
  else
    default_robot_scale_ = 1.0;

  robot_des = robot_description + "_planning/default_object_padding";

  if (parameters_robot_description->has_parameter({ robot_des }))
    default_object_padd_ = node_->get_parameter(robot_des).get_value<double>();
  else
    default_object_padd_ = 1.0;

  robot_des = robot_description + "_planning/default_attached_padding";

  if (parameters_robot_description->has_parameter({ robot_des }))
    default_attached_padd_ = node_->get_parameter(robot_des).get_value<double>();
  else
    default_attached_padd_ = 0.0;

  robot_des = robot_description + "_planning/default_robot_link_padding";

  if (parameters_robot_description->has_parameter({ robot_des }))
  {
    // TODO(anasarrak): no get_value for hashmap
    // default_robot_link_padd_ = node_->get_parameter(robot_des).get_value<std::map<std::string, double>>();
    default_robot_link_padd_ = std::map<std::string, double>();
  }
  else
    default_robot_link_padd_ = std::map<std::string, double>();

  robot_des = robot_description + "_planning/default_robot_link_scale";

  if (parameters_robot_description->has_parameter({ robot_des }))
  {
    // TODO(anasarrak): no get_value for hashmap
    // default_robot_link_scale_ = node_->get_parameter(robot_des).get_value<std::map<std::string, double>>();
    default_robot_link_scale_ = std::map<std::string, double>();
  }
  else
    default_robot_link_scale_ = std::map<std::string, double>();

  RCLCPP_DEBUG(node_->get_logger(), "Loaded %i default link paddings", default_robot_link_padd_.size());
  RCLCPP_DEBUG(node_->get_logger(), "Loaded %i default link scales", default_robot_link_scale_.size());
}
}  // namespace planning_scene_monitor
