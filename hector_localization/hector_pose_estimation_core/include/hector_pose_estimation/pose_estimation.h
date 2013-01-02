//=================================================================================================
// Copyright (c) 2011, Johannes Meyer and Martin Nowara, TU Darmstadt
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

#ifndef HECTOR_POSE_ESTIMATION_H
#define HECTOR_POSE_ESTIMATION_H

#include "types.h"
#include "system.h"
#include "measurement.h"
#include "parameters.h"
#include "global_reference.h"

#include <bfl/filter/extendedkalmanfilter.h>

#include <boost/shared_ptr.hpp>
#include <vector>

#include <boost/function.hpp>

#include <ros/time.h>
#include <tf/transform_datatypes.h>
#include <geometry_msgs/PoseStamped.h>
#include <geometry_msgs/PointStamped.h>
#include <geometry_msgs/QuaternionStamped.h>
#include <geometry_msgs/Vector3Stamped.h>
#include <nav_msgs/Odometry.h>
#include <sensor_msgs/NavSatFix.h>

#include "measurements/rate.h"
#include "measurements/gravity.h"
#include "measurements/zerorate.h"
//#include "measurements/heading.h"

namespace hector_pose_estimation {

class PoseEstimation
{
public:
  PoseEstimation(const SystemPtr& system = SystemPtr());
  template <typename ConcreteSystemModel> PoseEstimation(ConcreteSystemModel *system_model);
  virtual ~PoseEstimation();

  static PoseEstimation *Instance();

  virtual bool init();
  virtual void cleanup();
  virtual void reset();

  virtual void update(const SystemInput& input, ros::Time timestamp);
  virtual void update(double dt);

  template <typename ConcreteSystemModel> const SystemPtr& setSystemModel(ConcreteSystemModel *system_model, const std::string& name = "system");
  virtual const SystemPtr& setSystem(const SystemPtr& system);
  virtual const SystemPtr& setSystem(System *system);
  virtual const SystemModel *getSystemModel() const;
  virtual const SystemPtr& getSystem() const;

  virtual const MeasurementPtr& addMeasurement(const MeasurementPtr& measurement);
  virtual const MeasurementPtr& addMeasurement(Measurement *measurement);
  virtual const MeasurementPtr& addMeasurement(const std::string& name, const MeasurementPtr& measurement);
  virtual MeasurementPtr getMeasurement(const std::string &name) const;

  template <class ConcreteMeasurementModel>
  const MeasurementPtr& addMeasurement(const std::string& name, ConcreteMeasurementModel *model) {
    return addMeasurement(new Measurement_<ConcreteMeasurementModel>(model, name));
  }

  virtual const StateVector& getState();
  virtual const StateCovariance& getCovariance();
  virtual void setState(const StateVector& state);
  virtual void setCovariance(const StateCovariance& covariance);

  virtual SystemStatus getSystemStatus() const;
  virtual SystemStatus getMeasurementStatus() const;
  virtual bool inSystemStatus(SystemStatus test_status) const;
  virtual bool setSystemStatus(SystemStatus new_status);
  virtual bool setMeasurementStatus(SystemStatus new_status);
  virtual bool updateSystemStatus(SystemStatus set, SystemStatus clear);
  virtual bool updateMeasurementStatus(SystemStatus set, SystemStatus clear);

  typedef boost::function<bool(SystemStatus&)> SystemStatusCallback;
  virtual void setSystemStatusCallback(SystemStatusCallback callback);

  virtual ros::Time getTimestamp() const;
  virtual void setTimestamp(ros::Time timestamp);

  virtual GlobalReference* globalReference();

  virtual void getHeader(std_msgs::Header& header);
  virtual void getState(nav_msgs::Odometry& state, bool with_covariances = true);
  virtual void getPose(tf::Pose& pose);
  virtual void getPose(tf::Stamped<tf::Pose>& pose);
  virtual void getPose(geometry_msgs::Pose& pose);
  virtual void getPose(geometry_msgs::PoseStamped& pose);
  virtual void getPosition(tf::Point& point);
  virtual void getPosition(tf::Stamped<tf::Point>& point);
  virtual void getPosition(geometry_msgs::Point& pose);
  virtual void getPosition(geometry_msgs::PointStamped& pose);
  virtual void getGlobalPosition(double& latitude, double& longitude, double& altitude);
  virtual void getGlobalPosition(sensor_msgs::NavSatFix& global);
  virtual void getOrientation(tf::Quaternion& quaternion);
  virtual void getOrientation(tf::Stamped<tf::Quaternion>& quaternion);
  virtual void getOrientation(geometry_msgs::Quaternion& pose);
  virtual void getOrientation(geometry_msgs::QuaternionStamped& pose);
  virtual void getOrientation(double &yaw, double &pitch, double &roll);
  virtual void getImuWithBiases(geometry_msgs::Vector3& linear_acceleration, geometry_msgs::Vector3& angular_velocity);
  virtual void getVelocity(tf::Vector3& vector);
  virtual void getVelocity(tf::Stamped<tf::Vector3>& vector);
  virtual void getVelocity(geometry_msgs::Vector3& vector);
  virtual void getVelocity(geometry_msgs::Vector3Stamped& vector);
  virtual void getRate(tf::Vector3& vector);
  virtual void getRate(tf::Stamped<tf::Vector3>& vector);
  virtual void getRate(geometry_msgs::Vector3& vector);
  virtual void getRate(geometry_msgs::Vector3Stamped& vector);
  virtual void getBias(tf::Vector3& angular_velocity, tf::Vector3& linear_acceleration);
  virtual void getBias(tf::Stamped<tf::Vector3>& angular_velocity, tf::Stamped<tf::Vector3>& linear_acceleration);
  virtual void getBias(geometry_msgs::Vector3& angular_velocity, geometry_msgs::Vector3& linear_acceleration);
  virtual void getBias(geometry_msgs::Vector3Stamped& angular_velocity, geometry_msgs::Vector3Stamped& linear_acceleration);
  virtual void getTransforms(std::vector<tf::StampedTransform>& transforms);
  virtual void updateWorldToOtherTransform(tf::StampedTransform& world_to_other_transform);

  virtual ParameterList getParameters() const;

  virtual ParameterList& parameters() { return parameters_; }
  virtual const ParameterList& parameters() const { return parameters_; }

  virtual BFL::KalmanFilter *filter() { return filter_.get(); }
  virtual const BFL::KalmanFilter *filter() const { return filter_.get(); }

  virtual void updated();

protected:
  SystemPtr system_;
  typedef std::vector<MeasurementPtr> Measurements;
  Measurements measurements_;

private:
  boost::shared_ptr<BFL::ExtendedKalmanFilter> filter_;

  StateVector state_;
  StateCovariance covariance_;
  bool state_is_dirty_;
  bool covariance_is_dirty_;

  SystemStatus status_;
  SystemStatus measurement_status_;
  ParameterList parameters_;

  GlobalReference global_reference_;

  ros::Time timestamp_;
  std::string world_frame_;
  std::string nav_frame_;
  std::string base_frame_;
  std::string stabilized_frame_;
  std::string footprint_frame_;
  std::string position_frame_;

  ros::Time alignment_start_;
  double alignment_time_;

  SystemStatusCallback status_callback_;

  boost::shared_ptr<Rate> rate_;
  boost::shared_ptr<Gravity> gravity_;
  boost::shared_ptr<ZeroRate> zerorate_;
//  boost::shared_ptr<Heading> heading_;
};

template <typename ConcreteSystemModel>
const SystemPtr& PoseEstimation::setSystemModel(ConcreteSystemModel *new_system_model, const std::string& name) {
  setSystem(SystemPtr());
  if (!new_system_model || new_system_model == getSystemModel()) return getSystem();
  return setSystem(System::create(new_system_model, name));
}

template <typename ConcreteSystemModel>
PoseEstimation::PoseEstimation(ConcreteSystemModel *system_model) {
  *this = PoseEstimation(System::create(system_model));
}

} // namespace hector_pose_estimation

#endif // HECTOR_POSE_ESTIMATION_H
