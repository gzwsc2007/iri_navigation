/*********************************************************************
*
* Software License Agreement (BSD License)
*
*  Copyright (c) 2008, Willow Garage, Inc.
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
*   * Neither the name of the Willow Garage nor the names of its
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
*
*********************************************************************/
#ifndef TRAJECTORY_ROLLOUT_TRAJECTORY_PLANNER_H_
#define TRAJECTORY_ROLLOUT_TRAJECTORY_PLANNER_H_

#include <vector>
#include <string>
#include <sstream>
#include <math.h>
#include <ros/console.h>
#include <angles/angles.h>

//for creating a local cost grid
#include <map_cell.h>
#include <map_grid.h>

//for obstacle data access
#include <costmap_2d/costmap_2d.h>
#include <world_model.h>

#include <trajectory.h>

//we'll take in a path as a vector of poses
#include <geometry_msgs/PoseStamped.h>
#include <geometry_msgs/Point.h>
#include <geometry_msgs/Twist.h>
#include <iri_ackermann_local_planner/Position2DInt.h>

//for computing path distance
#include <queue>

//for some datatypes
#include <tf/transform_datatypes.h>

#include <iri_ackermann_local_planner/AckermannLocalPlannerConfig.h>
#include <boost/algorithm/string.hpp>

namespace iri_ackermann_local_planner {
  /**
   * @class TrajectoryPlanner
   * @brief Computes control velocities for a robot given a costmap, a plan, and the robot's position in the world. 
   */
  class TrajectoryPlanner{
    friend class TrajectoryPlannerTest; //Need this for gtest to work
    public:
      /**
       * @brief  Constructs a trajectory controller
       * @param world_model The WorldModel the trajectory controller uses to check for collisions 
       * @param costmap A reference to the Costmap the controller should use
       * @param footprint_spec A polygon representing the footprint of the robot. (Must be convex)
       * @param inscribed_radius The radius of the inscribed circle of the robot
       * @param circumscribed_radius The radius of the circumscribed circle of the robot
       * @param acc_lim_x The acceleration limit of the robot in the x direction
       * @param acc_lim_y The acceleration limit of the robot in the y direction
       * @param acc_lim_theta The acceleration limit of the robot in the theta direction
       * @param sim_time The number of seconds to "roll-out" each trajectory
       * @param sim_granularity The distance between simulation points should be small enough that the robot doesn't hit things
       * @param vx_samples The number of trajectories to sample in the x dimension
       * @param vtheta_samples The number of trajectories to sample in the theta dimension
       * @param pdist_scale A scaling factor for how close the robot should stay to the path
       * @param gdist_scale A scaling factor for how aggresively the robot should pursue a local goal
       * @param occdist_scale A scaling factor for how much the robot should prefer to stay away from obstacles
       * @param heading_lookahead How far the robot should look ahead of itself when differentiating between different rotational velocities
       * @param oscillation_reset_dist The distance the robot must travel before it can explore rotational velocities that were unsuccessful in the past
       * @param escape_reset_dist The distance the robot must travel before it can exit escape mode
       * @param escape_reset_theta The distance the robot must rotate before it can exit escape mode
       * @param holonomic_robot Set this to true if the robot being controlled can take y velocities and false otherwise
       * @param max_vel_x The maximum x velocity the controller will explore
       * @param min_vel_x The minimum x velocity the controller will explore
       * @param max_vel_th The maximum rotational velocity the controller will explore
       * @param min_vel_th The minimum rotational velocity the controller will explore
       * @param min_in_place_vel_th The absolute value of the minimum in-place rotational velocity the controller will explore
       * @param backup_vel The velocity to use while backing up
       * @param dwa Set this to true to use the Dynamic Window Approach, false to use acceleration limits
       * @param heading_scoring Set this to true to score trajectories based on the robot's heading after 1 timestep
       * @param heading_scoring_timestep How far to look ahead in time when we score heading based trajectories
       * @param simple_attractor Set this to true to allow simple attraction to a goal point instead of intelligent cost propagation
       * @param y_vels A vector of the y velocities the controller will explore
       * @param angular_sim_granularity The distance between simulation points for angular velocity should be small enough that the robot doesn't hit things
       */
      TrajectoryPlanner(WorldModel& world_model, 
          const costmap_2d::Costmap2D& costmap, 
          std::vector<geometry_msgs::Point> footprint_spec,
          double max_acc = 1.0, double max_vel = 0.3, double min_vel = -0.3,
          double max_steer_acc = 1.0, double max_steer_vel = 0.5, double min_steer_vel = -0.5,
          double max_steer_angle = 0.35, double min_steer_angle = -0.35, double axis_distance = 1.65,
          double sim_time = 10.0, double sim_granularity = 0.025, 
          int vx_samples = 20, int vtheta_samples = 20,
          double pdist_scale = 0.6, double gdist_scale = 0.8, double occdist_scale = 0.01, double hdiff_scale = 1.0,
          bool simple_attractor = false, double angular_sim_granularity = 0.025,int heading_points = 8, double xy_goal_tol = 0.5);

      /**
       * @brief  Destructs a trajectory controller
       */
      ~TrajectoryPlanner();

      /**
       * @brief Reconfigures the trajectory planner
       */
      void reconfigure(AckermannLocalPlannerConfig &cfg);

      /**
       * @brief  Given the current position, orientation, and velocity of the robot, return a trajectory to follow
       * @param global_pose The current pose of the robot in world space 
       * @param global_vel The current velocity of the robot in world space
       * @param drive_velocities Will be set to velocities to send to the robot base
       * @return The selected path or trajectory
       */
      Trajectory findBestPath(tf::Stamped<tf::Pose> global_pose, tf::Stamped<tf::Pose> global_vel,
          tf::Stamped<tf::Pose>& drive_velocities,geometry_msgs::Twist &ackermann_state);

      /**
       * @brief  Update the plan that the controller is following
       * @param new_plan A new plan for the controller to follow 
       * @param compute_dists Wheter or not to compute path/goal distances when a plan is updated
       */
      void updatePlan(const std::vector<geometry_msgs::PoseStamped>& new_plan, bool compute_dists = false);

      /**
       * @brief  Accessor for the goal the robot is currently pursuing in world corrdinates
       * @param x Will be set to the x position of the local goal 
       * @param y Will be set to the y position of the local goal 
       */
      void getLocalGoal(double& x, double& y);

      /**
       * @brief  Generate and score a single trajectory
       * @param x The x position of the robot  
       * @param y The y position of the robot  
       * @param theta The orientation of the robot
       * @param vx The x velocity of the robot
       * @param vy The y velocity of the robot
       * @param vtheta The theta velocity of the robot
       * @param vx_samp The x velocity used to seed the trajectory
       * @param vy_samp The y velocity used to seed the trajectory
       * @param vtheta_samp The theta velocity used to seed the trajectory
       * @return True if the trajectory is legal, false otherwise
       */
      bool checkTrajectory(double x, double y, double theta, double vx, double vy, 
          double vtheta, double vx_samp, double vy_samp, double vtheta_samp);

      /**
       * @brief  Generate and score a single trajectory
       * @param x The x position of the robot  
       * @param y The y position of the robot  
       * @param theta The orientation of the robot
       * @param vx The x velocity of the robot
       * @param vy The y velocity of the robot
       * @param vtheta The theta velocity of the robot
       * @param vx_samp The x velocity used to seed the trajectory
       * @param vy_samp The y velocity used to seed the trajectory
       * @param vtheta_samp The theta velocity used to seed the trajectory
       * @return The score (as double)
       */
      double scoreTrajectory(double x, double y, double theta, double vx, double vy, 
          double vtheta, double vx_samp, double vy_samp, double vtheta_samp);

      /**
       * @brief Compute the components and total cost for a map grid cell
       * @param cx The x coordinate of the cell in the map grid
       * @param cy The y coordinate of the cell in the map grid
       * @param path_cost Will be set to the path distance component of the cost function
       * @param goal_cost Will be set to the goal distance component of the cost function
       * @param occ_cost Will be set to the costmap value of the cell
       * @param total_cost Will be set to the value of the overall cost function, taking into account the scaling parameters
       * @return True if the cell is traversible and therefore a legal location for the robot to move to
       */
      bool getCellCosts(int cx, int cy, float &path_cost, float &goal_cost, float &occ_cost, float &total_cost);
    private:
      /**
       * @brief  Create the trajectories we wish to explore, score them, and return the best option
       * @param x The x position of the robot  
       * @param y The y position of the robot  
       * @param theta The orientation of the robot
       * @param vx The x velocity of the robot
       * @param vy The y velocity of the robot
       * @param vtheta The theta velocity of the robot
       * @param acc_x The x acceleration limit of the robot
       * @param acc_y The y acceleration limit of the robot
       * @param acc_theta The theta acceleration limit of the robot
       * @return 
       */
      Trajectory createTrajectories(double x, double y, double theta, double vx, double vy, double vtheta, 
          double acc_x, double acc_y, double acc_theta);

      /**
       * @brief  Generate and score a single trajectory
       * @param x The x position of the robot  
       * @param y The y position of the robot  
       * @param theta The orientation of the robot
       * @param vx The x velocity of the robot
       * @param vy The y velocity of the robot
       * @param vtheta The theta velocity of the robot
       * @param vx_samp The x velocity used to seed the trajectory
       * @param vy_samp The y velocity used to seed the trajectory
       * @param vtheta_samp The theta velocity used to seed the trajectory
       * @param acc_x The x acceleration limit of the robot
       * @param acc_y The y acceleration limit of the robot
       * @param acc_theta The theta acceleration limit of the robot
       * @param impossible_cost The cost value of a cell in the local map grid that is considered impassable
       * @param traj Will be set to the generated trajectory with its associated score 
       */
      void generateTrajectory(double x, double y, double theta, double vx, double vy, 
          double vtheta, double vx_samp, double vy_samp, double vtheta_samp, double acc_x, double acc_y,
          double acc_theta, double impossible_cost, Trajectory& traj);

      /**
       * @brief  Checks the legality of the robot footprint at a position and orientation using the world model
       * @param x_i The x position of the robot 
       * @param y_i The y position of the robot 
       * @param theta_i The orientation of the robot
       * @return 
       */
      double footprintCost(double x_i, double y_i, double theta_i);

      /**
       * @brief  Used to get the cells that make up the footprint of the robot
       * @param x_i The x position of the robot
       * @param y_i The y position of the robot
       * @param theta_i The orientation of the robot
       * @param  fill If true: returns all cells in the footprint of the robot. If false: returns only the cells that make up the outline of the footprint.
       * @return The cells that make up either the outline or entire footprint of the robot depending on fill
       */
      std::vector<iri_ackermann_local_planner::Position2DInt> getFootprintCells(double x_i, double y_i, double theta_i, bool fill);

      /**
       * @brief  Use Bresenham's algorithm to trace a line between two points in a grid
       * @param  x0 The x coordinate of the first point
       * @param  x1 The x coordinate of the second point
       * @param  y0 The y coordinate of the first point
       * @param  y1 The y coordinate of the second point
       * @param  pts Will be filled with the cells that lie on the line in the grid
       */
      void getLineCells(int x0, int x1, int y0, int y1, std::vector<iri_ackermann_local_planner::Position2DInt>& pts);

      /**
       * @brief Fill the outline of a polygon, in this case the robot footprint, in a grid
       * @param footprint The list of cells making up the footprint in the grid, will be modified to include all cells inside the footprint
       */
      void getFillCells(std::vector<iri_ackermann_local_planner::Position2DInt>& footprint);

      MapGrid map_; ///< @brief The local map grid where we propagate goal and path distance 
      const costmap_2d::Costmap2D& costmap_; ///< @brief Provides access to cost map information
      WorldModel& world_model_; ///< @brief The world model that the controller uses for collision detection

      std::vector<geometry_msgs::Point> footprint_spec_; ///< @brief The footprint specification of the robot

      std::vector<geometry_msgs::PoseStamped> global_plan_; ///< @brief The global path for the robot to follow

      double goal_x_,goal_y_; ///< @brief Storage for the local goal the robot is pursuing

      double sim_time_; ///< @brief The number of seconds each trajectory is "rolled-out"
      double sim_granularity_; ///< @brief The distance between simulation points
      double angular_sim_granularity_; ///< @brief The distance between angular simulation points

      int vx_samples_; ///< @brief The number of samples we'll take in the x dimenstion of the control space
      int vtheta_samples_; ///< @brief The number of samples we'll take in the theta dimension of the control space

      double pdist_scale_, gdist_scale_, occdist_scale_,hdiff_scale_; ///< @brief Scaling factors for the controller's cost function

      Trajectory traj_one, traj_two; ///< @brief Used for scoring trajectories


      /* ackerman parameters */
      double steering_speed_;
      /* ackerman reconfigure parameters */
      double ack_acc_max_;
      double ack_vel_min_;
      double ack_vel_max_;
      double ack_steer_acc_max_;
      double ack_steer_speed_max_;
      double ack_steer_speed_min_;
      double ack_steer_angle_max_;
      double ack_steer_angle_min_;
      double ack_axis_distance_;

      bool simple_attractor_;  ///< @brief Enables simple attraction to a goal point
      int heading_points_;
      double xy_goal_tol_;

      boost::mutex configuration_mutex_;

      double lineCost(int x0, int x1, int y0, int y1);
      double pointCost(int x, int y);
      double headingDiff(int cell_x, int cell_y, double x, double y, double heading);
  };
};

#endif
