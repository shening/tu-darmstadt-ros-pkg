//=================================================================================================
// Copyright (c) 2012, Johannes Meyer, TU Darmstadt
// All rights reserved.

// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above copyright
//       notice, this list of conditions and the following disclaimer in the
//       documentation and/or other materials provided with the distribution.
//     * Neither the name of the Flight Systems and Automatic Control group,
//       TU Darmstadt, nor the names of its contributors may be used to
//       endorse or promote products derived from this software without
//       specific prior written permission.

// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
// DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//=================================================================================================

#include <hector_quadrotor_gazebo_plugins/quadrotor_simple_controller.h>

#include <gazebo/Sensor.hh>
#include <gazebo/Global.hh>
#include <gazebo/XMLConfig.hh>
#include <gazebo/Simulator.hh>
#include <gazebo/gazebo.h>
#include <gazebo/World.hh>
#include <gazebo/PhysicsEngine.hh>
#include <gazebo/GazeboError.hh>
#include <gazebo/ControllerFactory.hh>

#include <cmath>

using namespace gazebo;

GZ_REGISTER_DYNAMIC_CONTROLLER("hector_gazebo_quadrotor_simple_controller", GazeboQuadrotorSimpleController)

GazeboQuadrotorSimpleController::GazeboQuadrotorSimpleController(Entity *parent)
   : Controller(parent)
   , controllers_(parameters)
{
  parent_ = dynamic_cast<Model*>(parent);
  if (!parent_) gzthrow("GazeboQuadrotorSimpleController controller requires a Model as its parent");

  if (!ros::isInitialized())
  {
    int argc = 0;
    char** argv = NULL;
    ros::init(argc,argv, "gazebo", ros::init_options::NoSigintHandler|ros::init_options::AnonymousName);
  }

  Param::Begin(&parameters);
  namespace_param_ = new ParamT<std::string>("robotNamespace", "", false);
  body_name_param_ = new ParamT<std::string>("bodyName", "", true);
  velocity_topic_param_ = new ParamT<std::string>("topicName", "cmd_vel", false);
  // wrench_topic_param_ = new ParamT<std::string>("forceTopicName", "force", false);
  max_force_param_ = new ParamT<double>("maxForce", -1.0, false);
  Param::End();
}

////////////////////////////////////////////////////////////////////////////////
// Destructor
GazeboQuadrotorSimpleController::~GazeboQuadrotorSimpleController()
{
  delete namespace_param_;
  delete body_name_param_;
  delete velocity_topic_param_;
  // delete wrench_topic_param_;
  delete max_force_param_;
}

////////////////////////////////////////////////////////////////////////////////
// Load the controller
void GazeboQuadrotorSimpleController::LoadChild(XMLConfigNode *node)
{
  namespace_param_->Load(node);
  namespace_ = namespace_param_->GetValue();

  body_name_param_->Load(node);
  body_name_ = body_name_param_->GetValue();

  // assert that the body by body_name_ exists
  body_ = dynamic_cast<Body*>(parent_->GetBody(body_name_));
  if (!body_) gzthrow("gazebo_quadrotor_simple_controller plugin error: body_name_: " << body_name_ << "does not exist\n");

  // check update rate against world physics update rate
  // should be equal or higher to guarantee the wrench applied is not "diluted"
  if (this->updatePeriod > 0 &&
      (gazebo::World::Instance()->GetPhysicsEngine()->GetUpdateRate() > 1.0/this->updatePeriod))
    ROS_ERROR("gazebo_ros_force controller update rate is less than physics update rate, wrench applied will be diluted (applied intermittently)");

  velocity_topic_param_->Load(node);
  velocity_topic_ = velocity_topic_param_->GetValue();
  // wrench_topic_param_->Load(node);
  // wrench_topic_ = wrench_topic_param_->GetValue();

  controllers_.roll.LoadChild(node->GetChild("rollpitch"));
  controllers_.pitch.LoadChild(node->GetChild("rollpitch"));
  controllers_.yaw.LoadChild(node->GetChild("yaw"));
  controllers_.velocity_x.LoadChild(node->GetChild("velocity_xy"));
  controllers_.velocity_y.LoadChild(node->GetChild("velocity_xy"));
  controllers_.velocity_z.LoadChild(node->GetChild("velocity_z"));

  max_force_param_->Load(node);
  max_force_ = max_force_param_->GetValue();

  inertia = body_->GetMass().GetPrincipalMoments();
  mass = body_->GetMass().GetAsDouble();
}

GazeboQuadrotorSimpleController::PIDController::PIDController(std::vector<Param*>& parameters)
{
  Param::Begin(&parameters);
  gain_p_param_ = new ParamT<double>("proportionalGain", 0.0, false);
  gain_d_param_ = new ParamT<double>("differentialGain", 0.0, false);
  gain_i_param_ = new ParamT<double>("integralGain", 0.0, false);
  time_constant_param_ = new ParamT<double>("timeConstant", 0.0, false);
  limit_param_ = new ParamT<double>("limit", -1.0, false);
  Param::End();

  reset();
}

GazeboQuadrotorSimpleController::PIDController::~PIDController()
{
  delete gain_p_param_;
  delete gain_d_param_;
  delete gain_i_param_;
  delete time_constant_param_;
  delete limit_param_;
}

void GazeboQuadrotorSimpleController::PIDController::LoadChild(XMLConfigNode *node)
{
  if (!node) return;
  gain_p_param_->Load(node);
  gain_p = gain_p_param_->GetValue();
  gain_d_param_->Load(node);
  gain_d = gain_d_param_->GetValue();
  gain_i_param_->Load(node);
  gain_i = gain_i_param_->GetValue();
  time_constant_param_->Load(node);
  time_constant = time_constant_param_->GetValue();
  limit_param_->Load(node);
  limit = limit_param_->GetValue();
}

double GazeboQuadrotorSimpleController::PIDController::update(double new_input, double x, double dx, double dt)
{
  // limit command
  if (limit > 0.0 && fabs(new_input) > limit) new_input = (new_input < 0 ? -1.0 : 1.0) * limit;

  // filter command
  if (dt + time_constant > 0.0) {
    dinput = (new_input - input) / (dt + time_constant);
    input  = (dt * new_input + time_constant * input) / (dt + time_constant);
  }

  // update proportional, differential and integral errors
  p = input - x;
  d = dinput - dx;
  i = i + dt * p;

  // update control output
  output = gain_p * p + gain_d * d + gain_i * i;
  return output;
}

void GazeboQuadrotorSimpleController::PIDController::reset()
{
  input = dinput = 0;
  p = i = d = output = 0;
}

////////////////////////////////////////////////////////////////////////////////
// Callbacks
void GazeboQuadrotorSimpleController::VelocityCallback(const geometry_msgs::TwistConstPtr& velocity)
{
  velocity_command_ = *velocity;
}

//void GazeboQuadrotorSimpleController::WrenchCallback(const geometry_msgs::WrenchConstPtr& wrench)
//{
//  wrench_command_ = *wrench;
//}

////////////////////////////////////////////////////////////////////////////////
// Initialize the controller
void GazeboQuadrotorSimpleController::InitChild()
{
  node_handle_ = new ros::NodeHandle(namespace_);

  if (velocity_topic_ != "")
  {
    ros::SubscribeOptions ops = ros::SubscribeOptions::create<geometry_msgs::Twist>(
      velocity_topic_, 1,
      boost::bind(&GazeboQuadrotorSimpleController::VelocityCallback, this, _1),
      ros::VoidPtr(), &callback_queue_);
    velocity_subscriber_ = node_handle_->subscribe(ops);
  }

//  if (wrench_topic_ != "")
//  {
//    ros::SubscribeOptions ops = ros::SubscribeOptions::create<geometry_msgs::Wrench>(
//      wrench_topic_, 1,
//      boost::bind(&GazeboQuadrotorSimpleController::WrenchCallback, this, _1),
//      ros::VoidPtr(), &callback_queue_);
//    wrench_subscriber_ = node_handle_->subscribe(ops);
//  }

  // callback_queue_thread_ = boost::thread( boost::bind( &GazeboQuadrotorSimpleController::CallbackQueueThread,this ) );
}

////////////////////////////////////////////////////////////////////////////////
// Update the controller
void GazeboQuadrotorSimpleController::UpdateChild()
{
  Vector3 force, torque;
  double dt = Simulator::Instance()->GetSimTime() - lastUpdate;

  // Get new commands
  callback_queue_.callAvailable();

  // Apply external force/torque
//  force.Set(wrench_command_.force.x,wrench_command_.force.y,wrench_command_.force.z);
//  torque.Set(wrench_command_.torque.x,wrench_command_.torque.y,wrench_command_.torque.z);
//  this->body_->SetForce(force);
//  this->body_->SetTorque(torque);

  // Get Pose/Orientation
  Pose3d pose = body_->GetWorldPose();
  Vector3 velocity = body_->GetWorldLinearVel();
  Vector3 acceleration = body_->GetWorldLinearAccel();
  Vector3 angular_velocity = body_->GetRelativeAngularVel();
  Vector3 euler = pose.rot.GetAsEuler();

  // Get gravity and mass
  Vector3 gravity_body = pose.rot.RotateVector(World::Instance()->GetPhysicsEngine()->GetGravity());
  double load_factor = gravity_body.GetLength() / fabs(gravity_body.z);

  // Rotate velocity and acceleration to horizontal body coordinates
  Quatern heading(cos(euler.z/2),0,0,sin(euler.z/2));
  velocity = heading.RotateVectorReverse(velocity);
  acceleration = heading.RotateVectorReverse(acceleration);

  // update controllers
  force.Set(0.0, 0.0, 0.0);
  torque.Set(0.0, 0.0, 0.0);
  double pitch_command =  controllers_.velocity_x.update(velocity_command_.linear.x, velocity.x, acceleration.x, dt) / gravity_body.GetLength();
  double roll_command  = -controllers_.velocity_y.update(velocity_command_.linear.y, velocity.y, acceleration.y, dt) / gravity_body.GetLength();
  torque.x = inertia.x *  controllers_.roll.update(roll_command, euler.x, angular_velocity.x, dt);
  torque.y = inertia.y *  controllers_.pitch.update(pitch_command, euler.y, angular_velocity.y, dt);
  // torque.x = inertia.x *  controllers_.roll.update(-velocity_command_.linear.y/gravity_body.GetLength(), euler.x, angular_velocity.x, dt);
  // torque.y = inertia.y *  controllers_.pitch.update(velocity_command_.linear.x/gravity_body.GetLength(), euler.y, angular_velocity.y, dt);
  torque.z = inertia.z *  controllers_.yaw.update(velocity_command_.angular.z, angular_velocity.z, 0, dt);
  force.z  = mass      * (controllers_.velocity_z.update(velocity_command_.linear.z,  velocity.z, acceleration.z, dt) + load_factor * gravity_body.GetLength());
  if (max_force_ > 0.0 && force.z > max_force_) force.z = max_force_;
  if (force.z < 0.0) force.z = 0.0;

//  static double lastDebugOutput = 0.0;
//  if (lastUpdate.Double() - lastDebugOutput > 0.1) {
//    ROS_DEBUG("Velocity = [%g %g %g], Acceleration = [%g %g %g]", velocity.x, velocity.y, velocity.z, acceleration.x, acceleration.y, acceleration.z);
//    ROS_DEBUG("Command: linear = [%g %g %g], angular = [%g %g %g], roll/pitch = [%g %g]", velocity_command_.linear.x, velocity_command_.linear.y, velocity_command_.linear.z, velocity_command_.angular.x*180/M_PI, velocity_command_.angular.y*180/M_PI, velocity_command_.angular.z*180/M_PI, roll_command*180/M_PI, pitch_command*180/M_PI);
//    ROS_DEBUG("Mass: %g kg, Inertia: [%g %g %g], Load: %g g", mass, inertia.x, inertia.y, inertia.z, load_factor);
//    ROS_DEBUG("Force: [%g %g %g], Torque: [%g %g %g]", force.x, force.y, force.z, torque.x, torque.y, torque.z);
//    lastDebugOutput = lastUpdate.Double();
//  }

  // set force and torque in gazebo
  body_->SetForce(force);
  body_->SetTorque(torque);
}

////////////////////////////////////////////////////////////////////////////////
// Finalize the controller
void GazeboQuadrotorSimpleController::FiniChild()
{
  node_handle_->shutdown();
  delete node_handle_;
}

////////////////////////////////////////////////////////////////////////////////
// Reset the controller
void GazeboQuadrotorSimpleController::ResetChild()
{
  controllers_.roll.reset();
  controllers_.pitch.reset();
  controllers_.yaw.reset();
  controllers_.velocity_x.reset();
  controllers_.velocity_y.reset();
  controllers_.velocity_z.reset();

  body_->SetForce(Vector3(0,0,0));
  body_->SetTorque(Vector3(0,0,0));
}
