//=================================================================================================
// Copyright (c) 2011, Johannes Meyer, TU Darmstadt
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

#include <hector_pose_estimation/measurements/gps.h>
#include <hector_pose_estimation/pose_estimation.h>

namespace hector_pose_estimation {

GPSModel::GPSModel()
  : MeasurementModel(MeasurementDimension)
{
  position_stddev_ = 10.0;
  velocity_stddev_ = 1.0;
  parameters().add("position_stddev", position_stddev_);
  parameters().add("velocity_stddev", velocity_stddev_);
}

bool GPSModel::init()
{
  NoiseCovariance noise = 0.0;
  noise(1,1) = noise(2,2) = pow(position_stddev_, 2);
  noise(3,3) = noise(4,4) = pow(velocity_stddev_, 2);
  this->AdditiveNoiseSigmaSet(noise);
  return true;
}

GPSModel::~GPSModel() {}

SystemStatus GPSModel::getStatusFlags() const {
  return STATE_XY_VELOCITY | STATE_XY_POSITION;
}

ColumnVector GPSModel::ExpectedValueGet() const {
  this->y_(1) = x_(POSITION_X);
  this->y_(2) = x_(POSITION_Y);
  this->y_(3) = x_(VELOCITY_X);
  this->y_(4) = x_(VELOCITY_Y);
  return y_;
}

Matrix GPSModel::dfGet(unsigned int i) const {
  if (i != 0) return Matrix();
  C_(1,POSITION_X) = 1.0;
  C_(2,POSITION_Y) = 1.0;
  C_(3,VELOCITY_X) = 1.0;
  C_(4,VELOCITY_Y) = 1.0;
  return C_;
}

GPS::GPS(const std::string &name)
  : Measurement_<GPSModel,GPSUpdate>(name)
  , reference_(0)
  , y_(4)
{
}

GPS::~GPS()
{}

void GPS::onReset() {
  reference_ = 0;
}

GPSModel::MeasurementVector const& GPS::getVector(const GPSUpdate &update) {
  if (!reference_) {
    y_(1) = y_(2) = y_(3) = y_(4) = 0.0/0.0;
    return y_;
  }

  reference_->fromWGS84(update.latitude, update.longitude, y_(1), y_(2));
  reference_->fromNorthEast(update.velocity_north, update.velocity_east, y_(3), y_(4));

  last_ = update;
  return y_;
}

bool GPS::beforeUpdate(PoseEstimation &estimator, const GPSUpdate &update) {
  // reset reference position if GPS has not been updated for a while
  if (timedout()) reference_ = 0;

  // find new reference position
  if (reference_ != estimator.globalReference()) {
    reference_ = estimator.globalReference();
    reference_->setPosition(update.latitude, update.longitude);

    StateVector state = estimator.getState();
    double new_latitude, new_longitude;
    reference_->toWGS84(-state(POSITION_X), -state(POSITION_Y), new_latitude, new_longitude);
    reference_->setPosition(new_latitude, new_longitude);

    ROS_INFO("Set new GPS reference position to %f/%f", reference_->position().latitude * 180.0/M_PI, reference_->position().longitude * 180.0/M_PI);
  }

  return true;
}

} // namespace hector_pose_estimation
