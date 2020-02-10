/*********************************************************************
 * Software License Agreement (BSD License)
 *
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

/* Author: Ioan Sucan */

#pragma once

#include <moveit/move_group/move_group_capability.h>
#if 0 //@todo
#include <actionlib/server/simple_action_server.h>
#endif
#include <moveit_msgs/action/move_group.hpp>
#include <memory>

namespace move_group
{
class MoveGroupMoveAction : public MoveGroupCapability
{
public:
  MoveGroupMoveAction();

  void initialize() override;

private:
#if 0 //@todo
  void executeMoveCallback(const moveit_msgs::action::MoveGroupGoalConstPtr& goal);
  void executeMoveCallbackPlanAndExecute(const moveit_msgs::action::MoveGroupGoalConstPtr& goal,
                                         moveit_msgs::action::MoveGroupResult& action_res);
  void executeMoveCallbackPlanOnly(const moveit_msgs::action::MoveGroupGoalConstPtr& goal,
                                   moveit_msgs::action::MoveGroupResult& action_res);
#endif
  void startMoveExecutionCallback();
  void startMoveLookCallback();
  void preemptMoveCallback();
  void setMoveState(MoveGroupState state);
#if 0 //@todo
  bool planUsingPlanningPipeline(const planning_interface::MotionPlanRequest& req,
                                 plan_execution::ExecutableMotionPlan& plan);
#endif

#if 0 //@todo
  std::unique_ptr<actionlib::SimpleActionServer<moveit_msgs::action::MoveGroupAction> > move_action_server_;
  moveit_msgs::action::MoveGroupFeedback move_feedback_;
#endif
  MoveGroupState move_state_;
  bool preempt_requested_;
};
}
